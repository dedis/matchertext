import core

variable {α : Type*}
variable (Pi : Set (α × α))

open Classical

-- Net matcher depth: +1 per opener, -1 per closer, 0 per nonmatcher. Noncomputable since only
-- a host implementation would need a computable version.
noncomputable def delta (x : α) : Int := if Opener Pi x then 1 else if Closer Pi x then -1 else 0
noncomputable def depth (l : List α) : Int := (l.map (delta Pi)).sum

-- Lemma: depth is additive over concatenation.
theorem depth_append (a b : List α) : depth Pi (a ++ b) = depth Pi a + depth Pi b := by simp [depth]

-- Lemma: one character's contribution.
theorem depth_singleton (x : α) : depth Pi [x] = (if Opener Pi x then 1 else if Closer Pi x then -1 else 0) := by simp [depth, delta]

-- Lemma (balanced): a matchertext string has net depth 0, since it is well balanced.
theorem depth_zero
    (hdisj : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    {m : List α} (hm : MT Pi m) : depth Pi m = 0 := by
  induction hm with
  | flat n hn =>
    induction n with
    | nil => simp [depth]
    | cons x xs ih =>
      have hr : depth Pi (x :: xs) = depth Pi ([x] ++ xs) := by simp
      have hx : Nonmatcher Pi x := by exact hn x (by simp)
      have hxs : depth Pi xs = 0 := by
        apply ih
        intro x hx
        exact hn x (by simp [hx])
      have hopen : ¬ Opener Pi x := by
        intro h
        exact hx (Or.inl h)
      have hclose : ¬ Closer Pi x := by
        intro h
        exact hx (Or.inr h)
      have hdx : depth Pi [x] = 0 := by simp [depth, delta, hopen, hclose]
      simp only [hr, depth_append, hxs, hdx]
      omega
  | nest a₁ a₂ a₃ o c hp h₁ h₂ h₃ ih₁ ih₂ ih₃ =>
    have ho : Opener Pi o := ⟨c, hp⟩
    have hc : Closer Pi c := ⟨o, hp⟩
    have hco : ¬ Opener Pi c := fun hoc => disjoint Pi hdisj c ⟨hoc, hc⟩
    have hdo: depth Pi [o] = 1 := by simp [depth_singleton, if_pos ho]
    have hdc: depth Pi [c] = -1 := by simp [depth_singleton, if_neg hco, if_pos hc]
    simp only [depth_append, ih₁, ih₂, ih₃, hdo, hdc]
    omega

-- Lemma (floor, the crux): no prefix of a matchertext string dips below depth 0, so
-- a reader tracking matcher balance never underflows inside the embedding.
theorem depth_prefix_nonneg
    (hdisj : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    {m : List α} (hm : MT Pi m) : ∀ p, p <+: m → 0 ≤ depth Pi p := by
  induction hm with
  | flat n hn =>
    intro p hpre
    have hp' : ∀ x ∈ p, Nonmatcher Pi x := fun x hx => hn x (hpre.subset hx)
    have hmp : MT Pi p := MT.flat p hp'
    have h : depth Pi p = 0 := depth_zero Pi hdisj hmp
    have hh : 0 ≤ depth Pi p := by simp [h]
    exact hh
  | nest a₁ a₂ a₃ o c hp h₁ h₂ h₃ ih₁ ih₂ ih₃ =>
    have ho : Opener Pi o := ⟨c, hp⟩
    have hc : Closer Pi c := ⟨o, hp⟩

    have hco : ¬ Opener Pi c := by
      intro hoc
      exact disjoint Pi hdisj c ⟨hoc, hc⟩

    have hdo : depth Pi [o] = 1 := by simp [depth_singleton, if_pos ho]
    have hdc : depth Pi [c] = -1 := by simp [depth_singleton, if_neg hco, if_pos hc]

    have hz₁ : depth Pi a₁ = 0 := depth_zero Pi hdisj h₁
    have hz₂ : depth Pi a₂ = 0 := depth_zero Pi hdisj h₂

    intro p hpre
    simp only [List.append_assoc] at hpre

    -- ── peel a₁ ────────────────────────────────────────────────
    rcases List.prefix_or_prefix_of_prefix hpre (List.prefix_append a₁ _) with h | h
    · exact ih₁ p h
    obtain ⟨p₁, rfl⟩ := h
    rw [List.prefix_append_right_inj] at hpre
    rw [depth_append, hz₁, zero_add]

    -- ── peel [o] ───────────────────────────────────────────────
    rcases List.prefix_or_prefix_of_prefix hpre (List.prefix_append [o] _) with h | h
    · rcases p₁ with _ | ⟨y, ys⟩
      · simp [depth]
      · simp at h
        obtain ⟨rfl, rfl⟩ := h
        rw [hdo]
        norm_num
    obtain ⟨p₂, rfl⟩ := h
    rw [List.prefix_append_right_inj] at hpre
    rw [depth_append, hdo]

    -- ── peel a₂ ────────────────────────────────────────────────
    rcases List.prefix_or_prefix_of_prefix hpre (List.prefix_append a₂ _) with h | h
    · have := ih₂ p₂ h; linarith
    obtain ⟨p₃, rfl⟩ := h
    rw [List.prefix_append_right_inj] at hpre
    rw [depth_append, hz₂]

    -- ── peel [c] ───────────────────────────────────────────────
    rcases List.prefix_or_prefix_of_prefix hpre (List.prefix_append [c] _) with h | h
    · rcases p₃ with _ | ⟨y, ys⟩
      · simp [depth]
      · simp at h
        obtain ⟨rfl, rfl⟩ := h
        rw [hdc]
        norm_num
    obtain ⟨p₄, rfl⟩ := h
    rw [List.prefix_append_right_inj] at hpre
    rw [depth_append, hdc]
    have := ih₃ p₄ hpre
    linarith

-- Theorem (L1): placing a matchertext value m in a hole closed by c and reading to
-- balance recovers exactly m: every prefix of m stays at depth ≥ 0, m itself returns
-- to 0, and c is the first character to drive the depth negative, so the balance
-- boundary is precisely |m|. It follows from the three lemmas above: the boundary
-- result is a corollary, not new machinery.
theorem embed_boundary
    (hdisj : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    {m : List α} (hm : MT Pi m) {c : α} (hc : Closer Pi c) :
    (∀ p, p <+: m → 0 ≤ depth Pi p) ∧ depth Pi m = 0 ∧ depth Pi (m ++ [c]) = -1 := by
  have hzero : depth Pi m = 0 := depth_zero Pi hdisj hm
  have hco : ¬ Opener Pi c := fun ho => disjoint Pi hdisj c ⟨ho, hc⟩
  refine ⟨fun p hpre => depth_prefix_nonneg Pi hdisj hm p hpre, hzero, ?_⟩
  rw [depth_append, hzero, depth_singleton, if_neg hco, if_pos hc]
  norm_num

-- 'embed_boundary' depends on axioms: [propext, choice, Quot.sound]