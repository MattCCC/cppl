(* The checking judgment (KERNEL.md 10, 11; check_under and check in
   kernel/src/check.cpp) and its soundness: whatever evidence the checker
   accepts, the proposition it accepts holds in every interpretation and every
   environment. Evidence is arbitrary input here, malformed or not, so this is
   the statement that no evidence can manufacture a proposition.

   Two procedures the rules call are parameters, each with the one property
   the proof needs of it (KERNEL.md 17, M2 and M3):

   - nf, normalization (KERNEL.md 9), used by reflexivity, must preserve the
     meaning of a typed term;
   - lin_ok, the arithmetic translation and certificate check (KERNEL.md 12,
     13), used by linear arithmetic, must accept only when the facts entail
     the goal.

   Everything else the rules do - typing, well-formedness, structural
   comparison, shifting, substitution, the hypothesis context, the premises
   of induction and of the conditional rule - is defined here and proven. *)

From Coq Require Import ZArith List Bool Lia.
Import ListNotations.
From CppL Require Import Syntax Semantics Typing.

Open Scope Z_scope.

Section Checker.

Variable sig : nat -> option (list ty * ty).
Hypothesis sig_ok : forall d ps R, sig d = Some (ps, R) -> ty_ok R = true.

Variable nf : term -> term.
Variable cert : Type.
Variable lin_ok : list ty -> list prop -> prop -> cert -> bool.

(* Evidence (KERNEL.md 10, kernel/include/cppl/kernel/proof.hpp). *)
Inductive evid : Type :=
| ERefl                                                          (* refl *)
| EAllI : ty -> evid -> evid                                     (* forall_intro(T, e) *)
| EAllE : prop -> evid -> term -> evid                           (* forall_elim(P, e, t) *)
| EHyp : nat -> evid                                             (* hyp(k) *)
| EImpI : prop -> evid -> evid                                   (* implies_intro(P, e) *)
| EImpE : prop -> evid -> evid -> evid                           (* implies_elim(P, e1, e2) *)
| EEqE : ty -> term -> term -> prop -> evid -> evid -> evid      (* eq_elim(T, a, b, C, e1, e2) *)
| ECond : ty -> term -> term -> term -> prop -> evid -> evid -> evid
                                                    (* cond_elim(T, c, t, f, M, e1, e2) *)
| ELin : list (prop * evid) -> cert -> evid                      (* linear(F1:e1..; cert) *)
| EAndI : evid -> evid -> evid                                   (* and_intro(e1, e2) *)
| EAndE : prop -> evid -> bool -> evid                           (* and_elim(P, e, side) *)
| EOrI : evid -> bool -> evid                                    (* or_intro(e, side) *)
| EOrE : prop -> evid -> evid -> evid -> evid                    (* or_elim(P, e, e1, e2) *)
| EFalseE : evid -> evid                                         (* false_elim(e) *)
| EInd : ty -> evid -> evid -> evid.                             (* unsigned_induction(T, e1, e2) *)

Section EvidInd.
  Variable Q : evid -> Prop.
  Hypothesis HRefl : Q ERefl.
  Hypothesis HAllI : forall T e, Q e -> Q (EAllI T e).
  Hypothesis HAllE : forall R e t, Q e -> Q (EAllE R e t).
  Hypothesis HHyp : forall k, Q (EHyp k).
  Hypothesis HImpI : forall R e, Q e -> Q (EImpI R e).
  Hypothesis HImpE : forall R e1 e2, Q e1 -> Q e2 -> Q (EImpE R e1 e2).
  Hypothesis HEqE : forall T a b C e1 e2, Q e1 -> Q e2 -> Q (EEqE T a b C e1 e2).
  Hypothesis HCond : forall T c t f M e1 e2, Q e1 -> Q e2 -> Q (ECond T c t f M e1 e2).
  Hypothesis HLin : forall fs k, Forall (fun fe => Q (snd fe)) fs -> Q (ELin fs k).
  Hypothesis HAndI : forall e1 e2, Q e1 -> Q e2 -> Q (EAndI e1 e2).
  Hypothesis HAndE : forall R e b, Q e -> Q (EAndE R e b).
  Hypothesis HOrI : forall e b, Q e -> Q (EOrI e b).
  Hypothesis HOrE : forall R e e1 e2, Q e -> Q e1 -> Q e2 -> Q (EOrE R e e1 e2).
  Hypothesis HFalseE : forall e, Q e -> Q (EFalseE e).
  Hypothesis HInd : forall T e1 e2, Q e1 -> Q e2 -> Q (EInd T e1 e2).

  Fixpoint evid_ind' (e : evid) : Q e :=
    match e with
    | ERefl => HRefl
    | EAllI T e => HAllI T e (evid_ind' e)
    | EAllE P e t => HAllE P e t (evid_ind' e)
    | EHyp k => HHyp k
    | EImpI P e => HImpI P e (evid_ind' e)
    | EImpE P e1 e2 => HImpE P e1 e2 (evid_ind' e1) (evid_ind' e2)
    | EEqE T a b C e1 e2 => HEqE T a b C e1 e2 (evid_ind' e1) (evid_ind' e2)
    | ECond T c t f M e1 e2 => HCond T c t f M e1 e2 (evid_ind' e1) (evid_ind' e2)
    | ELin fs k =>
        HLin fs k
          ((fix go (l : list (prop * evid)) : Forall (fun fe => Q (snd fe)) l :=
              match l with
              | [] => @Forall_nil _ (fun fe => Q (snd fe))
              | x :: xs => @Forall_cons _ (fun fe => Q (snd fe)) x xs (evid_ind' (snd x)) (go xs)
              end) fs)
    | EAndI e1 e2 => HAndI e1 e2 (evid_ind' e1) (evid_ind' e2)
    | EAndE P e b => HAndE P e b (evid_ind' e)
    | EOrI e b => HOrI e b (evid_ind' e)
    | EOrE P e e1 e2 => HOrE P e e1 e2 (evid_ind' e) (evid_ind' e1) (evid_ind' e2)
    | EFalseE e => HFalseE e (evid_ind' e)
    | EInd T e1 e2 => HInd T e1 e2 (evid_ind' e1) (evid_ind' e2)
    end.
End EvidInd.

(* The premises of unsigned induction, stated by the kernel itself
   (induction_base and induction_step in kernel/src/proof.cpp). *)
Definition ind_base (w : nat) (Q : prop) : prop := pinst (Lit w false 0) 0 Q.

Definition ind_step (w : nat) (Q : prop) : prop :=
  PAll (TInt w false)
    (PImp (pred (Prim OLt w false [Var 0; Lit w false (max_int w false)]) true)
          (PImp Q (pinst (Prim AddWrap w false [Var 0; Lit w false 1]) 0 (pshift 1 1 Q)))).

Definition is_eq (P : prop) : bool := match P with PEq _ _ _ => true | _ => false end.
Definition is_eq_or_false (P : prop) : bool :=
  match P with PEq _ _ _ | PFalse => true | _ => false end.

(* check_under. G is the binder types, innermost first; H the hypotheses,
   newest first, each with the number of binders enclosing it when it was
   introduced. The kernel's resource limits (term depth, normalization steps,
   certificate size, fact count) only reject, so they are left out: this
   checker accepts at least what the kernel accepts, and its soundness bounds
   the kernel's. *)
Fixpoint chk (G : list ty) (H : list (prop * nat)) (P : prop) (e : evid) {struct e} : bool :=
  match e with
  | ECond T c t f M e1 e2 =>
      match T with
      | TInt w s =>
          teq (type_of sig G (Prim OSelect w s [c; t; f])) T &&
          valid sig (T :: G) M &&
          prop_eqb (pinst (Prim OSelect w s [c; t; f]) 0 M) P &&
          chk G H (PImp (pred c true) (pinst t 0 M)) e1 &&
          chk G H (PImp (pred c false) (pinst f 0 M)) e2
      | _ => false
      end
  | ELin fs k =>
      is_eq_or_false P &&
      (fix go (fs : list (prop * evid)) : bool :=
         match fs with
         | [] => true
         | (F, ef) :: fs' => valid sig G F && is_eq F && chk G H F ef && go fs'
         end) fs &&
      lin_ok G (map fst fs) P k
  | EAllE Q e1 t =>
      valid sig G Q &&
      match Q with
      | PAll T B => chk G H Q e1 && teq (type_of sig G t) T && prop_eqb (pinst t 0 B) P
      | _ => false
      end
  | EImpE Q e1 e2 =>
      valid sig G Q &&
      match Q with
      | PImp A B => chk G H Q e1 && chk G H A e2 && prop_eqb B P
      | _ => false
      end
  | EAndE Q e1 side =>
      valid sig G Q &&
      match Q with
      | PAnd A B => chk G H Q e1 && prop_eqb (if side then B else A) P
      | _ => false
      end
  | EOrE Q e0 e1 e2 =>
      valid sig G Q &&
      match Q with
      | POr A B => chk G H Q e0 && chk G H (PImp A P) e1 && chk G H (PImp B P) e2
      | _ => false
      end
  | EFalseE e1 => chk G H PFalse e1
  | EEqE T a b C e1 e2 =>
      valid sig G (PEq T a b) && chk G H (PEq T a b) e1 &&
      valid sig (T :: G) C && chk G H (pinst b 0 C) e2 &&
      prop_eqb (pinst a 0 C) P
  | EHyp k =>
      match nth_error H k with
      | Some (A, n) => prop_eqb (pshift (length G - n) 0 A) P
      | None => false
      end
  | EInd T e1 e2 =>
      match P with
      | PAll T' Q =>
          ty_eqb T T' &&
          match T' with
          | TInt w false => chk G H (ind_base w Q) e1 && chk G H (ind_step w Q) e2
          | _ => false
          end
      | _ => false
      end
  | EAllI T e1 =>
      match P with
      | PAll T' Q => ty_eqb T T' && chk (T' :: G) H Q e1
      | _ => false
      end
  | EImpI A e1 =>
      match P with
      | PImp A' B => prop_eqb A A' && chk G ((A', length G) :: H) B e1
      | _ => false
      end
  | EAndI e1 e2 =>
      match P with
      | PAnd A B => chk G H A e1 && chk G H B e2
      | _ => false
      end
  | EOrI e1 side =>
      match P with
      | POr A B => chk G H (if side then B else A) e1
      | _ => false
      end
  | ERefl =>
      match P with
      | PEq _ a b => term_eqb (nf a) (nf b)
      | _ => false
      end
  end.

(* check: the goal must be well formed, and nothing is supposed to begin with. *)
Definition check (P : prop) (e : evid) : bool := valid sig [] P && chk [] [] P e.

(* ------------------------------------------------------------------------ *)
(* Soundness.                                                                *)

Variable Abs : Type.
Variable I : interp Abs.

Hypothesis proj_ok : forall id obs i v T,
  ty_ok (TValue id obs) = true -> dom Abs I (TValue id obs) v ->
  nth_error obs i = Some T -> dom Abs I T (i_proj Abs I (TValue id obs) i v).
Hypothesis elem_ok : forall U k v X x,
  ty_ok (TIndexed U k) = true -> dom Abs I (TIndexed U k) v ->
  is_int X = true -> dom Abs I X x -> dom Abs I U (i_elem Abs I (TIndexed U k) v x).
Hypothesis call_ok : forall d ps R args,
  sig d = Some (ps, R) -> Forall2 (dom Abs I) ps args -> dom Abs I R (i_call Abs I d args).

(* M2: normalization preserves the meaning of a typed term. *)
Hypothesis nf_sound : forall G rho t T,
  env_ok Abs I G rho -> type_of sig G t = Some T -> eval Abs I rho (nf t) = eval Abs I rho t.

(* M3: an accepted certificate means the facts entail the goal. *)
Hypothesis lin_sound : forall G Fs P k rho,
  lin_ok G Fs P k = true ->
  Forall (fun F => valid sig G F = true) Fs -> valid sig G P = true ->
  env_ok Abs I G rho -> Forall (holds Abs I rho) Fs -> holds Abs I rho P.

(* Every hypothesis in the context holds, restated for the binders introduced
   since it was supposed. *)
Definition hyps_ok (G : list ty) (rho : list (val Abs)) (H : list (prop * nat)) : Prop :=
  Forall (fun An => (snd An <= length G)%nat /\ holds Abs I (skipn (length G - snd An) rho) (fst An)) H.

Lemma hyps_ok_binder : forall G rho H T v,
  hyps_ok G rho H -> hyps_ok (T :: G) (v :: rho) H.
Proof.
  intros G rho H T v Hh. unfold hyps_ok in *. eapply Forall_impl; [|exact Hh].
  intros [A n] [Hn HA]. cbn [fst snd] in *. cbn [length]. split; [lia|].
  replace (S (length G) - n)%nat with (S (length G - n)) by lia. cbn [skipn]. exact HA.
Qed.

Lemma type_sound' : forall t G rho T,
  env_ok Abs I G rho -> type_of sig G t = Some T -> dom Abs I T (eval Abs I rho t).
Proof. intros. eapply type_sound; eauto. Qed.

Lemma pred_sound' : forall c pos G rho,
  env_ok Abs I G rho -> type_of sig G c = Some u1 ->
  valid sig G (pred c pos) = true /\
  (holds Abs I rho (pred c pos) <-> eval Abs I rho c = VZ (b2z pos)).
Proof. intros. eapply pred_sound; eauto. Qed.

Lemma lin_facts : forall G H fs,
  (fix go (fs : list (prop * evid)) : bool :=
     match fs with
     | [] => true
     | (F, ef) :: fs' => valid sig G F && is_eq F && chk G H F ef && go fs'
     end) fs = true ->
  Forall (fun fe => valid sig G (fst fe) = true /\ chk G H (fst fe) (snd fe) = true) fs.
Proof.
  intros G H fs E. induction fs as [|[F ef] fs IH]; constructor.
  - repeat rewrite andb_true_iff in E. simpl. tauto.
  - apply IH. repeat rewrite andb_true_iff in E. tauto.
Qed.

Lemma holds_iff_eval : forall rho T a b, holds Abs I rho (PEq T a b) <-> eval Abs I rho a = eval Abs I rho b.
Proof. intros. simpl. tauto. Qed.

Lemma type_lit : forall G w s v, int_ok w = true -> in_range w s v ->
  type_of sig G (Lit w s v) = Some (TInt w s).
Proof.
  intros G w s v O R. simpl. rewrite O. apply in_rangeb_iff in R. rewrite R. reflexivity.
Qed.

Lemma type_var0 : forall G T, ty_ok T = true -> type_of sig (T :: G) (Var 0) = Some T.
Proof. intros G T O. simpl. rewrite O. reflexivity. Qed.

Lemma type_lt : forall G w s a b, int_ok w = true ->
  type_of sig G a = Some (TInt w s) -> type_of sig G b = Some (TInt w s) ->
  type_of sig G (Prim OLt w s [a; b]) = Some u1.
Proof.
  intros G w s a b O Ta Tb. simpl. rewrite O. simpl. rewrite Ta, Tb. simpl.
  rewrite ty_eqb_refl. reflexivity.
Qed.

Lemma type_add : forall G w s a b, int_ok w = true ->
  type_of sig G a = Some (TInt w s) -> type_of sig G b = Some (TInt w s) ->
  type_of sig G (Prim AddWrap w s [a; b]) = Some (TInt w s).
Proof.
  intros G w s a b O Ta Tb. simpl. rewrite O. simpl. rewrite Ta, Tb. simpl.
  rewrite ty_eqb_refl. reflexivity.
Qed.

(* Every unsigned value is reached from 0 by successor steps below the
   maximum (FOUNDATIONS.md 74): the principle rule 15 rests on. *)
Lemma unsigned_induction_principle : forall (w : nat) (Pz : Z -> Prop),
  Pz 0 -> (forall z, 0 <= z < max_int w false -> Pz z -> Pz (z + 1)) ->
  forall z, 0 <= z <= max_int w false -> Pz z.
Proof.
  intros w Pz H0 HS z Hz.
  replace z with (Z.of_nat (Z.to_nat z)) by lia.
  assert (Hn : (Z.of_nat (Z.to_nat z) <= max_int w false)) by lia.
  clear Hz. induction (Z.to_nat z) as [|n IH].
  - exact H0.
  - rewrite Nat2Z.inj_succ. unfold Z.succ. apply HS; [lia|]. apply IH. lia.
Qed.

Theorem chk_sound : forall e G H P,
  chk G H P e = true -> valid sig G P = true ->
  forall rho, env_ok Abs I G rho -> hyps_ok G rho H -> holds Abs I rho P.
Proof.
  induction e using evid_ind'; intros G Hs P E Hv rho Henv Hh; simpl in E.
  - (* refl *)
    destruct P as [T a b| | | | |]; try discriminate.
    apply term_eqb_eq in E. simpl in Hv |- *. apply andb_true_iff in Hv as [Ta Tb].
    apply teq_true in Ta. apply teq_true in Tb.
    rewrite <- (nf_sound G rho a T Henv Ta), <- (nf_sound G rho b T Henv Tb), E.
    reflexivity.
  - (* forall_intro *)
    destruct P as [| T' Q| | | |]; try discriminate.
    apply andb_true_iff in E as [ET E]. apply ty_eqb_eq in ET. subst.
    simpl in Hv |- *. apply andb_true_iff in Hv as [_ Hv].
    intros v Hdv. eapply IHe; eauto.
    + constructor; assumption.
    + apply hyps_ok_binder. exact Hh.
  - (* forall_elim *)
    apply andb_true_iff in E as [VQ E].
    destruct R as [| T B| | | |]; try discriminate.
    repeat rewrite andb_true_iff in E. destruct E as [[E1 Et] EP].
    apply prop_eqb_eq in EP. subst P.
    apply teq_true in Et.
    pose proof (IHe G Hs (PAll T B) E1 VQ rho Henv Hh) as HQ. simpl in HQ.
    apply holds_pinst0. apply HQ. eapply type_sound'; eauto.
  - (* hyp *)
    destruct (nth_error Hs k) as [[A n]|] eqn:N; [|discriminate].
    apply prop_eqb_eq in E. subst P.
    unfold hyps_ok in Hh. rewrite Forall_forall in Hh.
    apply nth_error_In in N. specialize (Hh _ N) as [Hn HA]. simpl in Hn, HA.
    apply holds_pshift0. exact HA.
  - (* implies_intro *)
    destruct P as [| |A' B| | |]; try discriminate.
    apply andb_true_iff in E as [EA E]. simpl in Hv |- *.
    apply andb_true_iff in Hv as [VA VB]. intros HA.
    eapply IHe; eauto. constructor; [|exact Hh]. simpl. split; [lia|].
    rewrite Nat.sub_diag. exact HA.
  - (* implies_elim *)
    apply andb_true_iff in E as [VQ E].
    destruct R as [| |A B| | |]; try discriminate.
    repeat rewrite andb_true_iff in E. destruct E as [[E1 E2] EP].
    apply prop_eqb_eq in EP. subst P.
    pose proof (IHe1 G Hs (PImp A B) E1 VQ rho Henv Hh) as HAB. simpl in HAB.
    simpl in VQ. apply andb_true_iff in VQ as [VA VB].
    apply HAB. eapply IHe2; eauto.
  - (* eq_elim *)
    repeat rewrite andb_true_iff in E. destruct E as [[[[[Ta Tb] E1] VC] E2] EP].
    apply prop_eqb_eq in EP. subst P.
    assert (VE : valid sig G (PEq T a b) = true) by (simpl; rewrite Ta, Tb; reflexivity).
    pose proof (IHe1 G Hs (PEq T a b) E1 VE rho Henv Hh) as Hab. simpl in Hab.
    apply teq_true in Tb.
    assert (Vb : valid sig G (pinst b 0 C) = true) by (eapply valid_pinst0; eauto).
    pose proof (IHe2 G Hs (pinst b 0 C) E2 Vb rho Henv Hh) as HCb.
    apply holds_pinst0 in HCb. apply holds_pinst0. rewrite Hab. exact HCb.
  - (* cond_elim *)
    destruct T as [w s| |]; try discriminate.
    cbv beta iota in E. repeat rewrite andb_true_iff in E. destruct E as [[[[Tsel VM] EP] E1] E2].
    apply prop_eqb_eq in EP. subst P.
    apply teq_true in Tsel.
    assert (Tsel' := Tsel). simpl in Tsel'.
    destruct (int_ok w) eqn:O; simpl in Tsel'; [|discriminate].
    destruct (teq (type_of sig G c) u1) eqn:Tc; [|discriminate].
    destruct (teq (type_of sig G t) (TInt w s)) eqn:Tt; [|discriminate].
    destruct (teq (type_of sig G f) (TInt w s)) eqn:Tf; [|discriminate].
    apply teq_true in Tc. apply teq_true in Tt. apply teq_true in Tf.
    destruct (pred_sound' c true G rho Henv Tc) as [Vt Ht].
    destruct (pred_sound' c false G rho Henv Tc) as [Vf Hf].
    assert (V1 : valid sig G (PImp (pred c true) (pinst t 0 M)) = true).
    { simpl. rewrite Vt. simpl. eapply valid_pinst0; eauto. }
    assert (V2 : valid sig G (PImp (pred c false) (pinst f 0 M)) = true).
    { simpl. rewrite Vf. simpl. eapply valid_pinst0; eauto. }
    pose proof (IHe1 G Hs _ E1 V1 rho Henv Hh) as H1. simpl in H1.
    pose proof (IHe2 G Hs _ E2 V2 rho Henv Hh) as H2. simpl in H2.
    apply holds_pinst0.
    pose proof (type_sound' c G rho u1 Henv Tc) as Hc. apply u1_values in Hc.
    simpl. destruct Hc as [Hc|Hc]; rewrite Hc; simpl.
    + apply holds_pinst0. apply H2. apply Hf. exact Hc.
    + apply holds_pinst0. apply H1. apply Ht. exact Hc.
  - (* linear *)
    repeat rewrite andb_true_iff in E. destruct E as [[_ Efs] Elin].
    apply lin_facts in Efs.
    eapply lin_sound; eauto.
    + rewrite Forall_forall in Efs |- *. intros F HF. apply in_map_iff in HF as [[F' ef] [<- Hin]].
      apply (Efs _ Hin).
    + rewrite Forall_forall in Efs, H |- *. intros F HF.
      apply in_map_iff in HF as [[F' ef] [<- Hin]].
      destruct (Efs _ Hin) as [VF CF]. simpl in VF, CF.
      exact (H _ Hin G Hs F' CF VF rho Henv Hh).
  - (* and_intro *)
    destruct P as [| | |A B| |]; try discriminate.
    apply andb_true_iff in E as [E1 E2]. simpl in Hv |- *.
    apply andb_true_iff in Hv as [VA VB]. split; eauto.
  - (* and_elim *)
    apply andb_true_iff in E as [VQ E].
    destruct R as [| | |A B| |]; try discriminate.
    apply andb_true_iff in E as [E1 EP]. apply prop_eqb_eq in EP. subst P.
    pose proof (IHe G Hs (PAnd A B) E1 VQ rho Henv Hh) as [HA HB].
    destruct b; assumption.
  - (* or_intro *)
    destruct P as [| | | |A B|]; try discriminate.
    simpl in Hv |- *. apply andb_true_iff in Hv as [VA VB].
    destruct b; [right|left]; eauto.
  - (* or_elim *)
    apply andb_true_iff in E as [VQ E].
    destruct R as [| | | |A B|]; try discriminate.
    repeat rewrite andb_true_iff in E. destruct E as [[E0 E1] E2].
    pose proof (IHe1 G Hs (POr A B) E0 VQ rho Henv Hh) as HAB. simpl in HAB.
    simpl in VQ. apply andb_true_iff in VQ as [VA VB].
    destruct HAB as [HA|HB].
    + assert (V : valid sig G (PImp A P) = true) by (simpl; rewrite VA, Hv; reflexivity).
      exact (IHe2 G Hs _ E1 V rho Henv Hh HA).
    + assert (V : valid sig G (PImp B P) = true) by (simpl; rewrite VB, Hv; reflexivity).
      exact (IHe3 G Hs _ E2 V rho Henv Hh HB).
  - (* false_elim *)
    exfalso. exact (IHe G Hs PFalse E eq_refl rho Henv Hh).
  - (* unsigned_induction *)
    destruct P as [| T' Q| | | |]; try discriminate.
    apply andb_true_iff in E as [ET E]. apply ty_eqb_eq in ET. subst T.
    destruct T' as [w [|]| |]; try discriminate.
    apply andb_true_iff in E as [E1 E2].
    simpl in Hv. apply andb_true_iff in Hv as [O VQ]. simpl in O.
    pose proof (int_ok_width w O) as Hw.
    assert (Hr : forall v, 0 <= v <= max_int w false -> in_range w false v).
    { intros v Hv'. unfold in_range, min_int; cbv beta iota; lia. }
    assert (Hmax : 0 <= max_int w false).
    { unfold max_int. pose proof (pow_split w Hw).
      assert (0 < 2 ^ (Z.of_nat w - 1)) by (apply Z.pow_pos_nonneg; lia). lia. }
    assert (Hone : 1 <= max_int w false).
    { unfold max_int. pose proof (pow_split w Hw).
      assert (0 < 2 ^ (Z.of_nat w - 1)) by (apply Z.pow_pos_nonneg; lia). lia. }
    set (T := TInt w false) in *.
    assert (TL : forall G', type_of sig G' (Lit w false 0) = Some T).
    { intros G'. apply type_lit; [exact O|apply Hr; lia]. }
    (* The base case. *)
    assert (VB : valid sig G (ind_base w Q) = true) by (eapply valid_pinst0; eauto).
    pose proof (IHe1 G Hs _ E1 VB rho Henv Hh) as HB.
    unfold ind_base in HB. apply holds_pinst0 in HB. simpl in HB.
    (* The step's premises are well formed. *)
    assert (Tv0 : type_of sig (T :: G) (Var 0) = Some T) by (apply type_var0; exact O).
    assert (Tmax : type_of sig (T :: G) (Lit w false (max_int w false)) = Some T).
    { apply type_lit; [exact O|apply Hr; lia]. }
    assert (Tlt : type_of sig (T :: G) (Prim OLt w false [Var 0; Lit w false (max_int w false)]) = Some u1).
    { apply type_lt; assumption. }
    assert (Tone : type_of sig (T :: G) (Lit w false 1) = Some T).
    { apply type_lit; [exact O|apply Hr; lia]. }
    assert (Tsucc : type_of sig (T :: G) (Prim AddWrap w false [Var 0; Lit w false 1]) = Some T).
    { apply type_add; assumption. }
    assert (Hd0 : dom Abs I T (VZ 0)) by (apply dom_int_intro; apply range_has_zero; exact Hw).
    assert (Henv0 : env_ok Abs I (T :: G) (VZ 0 :: rho)) by (constructor; assumption).
    assert (VP : valid sig (T :: G) (pred (Prim OLt w false [Var 0; Lit w false (max_int w false)]) true) = true).
    { destruct (pred_sound' _ true (T :: G) (VZ 0 :: rho) Henv0 Tlt) as [VP _]. exact VP. }
    assert (VN : valid sig (T :: G) (pinst (Prim AddWrap w false [Var 0; Lit w false 1]) 0 (pshift 1 1 Q)) = true).
    { eapply (valid_pinst0 sig sig_ok); [exact Tsucc|].
      pose proof (valid_pshift sig Q [T] [T] G) as Hsh. simpl in Hsh. rewrite Hsh. exact VQ. }
    assert (VS : valid sig G (ind_step w Q) = true).
    { unfold ind_step. cbn [valid]. fold T. rewrite VP, VQ, VN.
      change (ty_ok T) with (int_ok w). rewrite O. reflexivity. }
    pose proof (IHe2 G Hs _ E2 VS rho Henv Hh) as HS.
    unfold ind_step in HS. simpl in HS.
    (* Every value of the type, by the principle. *)
    intros v Hdv. destruct Hdv as [z [-> Hz]].
    assert (Hz' : 0 <= z <= max_int w false) by (unfold in_range, min_int in Hz; cbv beta iota in Hz; lia).
    clear Hz. revert z Hz'. apply unsigned_induction_principle; [exact HB|].
    intros z Hlt HPz.
    assert (Hdz : dom Abs I T (VZ z)).
    { apply dom_int_intro. unfold in_range, min_int; cbv beta iota; lia. }
    assert (Henv' : env_ok Abs I (T :: G) (VZ z :: rho)) by (constructor; assumption).
    specialize (HS (VZ z) Hdz).
    destruct (pred_sound' _ true (T :: G) (VZ z :: rho) Henv' Tlt) as [_ Hpred].
    assert (Hprem : holds Abs I (VZ z :: rho)
                      (pred (Prim OLt w false [Var 0; Lit w false (max_int w false)]) true)).
    { apply Hpred. cbn [eval map prim_eval nth zv]. unfold b2v, b2z.
      replace (z <? max_int w false) with true by (symmetry; apply Z.ltb_lt; lia).
      reflexivity. }
    specialize (HS Hprem HPz).
    apply holds_pinst0 in HS.
    apply (holds_pshift Abs I Q 1 1) in HS; [|simpl; lia].
    simpl in HS. rewrite wrap_small in HS; [exact HS|exact Hw|].
    unfold in_range, min_int; cbv beta iota; lia.
Qed.

Theorem check_sound : forall P e, check P e = true -> holds Abs I [] P.
Proof.
  intros P e E. unfold check in E. apply andb_true_iff in E as [Hv E].
  eapply chk_sound; eauto.
  - constructor.
  - constructor.
Qed.

(* Consistency relative to the calculus (KERNEL.md 17, M4): no evidence, well
   formed or not, establishes False. *)
Corollary check_consistent : forall e, check PFalse e = false.
Proof.
  intros e. destruct (check PFalse e) eqn:E; [|reflexivity].
  exfalso. exact (check_sound PFalse e E).
Qed.

End Checker.
