/-
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.
Author: Leonardo de Moura
-/
prelude
import init.category.monad init.category.alternative init.data.list.basic
import init.meta.mk_dec_eq_instance
open list

universes u v

instance : alternative list :=
{ list.monad with
  failure := @nil,
  orelse  := @list.append }

instance {α : Type u} [decidable_eq α] : decidable_eq (list α) :=
by tactic.mk_dec_eq_instance

instance : decidable_eq string :=
list.decidable_eq

namespace list

variables {α : Type u} [decidable_eq α]
variables (p : α → Prop) [decidable_pred p]

instance decidable_bex : ∀ (l : list α), decidable (∃ x ∈ l, p x)
| [] := is_false (by intro; cases a; cases a_2; cases a)
| (x::xs) :=
  if hx : p x then
    is_true ⟨x, or.inl rfl, hx⟩
  else
    match decidable_bex xs with
    | is_true  hxs := is_true $ begin
        cases hxs with x' hx', cases hx' with hx' hpx',
        existsi x', existsi (or.inr hx'), assumption, exact x' = x
      end
    | is_false hxs := is_false $ begin
        intro hxxs, cases hxxs with x' hx', cases hx' with hx' hpx',
        cases hx', cc,
        apply hxs, existsi x', existsi a, assumption
      end
    end

instance decidable_ball (l : list α) : decidable (∀ x ∈ l, p x) :=
if h : ∃ x ∈ l, ¬ p x then
  is_false $ begin cases h with x h, cases h with hx h, intro h', apply h, apply h', assumption end
else
  is_true $ λ x hx, if h' : p x then h' else false.elim $ h ⟨x, hx, h'⟩

end list
