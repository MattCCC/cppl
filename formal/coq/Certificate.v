(* Certificate checking for linear arithmetic (KERNEL.md 13; Checker in
   kernel/src/linear.cpp) and its soundness: an accepted certificate leaves the
   system with no integer solution.

   This is the second half of what rule 9 rests on. The first half, that the
   translation of facts and a negated goal into a system keeps every machine
   valuation making the facts true and the goal false as an integer solution
   (KERNEL.md 12), is proven in Linear.v. Checker.v takes the two halves
   together as its lin_sound hypothesis, and Linear.v discharges it
   (lin_sound_model, check_sound_closed).

   The kernel's arithmetic is 128-bit and rejects on overflow; this model's is
   unbounded, so it accepts at least what the kernel accepts. *)

From Coq Require Import ZArith List Bool Lia.
Import ListNotations.

Open Scope Z_scope.

(* sum(coefficient * variable) + constant <= 0 (LinearConstraint). *)
Record constraint : Type := { terms : list (nat * Z); constant : Z }.

Record system : Type := {
  nvars : nat;
  constraints : list constraint;
  disjunctions : list (constraint * constraint)
}.

Inductive certificate : Type :=
| FarkasSum : list (nat * Z) -> certificate                           (* (position, multiplier) *)
| IntegerSplit : list (nat * Z) -> Z -> certificate -> certificate -> certificate
| DisjunctionCases : nat -> certificate -> certificate -> certificate.

Definition linear (sg : nat -> Z) (ts : list (nat * Z)) : Z :=
  fold_right (fun vc acc => snd vc * sg (fst vc) + acc) 0 ts.

Definition value (sg : nat -> Z) (c : constraint) : Z := linear sg (terms c) + constant c.

Definition sat (sg : nat -> Z) (c : constraint) : Prop := value sg c <= 0.

(* ------------------------------------------------------------------------ *)
(* Summing coefficients per variable.                                        *)

Fixpoint add_coeff (v : nat) (a : Z) (m : list (nat * Z)) : list (nat * Z) :=
  match m with
  | [] => [(v, a)]
  | (u, b) :: m' => if Nat.eqb u v then (u, b + a) :: m' else (u, b) :: add_coeff v a m'
  end.

Lemma linear_add_coeff : forall sg v a m,
  linear sg (add_coeff v a m) = a * sg v + linear sg m.
Proof.
  intros sg v a m. induction m as [|[u b] m IH]; simpl.
  - lia.
  - destruct (Nat.eqb_spec u v); simpl.
    + subst. lia.
    + rewrite IH. lia.
Qed.

Definition add_scaled (k : Z) (ts : list (nat * Z)) (m : list (nat * Z)) : list (nat * Z) :=
  fold_right (fun vc acc => add_coeff (fst vc) (k * snd vc) acc) m ts.

Lemma linear_add_scaled : forall sg k ts m,
  linear sg (add_scaled k ts m) = k * linear sg ts + linear sg m.
Proof.
  intros sg k ts m. induction ts as [|[v a] ts IH]; simpl.
  - lia.
  - unfold add_scaled in *. simpl. rewrite linear_add_coeff. rewrite IH. simpl. lia.
Qed.

Definition all_zero (m : list (nat * Z)) : bool := forallb (fun vc => Z.eqb (snd vc) 0) m.

Lemma linear_all_zero : forall sg m, all_zero m = true -> linear sg m = 0.
Proof.
  intros sg m. induction m as [|[v a] m IH]; simpl; intros H; [reflexivity|].
  apply andb_true_iff in H as [H1 H2]. apply Z.eqb_eq in H1. subst. rewrite IH by exact H2. lia.
Qed.

(* ------------------------------------------------------------------------ *)
(* The checker.                                                              *)

Fixpoint increasing (ps : list nat) : bool :=
  match ps with
  | [] => true
  | [_] => true
  | p :: ((q :: _) as rest) => Nat.ltb p q && increasing rest
  end.

(* contradiction(): the named standing constraints, in increasing order, each
   once, each with a positive multiplier, sum to no variable and a positive
   constant. *)
Definition farkas (active : list constraint) (ms : list (nat * Z)) : bool :=
  match ms with
  | [] => false
  | _ =>
      increasing (map fst ms) &&
      forallb (fun pm => Nat.ltb (fst pm) (length active) && Z.ltb 0 (snd pm)) ms &&
      let coeffs := fold_right (fun pm acc =>
                      add_scaled (snd pm) (terms (nth (fst pm) active {| terms := []; constant := 0 |})) acc)
                      [] ms in
      let k := fold_right (fun pm acc =>
                 snd pm * constant (nth (fst pm) active {| terms := []; constant := 0 |}) + acc) 0 ms in
      all_zero coeffs && Z.ltb 0 k
  end.

Definition increasing_vars (ts : list (nat * Z)) (n : nat) : bool :=
  increasing (map fst ts) && forallb (fun vc => Nat.ltb (fst vc) n && negb (Z.eqb (snd vc) 0)) ts.

Definition negate_plus_one (c : constraint) : constraint :=
  {| terms := map (fun vc => (fst vc, - snd vc)) (terms c); constant := - constant c + 1 |}.

Fixpoint refutes (s : system) (active : list constraint) (cert : certificate) : bool :=
  match cert with
  | FarkasSum ms => farkas active ms
  | IntegerSplit ts k below above =>
      let form := {| terms := ts; constant := k |} in
      increasing_vars ts (nvars s) &&
      refutes s (active ++ [form]) below &&
      refutes s (active ++ [negate_plus_one form]) above
  | DisjunctionCases d first second =>
      match nth_error (disjunctions s) d with
      | Some (m0, m1) => refutes s (active ++ [m0]) first && refutes s (active ++ [m1]) second
      | None => false
      end
  end.

Definition check_certificate (s : system) (cert : certificate) : bool :=
  refutes s (constraints s) cert.

(* ------------------------------------------------------------------------ *)
(* Soundness.                                                                *)

Lemma farkas_sound : forall active ms sg,
  farkas active ms = true -> Forall (sat sg) active -> False.
Proof.
  intros active ms sg E Hsat.
  destruct ms as [|pm ms']; [discriminate|].
  unfold farkas in E. cbv beta iota zeta in E. repeat rewrite andb_true_iff in E.
  destruct E as [[_ Hpos] [Hzero Hk]]. apply Z.ltb_lt in Hk.
  set (ms := pm :: ms') in *.
  set (dflt := {| terms := []; constant := 0 |}) in *.
  (* The weighted sum of the named constraints' values is the constant. *)
  assert (Hsum : forall l,
    fold_right (fun pm acc => snd pm * value sg (nth (fst pm) active dflt) + acc) 0 l =
    linear sg (fold_right (fun pm acc => add_scaled (snd pm) (terms (nth (fst pm) active dflt)) acc) [] l) +
    fold_right (fun pm acc => snd pm * constant (nth (fst pm) active dflt) + acc) 0 l).
  { induction l as [|[p m] l IH]; simpl; [reflexivity|].
    rewrite linear_add_scaled, IH. unfold value. ring. }
  (* Each named constraint holds, so each weighted value is at most zero. *)
  assert (Hle : forall l, forallb (fun pm => Nat.ltb (fst pm) (length active) && Z.ltb 0 (snd pm)) l = true ->
    fold_right (fun pm acc => snd pm * value sg (nth (fst pm) active dflt) + acc) 0 l <= 0).
  { induction l as [|[p m] l IH]; simpl; intros H; [lia|].
    apply andb_true_iff in H as [H1 H2]. apply andb_true_iff in H1 as [Hp Hm].
    apply Nat.ltb_lt in Hp. apply Z.ltb_lt in Hm.
    assert (value sg (nth p active dflt) <= 0).
    { rewrite Forall_forall in Hsat. apply Hsat. apply nth_In. exact Hp. }
    specialize (IH H2). nia. }
  specialize (Hle ms Hpos). rewrite Hsum in Hle.
  rewrite (linear_all_zero sg _ Hzero) in Hle. lia.
Qed.

Lemma split_cover : forall sg ts k,
  sat sg {| terms := ts; constant := k |} \/ sat sg (negate_plus_one {| terms := ts; constant := k |}).
Proof.
  intros sg ts k. unfold sat, value, negate_plus_one. simpl.
  assert (Hneg : forall l, linear sg (map (fun vc => (fst vc, - snd vc)) l) = - linear sg l).
  { induction l as [|[v a] l IH]; simpl; [reflexivity|]. rewrite IH. lia. }
  rewrite Hneg. lia.
Qed.

Theorem refutes_sound : forall s cert active sg,
  refutes s active cert = true ->
  Forall (sat sg) active ->
  (forall m0 m1, In (m0, m1) (disjunctions s) -> sat sg m0 \/ sat sg m1) ->
  False.
Proof.
  intros s cert. induction cert as [ms|ts k below IHb above IHa|d first IHf second IHs];
    intros active sg E Hsat Hdis; simpl in E.
  - eapply farkas_sound; eauto.
  - repeat rewrite andb_true_iff in E. destruct E as [[_ Eb] Ea].
    destruct (split_cover sg ts k) as [H|H].
    + eapply IHb; [exact Eb| |exact Hdis]. apply Forall_app. split; [exact Hsat|]. constructor; auto.
    + eapply IHa; [exact Ea| |exact Hdis]. apply Forall_app. split; [exact Hsat|]. constructor; auto.
  - destruct (nth_error (disjunctions s) d) as [[m0 m1]|] eqn:N; [|discriminate].
    apply andb_true_iff in E as [Ef Es].
    apply nth_error_In in N. destruct (Hdis m0 m1 N) as [H|H].
    + eapply IHf; [exact Ef| |exact Hdis]. apply Forall_app. split; [exact Hsat|]. constructor; auto.
    + eapply IHs; [exact Es| |exact Hdis]. apply Forall_app. split; [exact Hsat|]. constructor; auto.
Qed.

(* An accepted certificate leaves the system with no integer solution: no
   assignment satisfies every constraint and one member of every
   disjunction. *)
Corollary check_certificate_sound : forall s cert,
  check_certificate s cert = true ->
  forall sg, Forall (sat sg) (constraints s) ->
  (forall m0 m1, In (m0, m1) (disjunctions s) -> sat sg m0 \/ sat sg m1) ->
  False.
Proof. intros s cert E sg. eapply refutes_sound; eauto. Qed.
