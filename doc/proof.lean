import Mathlib

/- Premises given in the paper -/

variable {α : Type*}              -- the alphabet Σ, kept abstract on purpose
variable (Pi : Set (α × α))       -- the set of pairs Π

--
def Opener     (x : α)   : Prop := ∃ y, (x, y) ∈ Pi
def Closer     (x : α)   : Prop := ∃ y, (y, x) ∈ Pi
def Matcher    (x : α)   : Prop := Opener Pi x ∨ Closer Pi x
def Nonmatcher (x : α)   : Prop := ¬ Matcher Pi x

-- Prove: no opener can be a closer, and vice versa
theorem disjoint
    (h : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    (x : α) : ¬ (Opener Pi x ∧ Closer Pi x) := by
  intro hx
  rcases hx with ⟨ho, hc⟩
  rcases ho with ⟨y, hy⟩
  rcases hc with ⟨z, hz⟩
  exact h x y z hy hz

-- The inductive definition of Matchertext
inductive MT (Pi : Set (α × α)) : List α → Prop where
  | flat (n : List α) (h : ∀ x ∈ n, Nonmatcher Pi x) : MT Pi n
  | nest (m₁ m₂ m₃ : List α) (o c : α) (hp : (o, c) ∈ Pi) (h₁ : MT Pi m₁) (h₂ : MT Pi m₂) (h₃ : MT Pi m₃) :
      MT Pi (m₁ ++ [o] ++ m₂ ++ [c] ++ m₃)


/- Main Proof -/

-- Prove that the empty string is a matchertext string
-- Formally: "" ∈ MT
theorem mt_nil : MT Pi ([] : List α) := by
  apply MT.flat
  intro x hx
  cases hx

-- Prove that the concatenation of thw matchertext strings is a new matchertext string
-- Formally: (a ++ b) ∈ MT ↔ (a ∈ MT) ∧ (b ∈ MT)
-- Example; "(Hell[o])" ++ "W[o]rld{!}" ∈ MT
theorem mt_append {a b : List α} (ha : MT Pi a) (hb : MT Pi b) :
    MT Pi (a ++ b) := by
  induction ha with
  | flat n hn =>
    induction hb with
      | flat nn hnn =>
        apply MT.flat (n ++ nn)
        intro x hx
        have h: (x ∈ (n ++ nn)) = ((x ∈ n) ∨ (x ∈ nn)) := by simp
        rw[h] at hx
        cases hx with
          | inl hxn => exact hn x hxn
          | inr hxnn => exact hnn x hxnn
      | nest b₁ b₂ b₃ o c hp h₁ h₂ h₃ ih₁ ih₂ ih₃ =>
        have h : (n ++ (b₁ ++ [o] ++ b₂ ++ [c] ++ b₃)) = ((n ++ b₁) ++ [o] ++ b₂ ++ [c] ++ b₃) := by simp
        rw [h]
        apply MT.nest (n ++ b₁) b₂ b₃ o c hp ih₁ h₂ h₃
  | nest a₁ a₂ a₃ o c hp h₁ h₂ h₃ ih₁ ih₂ ih₃ =>
    have h : (a₁ ++ [o] ++ a₂ ++ [c] ++ a₃ ++ b) = (a₁ ++ [o] ++ a₂ ++ [c] ++ (a₃ ++ b)) := by simp
    rw [h]
    apply MT.nest a₁ a₂ (a₃ ++ b) o c hp h₁ h₂ ih₃

-- Prove that wrapping a matchertext string with a pair of matchers results in a new matchertext strin
-- Formally: ([o] ++ m ++ [c]) ∈ MT ↔ (m ∈ MT) ∧ (o ∈ Openers) ∧ (c ∈ Closers)
-- Example; ("[" ++ "Hello World !" ++ "]") ∈ MT
theorem mt_wrap {m : List α} (hm : MT Pi m) {o c : α} (hp : (o, c) ∈ Pi) :
    MT Pi ([o] ++ m ++ [c]) := by
  have h : ([o] ++ m ++ [c]) = (([] : List α) ++ [o] ++ m ++ [c] ++ []) := by simp
  rw [h]
  apply MT.nest [] m [] o c hp (mt_nil Pi) hm (mt_nil Pi)

-- Prove that given a list of openers and closers, the ith opener matches the (n-i)th closer
-- Note: the list of openers and closers can contain characters that are non-matchers
-- Formally: openers.filter(o => o ∈ Openers)
--                  .zip(closers.filter(c => c ∈ Closers).reverse())
--                  .foldLeft(True)((acc, curr) => (curr.o, curr.c) ∈ Pi ∧ acc)
-- Example:
--   1) List("Hello".tochars(), '[', "World".tochars(),'{') and List('}','!',']') describe valide matcher pairs
--   2) List('(') and List(']') describe invalid matcher pairs
inductive AllMatch (Pi : Set (α × α)) : List α → List α → Prop where
  | nil : AllMatch Pi [] []
  | cons (o c : α) (s_o' s_c' : List α) (hp : (o, c) ∈ Pi) (rest : AllMatch Pi s_o' s_c') :
    AllMatch Pi (o :: s_o') (s_c' ++ [c])
  | nonmatch_o (x : α) (s_o' s_c' : List α) (hx : Nonmatcher Pi x) (rest : AllMatch Pi s_o' s_c') :
    AllMatch Pi (x :: s_o') s_c'
  | nonmatch_c (y : α) (s_o' s_c' : List α) (hy : Nonmatcher Pi y) (rest : AllMatch Pi s_o' s_c') :
    AllMatch Pi s_o' (s_c' ++ [y])

-- Prove that embedding a matchertext string inside a valid matcher context always produces valid matchertext.
-- Note: the surrounding prefix and suffix may contain characters that are non-matchers.
-- Formally:
--   if AllMatch Pi s_o s_c
--   and MT Pi m
--   then MT Pi (s_o ++ m ++ s_c)
-- Example:
--   1) embedding "]; Or '1' = '1'; [" inside "SELECT * FROM users WHERE name = '[".tochars() and "];".tochars() produces invalid matchertext.
--   2) embedding "{x}" inside List('(', '[') and List(']', ')') produces valid matchertext.
theorem embedding_closed_under_context
    (hdisj : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    {m : List α} (hm : MT Pi m)
    {s_o s_c : List α} (hs : AllMatch Pi s_o s_c) :
    MT Pi (s_o ++ m ++ s_c) := by
  have _h_open_close_disjoint : ∀ x, ¬ (Opener Pi x ∧ Closer Pi x) :=
    fun x => disjoint Pi hdisj x
  induction hs with
  | nil =>
    have h: ([] ++ m ++ []) = (m) := by simp
    rw [h]
    apply hm
  | cons o c s_o' s_c' hp rest ih =>
    have ho : Opener Pi o := ⟨c, hp⟩
    have hc : Closer Pi c := ⟨o, hp⟩

    have o_not_closer : ¬ Closer Pi o := by
      intro hco
      exact (_h_open_close_disjoint o) ⟨ho, hco⟩
    have c_not_opener : ¬ Opener Pi c := by
      intro hoc
      exact (_h_open_close_disjoint c) ⟨hoc, hc⟩

    have h: (o :: s_o' ++ m ++ (s_c' ++ [c])) = ([o] ++ (s_o' ++ m ++ s_c') ++ [c]) := by simp
    rw [h]
    apply mt_wrap Pi ih hp
  | nonmatch_o x s_o' s_c' hx rest ih =>
    have h: ((x :: s_o') ++ m ++ s_c') = ([x] ++ (s_o' ++ m ++ s_c')) := by simp
    rw [h]
    apply mt_append Pi (MT.flat [x] (by simpa using hx)) ih
  | nonmatch_c y s_o' s_c' hy rest ih =>
    have h: (s_o' ++ m ++ (s_c' ++ [y])) = ((s_o' ++ m ++ s_c') ++ [y]) := by simp
    rw [h]
    apply mt_append Pi ih (MT.flat [y] (by simpa using hy))

-- 'always_embeddable' depends on axioms: [propext, Quot.sound]