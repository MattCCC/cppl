(* The syntax of the C++L formal core, cppl-core-0.9.0 (docs/KERNEL.md 2, 4, 7, 8).

   Each definition here restates one in kernel/src; the comment on it names
   which. De Bruijn indices are as in the kernel: #0 is the innermost binder.
   A typing context and an environment are lists whose head is #0, so pushing a
   binder is a cons. *)

From Coq Require Import ZArith List String Bool Lia.
Import ListNotations.

(* The core this model states. tests/architecture/formal_model.sh requires it
   to be kFormalCoreVersion in kernel/include/cppl/kernel/version.hpp, and the
   term, primitive and evidence formers below to be as many as the kernel's. *)
Definition core_version : string := "cppl-core-0.9.0".

(* ------------------------------------------------------------------------ *)
(* Types (KERNEL.md 2, kernel/include/cppl/kernel/types.hpp).               *)

Inductive ty : Type :=
| TInt : nat -> bool -> ty            (* width, signed *)
| TValue : string -> list ty -> ty    (* identity, observation signature *)
| TIndexed : ty -> nat -> ty.         (* element type, extent *)

Definition u1 : ty := TInt 1 false.   (* kBoolean *)

Section TyInd.
  Variable P : ty -> Prop.
  Hypothesis HInt : forall w s, P (TInt w s).
  Hypothesis HValue : forall i l, Forall P l -> P (TValue i l).
  Hypothesis HIndexed : forall e k, P e -> P (TIndexed e k).

  Fixpoint ty_ind' (t : ty) : P t :=
    match t with
    | TInt w s => HInt w s
    | TValue i l =>
        HValue i l
          ((fix go (l : list ty) : Forall P l :=
              match l with
              | [] => @Forall_nil ty P
              | x :: xs => @Forall_cons ty P x xs (ty_ind' x) (go xs)
              end) l)
    | TIndexed e k => HIndexed e k (ty_ind' e)
    end.
End TyInd.

Fixpoint ty_eqb (a b : ty) {struct a} : bool :=
  match a, b with
  | TInt w s, TInt w' s' => Nat.eqb w w' && Bool.eqb s s'
  | TValue i l, TValue i' l' =>
      String.eqb i i' &&
      (fix go (l l' : list ty) {struct l} : bool :=
         match l, l' with
         | [], [] => true
         | x :: xs, y :: ys => ty_eqb x y && go xs ys
         | _, _ => false
         end) l l'
  | TIndexed e k, TIndexed e' k' => ty_eqb e e' && Nat.eqb k k'
  | _, _ => false
  end.

Lemma ty_eqb_eq : forall a b, ty_eqb a b = true -> a = b.
Proof.
  induction a using ty_ind'; destruct b; simpl; intros E; try discriminate.
  - apply andb_true_iff in E as [E1 E2].
    apply Nat.eqb_eq in E1. apply Bool.eqb_prop in E2. subst. reflexivity.
  - apply andb_true_iff in E as [E1 E2]. apply String.eqb_eq in E1. subst.
    f_equal. revert l0 E2. induction H as [| x xs Hx _ IH]; intros l' E2;
      destruct l'; simpl in E2; try discriminate; auto.
    apply andb_true_iff in E2 as [E3 E4]. f_equal; auto.
  - apply andb_true_iff in E as [E1 E2]. apply Nat.eqb_eq in E2. subst.
    f_equal. auto.
Qed.

Lemma ty_eqb_refl : forall T, ty_eqb T T = true.
Proof.
  induction T using ty_ind'; simpl.
  - rewrite Nat.eqb_refl, Bool.eqb_reflx. reflexivity.
  - rewrite String.eqb_refl. simpl. induction H; simpl; auto.
    rewrite H, IHForall. reflexivity.
  - rewrite IHT, Nat.eqb_refl. reflexivity.
Qed.

(* is_supported (kernel/src/types.cpp): every width in 1..64 and every extent
   positive. The kernel also bounds identity length, node count and nesting;
   those bounds only reject more, so leaving them out of the model can only
   make the modelled checker accept more (see Checker.v). *)
Definition int_ok (w : nat) : bool := Nat.leb 1 w && Nat.leb w 64.

Fixpoint ty_ok (t : ty) : bool :=
  match t with
  | TInt w _ => int_ok w
  | TValue _ l =>
      (fix go (l : list ty) : bool :=
         match l with
         | [] => true
         | x :: xs => ty_ok x && go xs
         end) l
  | TIndexed e k => ty_ok e && Nat.leb 1 k
  end.

(* ------------------------------------------------------------------------ *)
(* Terms (KERNEL.md 4, kernel/include/cppl/kernel/term.hpp).                *)

Inductive op : Type :=
| AddWrap | SubWrap | MulWrap
| OEq | ONe | OLt | OLe | OGt | OGe
| ONot | OSelect
| AddFits | SubFits | MulFits
| OQuot | ORem | OConvert.

Inductive term : Type :=
| Var : nat -> term
| Lit : nat -> bool -> Z -> term                   (* lit(int(w, s), v) *)
| Call : nat -> list term -> term                  (* d(t1..tn) *)
| Prim : op -> nat -> bool -> list term -> term    (* prim(op, int(w, s), ts) *)
| Proj : ty -> nat -> term -> term                 (* proj(D, i, t) *)
| Elem : ty -> term -> term -> term.               (* elem(D, t, u) *)

Section TermInd.
  Variable P : term -> Prop.
  Hypothesis HVar : forall i, P (Var i).
  Hypothesis HLit : forall w s v, P (Lit w s v).
  Hypothesis HCall : forall d ts, Forall P ts -> P (Call d ts).
  Hypothesis HPrim : forall o w s ts, Forall P ts -> P (Prim o w s ts).
  Hypothesis HProj : forall D i t, P t -> P (Proj D i t).
  Hypothesis HElem : forall D t u, P t -> P u -> P (Elem D t u).

  Fixpoint term_ind' (t : term) : P t :=
    let fix go (l : list term) : Forall P l :=
        match l with
        | [] => @Forall_nil term P
        | x :: xs => @Forall_cons term P x xs (term_ind' x) (go xs)
        end in
    match t with
    | Var i => HVar i
    | Lit w s v => HLit w s v
    | Call d ts => HCall d ts (go ts)
    | Prim o w s ts => HPrim o w s ts (go ts)
    | Proj D i t => HProj D i t (term_ind' t)
    | Elem D t u => HElem D t u (term_ind' t) (term_ind' u)
    end.
End TermInd.

Definition op_eqb (a b : op) : bool :=
  match a, b with
  | AddWrap, AddWrap | SubWrap, SubWrap | MulWrap, MulWrap
  | OEq, OEq | ONe, ONe | OLt, OLt | OLe, OLe | OGt, OGt | OGe, OGe
  | ONot, ONot | OSelect, OSelect
  | AddFits, AddFits | SubFits, SubFits | MulFits, MulFits
  | OQuot, OQuot | ORem, ORem | OConvert, OConvert => true
  | _, _ => false
  end.

Lemma op_eqb_eq : forall a b, op_eqb a b = true -> a = b.
Proof. destruct a, b; simpl; congruence. Qed.

Fixpoint term_eqb (a b : term) {struct a} : bool :=
  let fix go (l l' : list term) {struct l} : bool :=
      match l, l' with
      | [], [] => true
      | x :: xs, y :: ys => term_eqb x y && go xs ys
      | _, _ => false
      end in
  match a, b with
  | Var i, Var j => Nat.eqb i j
  | Lit w s v, Lit w' s' v' => Nat.eqb w w' && Bool.eqb s s' && Z.eqb v v'
  | Call d ts, Call d' ts' => Nat.eqb d d' && go ts ts'
  | Prim o w s ts, Prim o' w' s' ts' =>
      op_eqb o o' && Nat.eqb w w' && Bool.eqb s s' && go ts ts'
  | Proj D i t, Proj D' i' t' => ty_eqb D D' && Nat.eqb i i' && term_eqb t t'
  | Elem D t u, Elem D' t' u' => ty_eqb D D' && term_eqb t t' && term_eqb u u'
  | _, _ => false
  end.

Lemma term_list_eqb_eq :
  forall ts, Forall (fun t => forall u, term_eqb t u = true -> t = u) ts ->
  forall ts',
    (fix go (l l' : list term) {struct l} : bool :=
       match l, l' with
       | [], [] => true
       | x :: xs, y :: ys => term_eqb x y && go xs ys
       | _, _ => false
       end) ts ts' = true -> ts = ts'.
Proof.
  intros ts H. induction H as [| x xs Hx _ IH]; intros ts' E; destruct ts';
    simpl in E; try discriminate; auto.
  apply andb_true_iff in E as [E1 E2]. f_equal; auto.
Qed.

Lemma term_eqb_eq : forall a b, term_eqb a b = true -> a = b.
Proof.
  induction a using term_ind'; destruct b; simpl; intros E; try discriminate.
  - apply Nat.eqb_eq in E. subst. reflexivity.
  - repeat rewrite andb_true_iff in E. destruct E as [[E1 E2] E3].
    apply Nat.eqb_eq in E1. apply Bool.eqb_prop in E2. apply Z.eqb_eq in E3.
    subst. reflexivity.
  - apply andb_true_iff in E as [E1 E2]. apply Nat.eqb_eq in E1. subst.
    f_equal. eapply term_list_eqb_eq; eauto.
  - repeat rewrite andb_true_iff in E. destruct E as [[[E1 E2] E3] E4].
    apply op_eqb_eq in E1. apply Nat.eqb_eq in E2. apply Bool.eqb_prop in E3.
    subst. f_equal. eapply term_list_eqb_eq; eauto.
  - repeat rewrite andb_true_iff in E. destruct E as [[E1 E2] E3].
    apply ty_eqb_eq in E1. apply Nat.eqb_eq in E2. subst. f_equal. auto.
  - repeat rewrite andb_true_iff in E. destruct E as [[E1 E2] E3].
    apply ty_eqb_eq in E1. subst. f_equal; auto.
Qed.

(* ------------------------------------------------------------------------ *)
(* Propositions (KERNEL.md 7, kernel/include/cppl/kernel/proposition.hpp).  *)

Inductive prop : Type :=
| PEq : ty -> term -> term -> prop       (* Eq(T, t, u) *)
| PAll : ty -> prop -> prop              (* Forall(T, P) *)
| PImp : prop -> prop -> prop            (* P -> Q *)
| PAnd : prop -> prop -> prop
| POr : prop -> prop -> prop
| PFalse : prop.

Fixpoint prop_eqb (a b : prop) : bool :=
  match a, b with
  | PEq T x y, PEq T' x' y' => ty_eqb T T' && term_eqb x x' && term_eqb y y'
  | PAll T P, PAll T' P' => ty_eqb T T' && prop_eqb P P'
  | PImp P Q, PImp P' Q' | PAnd P Q, PAnd P' Q' | POr P Q, POr P' Q' =>
      prop_eqb P P' && prop_eqb Q Q'
  | PFalse, PFalse => true
  | _, _ => false
  end.

Lemma prop_eqb_eq : forall a b, prop_eqb a b = true -> a = b.
Proof.
  induction a; destruct b; simpl; intros E; try discriminate;
    repeat rewrite andb_true_iff in E; try reflexivity.
  - destruct E as [[E1 E2] E3]. apply ty_eqb_eq in E1.
    apply term_eqb_eq in E2. apply term_eqb_eq in E3. subst. reflexivity.
  - destruct E as [E1 E2]. apply ty_eqb_eq in E1. subst. f_equal. auto.
  - destruct E as [E1 E2]. f_equal; auto.
  - destruct E as [E1 E2]. f_equal; auto.
  - destruct E as [E1 E2]. f_equal; auto.
Qed.

(* ------------------------------------------------------------------------ *)
(* Shifting and substitution (KERNEL.md 8, kernel/src/substitution.cpp).    *)

(* shift(t, n, c): every variable at or above the cutoff c is raised by n. The
   kernel returns its argument unchanged when n = 0; shift_zero below shows
   that is what this definition gives too. *)
Fixpoint shift (n c : nat) (t : term) : term :=
  match t with
  | Var i => if Nat.ltb i c then Var i else Var (i + n)
  | Lit w s v => Lit w s v
  | Call d ts => Call d (map (shift n c) ts)
  | Prim o w s ts => Prim o w s (map (shift n c) ts)
  | Proj D i u => Proj D i (shift n c u)
  | Elem D u x => Elem D (shift n c u) (shift n c x)
  end.

(* instantiate(t, a, k): the binder k levels out is replaced with a, stated
   under the k binders it descends through. *)
Fixpoint inst (a : term) (k : nat) (t : term) : term :=
  match t with
  | Var i =>
      if Nat.eqb i k then shift k 0 a
      else if Nat.ltb k i then Var (i - 1) else Var i
  | Lit w s v => Lit w s v
  | Call d ts => Call d (map (inst a k) ts)
  | Prim o w s ts => Prim o w s (map (inst a k) ts)
  | Proj D i u => Proj D i (inst a k u)
  | Elem D u x => Elem D (inst a k u) (inst a k x)
  end.

Fixpoint pshift (n c : nat) (P : prop) : prop :=
  match P with
  | PEq T x y => PEq T (shift n c x) (shift n c y)
  | PAll T Q => PAll T (pshift n (S c) Q)
  | PImp A B => PImp (pshift n c A) (pshift n c B)
  | PAnd A B => PAnd (pshift n c A) (pshift n c B)
  | POr A B => POr (pshift n c A) (pshift n c B)
  | PFalse => PFalse
  end.

Fixpoint pinst (a : term) (k : nat) (P : prop) : prop :=
  match P with
  | PEq T x y => PEq T (inst a k x) (inst a k y)
  | PAll T Q => PAll T (pinst a (S k) Q)
  | PImp A B => PImp (pinst a k A) (pinst a k B)
  | PAnd A B => PAnd (pinst a k A) (pinst a k B)
  | POr A B => POr (pinst a k A) (pinst a k B)
  | PFalse => PFalse
  end.

Lemma shift_zero : forall t c, shift 0 c t = t.
Proof.
  induction t using term_ind'; intros c; simpl; try reflexivity.
  - destruct (Nat.ltb i c); f_equal; lia.
  - f_equal. induction H; simpl; f_equal; auto.
  - f_equal. induction H; simpl; f_equal; auto.
  - f_equal; auto.
  - f_equal; auto.
Qed.

Lemma pshift_zero : forall P c, pshift 0 c P = P.
Proof.
  induction P; intros c; simpl; f_equal; auto using shift_zero.
Qed.

(* ------------------------------------------------------------------------ *)
(* Conditions as propositions (KERNEL.md 7, predicate in                    *)
(* kernel/src/proposition.cpp).                                              *)

Fixpoint strip_not (t : term) (pos : bool) : term * bool :=
  match t with
  | Prim ONot _ _ [c] => strip_not c (negb pos)
  | _ => (t, pos)
  end.

Definition pred (c : term) (pos : bool) : prop :=
  let (t, p) := strip_not c pos in
  match t with
  | Prim OEq w s [a; b] =>
      if p then PEq (TInt w s) a b
      else PEq u1 (Prim OEq w s [a; b]) (Lit 1 false 0)
  | Prim ONe w s [a; b] =>
      if negb p then PEq (TInt w s) a b
      else PEq u1 (Prim OEq w s [a; b]) (Lit 1 false 0)
  | _ => PEq u1 t (Lit 1 false (if p then 1 else 0)%Z)
  end.

(* Keep the decision procedures folded in later proofs. *)
Arguments int_ok : simpl never.
Arguments ty_eqb : simpl never.
