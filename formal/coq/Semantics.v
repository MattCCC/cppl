(* The meaning of the core: machine integers (KERNEL.md 3), primitives (4),
   terms and propositions in an interpretation, and what shifting and
   substitution mean (8). Nothing here is the kernel's own evaluation: this is
   the reference the kernel's rules are proven sound against. *)

From Coq Require Import ZArith List Bool Lia.
Import ListNotations.
From CppL Require Import Syntax.

Open Scope Z_scope.

(* ------------------------------------------------------------------------ *)
(* Machine integers (KERNEL.md 3, kernel/src/types.cpp).                    *)

Definition modulus (w : nat) : Z := 2 ^ Z.of_nat w.

Definition min_int (w : nat) (s : bool) : Z :=
  if s then - 2 ^ (Z.of_nat w - 1) else 0.

Definition max_int (w : nat) (s : bool) : Z :=
  if s then 2 ^ (Z.of_nat w - 1) - 1 else 2 ^ Z.of_nat w - 1.

Definition in_range (w : nat) (s : bool) (z : Z) : Prop :=
  min_int w s <= z <= max_int w s.

Definition in_rangeb (w : nat) (s : bool) (z : Z) : bool :=
  (min_int w s <=? z) && (z <=? max_int w s).

Arguments in_rangeb : simpl never.

Lemma in_rangeb_iff : forall w s z, in_rangeb w s z = true <-> in_range w s z.
Proof.
  intros. unfold in_rangeb, in_range. rewrite andb_true_iff, !Z.leb_le.
  tauto.
Qed.

(* wrap(T, v): the unique value of T congruent to v modulo 2^w. *)
Definition wrap (w : nat) (s : bool) (z : Z) : Z :=
  let m := z mod modulus w in
  if s && (max_int w s <? m) then m - modulus w else m.

Lemma pow_split : forall w : nat, (1 <= w)%nat ->
  2 ^ Z.of_nat w = 2 * 2 ^ (Z.of_nat w - 1).
Proof.
  intros w Hw. rewrite <- Z.pow_succ_r by lia. f_equal. lia.
Qed.

Lemma pow_pos' : forall w : nat, 0 < 2 ^ (Z.of_nat w - 1) \/ (w = 0)%nat.
Proof.
  intros w. destruct w; [right; reflexivity|left].
  apply Z.pow_pos_nonneg; lia.
Qed.

Lemma range_has_zero : forall w s, (1 <= w)%nat -> in_range w s 0.
Proof.
  intros w s Hw. unfold in_range, min_int, max_int.
  pose proof (pow_split w Hw).
  assert (0 < 2 ^ (Z.of_nat w - 1)) by (apply Z.pow_pos_nonneg; lia).
  destruct s; lia.
Qed.

Lemma wrap_in_range : forall w s z, (1 <= w)%nat -> in_range w s (wrap w s z).
Proof.
  intros w s z Hw. unfold wrap, in_range, min_int, max_int, modulus.
  pose proof (pow_split w Hw) as Hs.
  assert (Hp : 0 < 2 ^ (Z.of_nat w - 1)) by (apply Z.pow_pos_nonneg; lia).
  assert (Hm : 0 < 2 ^ Z.of_nat w) by lia.
  pose proof (Z.mod_pos_bound z (2 ^ Z.of_nat w) Hm) as [H0 H1].
  destruct s; simpl.
  - destruct (2 ^ (Z.of_nat w - 1) - 1 <? z mod 2 ^ Z.of_nat w) eqn:E.
    + apply Z.ltb_lt in E. lia.
    + apply Z.ltb_ge in E. lia.
  - lia.
Qed.

Lemma wrap_small : forall w s z, (1 <= w)%nat -> in_range w s z -> wrap w s z = z.
Proof.
  intros w s z Hw Hr. unfold wrap, in_range, min_int, max_int, modulus in *.
  pose proof (pow_split w Hw) as Hs.
  assert (Hp : 0 < 2 ^ (Z.of_nat w - 1)) by (apply Z.pow_pos_nonneg; lia).
  destruct s; simpl.
  - destruct (Z_le_gt_dec 0 z) as [Hz|Hz].
    + rewrite Z.mod_small by lia.
      destruct (2 ^ (Z.of_nat w - 1) - 1 <? z) eqn:E; [apply Z.ltb_lt in E|]; lia.
    + assert (z mod 2 ^ Z.of_nat w = z + 2 ^ Z.of_nat w) as ->.
      { rewrite <- (Z.mod_add z 1 (2 ^ Z.of_nat w)) by lia.
        rewrite Z.mul_1_l. apply Z.mod_small. lia. }
      destruct (2 ^ (Z.of_nat w - 1) - 1 <? z + 2 ^ Z.of_nat w) eqn:E;
        [apply Z.ltb_lt in E|apply Z.ltb_ge in E]; lia.
  - apply Z.mod_small. lia.
Qed.

(* The remainder of values of one type is a value of that type
   (KERNEL.md 4, rem). *)
Lemma range_bounds : forall w s,
  min_int w s <= 0 /\ (min_int w s < 0 -> 0 <= max_int w s).
Proof.
  intros w s. unfold min_int, max_int.
  pose proof (Z.pow_nonneg 2 (Z.of_nat w - 1)).
  destruct s; lia.
Qed.

Lemma rem_in_range : forall w s a b,
  in_range w s a -> b <> 0 -> in_range w s (Z.rem a b).
Proof.
  intros w s a b Ha Hb.
  assert (Hsgn : Z.rem a b = Z.sgn a * Z.rem (Z.abs a) (Z.abs b)).
  { destruct (Z_le_gt_dec 0 a); destruct (Z_le_gt_dec 0 b).
    - rewrite !Z.abs_eq by lia.
      destruct (Z.eq_dec a 0) as [->|]; [rewrite Z.rem_0_l; simpl; lia|].
      rewrite Z.sgn_pos by lia. lia.
    - rewrite (Z.abs_eq a) by lia. rewrite (Z.abs_neq b) by lia.
      rewrite Z.rem_opp_r by lia.
      destruct (Z.eq_dec a 0) as [->|]; [rewrite Z.rem_0_l; simpl; lia|].
      rewrite Z.sgn_pos by lia. lia.
    - rewrite (Z.abs_neq a) by lia. rewrite (Z.abs_eq b) by lia.
      rewrite Z.rem_opp_l by lia. rewrite Z.sgn_neg by lia.
      rewrite Z.opp_involutive. lia.
    - rewrite (Z.abs_neq a) by lia. rewrite (Z.abs_neq b) by lia.
      rewrite Z.rem_opp_l by lia. rewrite Z.rem_opp_r by lia.
      rewrite Z.sgn_neg by lia. rewrite Z.opp_involutive. lia. }
  assert (Habs : 0 <= Z.rem (Z.abs a) (Z.abs b) <= Z.abs a).
  { split.
    - apply Z.rem_nonneg; lia.
    - apply Z.rem_le; lia. }
  rewrite Hsgn. set (r := Z.rem (Z.abs a) (Z.abs b)) in *.
  pose proof (range_bounds w s) as [Hmin Hmax].
  unfold in_range in *.
  destruct (Z_le_gt_dec 0 a).
  - destruct (Z.eq_dec a 0) as [->|].
    + simpl. lia.
    + rewrite Z.sgn_pos by lia. rewrite Z.abs_eq in Habs by lia. lia.
  - rewrite Z.sgn_neg by lia. rewrite Z.abs_neq in Habs by lia. lia.
Qed.

(* ------------------------------------------------------------------------ *)
(* Values and interpretations.                                               *)

Section Semantics.

(* The carrier of every abstract and indexed type is drawn from an arbitrary
   type chosen by the interpretation: soundness holds for every choice. *)
Variable Abs : Type.

Inductive val : Type :=
| VZ : Z -> val
| VA : Abs -> val.

Definition zv (v : val) : Z := match v with VZ z => z | VA _ => 0 end.

Definition b2v (b : bool) : val := VZ (if b then 1 else 0).

(* An interpretation gives admitted definitions, observations and the
   carriers of the abstract and indexed types a meaning. The kernel admits
   neither injectivity nor extensionality for observations (TRUST.md
   TCB-CORE-016), so none is assumed of them here. *)
Record interp : Type := {
  i_call : nat -> list val -> val;
  i_proj : ty -> nat -> val -> val;
  i_elem : ty -> val -> val -> val;
  i_dom : ty -> val -> Prop
}.

Variable I : interp.

Definition dom (T : ty) (v : val) : Prop :=
  match T with
  | TInt w s => exists z, v = VZ z /\ in_range w s z
  | _ => i_dom I T v
  end.

(* The primitives, each as KERNEL.md 4 states it. An operand of the wrong
   shape (never produced from a typed term) reads as zero. *)
Definition prim_eval (o : op) (w : nat) (s : bool) (vs : list val) : val :=
  match o, vs with
  | AddWrap, [a; b] => VZ (wrap w s (zv a + zv b))
  | SubWrap, [a; b] => VZ (wrap w s (zv a - zv b))
  | MulWrap, [a; b] => VZ (wrap w s (zv a * zv b))
  | OEq, [a; b] => b2v (zv a =? zv b)
  | ONe, [a; b] => b2v (negb (zv a =? zv b))
  | OLt, [a; b] => b2v (zv a <? zv b)
  | OLe, [a; b] => b2v (zv a <=? zv b)
  | OGt, [a; b] => b2v (zv b <? zv a)
  | OGe, [a; b] => b2v (zv b <=? zv a)
  | ONot, [a] => VZ (1 - zv a)
  | OSelect, [c; t; f] => if zv c =? 1 then t else f
  | AddFits, [a; b] => b2v (in_rangeb w s (zv a + zv b))
  | SubFits, [a; b] => b2v (in_rangeb w s (zv a - zv b))
  | MulFits, [a; b] => b2v (in_rangeb w s (zv a * zv b))
  | OQuot, [a; b] => VZ (if zv b =? 0 then 0 else wrap w s (Z.quot (zv a) (zv b)))
  | ORem, [a; b] => VZ (if zv b =? 0 then zv a else Z.rem (zv a) (zv b))
  | OConvert, [a] => VZ (wrap w s (zv a))
  | _, _ => VZ 0
  end.

Definition dflt : val := VZ 0.

Fixpoint eval (rho : list val) (t : term) : val :=
  match t with
  | Var i => nth i rho dflt
  | Lit _ _ v => VZ v
  | Call d ts => i_call I d (map (eval rho) ts)
  | Prim o w s ts => prim_eval o w s (map (eval rho) ts)
  | Proj D i u => i_proj I D i (eval rho u)
  | Elem D u x => i_elem I D (eval rho u) (eval rho x)
  end.

Fixpoint holds (rho : list val) (P : prop) : Prop :=
  match P with
  | PEq _ x y => eval rho x = eval rho y
  | PAll T Q => forall v, dom T v -> holds (v :: rho) Q
  | PImp A B => holds rho A -> holds rho B
  | PAnd A B => holds rho A /\ holds rho B
  | POr A B => holds rho A \/ holds rho B
  | PFalse => False
  end.

(* ------------------------------------------------------------------------ *)
(* List facts.                                                               *)

Lemma nth_firstn_lt : forall (l : list val) i c,
  (i < c)%nat -> nth i (firstn c l) dflt = nth i l dflt.
Proof.
  induction l as [|x l IH]; intros i c H.
  - destruct c; reflexivity.
  - destruct c as [|c]; [lia|]. destruct i as [|i]; simpl; [reflexivity|].
    apply IH. lia.
Qed.

Lemma nth_skipn' : forall (l : list val) m i,
  nth i (skipn m l) dflt = nth (m + i) l dflt.
Proof.
  induction l; intros m i; destruct m; simpl; auto.
  destruct i; reflexivity.
Qed.

Lemma length_firstn_le : forall (l : list val) c,
  (c <= length l)%nat -> length (firstn c l) = c.
Proof. intros. rewrite firstn_length. lia. Qed.

(* ------------------------------------------------------------------------ *)
(* What shifting and substitution mean (KERNEL.md 8; M2 of 17).              *)

Lemma eval_map : forall rho (f : term -> term) (ts : list term) g,
  Forall (fun t => eval rho (f t) = g t) ts ->
  map (eval rho) (map f ts) = map g ts.
Proof.
  intros rho f ts g H. induction H; simpl; f_equal; auto.
Qed.

(* Shifting by n at cutoff c is the restatement under n more binders
   inserted at depth c. *)
Lemma eval_shift : forall t n c rho,
  (c <= length rho)%nat ->
  eval rho (shift n c t) = eval (firstn c rho ++ skipn (c + n) rho) t.
Proof.
  induction t using term_ind'; intros n c rho Hc; simpl; auto.
  - destruct (Nat.ltb i c) eqn:E.
    + apply Nat.ltb_lt in E. simpl.
      rewrite app_nth1 by (rewrite length_firstn_le by lia; lia).
      rewrite nth_firstn_lt by lia. reflexivity.
    + apply Nat.ltb_ge in E. simpl.
      rewrite app_nth2 by (rewrite length_firstn_le by lia; lia).
      rewrite length_firstn_le by lia. rewrite nth_skipn'. f_equal. lia.
  - f_equal. apply eval_map. eapply Forall_impl; [|exact H].
    intros a Ha. simpl in Ha. auto.
  - f_equal. apply eval_map. eapply Forall_impl; [|exact H].
    intros a Ha. simpl in Ha. auto.
  - f_equal. auto.
  - f_equal; auto.
Qed.

(* Substitution at depth k is evaluation with the argument's value placed
   where the binder stood. *)
Lemma eval_inst : forall t a k rho,
  (k <= length rho)%nat ->
  eval rho (inst a k t) = eval (firstn k rho ++ eval (skipn k rho) a :: skipn k rho) t.
Proof.
  induction t using term_ind'; intros a k rho Hk; simpl; auto.
  - destruct (Nat.eqb i k) eqn:E1.
    + apply Nat.eqb_eq in E1. subst.
      rewrite eval_shift by lia. simpl.
      rewrite app_nth2 by (rewrite length_firstn_le by lia; lia).
      rewrite length_firstn_le by lia. rewrite Nat.sub_diag. reflexivity.
    + apply Nat.eqb_neq in E1. destruct (Nat.ltb k i) eqn:E2.
      * apply Nat.ltb_lt in E2. simpl.
        rewrite app_nth2 by (rewrite length_firstn_le by lia; lia).
        rewrite length_firstn_le by lia.
        destruct (i - k)%nat eqn:D; [lia|]. simpl.
        rewrite nth_skipn'. f_equal. lia.
      * apply Nat.ltb_ge in E2. simpl.
        rewrite app_nth1 by (rewrite length_firstn_le by lia; lia).
        rewrite nth_firstn_lt by lia. reflexivity.
  - f_equal. apply eval_map. eapply Forall_impl; [|exact H].
    intros b Hb. simpl in Hb. auto.
  - f_equal. apply eval_map. eapply Forall_impl; [|exact H].
    intros b Hb. simpl in Hb. auto.
  - f_equal. auto.
  - f_equal; auto.
Qed.

Lemma holds_pshift : forall P n c rho,
  (c <= length rho)%nat ->
  (holds rho (pshift n c P) <-> holds (firstn c rho ++ skipn (c + n) rho) P).
Proof.
  induction P; intros n c rho Hc; simpl.
  - rewrite !eval_shift by lia. tauto.
  - split; intros H v Hv.
    + specialize (H v Hv). apply IHP in H; [|simpl; lia]. exact H.
    + apply IHP; [simpl; lia|]. exact (H v Hv).
  - rewrite IHP1, IHP2 by lia. tauto.
  - rewrite IHP1, IHP2 by lia. tauto.
  - rewrite IHP1, IHP2 by lia. tauto.
  - tauto.
Qed.

Lemma holds_pinst : forall P a k rho,
  (k <= length rho)%nat ->
  (holds rho (pinst a k P) <->
   holds (firstn k rho ++ eval (skipn k rho) a :: skipn k rho) P).
Proof.
  induction P; intros a k rho Hk; simpl.
  - rewrite !eval_inst by lia. tauto.
  - split; intros H v Hv.
    + specialize (H v Hv). apply IHP in H; [|simpl; lia]. exact H.
    + apply IHP; [simpl; lia|]. exact (H v Hv).
  - rewrite IHP1, IHP2 by lia. tauto.
  - rewrite IHP1, IHP2 by lia. tauto.
  - rewrite IHP1, IHP2 by lia. tauto.
  - tauto.
Qed.

(* The two forms the rules use: instantiating the innermost binder, and
   restating a proposition under binders introduced since it was stated. *)
Corollary holds_pinst0 : forall P a rho,
  holds rho (pinst a 0 P) <-> holds (eval rho a :: rho) P.
Proof. intros. rewrite holds_pinst by (simpl; lia). simpl. tauto. Qed.

Corollary holds_pshift0 : forall P n rho,
  holds rho (pshift n 0 P) <-> holds (skipn n rho) P.
Proof. intros. rewrite holds_pshift by (simpl; lia). simpl. tauto. Qed.

End Semantics.

Arguments VZ {Abs}.
Arguments VA {Abs}.
Arguments dflt {Abs}.
