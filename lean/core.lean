import Mathlib

variable {α : Type*}              -- the alphabet Σ, kept abstract
variable (Pi : Set (α × α))       -- the set of pairs Π

def Opener     (x : α)   : Prop := ∃ y, (x, y) ∈ Pi
def Closer     (x : α)   : Prop := ∃ y, (y, x) ∈ Pi
def Matcher    (x : α)   : Prop := Opener Pi x ∨ Closer Pi x
def Nonmatcher (x : α)   : Prop := ¬ Matcher Pi x

-- Lemma: no opener can be a closer, and vice versa.
theorem disjoint
    (h : ∀ x y z, (x, y) ∈ Pi → (z, x) ∈ Pi → False)
    (x : α) : ¬ (Opener Pi x ∧ Closer Pi x) := by
  intro hx
  rcases hx with ⟨ho, hc⟩
  rcases ho with ⟨y, hy⟩
  rcases hc with ⟨z, hz⟩
  exact h x y z hy hz

-- The inductive definition of matchertext.
inductive MT (Pi : Set (α × α)) : List α → Prop where
  | flat (n : List α) (h : ∀ x ∈ n, Nonmatcher Pi x) : MT Pi n
  | nest (m₁ m₂ m₃ : List α) (o c : α) (hp : (o, c) ∈ Pi) (h₁ : MT Pi m₁) (h₂ : MT Pi m₂) (h₃ : MT Pi m₃) :
      MT Pi (m₁ ++ [o] ++ m₂ ++ [c] ++ m₃)
