(* An interpretation exists, so the soundness theorem of Checker.v is not
   vacuous, and with the two procedures it takes as parameters set to ones
   that are sound by construction, every hypothesis it names is discharged.

   syntactic_consistency is therefore unconditional: the checker whose
   reflexivity compares terms as written, and which has no arithmetic step,
   accepts no evidence for False, whatever the definitions admitted. That
   checker is rules 2 to 8 and 10 to 15 of KERNEL.md 11 exactly; the kernel
   differs from it only in deciding reflexivity by normalization (rule 1) and
   in having rule 9, whose soundness is the M2 and M3 hypotheses of Checker.v.
   Normalize.v and Linear.v discharge those for models of the kernel's
   normalization and arithmetic (check_sound_closed).

   Audit.v prints the assumptions every theorem rests on. *)

From Coq Require Import ZArith List Bool Lia.
Import ListNotations.
From CppL Require Import Syntax Semantics Typing Checker.

Definition trivial_interp : interp unit := {|
  i_call := fun _ _ => VZ 0;
  i_proj := fun _ _ _ => VZ 0;
  i_elem := fun _ _ _ => VZ 0;
  i_dom := fun _ _ => True
|}.

Lemma zero_inhabits : forall T, ty_ok T = true -> dom unit trivial_interp T (VZ 0%Z).
Proof.
  intros [w s| |] O; simpl; auto.
  exists 0%Z. split; [reflexivity|]. apply range_has_zero.
  apply int_ok_width. exact O.
Qed.

Lemma ty_ok_observation : forall id obs i T,
  ty_ok (TValue id obs) = true -> nth_error obs i = Some T -> ty_ok T = true.
Proof.
  intros id obs. simpl. induction obs as [|x xs IH]; intros i T O N; destruct i;
    simpl in N; try discriminate.
  - inversion N; subst. apply andb_true_iff in O. tauto.
  - apply andb_true_iff in O as [_ O]. eapply IH; eauto.
Qed.

Theorem syntactic_consistency :
  forall (sig : nat -> option (list ty * ty)),
  (forall d ps R, sig d = Some (ps, R) -> ty_ok R = true) ->
  forall (cert : Type) (e : evid cert),
  check sig (fun t => t) cert (fun _ _ _ _ => false) PFalse e = false.
Proof.
  intros sig sig_ok cert e.
  apply (check_consistent sig sig_ok (fun t => t) cert (fun _ _ _ _ => false) unit trivial_interp).
  - intros id obs i v T O _ N. simpl. apply zero_inhabits. eapply ty_ok_observation; eauto.
  - intros U k v X x O _ _ _. simpl. apply zero_inhabits.
    simpl in O. apply andb_true_iff in O. tauto.
  - intros d ps R args S _. simpl. apply zero_inhabits. eapply sig_ok; eauto.
  - intros. reflexivity.
  - intros G Fs P k rho E. discriminate.
Qed.

(* The same checker is sound, not only consistent: whatever it accepts holds
   in every interpretation meeting the conditions on observations and
   definitions, with no hypothesis about normalization or arithmetic. *)
Theorem syntactic_soundness :
  forall (sig : nat -> option (list ty * ty)),
  (forall d ps R, sig d = Some (ps, R) -> ty_ok R = true) ->
  forall (cert : Type) (Abs : Type) (I : interp Abs),
  (forall id obs i v T,
      ty_ok (TValue id obs) = true -> dom Abs I (TValue id obs) v ->
      nth_error obs i = Some T -> dom Abs I T (i_proj Abs I (TValue id obs) i v)) ->
  (forall U k v X x,
      ty_ok (TIndexed U k) = true -> dom Abs I (TIndexed U k) v ->
      is_int X = true -> dom Abs I X x -> dom Abs I U (i_elem Abs I (TIndexed U k) v x)) ->
  (forall d ps R args,
      sig d = Some (ps, R) -> Forall2 (dom Abs I) ps args -> dom Abs I R (i_call Abs I d args)) ->
  forall P (e : evid cert),
  check sig (fun t => t) cert (fun _ _ _ _ => false) P e = true -> holds Abs I [] P.
Proof.
  intros sig sig_ok cert Abs I Hp He Hc P e E.
  eapply (check_sound sig sig_ok (fun t => t) cert (fun _ _ _ _ => false) Abs I); eauto.
  intros G Fs Q k rho L. discriminate.
Qed.
