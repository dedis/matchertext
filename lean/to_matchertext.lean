import core
import embed_boundary
import always_embeddable

variable {α : Type*}
variable (Pi : Set (α × α))

open Classical

-- A pending scan context: an opener awaiting its closer, with the output at its level.
abbrev Ctx (α : Type*) := α × List α

-- End of input: every opener still pending is unmatched, so it is escaped.
noncomputable def unwind (esc : α → List α) : List (Ctx α) → List α → List α
  | [], out => out
  | (o, b) :: st, out => unwind esc st (b ++ esc o ++ out)

-- One left-to-right pass. `st` holds the pending openers, `out` the output at the current
-- level. A closer is matched only if it pairs with the top; else it is unmatched and escaped.
noncomputable def encAux (esc : α → List α) :
    List (Ctx α) → List α → List α → List α
  | st, out, [] => unwind esc st out
  | st, out, x :: xs =>
      if Opener Pi x then encAux esc ((x, out) :: st) [] xs
      else if Closer Pi x then
        match st with
        | (o, b) :: st' =>
            if (o, x) ∈ Pi then encAux esc st' (b ++ [o] ++ out ++ [x]) xs
            else encAux esc st (out ++ esc x) xs
        | [] => encAux esc st (out ++ esc x) xs
      else encAux esc st (out ++ [x]) xs

noncomputable def toMatchertext (esc : α → List α) (v : List α) : List α :=
  encAux Pi esc [] [] v

-- Lemma: an escape contains no matchers, hence is matchertext.
theorem esc_mt (esc : α → List α)
    (hesc : ∀ c x, x ∈ esc c → Nonmatcher Pi x) (c : α) :
    MT Pi (esc c) :=
  MT.flat _ (fun x hx => hesc c x hx)

-- Lemma: unwinding the pending stack preserves matchertext.
theorem unwind_mt (esc : α → List α)
    (hesc : ∀ c x, x ∈ esc c → Nonmatcher Pi x)
    (st : List (Ctx α)) (hst : ∀ p ∈ st, MT Pi p.2)
    (out : List α) (hout : MT Pi out) :
    MT Pi (unwind esc st out) := by
  induction st generalizing out with
  | nil => simpa [unwind] using hout
  | cons hd st' ih =>
    obtain ⟨o, b⟩ := hd
    have hb : MT Pi b := hst (o, b) (by simp)
    have htail : ∀ p ∈ st', MT Pi p.2 := fun p hp => hst p (List.mem_cons_of_mem _ hp)
    simp only [unwind]
    apply ih
    · exact htail
    · exact mt_append Pi (mt_append Pi hb (esc_mt Pi esc hesc o)) hout

-- Invariant (the crux): every accumulated segment stays matchertext.
theorem encAux_mt (esc : α → List α)
    (hesc : ∀ c x, x ∈ esc c → Nonmatcher Pi x)
    (st : List (Ctx α)) (hst : ∀ p ∈ st, MT Pi p.2)
    (out : List α) (hout : MT Pi out)
    (v : List α) :
    MT Pi (encAux Pi esc st out v) := by
  induction v generalizing st out with
  | nil =>
    rw [encAux]
    exact unwind_mt Pi esc hesc st hst out hout
  | cons x xs ih =>
    simp only [encAux]
    by_cases hox : Opener Pi x
    · -- opener: push `(x, out)`, start a fresh accumulator
      rw [if_pos hox]
      apply ih
      · intro p hp
        rcases List.mem_cons.mp hp with rfl | hmem
        · exact hout
        · exact hst p hmem
      · exact mt_nil Pi
    · rw [if_neg hox]
      by_cases hcx : Closer Pi x
      · rw [if_pos hcx]
        rcases st with _ | ⟨⟨o, b⟩, st'⟩
        · -- empty stack: the closer is unmatched, escape it
          show MT Pi (encAux Pi esc [] (out ++ esc x) xs)
          apply ih
          · simp
          · exact mt_append Pi hout (esc_mt Pi esc hesc x)
        · show MT Pi (if (o, x) ∈ Pi then encAux Pi esc st' (b ++ [o] ++ out ++ [x]) xs
                      else encAux Pi esc ((o, b) :: st') (out ++ esc x) xs)
          by_cases hp' : (o, x) ∈ Pi
          · -- matched: commit `[o] ++ body ++ [x]` into the parent level
            rw [if_pos hp']
            have hb : MT Pi b := hst (o, b) (by simp)
            apply ih
            · intro p hp
              exact hst p (List.mem_cons_of_mem _ hp)
            · simpa using mt_append Pi hb (mt_wrap Pi hout hp')
          · -- kind mismatch: unmatched closer, escape it, stack untouched
            rw [if_neg hp']
            apply ih
            · exact hst
            · exact mt_append Pi hout (esc_mt Pi esc hesc x)
      · -- nonmatcher: passes through verbatim
        rw [if_neg hcx]
        have hnx : Nonmatcher Pi x := by
          simp only [Nonmatcher, Matcher, not_or]
          exact ⟨hox, hcx⟩
        apply ih
        · exact hst
        · exact mt_append Pi hout (MT.flat [x] (by simpa using hnx))

-- Theorem (output ∈ L): any string at all, once encoded, is matchertext.
theorem toMatchertext_mt (esc : α → List α)
    (hesc : ∀ c x, x ∈ esc c → Nonmatcher Pi x) (v : List α) :
    MT Pi (toMatchertext Pi esc v) :=
  encAux_mt Pi esc hesc [] (by simp) [] (mt_nil Pi) v

-- Corollary (write-path payoff): reading to matcher balance recovers exactly the encoded
-- value, for a freely chosen v. embed_boundary assumes MT Pi m, which toMatchertext_mt supplies.
theorem embed_boundary_encoded
    (hdisj : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    (esc : α → List α)
    (hesc : ∀ c x, x ∈ esc c → Nonmatcher Pi x)
    (v : List α) {c : α} (hc : Closer Pi c) :
    (∀ p, p <+: toMatchertext Pi esc v → 0 ≤ depth Pi p)
      ∧ depth Pi (toMatchertext Pi esc v) = 0
      ∧ depth Pi (toMatchertext Pi esc v ++ [c]) = -1 :=
  embed_boundary Pi hdisj (toMatchertext_mt Pi esc hesc v) hc
