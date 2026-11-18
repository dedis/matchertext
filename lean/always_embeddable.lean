module

public import core

@[expose] public section

variable {α : Type*}
variable (Pi : Set (α × α))


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

-- A context: a prefix and a suffix whose unmatched openers and closers pair up, the ith opener of the prefix
-- with the ith closer from the end of the suffix. Around and between them the context may hold any
-- matchertext: nonmatchers, and balanced groups such as the "t1(name,id)" of a SQL prefix.
-- Example:
--   1) "INSERT INTO t1(name,id) VALUES(" and ");" form a context: the balanced "INSERT INTO t1(name,id) VALUES",
--      then the pair "(" ")", then the nonmatcher ";".
--   2) "(" and "]" do not: the two do not pair.
inductive AllMatch (Pi : Set (α × α)) : List α → List α → Prop where
  | pair (o c : α) (hp : (o, c) ∈ Pi) : AllMatch Pi [o] [c]
  | cons (o c : α) (s_o' s_c' : List α) (hp : (o, c) ∈ Pi) (rest : AllMatch Pi s_o' s_c') :
    AllMatch Pi (o :: s_o') (s_c' ++ [c])
  | mt_o (b : List α) (s_o' s_c' : List α) (hb : MT Pi b) (rest : AllMatch Pi s_o' s_c') :
    AllMatch Pi (b ++ s_o') s_c'
  | mt_c (b : List α) (s_o' s_c' : List α) (hb : MT Pi b) (rest : AllMatch Pi s_o' s_c') :
    AllMatch Pi s_o' (s_c' ++ b)

-- Prove that embedding a matchertext string inside a valid matcher context always produces valid matchertext.
-- Note: the surrounding prefix and suffix may hold any matchertext besides the matchers that pair up.
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
  | pair o c hp =>
    exact mt_wrap Pi hm hp
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
  | mt_o b s_o' s_c' hb rest ih =>
    have h: ((b ++ s_o') ++ m ++ s_c') = (b ++ (s_o' ++ m ++ s_c')) := by simp
    rw [h]
    apply mt_append Pi hb ih
  | mt_c b s_o' s_c' hb rest ih =>
    have h: (s_o' ++ m ++ (s_c' ++ b)) = ((s_o' ++ m ++ s_c') ++ b) := by simp
    rw [h]
    apply mt_append Pi ih hb

-- 'always_embeddable' depends on axioms: [propext]