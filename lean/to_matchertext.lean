module

public import core
public import embed_boundary
public import always_embeddable

@[expose] public section

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
-- Two decisions may escape more: `cap st` escapes an opener instead of nesting it (a depth
-- limit), and `split b out` escapes both members of a matched pair, given the output `b`
-- before its opener and its body `out`. Any choice of either keeps the output matchertext.
noncomputable def encAux (esc : α → List α) (cap : List (Ctx α) → Bool)
    (split : List α → List α → Bool) :
    List (Ctx α) → List α → List α → List α
  | st, out, [] => unwind esc st out
  | st, out, x :: xs =>
      if Opener Pi x then
        if cap st then encAux esc cap split st (out ++ esc x) xs
        else encAux esc cap split ((x, out) :: st) [] xs
      else if Closer Pi x then
        match st with
        | (o, b) :: st' =>
            if (o, x) ∈ Pi then
              if split b out then encAux esc cap split st' (b ++ esc o ++ out ++ esc x) xs
              else encAux esc cap split st' (b ++ [o] ++ out ++ [x]) xs
            else encAux esc cap split st (out ++ esc x) xs
        | [] => encAux esc cap split st (out ++ esc x) xs
      else encAux esc cap split st (out ++ [x]) xs

-- The minimal encoder: only unmatched matchers are escaped.
noncomputable def toMatchertext (esc : α → List α) (v : List α) : List α :=
  encAux Pi esc (fun _ => false) (fun _ _ => false) [] [] v

-- The C encoder (sqlite/src/matchertext.c): nesting stops one level below the host's
-- limit `n`, and a matched pair right after an introducer `bs` and a tag `t` with
-- `tag t` is escaped, so that input already shaped like an escape decodes to itself.
noncomputable def toMatchertextC (esc : α → List α) (n : Nat) (bs : α) (tag : α → Prop)
    (v : List α) : List α :=
  encAux Pi esc (fun st => decide (n ≤ st.length + 1))
    (fun b out => decide (out = [] ∧ ∃ p t, b = p ++ [bs, t] ∧ tag t)) [] [] v

-- Neither conjunct of the hypothesis below implies the other. Depth 0 does not
-- give MT: ")(" balances to zero yet dips below it at the first character.
theorem depth_zero_not_mt
    (hdisj : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    {o c : α} (hp : (o, c) ∈ Pi) :
    depth Pi [c, o] = 0 ∧ ¬ MT Pi [c, o] := by
  have ho : Opener Pi o := ⟨c, hp⟩
  have hc : Closer Pi c := ⟨o, hp⟩
  have hco : ¬ Opener Pi c := fun h => disjoint Pi hdisj c ⟨h, hc⟩
  refine ⟨by simp [depth, delta, ho, hco, hc], fun hm => ?_⟩
  have h := depth_prefix_nonneg Pi hdisj hm [c] ⟨[o], rfl⟩
  rw [depth_singleton, ite_eq_right hco, ite_eq_left hc] at h
  norm_num at h

-- Lemma: matcher-free escapes are matchertext of depth 0; one way to discharge
-- the hypothesis below, no longer the only one.
theorem esc_mt (esc : α → List α)
    (hesc : ∀ c x, x ∈ esc c → Nonmatcher Pi x) (c : α) :
    MT Pi (esc c) ∧ depth Pi (esc c) = 0 := by
  refine ⟨MT.flat _ (fun x hx => hesc c x hx), ?_⟩
  apply List.sum_eq_zero
  intro y hy
  obtain ⟨x, hx, rfl⟩ := List.mem_map.mp hy
  have hn := hesc c x hx
  simp only [Nonmatcher, Matcher, not_or] at hn
  simp [delta, hn.1, hn.2]

-- Lemma: the C alphabet's four-character escapes (backslash, tag, then a matched
-- pair, e.g. `\o()`) are matchertext by nesting rather than flatness, and of depth 0.
theorem esc_mt_paired
    (hdisj : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    {bs t o c : α}
    (hbs : Nonmatcher Pi bs) (ht : Nonmatcher Pi t) (hp : (o, c) ∈ Pi) :
    MT Pi [bs, t, o, c] ∧ depth Pi [bs, t, o, c] = 0 := by
  have ho : Opener Pi o := ⟨c, hp⟩
  have hc : Closer Pi c := ⟨o, hp⟩
  have hco : ¬ Opener Pi c := fun hoc => disjoint Pi hdisj c ⟨hoc, hc⟩
  constructor
  · have h : ([bs, t, o, c] : List α) = [bs, t] ++ [o] ++ [] ++ [c] ++ [] := by simp
    rw [h]
    apply MT.nest [bs, t] [] [] o c hp ?_ (mt_nil Pi) (mt_nil Pi)
    apply MT.flat
    intro x hx
    rcases List.mem_pair.mp hx with rfl | rfl
    · exact hbs
    · exact ht
  · simp only [Nonmatcher, Matcher, not_or] at hbs ht
    simp [depth, delta, hbs.1, hbs.2, ht.1, ht.2, ho, hco, hc]

-- Lemma: unwinding the pending stack preserves matchertext.
theorem unwind_mt (esc : α → List α)
    (hesc : ∀ c, MT Pi (esc c) ∧ depth Pi (esc c) = 0)
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
    · exact mt_append Pi (mt_append Pi hb (hesc o).1) hout

-- Invariant (the crux): every accumulated segment stays matchertext, whatever the
-- decisions `cap` and `split` choose.
theorem encAux_mt (esc : α → List α)
    (hesc : ∀ c, MT Pi (esc c) ∧ depth Pi (esc c) = 0)
    (cap : List (Ctx α) → Bool) (split : List α → List α → Bool)
    (st : List (Ctx α)) (hst : ∀ p ∈ st, MT Pi p.2)
    (out : List α) (hout : MT Pi out)
    (v : List α) :
    MT Pi (encAux Pi esc cap split st out v) := by
  induction v generalizing st out with
  | nil =>
    rw [encAux]
    exact unwind_mt Pi esc hesc st hst out hout
  | cons x xs ih =>
    simp only [encAux]
    by_cases hox : Opener Pi x
    · rw [ite_eq_left hox]
      by_cases hcap : cap st
      · -- opener at the limit: escape it in place
        rw [ite_eq_left hcap]
        exact ih st hst _ (mt_append Pi hout (hesc x).1)
      · -- opener: push `(x, out)`, start a fresh accumulator
        rw [ite_eq_right hcap]
        apply ih
        · intro p hp
          rcases List.mem_cons.mp hp with rfl | hmem
          · exact hout
          · exact hst p hmem
        · exact mt_nil Pi
    · rw [ite_eq_right hox]
      by_cases hcx : Closer Pi x
      · rw [ite_eq_left hcx]
        rcases st with _ | ⟨⟨o, b⟩, st'⟩
        · -- empty stack: the closer is unmatched, escape it
          show MT Pi (encAux Pi esc cap split [] (out ++ esc x) xs)
          apply ih
          · simp
          · exact mt_append Pi hout (hesc x).1
        · show MT Pi (if (o, x) ∈ Pi then
                        (if split b out then
                          encAux Pi esc cap split st' (b ++ esc o ++ out ++ esc x) xs
                        else encAux Pi esc cap split st' (b ++ [o] ++ out ++ [x]) xs)
                      else encAux Pi esc cap split ((o, b) :: st') (out ++ esc x) xs)
          by_cases hp' : (o, x) ∈ Pi
          · rw [ite_eq_left hp']
            have hb : MT Pi b := hst (o, b) (by simp)
            have htail : ∀ p ∈ st', MT Pi p.2 := fun p hp => hst p (List.mem_cons_of_mem _ hp)
            by_cases hsp : split b out
            · -- matched but split: escape both members around the body
              rw [ite_eq_left hsp]
              exact ih st' htail _ (mt_append Pi (mt_append Pi (mt_append Pi hb (hesc o).1)
                hout) (hesc x).1)
            · -- matched: commit `[o] ++ body ++ [x]` into the parent level
              rw [ite_eq_right hsp]
              apply ih
              · exact htail
              · simpa using mt_append Pi hb (mt_wrap Pi hout hp')
          · -- kind mismatch: unmatched closer, escape it, stack untouched
            rw [ite_eq_right hp']
            apply ih
            · exact hst
            · exact mt_append Pi hout (hesc x).1
      · -- nonmatcher: passes through verbatim
        rw [ite_eq_right hcx]
        have hnx : Nonmatcher Pi x := by
          simp only [Nonmatcher, Matcher, not_or]
          exact ⟨hox, hcx⟩
        apply ih
        · exact hst
        · exact mt_append Pi hout (MT.flat [x] (by simpa using hnx))

-- Theorem (output ∈ L): any string at all, once encoded, is matchertext.
theorem toMatchertext_mt (esc : α → List α)
    (hesc : ∀ c, MT Pi (esc c) ∧ depth Pi (esc c) = 0) (v : List α) :
    MT Pi (toMatchertext Pi esc v) :=
  encAux_mt Pi esc hesc _ _ [] (by simp) [] (mt_nil Pi) v

-- Theorem: the same for the C encoder's decisions.
theorem toMatchertextC_mt (esc : α → List α)
    (hesc : ∀ c, MT Pi (esc c) ∧ depth Pi (esc c) = 0)
    (n : Nat) (bs : α) (tag : α → Prop) (v : List α) :
    MT Pi (toMatchertextC Pi esc n bs tag v) :=
  encAux_mt Pi esc hesc _ _ [] (by simp) [] (mt_nil Pi) v

-- Corollary (write-path payoff): reading to matcher balance recovers exactly the encoded
-- value, for a freely chosen v. embed_boundary assumes MT Pi m, which toMatchertext_mt supplies.
theorem embed_boundary_encoded
    (hdisj : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    (esc : α → List α)
    (hesc : ∀ c, MT Pi (esc c) ∧ depth Pi (esc c) = 0)
    (v : List α) {c : α} (hc : Closer Pi c) :
    (∀ p, p <+: toMatchertext Pi esc v → 0 ≤ depth Pi p)
      ∧ depth Pi (toMatchertext Pi esc v) = 0
      ∧ depth Pi (toMatchertext Pi esc v ++ [c]) = -1 :=
  embed_boundary Pi hdisj (toMatchertext_mt Pi esc hesc v) hc
