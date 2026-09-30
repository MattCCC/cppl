(* Typing and well-formedness (KERNEL.md 5-7; type_of_impl in
   kernel/src/context.cpp, validate_proposition in kernel/src/check.cpp), the
   substitution lemmas that keep a goal well formed wherever the checker
   reaches it, type soundness, and the meaning of `predicate`. *)

From Coq Require Import ZArith List Bool Lia.
Import ListNotations.
From CppL Require Import Syntax Semantics.

Open Scope Z_scope.

Section Typing.

(* The admitted definitions' signatures (Context in kernel/src/context.cpp).
   Their bodies matter only to normalization, which Checker.v takes as a
   parameter. Context::define admits only supported parameter and result
   types. *)
Variable sig : nat -> option (list ty * ty).
Hypothesis sig_ok : forall d ps R, sig d = Some (ps, R) -> ty_ok R = true.

Definition teq (o : option ty) (T : ty) : bool :=
  match o with Some U => ty_eqb U T | None => false end.

Definition is_int (T : ty) : bool :=
  match T with TInt _ _ => true | _ => false end.

Fixpoint type_of (G : list ty) (t : term) {struct t} : option ty :=
  match t with
  | Var i =>
      match nth_error G i with
      | Some T => if ty_ok T then Some T else None
      | None => None
      end
  | Lit w s v => if ty_ok (TInt w s) && in_rangeb w s v then Some (TInt w s) else None
  | Call d ts =>
      match sig d with
      | Some (ps, R) =>
          if (fix go (ps : list ty) (ts : list term) {struct ts} : bool :=
                match ps, ts with
                | [], [] => true
                | p :: ps', u :: ts' => teq (type_of G u) p && go ps' ts'
                | _, _ => false
                end) ps ts
          then Some R else None
      | None => None
      end
  | Prim o w s ts =>
      if negb (ty_ok (TInt w s)) then None else
      match o, ts with
      | OConvert, [a] =>
          match type_of G a with Some (TInt _ _) => Some (TInt w s) | _ => None end
      | OSelect, [c; x; y] =>
          if teq (type_of G c) u1 && teq (type_of G x) (TInt w s) && teq (type_of G y) (TInt w s)
          then Some (TInt w s) else None
      | ONot, [a] =>
          if ty_eqb (TInt w s) u1 && teq (type_of G a) (TInt w s) then Some (TInt w s) else None
      | (AddWrap | SubWrap | MulWrap | OQuot | ORem), [a; b] =>
          if teq (type_of G a) (TInt w s) && teq (type_of G b) (TInt w s)
          then Some (TInt w s) else None
      | (OEq | ONe | OLt | OLe | OGt | OGe | AddFits | SubFits | MulFits), [a; b] =>
          if teq (type_of G a) (TInt w s) && teq (type_of G b) (TInt w s)
          then Some u1 else None
      | _, _ => None
      end
  | Proj D i u =>
      match D with
      | TValue _ obs => if ty_ok D && teq (type_of G u) D then nth_error obs i else None
      | _ => None
      end
  | Elem D u x =>
      match D with
      | TIndexed U _ =>
          if ty_ok D && teq (type_of G u) D &&
             match type_of G x with Some X => is_int X | None => false end
          then Some U else None
      | _ => None
      end
  end.

Fixpoint valid (G : list ty) (P : prop) : bool :=
  match P with
  | PEq T x y => teq (type_of G x) T && teq (type_of G y) T
  | PAll T Q => ty_ok T && valid (T :: G) Q
  | PImp A B | PAnd A B | POr A B => valid G A && valid G B
  | PFalse => true
  end.

Lemma teq_true : forall o T, teq o T = true -> o = Some T.
Proof.
  intros [U|] T E; simpl in E; [apply ty_eqb_eq in E; subst; reflexivity|discriminate].
Qed.

Lemma teq_some : forall T, teq (Some T) T = true.
Proof. intros. simpl. apply ty_eqb_refl. Qed.

(* ------------------------------------------------------------------------ *)
(* Typing is compositional: replacing variables by terms of their types      *)
(* keeps every type. Shifting and instantiation are both such replacements.  *)

Fixpoint tsubst (sg : nat -> term) (t : term) : term :=
  match t with
  | Var i => sg i
  | Lit w s v => Lit w s v
  | Call d ts => Call d (map (tsubst sg) ts)
  | Prim o w s ts => Prim o w s (map (tsubst sg) ts)
  | Proj D i u => Proj D i (tsubst sg u)
  | Elem D u x => Elem D (tsubst sg u) (tsubst sg x)
  end.

Definition shift_sg (n c : nat) (i : nat) : term :=
  if Nat.ltb i c then Var i else Var (i + n).

Definition inst_sg (a : term) (k : nat) (i : nat) : term :=
  if Nat.eqb i k then shift k 0 a else if Nat.ltb k i then Var (i - 1) else Var i.

Lemma shift_as_tsubst : forall t n c, shift n c t = tsubst (shift_sg n c) t.
Proof.
  induction t using term_ind'; intros n c; simpl; auto.
  - f_equal. induction H; simpl; f_equal; auto.
  - f_equal. induction H; simpl; f_equal; auto.
  - f_equal; auto.
  - f_equal; auto.
Qed.

Lemma inst_as_tsubst : forall t a k, inst a k t = tsubst (inst_sg a k) t.
Proof.
  induction t using term_ind'; intros a k; simpl; auto.
  - f_equal. induction H; simpl; f_equal; auto.
  - f_equal. induction H; simpl; f_equal; auto.
  - f_equal; auto.
  - f_equal; auto.
Qed.

Lemma call_args_congr : forall G G' (f : term -> term) ts,
  Forall (fun u => type_of G' (f u) = type_of G u) ts ->
  forall ps,
    (fix go (ps : list ty) (ts : list term) {struct ts} : bool :=
       match ps, ts with
       | [], [] => true
       | p :: ps', u :: ts' => teq (type_of G' u) p && go ps' ts'
       | _, _ => false
       end) ps (map f ts) =
    (fix go (ps : list ty) (ts : list term) {struct ts} : bool :=
       match ps, ts with
       | [], [] => true
       | p :: ps', u :: ts' => teq (type_of G u) p && go ps' ts'
       | _, _ => false
       end) ps ts.
Proof.
  intros G G' f ts H. induction H as [| x xs Hx _ IH]; intros ps; destruct ps; simpl; auto.
  rewrite Hx, IH. reflexivity.
Qed.

Lemma type_of_tsubst : forall t G G' sg,
  (forall i, type_of G' (sg i) = type_of G (Var i)) ->
  type_of G' (tsubst sg t) = type_of G t.
Proof.
  induction t using term_ind'; intros G G' sg Hsg; simpl.
  - apply Hsg.
  - reflexivity.
  - destruct (sig d) as [[ps R]|]; auto.
    rewrite (call_args_congr G G' (tsubst sg)); auto.
    eapply Forall_impl; [|exact H]. intros u Hu. apply Hu. exact Hsg.
  - destruct ts as [|a [|b [|c [|d ts]]]]; simpl; auto;
      repeat match goal with
             | Hf : Forall _ (_ :: _) |- _ =>
                 let Hh := fresh "Hh" in let Ht := fresh "Ht" in
                 inversion Hf as [|? ? Hh Ht]; subst; clear Hf
             end;
      repeat match goal with
             | Hh : forall (G0 G0' : list ty) (sg0 : nat -> term), _ |- _ =>
                 specialize (Hh G G' sg Hsg)
             end;
      repeat match goal with
             | Hh : type_of G' (tsubst sg _) = _ |- _ => rewrite Hh; clear Hh
             end; reflexivity.
  - rewrite (IHt G G' sg Hsg). reflexivity.
  - rewrite (IHt1 G G' sg Hsg), (IHt2 G G' sg Hsg). reflexivity.
Qed.

Lemma nth_error_app_ge : forall (A : Type) (l1 l2 : list A) i,
  (length l1 <= i)%nat -> nth_error (l1 ++ l2) i = nth_error l2 (i - length l1).
Proof. intros. apply nth_error_app2. exact H. Qed.

Lemma type_of_shift : forall t G1 D G2,
  type_of (G1 ++ D ++ G2) (shift (length D) (length G1) t) = type_of (G1 ++ G2) t.
Proof.
  intros t G1 D G2. rewrite shift_as_tsubst. apply type_of_tsubst.
  intros i. unfold shift_sg. simpl. destruct (Nat.ltb i (length G1)) eqn:E.
  - apply Nat.ltb_lt in E. simpl. rewrite !nth_error_app1 by lia. reflexivity.
  - apply Nat.ltb_ge in E. simpl.
    rewrite !nth_error_app_ge by (rewrite ?app_length; lia).
    replace (i + length D - length G1 - length D)%nat with (i - length G1)%nat by lia.
    reflexivity.
Qed.

Lemma type_of_ok : forall t G T, type_of G t = Some T -> ty_ok T = true.
Proof.
  induction t using term_ind'; intros G T E; simpl in E.
  - destruct (nth_error G i) as [U|]; [|discriminate].
    destruct (ty_ok U) eqn:O; inversion E; subst; exact O.
  - destruct (int_ok w && in_rangeb w s v) eqn:O; inversion E; subst.
    apply andb_true_iff in O. simpl. tauto.
  - destruct (sig d) as [[ps R]|] eqn:S; [|discriminate].
    match type of E with (if ?c then _ else _) = _ => destruct c end;
      inversion E; subst. eapply sig_ok; eauto.
  - destruct (int_ok w) eqn:O; simpl in E; [|discriminate].
    destruct o; destruct ts as [|a [|b [|c [|d ts]]]]; try discriminate;
      repeat match type of E with
             | (if ?c then _ else _) = _ => destruct c
             | match ?x with _ => _ end = _ => destruct x
             end; try discriminate; inversion E; subst; simpl; auto.
  - destruct D; try discriminate.
    destruct (ty_ok (TValue s l) && teq (type_of G t) (TValue s l)) eqn:O; [|discriminate].
    apply andb_true_iff in O as [O _]. simpl in O.
    revert i E. induction l as [|x xs IH]; intros i E; destruct i; simpl in E; try discriminate.
    + inversion E; subst. apply andb_true_iff in O. tauto.
    + apply andb_true_iff in O as [_ O]. eapply IH; eauto.
  - destruct D; try discriminate.
    match type of E with (if ?c then _ else _) = _ => destruct c eqn:O end; [|discriminate].
    inversion E; subst. simpl in O. repeat rewrite andb_true_iff in O. tauto.
Qed.

Lemma type_of_inst : forall t G1 U G2 a,
  type_of G2 a = Some U ->
  type_of (G1 ++ G2) (inst a (length G1) t) = type_of (G1 ++ U :: G2) t.
Proof.
  intros t G1 U G2 a Ha. rewrite inst_as_tsubst. apply type_of_tsubst.
  intros i. unfold inst_sg. simpl. destruct (Nat.eqb i (length G1)) eqn:E1.
  - apply Nat.eqb_eq in E1. subst.
    pose proof (type_of_shift a [] G1 G2) as Hs. simpl in Hs. rewrite Hs, Ha.
    rewrite nth_error_app_ge by lia. rewrite Nat.sub_diag. simpl.
    rewrite (type_of_ok a G2 U Ha). reflexivity.
  - apply Nat.eqb_neq in E1. destruct (Nat.ltb (length G1) i) eqn:E2.
    + apply Nat.ltb_lt in E2. simpl.
      rewrite !nth_error_app_ge by lia.
      destruct (i - length G1)%nat eqn:D; [lia|]. simpl.
      replace (i - 1 - length G1)%nat with n by lia. reflexivity.
    + apply Nat.ltb_ge in E2. simpl. rewrite !nth_error_app1 by lia. reflexivity.
Qed.

Lemma valid_pshift : forall P G1 D G2,
  valid (G1 ++ D ++ G2) (pshift (length D) (length G1) P) = valid (G1 ++ G2) P.
Proof.
  induction P; intros G1 D G2; simpl.
  - rewrite !type_of_shift. reflexivity.
  - f_equal. apply (IHP (t :: G1) D G2).
  - rewrite IHP1, IHP2. reflexivity.
  - rewrite IHP1, IHP2. reflexivity.
  - rewrite IHP1, IHP2. reflexivity.
  - reflexivity.
Qed.

Lemma valid_pinst : forall P G1 U G2 a,
  type_of G2 a = Some U ->
  valid (G1 ++ G2) (pinst a (length G1) P) = valid (G1 ++ U :: G2) P.
Proof.
  induction P; intros G1 U G2 a Ha; simpl.
  - rewrite !(type_of_inst _ G1 U G2 a Ha). reflexivity.
  - f_equal. apply (IHP (t :: G1) U G2 a Ha).
  - rewrite (IHP1 G1 U G2 a Ha), (IHP2 G1 U G2 a Ha). reflexivity.
  - rewrite (IHP1 G1 U G2 a Ha), (IHP2 G1 U G2 a Ha). reflexivity.
  - rewrite (IHP1 G1 U G2 a Ha), (IHP2 G1 U G2 a Ha). reflexivity.
  - reflexivity.
Qed.

Corollary valid_pinst0 : forall P G U a,
  type_of G a = Some U -> valid (U :: G) P = true -> valid G (pinst a 0 P) = true.
Proof.
  intros P G U a Ha Hv. pose proof (valid_pinst P [] U G a Ha) as E. simpl in E.
  rewrite E. exact Hv.
Qed.

(* ------------------------------------------------------------------------ *)
(* Type soundness: a typed term denotes a value of its type.                 *)

Variable Abs : Type.
Variable I : interp Abs.

(* What an interpretation must give the parts of the core the kernel leaves
   uninterpreted: an observation of a value of its domain is a value of the
   observation's type, and a definition applied to values of its parameter
   types gives a value of its result type. *)
Hypothesis proj_ok : forall id obs i v T,
  ty_ok (TValue id obs) = true -> dom Abs I (TValue id obs) v ->
  nth_error obs i = Some T -> dom Abs I T (i_proj Abs I (TValue id obs) i v).
Hypothesis elem_ok : forall U k v X x,
  ty_ok (TIndexed U k) = true -> dom Abs I (TIndexed U k) v ->
  is_int X = true -> dom Abs I X x -> dom Abs I U (i_elem Abs I (TIndexed U k) v x).
Hypothesis call_ok : forall d ps R args,
  sig d = Some (ps, R) -> Forall2 (dom Abs I) ps args -> dom Abs I R (i_call Abs I d args).

Definition env_ok (G : list ty) (rho : list (val Abs)) : Prop := Forall2 (dom Abs I) G rho.

Lemma env_nth : forall G rho i T,
  env_ok G rho -> nth_error G i = Some T -> dom Abs I T (nth i rho dflt).
Proof.
  intros G rho i T H. revert i. induction H as [|T0 v G' rho' Hd _ IH]; intros i E.
  - destruct i; discriminate.
  - destruct i; simpl in E; [inversion E; subst; exact Hd|]. apply IH. exact E.
Qed.

Lemma env_length : forall G rho, env_ok G rho -> length rho = length G.
Proof. intros G rho H. induction H; simpl; auto. Qed.

Lemma dom_int : forall w s v,
  dom Abs I (TInt w s) v -> v = VZ (zv Abs v) /\ in_range w s (zv Abs v).
Proof. intros w s v [z [-> Hz]]. simpl. auto. Qed.

Lemma dom_int_intro : forall w s z, in_range w s z -> dom Abs I (TInt w s) (VZ z).
Proof. intros. exists z. auto. Qed.

Lemma b2v_u1 : forall b, dom Abs I u1 (b2v Abs b).
Proof.
  intros b. apply dom_int_intro. unfold in_range, min_int, max_int. simpl.
  destruct b; lia.
Qed.

Lemma int_ok_width : forall w, int_ok w = true -> (1 <= w)%nat.
Proof. intros w H. unfold int_ok in H. apply andb_true_iff in H as [H _]. apply Nat.leb_le. exact H. Qed.

Lemma u1_values : forall v, dom Abs I u1 v -> v = VZ 0 \/ v = VZ 1.
Proof.
  intros v [z [-> Hz]]. unfold in_range, min_int, max_int in Hz. simpl in Hz.
  assert (z = 0 \/ z = 1) as [->| ->] by lia; auto.
Qed.

Lemma type_sound : forall t G rho T,
  env_ok G rho -> type_of G t = Some T -> dom Abs I T (eval Abs I rho t).
Proof.
  induction t using term_ind'; intros G rho T Henv E; simpl in E |- *.
  - destruct (nth_error G i) eqn:N; [|discriminate].
    destruct (ty_ok t); inversion E; subst. eapply env_nth; eauto.
  - destruct (int_ok w && in_rangeb w s v) eqn:O; inversion E; subst.
    apply andb_true_iff in O as [_ O]. apply in_rangeb_iff in O. apply dom_int_intro. exact O.
  - destruct (sig d) as [[ps R]|] eqn:S; [|discriminate].
    match type of E with (if ?c then _ else _) = _ => destruct c eqn:C end; [|discriminate].
    inversion E; subst. eapply call_ok; [exact S|].
    clear S E. revert ps C. induction H as [|x xs Hx _ IH]; intros ps C; destruct ps;
      simpl in C; try discriminate; constructor.
    + apply andb_true_iff in C as [C _]. apply teq_true in C. eapply Hx; eauto.
    + apply andb_true_iff in C as [_ C]. apply IH. exact C.
  - destruct (int_ok w) eqn:O; simpl in E; [|discriminate].
    pose proof (int_ok_width w O) as Hw.
    destruct o; destruct ts as [|a [|b [|c [|d ts]]]]; try discriminate;
      repeat match goal with
             | Hf : Forall _ (_ :: _) |- _ =>
                 let Hh := fresh "Hh" in let Ht := fresh "Ht" in
                 inversion Hf as [|? ? Hh Ht]; subst; clear Hf
             end;
      repeat match type of E with
             | (if ?c then _ else _) = _ => destruct c eqn:?
             | match type_of ?G ?x with _ => _ end = _ => destruct (type_of G x) as [[]|] eqn:?
             end; try discriminate; inversion E; subst; clear E;
      repeat match goal with
             | Hb : (_ && _)%bool = true |- _ => apply andb_true_iff in Hb as [? ?]
             | Ht : teq _ _ = true |- _ => apply teq_true in Ht
             end;
      cbn [eval map prim_eval];
      try (apply b2v_u1);
      try (apply dom_int_intro; apply wrap_in_range; exact Hw).
    + (* ONot *)
      apply ty_eqb_eq in H. unfold u1 in H. inversion H; subst.
      pose proof (Hh _ _ _ Henv H0) as Ha. apply u1_values in Ha.
      destruct Ha as [Ha|Ha]; rewrite Ha; apply dom_int_intro;
        unfold in_range, min_int, max_int; simpl; lia.
    + (* OSelect *)
      destruct (zv Abs (eval Abs I rho a) =? 1); eauto.
    + (* OQuot *)
      destruct (zv Abs (eval Abs I rho b) =? 0); apply dom_int_intro;
        [apply range_has_zero; exact Hw|apply wrap_in_range; exact Hw].
    + (* ORem *)
      pose proof (Hh _ _ _ Henv H) as Ha. apply dom_int in Ha as [_ Ha].
      destruct (zv Abs (eval Abs I rho b) =? 0) eqn:Z0; apply dom_int_intro; [exact Ha|].
      apply rem_in_range; [exact Ha|]. apply Z.eqb_neq. exact Z0.
  - destruct D; try discriminate.
    destruct (ty_ok (TValue s l) && teq (type_of G t) (TValue s l)) eqn:O; [|discriminate].
    apply andb_true_iff in O as [O1 O2]. apply teq_true in O2.
    eapply proj_ok; eauto.
  - destruct D; try discriminate.
    match type of E with (if ?c then _ else _) = _ => destruct c eqn:O end; [|discriminate].
    inversion E; subst. repeat rewrite andb_true_iff in O. destruct O as [[O1 O2] O3].
    apply teq_true in O2.
    destruct (type_of G t2) as [X|] eqn:TX; [|discriminate].
    eapply elem_ok; eauto.
Qed.

(* ------------------------------------------------------------------------ *)
(* A condition as a proposition (KERNEL.md 7).                              *)

Definition b2z (b : bool) : Z := if b then 1 else 0.

Lemma strip_not_sound : forall c pos G rho,
  env_ok G rho -> type_of G c = Some u1 ->
  type_of G (fst (strip_not c pos)) = Some u1 /\
  (eval Abs I rho c = VZ (b2z pos) <->
   eval Abs I rho (fst (strip_not c pos)) = VZ (b2z (snd (strip_not c pos)))).
Proof.
  induction c using term_ind'; intros pos G rho Henv E; simpl; try (split; [exact E|tauto]).
  destruct o; try (split; [exact E|tauto]).
  destruct ts as [|a [|b ts]]; try (split; [exact E|tauto]).
  inversion H as [|? ? Ha _]; subst.
  simpl in E. destruct (int_ok w) eqn:O; simpl in E; [|discriminate].
  destruct (ty_eqb (TInt w s) u1) eqn:U; simpl in E; [|discriminate].
  apply ty_eqb_eq in U. unfold u1 in U. inversion U; subst.
  destruct (teq (type_of G a) (TInt 1 false)) eqn:T; [|discriminate]. apply teq_true in T.
  specialize (Ha (negb pos) G rho Henv T) as [Ht Hiff].
  split; [exact Ht|]. rewrite <- Hiff. simpl.
  pose proof (type_sound a G rho u1 Henv T) as Hv. apply u1_values in Hv.
  destruct Hv as [-> | ->]; destruct pos; simpl; split; intros Q;
    try (inversion Q; lia); try reflexivity.
Qed.

Lemma lit_u1 : forall G b, type_of G (Lit 1 false (b2z b)) = Some u1.
Proof. intros G b. simpl. destruct b; reflexivity. Qed.

Lemma pred_sound : forall c pos G rho,
  env_ok G rho -> type_of G c = Some u1 ->
  valid G (pred c pos) = true /\
  (holds Abs I rho (pred c pos) <-> eval Abs I rho c = VZ (b2z pos)).
Proof.
  intros c pos G rho Henv E. unfold pred.
  destruct (strip_not_sound c pos G rho Henv E) as [Ht Hiff]. rewrite Hiff.
  destruct (strip_not c pos) as [t p]. simpl in Ht |- *. clear Hiff.
  assert (Default : valid G (PEq u1 t (Lit 1 false (b2z p))) = true /\
                    (holds Abs I rho (PEq u1 t (Lit 1 false (b2z p))) <->
                     eval Abs I rho t = VZ (b2z p))).
  { cbn [valid holds]. rewrite Ht, lit_u1. cbn [teq eval]. rewrite ty_eqb_refl.
    simpl. tauto. }
  destruct t as [| | |o w0 s0 l| |]; try exact Default.
  destruct o; try exact Default; destruct l as [|a [|b [|x l]]]; try exact Default;
    simpl in Ht;
    (destruct (int_ok w0) eqn:O; simpl in Ht; [|discriminate]);
    (destruct (teq (type_of G a) (TInt w0 s0)) eqn:Ta; [|discriminate]);
    (destruct (teq (type_of G b) (TInt w0 s0)) eqn:Tb; [|discriminate]);
    pose proof (teq_true _ _ Ta) as Ta'; pose proof (teq_true _ _ Tb) as Tb';
    pose proof (type_sound a G rho _ Henv Ta') as [za [Ea _]];
    pose proof (type_sound b G rho _ Henv Tb') as [zb [Eb _]].
  - (* OEq *)
    destruct p.
    + split.
      * cbn [valid]. rewrite Ta, Tb. reflexivity.
      * simpl. rewrite ?Ea, ?Eb. unfold b2v, b2z; simpl.
        destruct (Z.eqb_spec za zb); simpl; split; intros; congruence.
    + split.
      * simpl. rewrite O, Ta, Tb. reflexivity.
      * simpl. tauto.
  - (* ONe *)
    destruct p.
    + split.
      * simpl. rewrite O, Ta, Tb. reflexivity.
      * simpl. rewrite ?Ea, ?Eb. unfold b2v, b2z; simpl.
        destruct (Z.eqb_spec za zb); simpl; split; intros; congruence.
    + split.
      * cbn [valid negb]. rewrite Ta, Tb. reflexivity.
      * simpl. rewrite ?Ea, ?Eb. unfold b2v, b2z; simpl.
        destruct (Z.eqb_spec za zb); simpl; split; intros; congruence.
Qed.

End Typing.
