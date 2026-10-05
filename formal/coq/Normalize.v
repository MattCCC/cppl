(* The kernel's normalization (KERNEL.md 9; normalize_impl in
   kernel/src/context.cpp, normalize_primitive and the polynomial reader in
   kernel/src/arithmetic.cpp), modelled, and proven to keep the type and the
   meaning of every typed term. That is the M2 premise Checker.v states as
   nf_sound, discharged for this normalizer: check_sound_normalized below is
   check_sound with reflexivity deciding equality by it.

   The model follows the kernel's algorithm step for step: definitions are
   unfolded into their bodies, primitives are rebuilt from normalized
   operands, arithmetic of one type is read into a polynomial over its opaque
   subterms with coefficients modulo 2^w and rendered back, and comparisons,
   negation, selection, representability, division and conversion are folded
   where the kernel folds them. It differs from the kernel in three ways:

   - resource limits: the kernel fails when a budget is exhausted; the model
     has no budgets, and bounds only the depth of unfolding by fuel, returning
     the term unchanged where the fuel runs out;
   - term order: monomials, factors and commutative operands are placed by a
     structural order that does not compare the domains of observations and
     elements, so two normal forms of one value can differ in placement where
     the kernel's would agree, or the other way round;
   - a malformed term, which the kernel refuses to normalize, is left as it
     stands.

   None of them bears on soundness: nf_sound holds for this normalizer, and
   placement never changes a value. They bear on how exactly the model's
   reflexivity steps match the kernel's, which, as for the rules, rests on the
   transcription and not on a proof (KERNEL.md 17).

   What normalization must satisfy for soundness is that the interpretation
   gives each admitted definition the meaning of its body. That is a condition
   on the interpretation (call_body), alongside the conditions Checker.v
   already places on observations and calls, not a property of the checker.
   Context::define admits a definition only when its body has its declared
   result type over its parameters (body_typed). *)

From Coq Require Import ZArith List Bool Lia Permutation Setoid Morphisms.
Import ListNotations.
From CppL Require Import Syntax Semantics Typing Checker.

Open Scope Z_scope.

(* ------------------------------------------------------------------------ *)
(* Term order (compare in kernel/src/arithmetic.cpp).                        *)

Definition rank (t : term) : nat :=
  match t with
  | Var _ => 0 | Lit _ _ _ => 1 | Call _ _ => 2 | Prim _ _ _ _ => 3 | Proj _ _ _ => 4 | Elem _ _ _ => 5
  end%nat.

(* PrimOp's declaration order in kernel/include/cppl/kernel/term.hpp. *)
Definition op_rank (o : op) : nat :=
  match o with
  | AddWrap => 0 | OEq => 1 | ONe => 2 | OLt => 3 | OLe => 4 | OGt => 5 | OGe => 6
  | ONot => 7 | OSelect => 8 | SubWrap => 9 | MulWrap => 10
  | AddFits => 11 | SubFits => 12 | MulFits => 13 | OQuot => 14 | ORem => 15 | OConvert => 16
  end%nat.

Definition lex (c1 c2 : comparison) : comparison :=
  match c1 with Eq => c2 | _ => c1 end.

Definition bcmp (a b : bool) : comparison :=
  match a, b with
  | false, true => Lt | true, false => Gt | _, _ => Eq
  end.

Fixpoint tcmp (a b : term) {struct a} : comparison :=
  match a, b with
  | Var i, Var j => Nat.compare i j
  | Lit w s v, Lit w' s' v' => lex (Nat.compare w w') (lex (bcmp s s') (Z.compare v v'))
  | Call d ts, Call d' ts' =>
      lex (Nat.compare d d')
        ((fix go (xs ys : list term) {struct xs} : comparison :=
            match xs, ys with
            | [], [] => Eq
            | [], _ => Lt
            | _, [] => Gt
            | x :: xs', y :: ys' => lex (tcmp x y) (go xs' ys')
            end) ts ts')
  | Prim o w s ts, Prim o' w' s' ts' =>
      lex (Nat.compare (op_rank o) (op_rank o'))
        (lex (Nat.compare w w')
           (lex (bcmp s s')
              ((fix go (xs ys : list term) {struct xs} : comparison :=
                  match xs, ys with
                  | [], [] => Eq
                  | [], _ => Lt
                  | _, [] => Gt
                  | x :: xs', y :: ys' => lex (tcmp x y) (go xs' ys')
                  end) ts ts')))
  | Proj _ i u, Proj _ i' u' => lex (Nat.compare i i') (tcmp u u')
  | Elem _ u x, Elem _ u' x' => lex (tcmp u u') (tcmp x x')
  | _, _ => Nat.compare (rank a) (rank b)
  end.

Definition tlt (a b : term) : bool :=
  match tcmp a b with Lt => true | _ => false end.

Fixpoint lcmp (xs ys : list term) : comparison :=
  match xs, ys with
  | [], [] => Eq
  | [], _ => Lt
  | _, [] => Gt
  | x :: xs', y :: ys' => lex (tcmp x y) (lcmp xs' ys')
  end.

(* FactorOrder: monomials by degree, then by their factors. *)
Definition flt (k k' : list term) : bool :=
  Nat.ltb (length k) (length k') ||
  (Nat.eqb (length k) (length k') && match lcmp k k' with Lt => true | _ => false end).

Fixpoint flist_eqb (k k' : list term) : bool :=
  match k, k' with
  | [], [] => true
  | x :: xs, y :: ys => term_eqb x y && flist_eqb xs ys
  | _, _ => false
  end.

Lemma flist_eqb_eq : forall k k', flist_eqb k k' = true -> k = k'.
Proof.
  induction k as [|x xs IH]; intros [|y ys] E; simpl in E; try discriminate; auto.
  apply andb_true_iff in E as [E1 E2]. apply term_eqb_eq in E1. subst. f_equal. auto.
Qed.

(* std::ranges::merge of two sorted factor lists: a factor of the second list
   goes first only when it is strictly less. *)
Fixpoint fmerge (xs : list term) : list term -> list term :=
  match xs with
  | [] => fun ys => ys
  | x :: xs' =>
      fix go (ys : list term) : list term :=
        match ys with
        | [] => xs
        | y :: ys' => if tlt y x then y :: go ys' else x :: fmerge xs' ys
        end
  end.

Lemma fmerge_perm : forall xs ys, Permutation (fmerge xs ys) (xs ++ ys).
Proof.
  induction xs as [|x xs IH]; intros ys; simpl; [apply Permutation_refl|].
  induction ys as [|y ys IHy]; simpl.
  - rewrite app_nil_r. apply Permutation_refl.
  - destruct (tlt y x).
    + eapply Permutation_trans; [apply perm_skip; exact IHy|].
      exact (Permutation_middle (x :: xs) ys y).
    + apply perm_skip. apply IH.
Qed.

(* ------------------------------------------------------------------------ *)
(* Polynomials (Work and Polynomial in kernel/src/arithmetic.cpp): monomials   *)
(* in factor order with nonzero coefficients modulo 2^w, and a constant.       *)

Definition mono : Type := (list term * Z)%type.
Definition poly : Type := (list mono * Z)%type.

Section Poly.

Variable m : Z.
Hypothesis m_pos : 0 < m.

Fixpoint ins (k : list term) (c : Z) (l : list mono) : list mono :=
  match l with
  | [] => [(k, c)]
  | (k', c') :: l' =>
      if flist_eqb k k' then
        (if (c' + c) mod m =? 0 then l' else (k', (c' + c) mod m) :: l')
      else if flt k k' then (k, c) :: l
      else (k', c') :: ins k c l'
  end.

(* Reader::accumulate. *)
Definition accumulate (p : poly) (k : list term) (c : Z) : poly :=
  if c mod m =? 0 then p
  else match k with
       | [] => (fst p, (snd p + c mod m) mod m)
       | _ => (ins k (c mod m) (fst p), snd p)
       end.

(* Reader::sum: every monomial of `added`, scaled, then its constant. *)
Definition psum (into added : poly) (scale : Z) : poly :=
  let p := fold_left (fun acc kc => accumulate acc (fst kc) (snd kc * scale)) (fst added) into in
  (fst p, (snd p + snd added * scale) mod m).

(* Reader::product: every pair of monomials, the constant as the monomial of
   no factor. *)
Definition each (p : poly) : list mono :=
  fst p ++ (if snd p =? 0 then [] else [([], snd p)]).

Definition pprod (l r : poly) : poly :=
  fold_left
    (fun acc lm =>
       fold_left (fun acc2 rm => accumulate acc2 (fmerge (fst lm) (fst rm)) (snd lm * snd rm)) (each r) acc)
    (each l) ([], 0).

Definition pneg (p : poly) : poly :=
  (map (fun kc => (fst kc, (- snd kc) mod m)) (fst p), (- snd p) mod m).

(* ---- values, compared modulo m ---- *)

(* Congruence modulo m, as a relation rewriting can work under. *)
Inductive cong (a b : Z) : Prop := cong_intro : a mod m = b mod m -> cong a b.

Lemma cong_iff : forall a b, cong a b <-> a mod m = b mod m.
Proof. split; [intros []; auto | constructor; auto]. Qed.

Global Instance cong_equiv : Equivalence cong.
Proof.
  split; [intros a; constructor; reflexivity | intros a b []; constructor; auto |
          intros a b c [] []; constructor; congruence].
Qed.

Global Instance cong_add : Proper (cong ==> cong ==> cong) Z.add.
Proof. intros a a' [Ha] b b' [Hb]. constructor. rewrite Zplus_mod, Ha, Hb, <- Zplus_mod. reflexivity. Qed.

Global Instance cong_mul : Proper (cong ==> cong ==> cong) Z.mul.
Proof. intros a a' [Ha] b b' [Hb]. constructor. rewrite Zmult_mod, Ha, Hb, <- Zmult_mod. reflexivity. Qed.

Global Instance cong_sub : Proper (cong ==> cong ==> cong) Z.sub.
Proof. intros a a' [Ha] b b' [Hb]. constructor. rewrite Zminus_mod, Ha, Hb, <- Zminus_mod. reflexivity. Qed.

Global Instance cong_opp : Proper (cong ==> cong) Z.opp.
Proof. intros a a' H. rewrite <- !Z.sub_0_l. rewrite H. reflexivity. Qed.

Lemma cong_mod : forall a, cong (a mod m) a.
Proof. intros. constructor. apply Z.mod_mod. lia. Qed.

Lemma eq_cong : forall a b, a = b -> cong a b.
Proof. intros a b ->. reflexivity. Qed.

Lemma cong_zero : forall a, a mod m = 0 -> cong a 0.
Proof. intros a H. constructor. rewrite H, Z.mod_0_l by lia. reflexivity. Qed.

Variable fv : term -> Z.

Fixpoint mval (k : list term) : Z :=
  match k with [] => 1 | t :: k' => fv t * mval k' end.

Fixpoint lval (l : list mono) : Z :=
  match l with [] => 0 | kc :: l' => snd kc * mval (fst kc) + lval l' end.

Definition pval (p : poly) : Z := lval (fst p) + snd p.

Lemma mval_app : forall a b, mval (a ++ b) = mval a * mval b.
Proof. induction a as [|t a IH]; intros b; cbn [mval app]; [ring|]. rewrite IH. ring. Qed.

Lemma mval_perm : forall a b, Permutation a b -> mval a = mval b.
Proof.
  intros a b P. induction P; cbn [mval].
  - reflexivity.
  - rewrite IHP. reflexivity.
  - ring.
  - rewrite IHP1, IHP2. reflexivity.
Qed.

Lemma mval_fmerge : forall a b, mval (fmerge a b) = mval a * mval b.
Proof. intros. rewrite (mval_perm _ _ (fmerge_perm a b)). apply mval_app. Qed.

Lemma lval_app : forall a b, lval (a ++ b) = lval a + lval b.
Proof. induction a as [|kc a IH]; intros b; cbn [lval app]; [ring|]. rewrite IH. ring. Qed.

Lemma ins_val : forall k c l, cong (lval (ins k c l)) (c * mval k + lval l).
Proof.
  intros k c l. induction l as [|[k' c'] l IH]; cbn [ins lval fst snd]; [reflexivity|].
  destruct (flist_eqb k k') eqn:E.
  - apply flist_eqb_eq in E. subst k'.
    replace (c * mval k + (c' * mval k + lval l)) with ((c' + c) * mval k + lval l) by ring.
    destruct ((c' + c) mod m =? 0) eqn:Z0.
    + apply Z.eqb_eq, cong_zero in Z0. rewrite Z0. apply eq_cong. ring.
    + cbn [lval fst snd]. rewrite cong_mod. reflexivity.
  - destruct (flt k k'); cbn [lval fst snd]; [reflexivity|].
    rewrite IH. apply eq_cong. ring.
Qed.

Lemma accumulate_val : forall p k c, cong (pval (accumulate p k c)) (pval p + c * mval k).
Proof.
  intros [l z] k c. unfold accumulate.
  destruct (c mod m =? 0) eqn:Z0.
  - apply Z.eqb_eq, cong_zero in Z0. rewrite Z0. apply eq_cong. ring.
  - destruct k as [|t k'].
    + unfold pval. cbn [fst snd mval]. rewrite !cong_mod. apply eq_cong. ring.
    + unfold pval. cbn [fst snd]. rewrite ins_val, cong_mod. apply eq_cong. ring.
Qed.

Lemma psum_val : forall into added scale,
  cong (pval (psum into added scale)) (pval into + scale * pval added).
Proof.
  intros into [l z] scale.
  assert (H : forall L acc,
             cong (pval (fold_left (fun acc kc => accumulate acc (fst kc) (snd kc * scale)) L acc))
                  (pval acc + scale * lval L)).
  { induction L as [|[k c] L IH]; intros acc; cbn [fold_left lval fst snd].
    - apply eq_cong. ring.
    - rewrite IH, accumulate_val. apply eq_cong. ring. }
  unfold psum. cbn [fst snd].
  pose proof (H l into) as Hl.
  remember (fold_left (fun acc kc => accumulate acc (fst kc) (snd kc * scale)) l into) as p eqn:F.
  clear F. destruct p as [pl pz].
  unfold pval in *. cbn [fst snd] in *. rewrite cong_mod.
  transitivity ((lval pl + pz) + z * scale); [apply eq_cong; ring|].
  rewrite Hl. apply eq_cong. ring.
Qed.

Lemma each_val : forall p, lval (each p) = pval p.
Proof.
  intros [l z]. unfold each, pval. cbn [fst snd]. rewrite lval_app.
  destruct (z =? 0) eqn:E; cbn [lval mval fst snd].
  - apply Z.eqb_eq in E. subst. ring.
  - ring.
Qed.

Lemma pprod_val : forall l r, cong (pval (pprod l r)) (pval l * pval r).
Proof.
  intros l r. unfold pprod.
  assert (Inner : forall (lm : mono) R acc,
             cong (pval (fold_left (fun acc2 rm => accumulate acc2 (fmerge (fst lm) (fst rm)) (snd lm * snd rm)) R acc))
                  (pval acc + snd lm * mval (fst lm) * lval R)).
  { intros lm. induction R as [|rm R IH]; intros acc; cbn [fold_left lval].
    - apply eq_cong. ring.
    - rewrite IH, accumulate_val, mval_fmerge. apply eq_cong. ring. }
  assert (Outer : forall L acc,
             cong (pval (fold_left
                     (fun acc lm =>
                        fold_left (fun acc2 rm => accumulate acc2 (fmerge (fst lm) (fst rm)) (snd lm * snd rm))
                          (each r) acc) L acc))
                  (pval acc + lval L * pval r)).
  { induction L as [|lm L IH]; intros acc; cbn [fold_left lval].
    - apply eq_cong. ring.
    - rewrite IH, Inner, each_val. apply eq_cong. ring. }
  rewrite Outer, each_val. unfold pval at 1. cbn [fst snd lval]. apply eq_cong. ring.
Qed.

Lemma pneg_val : forall p, cong (pval (pneg p)) (- pval p).
Proof.
  intros [l z]. unfold pneg, pval. cbn [fst snd].
  assert (H : forall L, cong (lval (map (fun kc => (fst kc, (- snd kc) mod m)) L)) (- lval L)).
  { induction L as [|[k c] L IH]; cbn [map lval fst snd]; [reflexivity|].
    rewrite IH, cong_mod. apply eq_cong. ring. }
  rewrite H, cong_mod. apply eq_cong. ring.
Qed.

(* Every factor of every monomial satisfies P. *)
Definition atoms (P : term -> Prop) (p : poly) : Prop := Forall (fun kc => Forall P (fst kc)) (fst p).

Lemma ins_atoms : forall P k c l,
  Forall P k -> Forall (fun kc => Forall P (fst kc)) l -> Forall (fun kc => Forall P (fst kc)) (ins k c l).
Proof.
  intros P k c l Hk Hl. induction Hl as [|[k' c'] l Hk' Hl IH]; simpl; [constructor; auto|].
  destruct (flist_eqb k k'); [destruct ((c' + c) mod m =? 0); auto|].
  destruct (flt k k'); constructor; auto.
Qed.

Lemma accumulate_atoms : forall P p k c, atoms P p -> Forall P k -> atoms P (accumulate p k c).
Proof.
  intros P [l z] k c Hp Hk. unfold accumulate, atoms in *; simpl in *.
  destruct (c mod m =? 0); auto. destruct k; simpl; auto. apply ins_atoms; auto.
Qed.

Lemma psum_atoms : forall P into added scale, atoms P into -> atoms P added -> atoms P (psum into added scale).
Proof.
  intros P into [l z] scale Hi Ha. unfold psum, atoms in *; simpl in *.
  assert (H : forall L acc, Forall (fun kc => Forall P (fst kc)) L -> atoms P acc ->
             atoms P (fold_left (fun acc kc => accumulate acc (fst kc) (snd kc * scale)) L acc)).
  { induction L as [|kc L IH]; intros acc HL Hacc; simpl; auto.
    inversion HL; subst. apply IH; auto. apply accumulate_atoms; auto. }
  exact (H l into Ha Hi).
Qed.

Lemma each_atoms : forall P p, atoms P p -> Forall (fun kc => Forall P (fst kc)) (each p).
Proof.
  intros P [l z] H. unfold each, atoms in *. simpl in *. apply Forall_app. split; auto.
  destruct (z =? 0); auto.
Qed.

Lemma pprod_atoms : forall P l r, atoms P l -> atoms P r -> atoms P (pprod l r).
Proof.
  intros P l r Hl Hr. unfold pprod.
  pose proof (each_atoms P l Hl) as El. pose proof (each_atoms P r Hr) as Er.
  assert (Inner : forall (lm : mono) R acc, Forall P (fst lm) -> Forall (fun kc => Forall P (fst kc)) R ->
             atoms P acc ->
             atoms P (fold_left (fun acc2 rm => accumulate acc2 (fmerge (fst lm) (fst rm)) (snd lm * snd rm)) R acc)).
  { intros lm R. induction R as [|rm R IH]; intros acc Hlm HR Hacc; simpl; auto.
    inversion HR; subst. apply IH; auto. apply accumulate_atoms; auto.
    eapply Permutation_Forall; [apply Permutation_sym, fmerge_perm|]. apply Forall_app; auto. }
  assert (Outer : forall L acc, Forall (fun kc => Forall P (fst kc)) L -> atoms P acc ->
             atoms P (fold_left
                        (fun acc lm =>
                           fold_left (fun acc2 rm => accumulate acc2 (fmerge (fst lm) (fst rm)) (snd lm * snd rm))
                             (each r) acc) L acc)).
  { induction L as [|lm L IH]; intros acc HL Hacc; simpl; auto.
    inversion HL; subst. apply IH; auto. }
  apply Outer; auto. constructor.
Qed.

Lemma pneg_atoms : forall P p, atoms P p -> atoms P (pneg p).
Proof.
  intros P [l z] H. unfold pneg, atoms in *. simpl in *. apply Forall_map.
  eapply Forall_impl; [|exact H]. auto.
Qed.

End Poly.

(* ------------------------------------------------------------------------ *)
(* The normalizer.                                                           *)

Section Normalizer.

(* The admitted definitions: their signatures, as Typing.v reads them, and
   their bodies (Context in kernel/src/context.cpp). *)
Variable sig : nat -> option (list ty * ty).
Variable body : nat -> option term.

Definition is_arith (o : op) : bool :=
  match o with AddWrap | SubWrap | MulWrap => true | _ => false end.

Definition same_int (w : nat) (s : bool) (w' : nat) (s' : bool) : bool :=
  Nat.eqb w' w && Bool.eqb s' s.

Section Type_.

Variable w : nat.
Variable s : bool.

(* Reader::read: arithmetic of this type is read through, a literal of it is
   a constant, anything else is an opaque factor. *)
Definition atom (t : term) : poly := ([([t], 1)], 0).

Fixpoint read (t : term) : poly :=
  match t with
  | Lit w' s' v => if same_int w s w' s' then ([], v mod modulus w) else atom t
  | Prim o w' s' [a; b] =>
      if is_arith o && same_int w s w' s' then
        match o with
        | AddWrap => psum (modulus w) (read a) (read b) 1
        | SubWrap => psum (modulus w) (read a) (read b) (modulus w - 1)
        | _ => pprod (modulus w) (read a) (read b)
        end
      else atom t
  | _ => atom t
  end.

(* literal_of: a literal of this type congruent to c. *)
Definition litw (c : Z) : term := Lit w s (wrap w s c).

(* render_factors and render. *)
Fixpoint rmul (acc : term) (fs : list term) : term :=
  match fs with [] => acc | f :: fs' => rmul (Prim MulWrap w s [acc; f]) fs' end.

Definition rfactors (fs : list term) : term :=
  match fs with [] => litw 1 | f :: fs' => rmul f fs' end.

Definition scaled (fs : list term) (c : Z) : term :=
  if c =? 1 then rfactors fs else Prim MulWrap w s [litw c; rfactors fs].

Definition negc (c : Z) : bool := 2 ^ (Z.of_nat w - 1) <? c.
Definition mag (c : Z) : Z := (- c) mod modulus w.

Definition addpos (acc : option term) (kc : mono) : option term :=
  if negc (snd kc) then acc
  else Some (match acc with
             | None => scaled (fst kc) (snd kc)
             | Some a => Prim AddWrap w s [a; scaled (fst kc) (snd kc)]
             end).

Definition subneg (acc : option term) (kc : mono) : option term :=
  if negc (snd kc)
  then Some (Prim SubWrap w s [match acc with None => litw 0 | Some a => a end; scaled (fst kc) (mag (snd kc))])
  else acc.

Definition render (p : poly) : term :=
  let sum := fold_left subneg (fst p) (fold_left addpos (fst p) None) in
  if snd p =? 0 then match sum with None => litw 0 | Some a => a end
  else match sum with
       | None => litw (snd p)
       | Some a => if negc (snd p) then Prim SubWrap w s [a; litw (mag (snd p))]
                   else Prim AddWrap w s [a; litw (snd p)]
       end.

(* The value a literal of this type denotes (literal_value). *)
Definition lit_value (t : term) : option Z :=
  match t with
  | Lit w' s' v => if same_int w s w' s' then Some (wrap w s v) else None
  | _ => None
  end.

(* less: decided between literals, between identical terms, and at the
   bounds of the type; otherwise kept. *)
Definition less (a b : term) : term :=
  match a, b with
  | Lit _ _ va, Lit _ _ vb => Lit 1 false (b2z (wrap w s va <? wrap w s vb))
  | _, _ =>
      if term_eqb a b then Lit 1 false 0
      else if match b with Lit _ _ vb => wrap w s vb =? min_int w s | _ => false end then Lit 1 false 0
      else if match a with Lit _ _ va => wrap w s va =? max_int w s | _ => false end then Lit 1 false 0
      else Prim OLt w s [a; b]
  end.

(* Equal: stated of the difference of the two sides. *)
Definition equal (a b : term) : term :=
  let p := psum (modulus w) (read a) (read b) (modulus w - 1) in
  match fst p with
  | [] => Lit 1 false (b2z (snd p =? 0))
  | _ =>
      let pos := render p in
      let opp := render (pneg (modulus w) p) in
      Prim OEq w s [if tlt opp pos then opp else pos; litw 0]
  end.

Definition select (c x y : term) : term :=
  if match c with Lit w' s' v => same_int 1 false w' s' && ((v =? 0) || (v =? 1)) | _ => false end
  then match c with Lit _ _ v => if v =? 1 then x else y | _ => y end
  else if term_eqb x y then x
  else match c with
       | Prim ONot _ _ [c'] => Prim OSelect w s [c'; y; x]
       | _ => Prim OSelect w s [c; x; y]
       end.

(* representability *)
Definition fits (o : op) (a b : term) : term :=
  match lit_value a, lit_value b with
  | Some va, Some vb =>
      Lit 1 false (b2z (in_rangeb w s (match o with AddFits => va + vb | SubFits => va - vb | _ => va * vb end)))
  | _, _ => if negb (op_eqb o SubFits) && tlt b a then Prim o w s [b; a] else Prim o w s [a; b]
  end.

(* Quotient and Remainder: folded only where the total definition decides. *)
Definition divide (quot : bool) (a b : term) : term :=
  match lit_value b with
  | Some y =>
      if y =? 0 then (if quot then Lit w s 0 else a)
      else match lit_value a with
           | Some x => Lit w s (if quot then wrap w s (Z.quot x y) else Z.rem x y)
           | None =>
               if y =? 1 then (if quot then a else Lit w s 0)
               else if y =? -1 then
                 (if quot then render (read (Prim SubWrap w s [Lit w s 0; a])) else Lit w s 0)
               else Prim (if quot then OQuot else ORem) w s [a; b]
           end
  | None =>
      match lit_value a with
      | Some x => if x =? 0 then Lit w s 0 else Prim (if quot then OQuot else ORem) w s [a; b]
      | None => Prim (if quot then OQuot else ORem) w s [a; b]
      end
  end.

Definition convert (a : term) : term :=
  match a with
  | Lit w' s' v => if int_ok w' then Lit w s (wrap w s (wrap w' s' v)) else Prim OConvert w s [a]
  | _ => Prim OConvert w s [a]
  end.

End Type_.

Definition negate (c : term) : term :=
  match c with
  | Lit _ _ v => Lit 1 false (if v =? 0 then 1 else 0)
  | Prim ONot _ _ [x] => x
  | _ => Prim ONot 1 false [c]
  end.

Definition arity (o : op) : nat :=
  match o with ONot | OConvert => 1 | OSelect => 3 | _ => 2 end.

(* normalize_primitive. *)
Definition nprim (o : op) (w : nat) (s : bool) (ts : list term) : term :=
  if negb (int_ok w) || negb (Nat.eqb (length ts) (arity o)) then Prim o w s ts
  else match o, ts with
       | AddWrap, _ | SubWrap, _ | MulWrap, _ => render w s (read w s (Prim o w s ts))
       | ONot, [a] => negate a
       | OSelect, [c; x; y] => select w s c x y
       | OEq, [a; b] => equal w s a b
       | ONe, [a; b] => negate (equal w s a b)
       | OLt, [a; b] => less w s a b
       | OGt, [a; b] => less w s b a
       | OLe, [a; b] => negate (less w s b a)
       | OGe, [a; b] => negate (less w s a b)
       | AddFits, [a; b] | SubFits, [a; b] | MulFits, [a; b] => fits w s o a b
       | OQuot, [a; b] => divide w s true a b
       | ORem, [a; b] => divide w s false a b
       | OConvert, [a] => convert w s a
       | _, _ => Prim o w s ts
       end.

(* substitute: Var i is the i-th argument from the end. *)
Definition argsub (ts : list term) (i : nat) : term := nth i (rev ts) (Prim ONot 1 false []).

(* normalize_impl. The fuel bounds unfolding as max_term_depth bounds the
   kernel's recursion. *)
Fixpoint norm (fuel : nat) (t : term) : term :=
  match fuel with
  | O => t
  | S n =>
      match t with
      | Var _ => t
      | Lit _ _ _ => t
      | Call d ts =>
          match sig d, body d with
          | Some (ps, _), Some b =>
              if Nat.eqb (length ps) (length ts) then norm n (tsubst (argsub (map (norm n) ts)) b) else t
          | _, _ => t
          end
      | Prim o w s ts => nprim o w s (map (norm n) ts)
      | Proj D i u => Proj D i (norm n u)
      | Elem D u x => Elem D (norm n u) (norm n x)
      end
  end.

(* CoreLimits::max_term_depth. *)
Definition max_depth : nat := 512.

Definition nf : term -> term := norm max_depth.

End Normalizer.

(* ------------------------------------------------------------------------ *)
(* Machine-integer facts the folds rest on.                                  *)

Lemma modulus_pos : forall w, 0 < modulus w.
Proof. intros. unfold modulus. apply Z.pow_pos_nonneg; lia. Qed.

Lemma wrap_cong : forall w s z, cong (modulus w) (wrap w s z) z.
Proof.
  intros w s z. constructor. pose proof (modulus_pos w) as Hm. unfold wrap.
  destruct (s && (max_int w s <? z mod modulus w)).
  - rewrite Zminus_mod, Z_mod_same_full, Z.sub_0_r, !Z.mod_mod by lia. reflexivity.
  - apply Z.mod_mod. lia.
Qed.

Lemma range_width : forall w s, (1 <= w)%nat -> max_int w s - min_int w s = modulus w - 1.
Proof.
  intros w s Hw. unfold max_int, min_int, modulus. pose proof (pow_split w Hw). destruct s; lia.
Qed.

Lemma range_unique : forall w s x y, (1 <= w)%nat -> in_range w s x -> in_range w s y ->
  cong (modulus w) x y -> x = y.
Proof.
  intros w s x y Hw Hx Hy [E]. pose proof (range_width w s Hw) as Wd.
  pose proof (modulus_pos w) as Hm. unfold in_range in *.
  pose proof (Z.div_mod x (modulus w) ltac:(lia)) as Dx.
  pose proof (Z.div_mod y (modulus w) ltac:(lia)) as Dy.
  assert (Hq : x / modulus w = y / modulus w) by nia.
  lia.
Qed.

Lemma in_range_mod : forall w s z, (1 <= w)%nat -> in_range w s z -> in_range w s (wrap w s z).
Proof. intros. apply wrap_in_range. exact H. Qed.

Section Sound.

Variable sig : nat -> option (list ty * ty).
Hypothesis sig_ok : forall d ps R, sig d = Some (ps, R) -> ty_ok R = true.
Variable body : nat -> option term.
(* Context::define admits a definition only when its body has its result
   type over its parameters, the last of them nearest (Var 0). *)
Hypothesis body_typed : forall d ps R b, sig d = Some (ps, R) -> body d = Some b -> type_of sig (rev ps) b = Some R.

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
(* The interpretation gives each admitted definition the meaning of its
   body: an interpretation is a model of the definitions. *)
Hypothesis call_body : forall d ps R b args,
  sig d = Some (ps, R) -> body d = Some b -> Forall2 (dom Abs I) ps args ->
  i_call Abs I d args = eval Abs I (rev args) b.

Notation tyof := (type_of sig).
Notation ev := (eval Abs I).
Notation zval := (zv Abs).

Lemma typed_dom : forall G rho t T, env_ok Abs I G rho -> tyof G t = Some T -> dom Abs I T (ev rho t).
Proof. intros. eapply type_sound; eauto. Qed.

Lemma typed_int : forall G rho t w s, env_ok Abs I G rho -> tyof G t = Some (TInt w s) ->
  ev rho t = VZ (zval (ev rho t)) /\ in_range w s (zval (ev rho t)).
Proof. intros G rho t w s Henv Ht. apply (dom_int Abs I). eapply typed_dom; eauto. Qed.

Definition fvr (rho : list (val Abs)) (t : term) : Z := zval (ev rho t).

Lemma read_val : forall rho w s t, cong (modulus w) (fvr rho t) (pval (fvr rho) (read w s t)).
Proof.
  intros rho w s. pose proof (modulus_pos w) as Hm.
  induction t using term_ind'; cbn [read];
    try (unfold atom, pval; cbn [lval mval fst snd]; apply eq_cong; ring).
  - destruct (same_int w s w0 s0) eqn:E.
    + unfold same_int in E. apply andb_true_iff in E as [E1 E2].
      apply Nat.eqb_eq in E1. apply Bool.eqb_prop in E2. subst.
      unfold pval, fvr. cbn [lval fst snd eval zv]. rewrite cong_mod by lia. apply eq_cong. ring.
    + unfold atom, pval. cbn [lval mval fst snd]. apply eq_cong. ring.
  - destruct ts as [|a [|b [|c ts]]];
      try (unfold atom, pval; cbn [lval mval fst snd]; apply eq_cong; ring).
    inversion H as [|? ? Ha Hr]; subst. inversion Hr as [|? ? Hb _]; subst.
    destruct (is_arith o && same_int w s w0 s0) eqn:E;
      [|unfold atom, pval; cbn [lval mval fst snd]; apply eq_cong; ring].
    apply andb_true_iff in E as [Eo E]. unfold same_int in E. apply andb_true_iff in E as [E1 E2].
    apply Nat.eqb_eq in E1. apply Bool.eqb_prop in E2. subst.
    destruct o; try discriminate; unfold fvr at 1; cbn [eval map prim_eval zv]; rewrite wrap_cong;
      change (zval (ev rho a)) with (fvr rho a); change (zval (ev rho b)) with (fvr rho b).
    + rewrite psum_val by exact Hm. rewrite <- Ha, <- Hb. apply eq_cong. ring.
    + rewrite psum_val by exact Hm. rewrite <- Ha, <- Hb.
      rewrite <- (cong_mod (modulus w) Hm (modulus w)) at 2. rewrite Z_mod_same_full.
      apply eq_cong. ring.
    + rewrite pprod_val by exact Hm. rewrite <- Ha, <- Hb. reflexivity.
Qed.


(* ---- typing of the terms normalization builds ---- *)

Lemma arith_typed : forall G o w s a b, is_arith o = true -> int_ok w = true ->
  tyof G a = Some (TInt w s) -> tyof G b = Some (TInt w s) -> tyof G (Prim o w s [a; b]) = Some (TInt w s).
Proof.
  intros G o w s a b Ho Hw Ha Hb.
  destruct o; try discriminate; cbn [type_of ty_ok]; rewrite Hw; cbn [negb];
    rewrite Ha, Hb; cbn [teq]; rewrite ty_eqb_refl; reflexivity.
Qed.

Lemma arith_args : forall G o w s a b T, is_arith o = true -> tyof G (Prim o w s [a; b]) = Some T ->
  int_ok w = true /\ tyof G a = Some (TInt w s) /\ tyof G b = Some (TInt w s) /\ T = TInt w s.
Proof.
  intros G o w s a b T Ho H.
  destruct o; try discriminate; cbn [type_of ty_ok] in H;
    destruct (int_ok w) eqn:Hw; cbn [negb] in H; try discriminate;
    destruct (teq (tyof G a) (TInt w s)) eqn:Ea; cbn [andb] in H; try discriminate;
    destruct (teq (tyof G b) (TInt w s)) eqn:Eb; try discriminate;
    inversion H; subst; apply teq_true in Ea; apply teq_true in Eb; auto.
Qed.

Lemma litw_typed : forall G w s c, int_ok w = true -> tyof G (litw w s c) = Some (TInt w s).
Proof.
  intros G w s c Hw. unfold litw. cbn [type_of ty_ok]. rewrite Hw.
  assert (R : in_rangeb w s (wrap w s c) = true)
    by (apply in_rangeb_iff, wrap_in_range, int_ok_width; exact Hw).
  rewrite R. reflexivity.
Qed.

Lemma read_atoms : forall G w s t, tyof G t = Some (TInt w s) ->
  atoms (fun a => tyof G a = Some (TInt w s)) (read w s t).
Proof.
  intros G w s. induction t using term_ind'; intros Ht; cbn [read];
    try (unfold atom, atoms; cbn [fst]; repeat constructor; exact Ht).
  - destruct (same_int w s w0 s0); [unfold atoms; constructor|].
    unfold atom, atoms; cbn [fst]; repeat constructor; exact Ht.
  - destruct ts as [|a [|b [|c ts]]];
      try (unfold atom, atoms; cbn [fst]; repeat constructor; exact Ht).
    inversion H as [|? ? Ha Hr]; subst. inversion Hr as [|? ? Hb _]; subst.
    destruct (is_arith o && same_int w s w0 s0) eqn:E;
      [|unfold atom, atoms; cbn [fst]; repeat constructor; exact Ht].
    apply andb_true_iff in E as [Eo E]. unfold same_int in E. apply andb_true_iff in E as [E1 E2].
    apply Nat.eqb_eq in E1. apply Bool.eqb_prop in E2. subst.
    destruct (arith_args G o w s a b _ Eo Ht) as [_ [Ta [Tb _]]].
    specialize (Ha Ta). specialize (Hb Tb).
    destruct o; try discriminate.
    + apply psum_atoms; auto.
    + apply psum_atoms; auto.
    + apply pprod_atoms; auto.
Qed.

Section Render.

Variable G : list ty.
Variable w : nat.
Variable s : bool.
Hypothesis Hw : int_ok w = true.
Let P := fun a => tyof G a = Some (TInt w s).

Lemma rmul_typed : forall fs acc, P acc -> Forall P fs -> P (rmul w s acc fs).
Proof.
  induction fs as [|f fs IH]; intros acc Ha Hf; cbn [rmul]; auto.
  inversion Hf; subst. apply IH; auto. unfold P. apply arith_typed; auto.
Qed.

Lemma rfactors_typed : forall fs, Forall P fs -> P (rfactors w s fs).
Proof.
  intros [|f fs] H; cbn [rfactors]; [apply litw_typed; exact Hw|].
  inversion H; subst. apply rmul_typed; auto.
Qed.

Lemma scaled_typed : forall fs c, Forall P fs -> P (scaled w s fs c).
Proof.
  intros fs c H. unfold scaled. destruct (c =? 1); [apply rfactors_typed; auto|].
  unfold P. apply arith_typed; auto; [apply litw_typed; exact Hw|apply rfactors_typed; auto].
Qed.

Definition opt_ok (o : option term) : Prop := match o with None => True | Some a => P a end.

Lemma addpos_typed : forall L acc, Forall (fun kc => Forall P (fst kc)) L -> opt_ok acc ->
  opt_ok (fold_left (addpos w s) L acc).
Proof.
  induction L as [|kc L IH]; intros acc HL Ha; cbn [fold_left]; auto.
  inversion HL; subst. apply IH; auto. unfold addpos.
  destruct (negc w (snd kc)); auto. cbn [opt_ok].
  destruct acc as [a|]; [|apply scaled_typed; auto].
  unfold P. apply arith_typed; auto. apply scaled_typed; auto.
Qed.

Lemma subneg_typed : forall L acc, Forall (fun kc => Forall P (fst kc)) L -> opt_ok acc ->
  opt_ok (fold_left (subneg w s) L acc).
Proof.
  induction L as [|kc L IH]; intros acc HL Ha; cbn [fold_left]; auto.
  inversion HL; subst. apply IH; auto. unfold subneg.
  destruct (negc w (snd kc)); auto. cbn [opt_ok].
  unfold P. apply arith_typed; auto; [|apply scaled_typed; auto].
  destruct acc as [a|]; [exact Ha|apply litw_typed; exact Hw].
Qed.

Lemma render_typed : forall p, atoms P p -> P (render w s p).
Proof.
  intros [l z] H. unfold atoms in H. cbn [fst] in H. unfold render. cbn [fst snd].
  pose proof (subneg_typed l _ H (addpos_typed l None H Logic.I)) as S.
  destruct (fold_left (subneg w s) l (fold_left (addpos w s) l None)) as [a|];
    destruct (z =? 0); cbn [opt_ok] in S; try (apply litw_typed; exact Hw); auto.
  destruct (negc w z); unfold P; apply arith_typed; auto; apply litw_typed; exact Hw.
Qed.

End Render.


Section RenderVal.

Variable rho : list (val Abs).
Variable w : nat.
Variable s : bool.
Let m := modulus w.
Let fv := fvr rho.
Let Hm : 0 < m := modulus_pos w.

Lemma litw_zval : forall c, zval (ev rho (litw w s c)) = wrap w s c.
Proof. reflexivity. Qed.

Lemma rmul_val : forall fs acc, cong m (fv (rmul w s acc fs)) (fv acc * mval fv fs).
Proof.
  induction fs as [|f fs IH]; intros acc; cbn [rmul mval]; [apply eq_cong; ring|].
  rewrite IH. unfold fv at 1, fvr at 1. cbn [eval map prim_eval zv]. rewrite wrap_cong.
  change (zval (ev rho acc)) with (fv acc). change (zval (ev rho f)) with (fv f). apply eq_cong. ring.
Qed.

Lemma rfactors_val : forall fs, cong m (fv (rfactors w s fs)) (mval fv fs).
Proof.
  intros [|f fs]; cbn [rfactors mval].
  - unfold fv, fvr. rewrite litw_zval, wrap_cong. reflexivity.
  - rewrite rmul_val. reflexivity.
Qed.

Lemma scaled_val : forall fs c, cong m (fv (scaled w s fs c)) (c * mval fv fs).
Proof.
  intros fs c. unfold scaled. destruct (c =? 1) eqn:E.
  - apply Z.eqb_eq in E. subst. rewrite rfactors_val. apply eq_cong. ring.
  - unfold fv at 1, fvr at 1. cbn [eval map prim_eval zv]. rewrite wrap_cong, litw_zval, wrap_cong.
    change (zval (ev rho (rfactors w s fs))) with (fv (rfactors w s fs)). rewrite rfactors_val. reflexivity.
Qed.

Definition accv (o : option term) : Z := match o with None => 0 | Some a => fv a end.

Fixpoint posv (L : list mono) : Z :=
  match L with [] => 0 | kc :: L' => (if negc w (snd kc) then 0 else snd kc * mval fv (fst kc)) + posv L' end.

Fixpoint negv (L : list mono) : Z :=
  match L with [] => 0 | kc :: L' => (if negc w (snd kc) then snd kc * mval fv (fst kc) else 0) + negv L' end.

Lemma pos_neg : forall L, posv L + negv L = lval fv L.
Proof.
  induction L as [|kc L IH]; cbn [posv negv lval]; [reflexivity|].
  rewrite <- IH. destruct (negc w (snd kc)); ring.
Qed.

Lemma addpos_val : forall L acc, cong m (accv (fold_left (addpos w s) L acc)) (accv acc + posv L).
Proof.
  induction L as [|kc L IH]; intros acc; cbn [fold_left posv]; [apply eq_cong; ring|].
  rewrite IH. unfold addpos. destruct (negc w (snd kc)); [apply eq_cong; ring|].
  destruct acc as [a|]; cbn [accv].
  - unfold fv at 1, fvr at 1. cbn [eval map prim_eval zv]. rewrite wrap_cong.
    change (zval (ev rho a)) with (fv a).
    change (zval (ev rho (scaled w s (fst kc) (snd kc)))) with (fv (scaled w s (fst kc) (snd kc))).
    rewrite scaled_val. apply eq_cong. ring.
  - rewrite scaled_val. apply eq_cong. ring.
Qed.

Lemma subneg_val : forall L acc, cong m (accv (fold_left (subneg w s) L acc)) (accv acc + negv L).
Proof.
  induction L as [|kc L IH]; intros acc; cbn [fold_left negv]; [apply eq_cong; ring|].
  rewrite IH. unfold subneg. destruct (negc w (snd kc)); [|apply eq_cong; ring].
  cbn [accv]. unfold fv at 1, fvr at 1. cbn [eval map prim_eval zv]. rewrite wrap_cong.
  change (zval (ev rho (scaled w s (fst kc) (mag w (snd kc)))))
    with (fv (scaled w s (fst kc) (mag w (snd kc)))).
  rewrite scaled_val. unfold mag. rewrite cong_mod by exact Hm.
  destruct acc as [a|]; cbn [accv].
  - change (zval (ev rho a)) with (fv a). apply eq_cong. ring.
  - rewrite litw_zval, wrap_cong. apply eq_cong. ring.
Qed.

Lemma render_val : forall p, cong m (fv (render w s p)) (pval fv p).
Proof.
  intros [l z]. unfold render, pval. cbn [fst snd].
  assert (HS : cong m (accv (fold_left (subneg w s) l (fold_left (addpos w s) l None))) (lval fv l)).
  { rewrite subneg_val, addpos_val. cbn [accv]. rewrite <- pos_neg. apply eq_cong. ring. }
  destruct (fold_left (subneg w s) l (fold_left (addpos w s) l None)) as [a|];
    cbn [accv] in HS; destruct (z =? 0) eqn:Z0.
  - apply Z.eqb_eq in Z0. subst. rewrite HS. apply eq_cong. ring.
  - destruct (negc w z); unfold fv at 1, fvr at 1; cbn [eval map prim_eval zv];
      rewrite wrap_cong, litw_zval, wrap_cong; change (zval (ev rho a)) with (fv a); rewrite HS.
    + unfold mag. rewrite cong_mod by exact Hm. apply eq_cong. ring.
    + reflexivity.
  - apply Z.eqb_eq in Z0. subst. unfold fv at 1, fvr at 1. rewrite litw_zval, wrap_cong, <- HS. apply eq_cong. ring.
  - unfold fv at 1, fvr at 1. rewrite litw_zval, wrap_cong, <- HS. apply eq_cong. ring.
Qed.

End RenderVal.


(* ---- each fold keeps type and meaning ---- *)

Lemma same_value : forall G rho t1 t2 w s, env_ok Abs I G rho -> int_ok w = true ->
  tyof G t1 = Some (TInt w s) -> tyof G t2 = Some (TInt w s) ->
  cong (modulus w) (fvr rho t1) (fvr rho t2) -> ev rho t1 = ev rho t2.
Proof.
  intros G rho t1 t2 w s Henv Hw T1 T2 C.
  destruct (typed_int G rho t1 w s Henv T1) as [E1 R1]. destruct (typed_int G rho t2 w s Henv T2) as [E2 R2].
  rewrite E1, E2. f_equal. apply (range_unique w s); [apply int_ok_width; exact Hw|exact R1|exact R2|exact C].
Qed.

Lemma arith_sound : forall G rho o w s a b, env_ok Abs I G rho -> is_arith o = true ->
  tyof G (Prim o w s [a; b]) = Some (TInt w s) ->
  tyof G (render w s (read w s (Prim o w s [a; b]))) = Some (TInt w s) /\
  ev rho (render w s (read w s (Prim o w s [a; b]))) = ev rho (Prim o w s [a; b]).
Proof.
  intros G rho o w s a b Henv Ho Ht.
  destruct (arith_args G o w s a b _ Ho Ht) as [Hw _].
  assert (Tr : tyof G (render w s (read w s (Prim o w s [a; b]))) = Some (TInt w s)).
  { apply render_typed; auto. apply read_atoms. exact Ht. }
  split; auto. eapply same_value; eauto.
  rewrite render_val, <- read_val. reflexivity.
Qed.

Lemma lit_bool_typed : forall G z, (z = 0 \/ z = 1) -> tyof G (Lit 1 false z) = Some u1.
Proof. intros G z [-> | ->]; reflexivity. Qed.

Lemma b2z_bool : forall b, b2z b = 0 \/ b2z b = 1.
Proof. intros []; cbn; auto. Qed.

Lemma lit_b2z_typed : forall G b, tyof G (Lit 1 false (b2z b)) = Some u1.
Proof. intros. apply lit_bool_typed, b2z_bool. Qed.

Lemma lit_b2z_eval : forall rho b, ev rho (Lit 1 false (b2z b)) = b2v Abs b.
Proof. intros rho []; reflexivity. Qed.

Lemma not_typed : forall G c, tyof G c = Some u1 -> tyof G (Prim ONot 1 false [c]) = Some u1.
Proof. intros G c H. cbn [type_of ty_ok int_ok]. cbn. rewrite H. reflexivity. Qed.

Lemma not_args : forall G w s c T, tyof G (Prim ONot w s [c]) = Some T -> T = u1 /\ w = 1%nat /\ s = false /\ tyof G c = Some u1.
Proof.
  intros G w s c T H. cbn [type_of] in H.
  destruct (negb (ty_ok (TInt w s))); [discriminate|].
  destruct (ty_eqb (TInt w s) u1) eqn:E; [|discriminate].
  apply ty_eqb_eq in E. unfold u1 in E. inversion E; subst.
  destruct (teq (tyof G c) (TInt 1 false)) eqn:Ec; [|discriminate]. apply teq_true in Ec.
  inversion H; subst. auto.
Qed.

Lemma negate_sound : forall G rho c, env_ok Abs I G rho -> tyof G c = Some u1 ->
  tyof G (negate c) = Some u1 /\ ev rho (negate c) = VZ (1 - zval (ev rho c)).
Proof.
  intros G rho c Henv Hc.
  destruct c as [i|w s v|d ts|o w s ts|D i u|D u x];
    try (split; [apply not_typed; exact Hc|reflexivity]).
  - (* a literal of the boolean type is 0 or 1 *)
    cbn [negate]. pose proof (typed_dom G rho _ _ Henv Hc) as D.
    apply (u1_values Abs I) in D. cbn [eval] in D.
    destruct D as [D|D]; inversion D; subst; split; try reflexivity.
  - destruct o; try (split; [apply not_typed; exact Hc|reflexivity]).
    destruct ts as [|x [|y ts]]; try (split; [apply not_typed; exact Hc|reflexivity]).
    cbn [negate]. apply not_args in Hc as [_ [-> [-> Hx]]]. split; auto.
    destruct (typed_int G rho x 1 false Henv Hx) as [E _].
    cbn [eval map prim_eval zv]. rewrite E at 1. f_equal. lia.
Qed.

Lemma cmp_typed : forall G o w s a b,
  (o = OEq \/ o = ONe \/ o = OLt \/ o = OLe \/ o = OGt \/ o = OGe \/ o = AddFits \/ o = SubFits \/ o = MulFits) ->
  int_ok w = true -> tyof G a = Some (TInt w s) -> tyof G b = Some (TInt w s) ->
  tyof G (Prim o w s [a; b]) = Some u1.
Proof.
  intros G o w s a b Ho Hw Ha Hb.
  destruct Ho as [->|[->|[->|[->|[->|[->|[->|[->| ->]]]]]]]]; cbn [type_of ty_ok]; rewrite Hw; cbn [negb];
    rewrite Ha, Hb; cbn [teq]; rewrite ty_eqb_refl; reflexivity.
Qed.

Lemma cmp_args : forall G o w s a b T,
  (o = OEq \/ o = ONe \/ o = OLt \/ o = OLe \/ o = OGt \/ o = OGe \/ o = AddFits \/ o = SubFits \/ o = MulFits) ->
  tyof G (Prim o w s [a; b]) = Some T ->
  T = u1 /\ int_ok w = true /\ tyof G a = Some (TInt w s) /\ tyof G b = Some (TInt w s).
Proof.
  intros G o w s a b T Ho H.
  destruct Ho as [->|[->|[->|[->|[->|[->|[->|[->| ->]]]]]]]]; cbn [type_of ty_ok] in H;
    destruct (int_ok w) eqn:Hw; cbn [negb] in H; try discriminate;
    destruct (teq (tyof G a) (TInt w s)) eqn:Ea; cbn [andb] in H; try discriminate;
    destruct (teq (tyof G b) (TInt w s)) eqn:Eb; try discriminate;
    inversion H; subst; apply teq_true in Ea; apply teq_true in Eb; auto.
Qed.

Lemma lit_typed_value : forall G rho w s w' s' v, env_ok Abs I G rho ->
  tyof G (Lit w' s' v) = Some (TInt w s) -> w' = w /\ s' = s /\ in_range w s v /\ int_ok w = true.
Proof.
  intros G rho w s w' s' v Henv H. cbn [type_of ty_ok] in H.
  destruct (int_ok w' && in_rangeb w' s' v) eqn:E; [|discriminate]. inversion H; subst.
  apply andb_true_iff in E as [E1 E2]. apply in_rangeb_iff in E2. auto.
Qed.

Lemma less_sound : forall G rho w s a b, env_ok Abs I G rho -> int_ok w = true ->
  tyof G a = Some (TInt w s) -> tyof G b = Some (TInt w s) ->
  tyof G (less w s a b) = Some u1 /\ ev rho (less w s a b) = ev rho (Prim OLt w s [a; b]).
Proof.
  intros G rho w s a b Henv Hw Ha Hb.
  pose proof (int_ok_width w Hw) as W.
  destruct (typed_int G rho a w s Henv Ha) as [Ea Ra]. destruct (typed_int G rho b w s Henv Hb) as [Eb Rb].
  assert (Keep : tyof G (Prim OLt w s [a; b]) = Some u1) by (apply cmp_typed; auto 10).
  assert (False0 : zval (ev rho a) <? zval (ev rho b) = false ->
                   tyof G (Lit 1 false 0) = Some u1 /\ ev rho (Lit 1 false 0) = ev rho (Prim OLt w s [a; b])).
  { intros F. split; [reflexivity|]. cbn [eval map prim_eval]. rewrite F. reflexivity. }
  unfold less.
  destruct a as [| wa sa va| | | |]; destruct b as [| wb sb vb| | | |];
  (* two literals *)
  try (destruct (lit_typed_value G rho w s wa sa va Henv Ha) as [-> [-> [Va _]]];
       destruct (lit_typed_value G rho w s wb sb vb Henv Hb) as [-> [-> [Vb _]]];
       rewrite !wrap_small by assumption; split; [apply lit_b2z_typed|];
       rewrite lit_b2z_eval; reflexivity);
  (* identical terms, then the bounds, then kept *)
  (match goal with
   | |- context [term_eqb ?x ?y] =>
       destruct (term_eqb x y) eqn:Eq;
       [apply term_eqb_eq in Eq; rewrite <- Eq in *; apply False0; apply Z.ltb_ge; lia|]
   end);
  repeat match goal with
         | |- context [wrap w s ?v =? min_int w s] =>
             destruct (wrap w s v =? min_int w s) eqn:Emin;
             [apply Z.eqb_eq in Emin; apply False0; apply Z.ltb_ge;
              match goal with H : tyof G (Lit ?w' ?s' v) = _ |- _ =>
                destruct (lit_typed_value G rho w s w' s' v Henv H) as [-> [-> [Vv _]]] end;
              rewrite wrap_small in Emin by assumption; cbn [eval zv] in *;
              unfold in_range in Ra; lia|]
         | |- context [wrap w s ?v =? max_int w s] =>
             destruct (wrap w s v =? max_int w s) eqn:Emax;
             [apply Z.eqb_eq in Emax; apply False0; apply Z.ltb_ge;
              match goal with H : tyof G (Lit ?w' ?s' v) = _ |- _ =>
                destruct (lit_typed_value G rho w s w' s' v Henv H) as [-> [-> [Vv _]]] end;
              rewrite wrap_small in Emax by assumption; cbn [eval zv] in *;
              unfold in_range in Rb; lia|]
         end;
  split; auto.
Qed.


Lemma zero_in_range : forall w s, int_ok w = true -> in_range w s 0.
Proof. intros. apply range_has_zero, int_ok_width. assumption. Qed.

Lemma litw0_zval : forall rho w s, int_ok w = true -> zval (ev rho (litw w s 0)) = 0.
Proof. intros rho w s Hw. rewrite litw_zval. apply wrap_small; [apply int_ok_width; exact Hw|apply zero_in_range; exact Hw]. Qed.

Lemma zero_iff : forall w s zq za zb, int_ok w = true -> in_range w s zq -> in_range w s za -> in_range w s zb ->
  (cong (modulus w) zq (za - zb) \/ cong (modulus w) zq (- (za - zb))) -> (zq =? 0) = (za =? zb).
Proof.
  intros w s zq za zb Hw Rq Ra Rb C. pose proof (int_ok_width w Hw) as W.
  destruct (Z.eqb_spec zq 0) as [Q|Q]; destruct (Z.eqb_spec za zb) as [E|E]; try reflexivity; exfalso.
  - subst zq. apply E. apply (range_unique w s); auto.
    destruct C as [C|C]; [rewrite <- (Z.sub_add zb za), <- C; apply eq_cong; ring|].
    rewrite <- (Z.sub_add zb za). apply cong_opp in C. rewrite Z.opp_involutive in C. rewrite <- C.
    apply eq_cong. ring.
  - subst zb. apply Q. apply (range_unique w s); auto; [apply zero_in_range; exact Hw|].
    destruct C as [C|C]; rewrite C; apply eq_cong; ring.
Qed.

Lemma psum_const_range : forall m into added scale, 0 < m -> 0 <= snd (psum m into added scale) < m.
Proof. intros. unfold psum. cbn [snd]. apply Z.mod_pos_bound. assumption. Qed.

Lemma equal_sound : forall G rho w s a b, env_ok Abs I G rho -> int_ok w = true ->
  tyof G a = Some (TInt w s) -> tyof G b = Some (TInt w s) ->
  tyof G (equal w s a b) = Some u1 /\ ev rho (equal w s a b) = ev rho (Prim OEq w s [a; b]).
Proof.
  intros G rho w s a b Henv Hw Ha Hb. pose proof (modulus_pos w) as Hm.
  destruct (typed_int G rho a w s Henv Ha) as [Ea Ra]. destruct (typed_int G rho b w s Henv Hb) as [Eb Rb].
  set (m := modulus w) in *.
  set (p := psum m (read w s a) (read w s b) (m - 1)).
  assert (Hp : cong m (pval (fvr rho) p) (fvr rho a - fvr rho b)).
  { unfold p. rewrite psum_val by exact Hm. rewrite <- !read_val.
    rewrite <- (cong_mod m Hm m) at 2. rewrite Z_mod_same_full. apply eq_cong. ring. }
  assert (Pa : atoms (fun t => tyof G t = Some (TInt w s)) p).
  { apply psum_atoms; apply read_atoms; assumption. }
  change (zval (ev rho a)) with (fvr rho a) in Ra. change (zval (ev rho b)) with (fvr rho b) in Rb.
  cbn [eval map prim_eval]. unfold equal. fold m. fold p.
  destruct (fst p) as [|k rest] eqn:Fp.
  - split; [apply lit_b2z_typed|]. rewrite lit_b2z_eval. f_equal.
    assert (Hz : cong m (snd p) (fvr rho a - fvr rho b)) by (rewrite <- Hp; unfold pval; rewrite Fp; apply eq_cong; cbn; ring).
    pose proof (psum_const_range m (read w s a) (read w s b) (m - 1) Hm) as Rz. fold p in Rz.
    change (zval (ev rho a)) with (fvr rho a). change (zval (ev rho b)) with (fvr rho b).
    destruct (Z.eqb_spec (snd p) 0) as [Z0|Z0]; destruct (Z.eqb_spec (fvr rho a) (fvr rho b)) as [E|E];
      try reflexivity; exfalso.
    + apply E. apply (range_unique w s); auto; [apply int_ok_width; exact Hw|].
      rewrite Z0 in Hz. rewrite <- (Z.sub_add (fvr rho b) (fvr rho a)), <- Hz. apply eq_cong. ring.
    + apply Z0. rewrite E, Z.sub_diag in Hz. destruct Hz as [Hz]. rewrite Z.mod_0_l, Z.mod_small in Hz by lia.
      exact Hz.
  - assert (Tq : forall q, q = render w s p \/ q = render w s (pneg m p) -> tyof G q = Some (TInt w s)).
    { intros q [-> | ->]; apply render_typed; auto; apply pneg_atoms; auto. }
    set (q := if tlt (render w s (pneg m p)) (render w s p) then render w s (pneg m p) else render w s p).
    assert (Hq : q = render w s p \/ q = render w s (pneg m p)) by (unfold q; destruct tlt; auto).
    pose proof (Tq q Hq) as Tq'.
    split; [apply cmp_typed; auto 10; apply litw_typed; exact Hw|].
    cbn [eval map prim_eval]. rewrite litw0_zval by exact Hw.
    destruct (typed_int G rho q w s Henv Tq') as [_ Rq]. f_equal.
    apply (zero_iff w s); auto.
    destruct Hq as [-> | ->]; [left|right].
    + change (zval (ev rho (render w s p))) with (fvr rho (render w s p)). rewrite render_val. exact Hp.
    + change (zval (ev rho (render w s (pneg m p)))) with (fvr rho (render w s (pneg m p))).
      rewrite render_val, pneg_val by exact Hm. rewrite Hp. reflexivity.
Qed.

Lemma sel_args : forall G w s c x y T, tyof G (Prim OSelect w s [c; x; y]) = Some T ->
  T = TInt w s /\ int_ok w = true /\ tyof G c = Some u1 /\ tyof G x = Some (TInt w s) /\ tyof G y = Some (TInt w s).
Proof.
  intros G w s c x y T H. cbn [type_of ty_ok] in H.
  destruct (int_ok w) eqn:Hw; cbn [negb] in H; [|discriminate].
  destruct (teq (tyof G c) u1) eqn:Ec; cbn [andb] in H; [|discriminate].
  destruct (teq (tyof G x) (TInt w s)) eqn:Ex; cbn [andb] in H; [|discriminate].
  destruct (teq (tyof G y) (TInt w s)) eqn:Ey; [|discriminate].
  inversion H; subst. apply teq_true in Ec, Ex, Ey. auto.
Qed.

Lemma sel_typed : forall G w s c x y, int_ok w = true -> tyof G c = Some u1 ->
  tyof G x = Some (TInt w s) -> tyof G y = Some (TInt w s) -> tyof G (Prim OSelect w s [c; x; y]) = Some (TInt w s).
Proof.
  intros G w s c x y Hw Hc Hx Hy. cbn [type_of ty_ok]. rewrite Hw. cbn [negb].
  rewrite Hc, Hx, Hy. cbn [teq]. rewrite !ty_eqb_refl. reflexivity.
Qed.

Lemma select_sound : forall G rho w s c x y T, env_ok Abs I G rho ->
  tyof G (Prim OSelect w s [c; x; y]) = Some T ->
  tyof G (select w s c x y) = Some T /\ ev rho (select w s c x y) = ev rho (Prim OSelect w s [c; x; y]).
Proof.
  intros G rho w s c x y T Henv H.
  pose proof H as H0. apply sel_args in H0 as [-> [Hw [Hc [Hx Hy]]]].
  unfold select. cbn [eval map prim_eval].
  destruct (match c with Lit w' s' v => same_int 1 false w' s' && ((v =? 0) || (v =? 1)) | _ => false end) eqn:L.
  - destruct c; try discriminate. cbn [eval zv]. destruct (z =? 1); split; auto.
  - destruct (term_eqb x y) eqn:E.
    + apply term_eqb_eq in E. subst. split; auto. destruct (zval _ =? 1); reflexivity.
    + destruct c as [| | |o wo so ts| |]; try (split; auto; fail).
      destruct o; try (split; auto; fail).
      destruct ts as [|c' [|c'' ts]]; try (split; auto; fail).
      apply not_args in Hc as [_ [-> [-> Hc']]].
      split; [apply sel_typed; auto|].
      cbn [eval map prim_eval zv]. pose proof (typed_dom G rho c' u1 Henv Hc') as D.
      apply (u1_values Abs I) in D. destruct D as [D|D]; rewrite D; reflexivity.
Qed.

Lemma lit_value_spec : forall G rho w s t x, env_ok Abs I G rho -> int_ok w = true ->
  tyof G t = Some (TInt w s) -> lit_value w s t = Some x -> ev rho t = VZ x /\ in_range w s x.
Proof.
  intros G rho w s t x Henv Hw Ht L. destruct t; try discriminate. cbn [lit_value] in L.
  destruct (same_int w s n b) eqn:E; [|discriminate]. inversion L; subst.
  destruct (lit_typed_value G rho w s n b z Henv Ht) as [-> [-> [R _]]].
  rewrite wrap_small by (first [apply int_ok_width; assumption | assumption]). split; auto.
Qed.

Lemma fits_sound : forall G rho o w s a b T, env_ok Abs I G rho ->
  (o = AddFits \/ o = SubFits \/ o = MulFits) -> tyof G (Prim o w s [a; b]) = Some T ->
  tyof G (fits w s o a b) = Some T /\ ev rho (fits w s o a b) = ev rho (Prim o w s [a; b]).
Proof.
  intros G rho o w s a b T Henv Ho H.
  assert (Ho' : o = OEq \/ o = ONe \/ o = OLt \/ o = OLe \/ o = OGt \/ o = OGe \/ o = AddFits \/ o = SubFits \/ o = MulFits)
    by (destruct Ho as [->|[->| ->]]; auto 10).
  pose proof H as H0. apply cmp_args in H0 as [-> [Hw [Ha Hb]]]; auto.
  unfold fits.
  destruct (lit_value w s a) as [va|] eqn:La; destruct (lit_value w s b) as [vb|] eqn:Lb.
  1: { destruct (lit_value_spec G rho w s a va Henv Hw Ha La) as [Ea _].
       destruct (lit_value_spec G rho w s b vb Henv Hw Hb Lb) as [Eb _].
       split; [apply lit_b2z_typed|]. rewrite lit_b2z_eval. cbn [eval map prim_eval]. rewrite Ea, Eb.
       cbn [zv]. destruct Ho as [->|[->| ->]]; reflexivity. }
  all: destruct (negb (op_eqb o SubFits) && tlt b a) eqn:Sw; [|split; auto].
  all: apply andb_true_iff in Sw as [Sw _];
       destruct Ho as [->|[->| ->]]; try discriminate; split; try (apply cmp_typed; auto 10);
       cbn [eval map prim_eval]; first [rewrite Z.add_comm; reflexivity | rewrite Z.mul_comm; reflexivity].
Qed.

Lemma quot_args : forall G o w s a b T, (o = OQuot \/ o = ORem) -> tyof G (Prim o w s [a; b]) = Some T ->
  T = TInt w s /\ int_ok w = true /\ tyof G a = Some (TInt w s) /\ tyof G b = Some (TInt w s).
Proof.
  intros G o w s a b T Ho H.
  destruct Ho as [-> | ->]; cbn [type_of ty_ok] in H;
    destruct (int_ok w) eqn:Hw; cbn [negb] in H; try discriminate;
    destruct (teq (tyof G a) (TInt w s)) eqn:Ea; cbn [andb] in H; try discriminate;
    destruct (teq (tyof G b) (TInt w s)) eqn:Eb; try discriminate;
    inversion H; subst; apply teq_true in Ea; apply teq_true in Eb; auto.
Qed.

Lemma quot_typed : forall G o w s a b, (o = OQuot \/ o = ORem) -> int_ok w = true ->
  tyof G a = Some (TInt w s) -> tyof G b = Some (TInt w s) -> tyof G (Prim o w s [a; b]) = Some (TInt w s).
Proof.
  intros G o w s a b Ho Hw Ha Hb.
  destruct Ho as [-> | ->]; cbn [type_of ty_ok]; rewrite Hw; cbn [negb];
    rewrite Ha, Hb; cbn [teq]; rewrite ty_eqb_refl; reflexivity.
Qed.

Lemma lit_typed : forall G w s v, int_ok w = true -> in_range w s v -> tyof G (Lit w s v) = Some (TInt w s).
Proof.
  intros G w s v Hw R. cbn [type_of ty_ok]. rewrite Hw.
  apply in_rangeb_iff in R. rewrite R. reflexivity.
Qed.

Lemma quot_minus_one : forall z, z ÷ -1 = - z.
Proof. intros z. change (-1) with (- (1)). rewrite Z.quot_opp_r, Z.quot_1_r by lia. reflexivity. Qed.

Lemma rem_minus_one : forall z, Z.rem z (-1) = 0.
Proof. intros z. change (-1) with (- (1)). rewrite Z.rem_opp_r, Z.rem_1_r by lia. reflexivity. Qed.

Lemma divide_sound : forall G rho (quot : bool) w s a b T, env_ok Abs I G rho ->
  tyof G (Prim (if quot then OQuot else ORem) w s [a; b]) = Some T ->
  tyof G (divide w s quot a b) = Some T /\ ev rho (divide w s quot a b) = ev rho (Prim (if quot then OQuot else ORem) w s [a; b]).
Proof.
  intros G rho quot w s a b T Henv H.
  pose proof H as H0. apply quot_args in H0 as [-> [Hw [Ha Hb]]]; [|destruct quot; auto].
  pose proof (int_ok_width w Hw) as W.
  destruct (typed_int G rho a w s Henv Ha) as [Ea Ra]. destruct (typed_int G rho b w s Henv Hb) as [Eb Rb].
  assert (Z0t : tyof G (Lit w s 0) = Some (TInt w s)) by (apply lit_typed; auto; apply zero_in_range; auto).
  unfold divide. cbn [eval map prim_eval].
  destruct (lit_value w s b) as [y|] eqn:Lb.
  - destruct (lit_value_spec G rho w s b y Henv Hw Hb Lb) as [Eb' Ry]. rewrite Eb'. cbn [zv].
    destruct (y =? 0) eqn:Y0.
    + apply Z.eqb_eq in Y0. subst y. destruct quot; cbn [prim_eval]; split; auto.
    + destruct (lit_value w s a) as [x|] eqn:La.
      * destruct (lit_value_spec G rho w s a x Henv Hw Ha La) as [Ea' Rx]. rewrite Ea'. cbn [zv].
        apply Z.eqb_neq in Y0.
        destruct quot; cbn [prim_eval]; split.
        -- apply lit_typed; auto. apply wrap_in_range. exact W.
        -- cbn [eval zv]. rewrite (proj2 (Z.eqb_neq y 0) Y0). reflexivity.
        -- apply lit_typed; auto. apply rem_in_range; auto.
        -- cbn [eval zv]. rewrite (proj2 (Z.eqb_neq y 0) Y0). reflexivity.
      * destruct (y =? 1) eqn:Y1; [apply Z.eqb_eq in Y1; subst y|].
        { destruct quot; cbn [prim_eval]; split; auto; cbn [eval].
          - rewrite Z.quot_1_r, wrap_small by auto. rewrite Ea at 1. reflexivity.
          - rewrite Z.rem_1_r. reflexivity. }
        destruct (y =? -1) eqn:Ym; [apply Z.eqb_eq in Ym; subst y|].
        { destruct quot; cbn [prim_eval].
          - assert (Ts : tyof G (Prim SubWrap w s [Lit w s 0; a]) = Some (TInt w s)) by (apply arith_typed; auto).
            destruct (arith_sound G rho SubWrap w s (Lit w s 0) a Henv eq_refl Ts) as [T1 E1].
            split; auto. rewrite E1. cbn [eval map prim_eval zv].
            change (-1 =? 0) with false. cbn iota.
            rewrite quot_minus_one. reflexivity.
          - split; auto. cbn [eval zv]. change (-1 =? 0) with false. cbn iota.
            rewrite rem_minus_one. reflexivity. }
        destruct quot; split; auto; cbn [eval map prim_eval]; rewrite Eb'; reflexivity.
  - destruct (lit_value w s a) as [x|] eqn:La.
    + destruct (lit_value_spec G rho w s a x Henv Hw Ha La) as [Ea' Rx].
      destruct (x =? 0) eqn:X0; [apply Z.eqb_eq in X0; subst x|].
      * rewrite Ea'. cbn [zv]. destruct quot; cbn [prim_eval]; split; auto; cbn [eval];
          destruct (zval (ev rho b) =? 0) eqn:B0; try reflexivity.
        all: rewrite wrap_small by (auto; apply zero_in_range; auto); reflexivity.
      * destruct quot; split; auto.
    + destruct quot; split; auto.
Qed.

Lemma convert_sound : forall G rho w s a T, env_ok Abs I G rho ->
  tyof G (Prim OConvert w s [a]) = Some T ->
  tyof G (convert w s a) = Some T /\ ev rho (convert w s a) = ev rho (Prim OConvert w s [a]).
Proof.
  intros G rho w s a T Henv H. pose proof H as H0. cbn [type_of ty_ok] in H0.
  destruct (int_ok w) eqn:Hw; cbn [negb] in H0; [|discriminate].
  destruct (tyof G a) as [[wa sa| |]|] eqn:Ta; try discriminate. inversion H0; subst.
  unfold convert. destruct a; try (split; auto; fail).
  destruct (int_ok n) eqn:Hn; [|split; auto].
  destruct (lit_typed_value G rho wa sa n b z Henv Ta) as [-> [-> [R _]]].
  split.
  - apply lit_typed; auto. apply wrap_in_range, int_ok_width. exact Hw.
  - cbn [eval map prim_eval zv]. rewrite (wrap_small wa sa z) by (auto; apply int_ok_width; auto). reflexivity.
Qed.


Lemma zv_b2v : forall b, zval (b2v Abs b) = b2z b.
Proof. intros []; reflexivity. Qed.

Lemma nprim_sound : forall G rho o w s ts T, env_ok Abs I G rho -> tyof G (Prim o w s ts) = Some T ->
  tyof G (nprim o w s ts) = Some T /\ ev rho (nprim o w s ts) = ev rho (Prim o w s ts).
Proof.
  intros G rho o w s ts T Henv H. unfold nprim.
  destruct (negb (int_ok w) || negb (Nat.eqb (length ts) (arity o))) eqn:B; [split; auto|].
  apply orb_false_iff in B as [B1 B2]. apply negb_false_iff in B1, B2.
  assert (Cmp : forall o', (o' = OEq \/ o' = ONe \/ o' = OLt \/ o' = OLe \/ o' = OGt \/ o' = OGe \/
                            o' = AddFits \/ o' = SubFits \/ o' = MulFits) -> o' = o' ) by auto.
  destruct o; destruct ts as [|a [|b [|c [|d ts]]]]; cbn [length arity Nat.eqb] in B2; try discriminate.
  (* arithmetic *)
  1, 2, 3:
    pose proof H as H0; apply arith_args in H0 as [_ [_ [_ ->]]]; auto;
    apply arith_sound; auto.
  (* Equal *)
  - pose proof H as H0. apply cmp_args in H0 as [-> [Hw [Ha Hb]]]; auto.
    apply equal_sound; auto.
  (* NotEqual *)
  - pose proof H as H0. apply cmp_args in H0 as [-> [Hw [Ha Hb]]]; auto 10.
    destruct (equal_sound G rho w s a b Henv Hw Ha Hb) as [Te Ee].
    destruct (negate_sound G rho _ Henv Te) as [Tn En]. split; auto.
    rewrite En, Ee. cbn [eval map prim_eval]. rewrite zv_b2v. destruct (_ =? _); reflexivity.
  (* Less *)
  - pose proof H as H0. apply cmp_args in H0 as [-> [Hw [Ha Hb]]]; auto 10.
    apply less_sound; auto.
  (* LessEqual *)
  - pose proof H as H0. apply cmp_args in H0 as [-> [Hw [Ha Hb]]]; auto 10.
    destruct (less_sound G rho w s b a Henv Hw Hb Ha) as [Tl El].
    destruct (negate_sound G rho _ Henv Tl) as [Tn En]. split; auto.
    rewrite En, El. cbn [eval map prim_eval]. rewrite zv_b2v, (Z.leb_antisym (zval (ev rho b))).
    destruct (_ <? _); reflexivity.
  (* Greater *)
  - pose proof H as H0. apply cmp_args in H0 as [-> [Hw [Ha Hb]]]; auto 10.
    destruct (less_sound G rho w s b a Henv Hw Hb Ha) as [Tl El]. split; auto.
  (* GreaterEqual *)
  - pose proof H as H0. apply cmp_args in H0 as [-> [Hw [Ha Hb]]]; auto 10.
    destruct (less_sound G rho w s a b Henv Hw Ha Hb) as [Tl El].
    destruct (negate_sound G rho _ Henv Tl) as [Tn En]. split; auto.
    rewrite En, El. cbn [eval map prim_eval]. rewrite zv_b2v, (Z.leb_antisym (zval (ev rho a))).
    destruct (_ <? _); reflexivity.
  (* Not *)
  - pose proof H as H0. apply not_args in H0 as [-> [-> [-> Ha]]].
    destruct (negate_sound G rho a Henv Ha) as [Tn En]. split; auto.
  (* Select *)
  - apply select_sound; auto.
  (* representability *)
  - apply fits_sound; auto.
  - apply fits_sound; auto.
  - apply fits_sound; auto.
  (* Quotient, Remainder *)
  - exact (divide_sound G rho true w s a b T Henv H).
  - exact (divide_sound G rho false w s a b T Henv H).
  (* Convert *)
  - apply convert_sound; auto.
Qed.

(* ---- the typing of a primitive depends only on its operands' types ---- *)

Lemma prim_congr : forall G rho o w s ts ts',
  Forall2 (fun t t' => tyof G t' = tyof G t /\ ev rho t' = ev rho t) ts ts' ->
  tyof G (Prim o w s ts') = tyof G (Prim o w s ts) /\ ev rho (Prim o w s ts') = ev rho (Prim o w s ts).
Proof.
  intros G rho o w s ts ts' F. split.
  - destruct F as [|a a' ts ts' [Ta _] F]; [reflexivity|].
    destruct F as [|b b' ts ts' [Tb _] F];
      [destruct o; cbn [type_of]; rewrite ?Ta; reflexivity|].
    destruct F as [|c c' ts ts' [Tc _] F];
      [destruct o; cbn [type_of]; rewrite ?Ta, ?Tb; reflexivity|].
    destruct F as [|d d' ts ts' [Td _] F];
      [destruct o; cbn [type_of]; rewrite ?Ta, ?Tb, ?Tc; reflexivity|].
    destruct o; reflexivity.
  - cbn [eval]. f_equal. induction F as [|t t' ts ts' [_ E] F IH]; cbn [map]; [reflexivity|].
    rewrite E, IH. reflexivity.
Qed.

Lemma prim_args_typed : forall G o w s ts T, tyof G (Prim o w s ts) = Some T ->
  Forall (fun a => exists U, tyof G a = Some U) ts.
Proof.
  intros G o w s ts T H. cbn [type_of] in H.
  destruct (negb (ty_ok (TInt w s))); [discriminate|].
  destruct o; destruct ts as [|a [|b [|c [|d ts]]]]; try discriminate;
    repeat match goal with
           | H : (if ?c then _ else _) = _ |- _ => destruct c eqn:?; [|discriminate]
           | H : match tyof G ?x with _ => _ end = _ |- _ => destruct (tyof G x) as [[]|] eqn:?; try discriminate
           end;
    repeat match goal with
           | Hb : (_ && _)%bool = true |- _ => apply andb_true_iff in Hb as [? ?]
           | Ht : teq _ _ = true |- _ => apply teq_true in Ht
           end;
    repeat constructor; eauto.
Qed.

Lemma call_args_typed : forall G d ts T, tyof G (Call d ts) = Some T ->
  exists ps, sig d = Some (ps, T) /\ Forall2 (fun t p => tyof G t = Some p) ts ps.
Proof.
  intros G d ts T H. cbn [type_of] in H.
  destruct (sig d) as [[ps R]|] eqn:S; [|discriminate].
  match type of H with (if ?c then _ else _) = _ => destruct c eqn:C end; [|discriminate].
  inversion H; subst. exists ps. split; auto.
  clear S H. revert ps C. induction ts as [|t ts IH]; intros [|p ps] C; cbn in C; try discriminate; constructor.
  - apply andb_true_iff in C as [C _]. apply teq_true. exact C.
  - apply andb_true_iff in C as [_ C]. apply IH. exact C.
Qed.

Lemma eval_tsubst : forall t rho sg rho',
  (forall i, ev rho (sg i) = nth i rho' dflt) -> ev rho (tsubst sg t) = ev rho' t.
Proof.
  induction t using term_ind'; intros rho sg rho' Hsg; cbn [tsubst eval]; auto.
  - f_equal. rewrite map_map. induction H; cbn [map]; auto. f_equal; auto.
  - f_equal. rewrite map_map. induction H; cbn [map]; auto. f_equal; auto.
  - f_equal. auto.
  - f_equal; auto.
Qed.

Lemma forall2_rev : forall (A B : Type) (P : A -> B -> Prop) xs ys, Forall2 P xs ys -> Forall2 P (rev xs) (rev ys).
Proof.
  intros A B P xs ys F. induction F; cbn [rev]; auto.
  apply Forall2_app; auto.
Qed.

Lemma forall2_nth : forall (A B : Type) (P : A -> B -> Prop) xs ys i dx dy, Forall2 P xs ys ->
  (i < length xs)%nat -> P (nth i xs dx) (nth i ys dy).
Proof.
  intros A B P xs ys i dx dy F. revert i. induction F; intros i L; cbn [length] in L; [lia|].
  destruct i; cbn [nth]; auto. apply IHF. lia.
Qed.

Lemma norm_sound : forall fuel G rho t T, env_ok Abs I G rho -> tyof G t = Some T ->
  tyof G (norm sig body fuel t) = Some T /\ ev rho (norm sig body fuel t) = ev rho t.
Proof.
  induction fuel as [|n IH]; intros G rho t T Henv Ht; [split; auto|].
  destruct t as [i|w s v|d ts|o w s ts|D i u|D u x]; cbn [norm]; try (split; auto; fail).
  - (* a call is unfolded into its body *)
    destruct (call_args_typed G d ts T Ht) as [ps [S F]]. rewrite S.
    destruct (body d) as [b|] eqn:Bd; [|split; auto].
    assert (Len : length ps = length ts) by (symmetry; eapply Forall2_length; eauto).
    rewrite Len, Nat.eqb_refl.
    set (ts' := map (norm sig body n) ts).
    assert (F' : Forall2 (fun t' p => tyof G t' = Some p) ts' ps).
    { unfold ts'. clear -F IH Henv. induction F; cbn [map]; constructor; auto.
      destruct (IH G rho x y Henv H) as [T' _]. exact T'. }
    assert (Ev : map (ev rho) ts' = map (ev rho) ts).
    { unfold ts'. clear -F IH Henv. induction F; cbn [map]; auto. f_equal; auto.
      destruct (IH G rho x y Henv H) as [_ E']. exact E'. }
    assert (Sub : forall i, tyof G (argsub ts' i) = tyof (rev ps) (Var i)).
    { intros i. unfold argsub. cbn [type_of].
      pose proof (forall2_rev _ _ _ _ _ F') as R.
      destruct (Nat.lt_ge_cases i (length (rev ps))) as [L|L].
      - assert (Li : (i < length (rev ts'))%nat) by (rewrite (Forall2_length R); exact L).
        pose proof (forall2_nth _ _ _ _ _ i (Prim ONot 1 false []) u1 R Li) as Hi.
        rewrite (nth_error_nth' (rev ps) u1 L). rewrite Hi.
        rewrite (type_of_ok sig sig_ok _ G _ Hi). reflexivity.
      - rewrite (proj2 (nth_error_None (rev ps) i) L).
        rewrite nth_overflow by (rewrite (Forall2_length R); exact L). reflexivity. }
    assert (Tu : tyof G (tsubst (argsub ts') b) = Some T).
    { rewrite (type_of_tsubst sig b (rev ps) G (argsub ts') Sub). eapply body_typed; eauto. }
    destruct (IH G rho _ T Henv Tu) as [Tn En]. split; auto.
    rewrite En. rewrite (eval_tsubst b rho (argsub ts') (rev (map (ev rho) ts'))).
    + rewrite Ev. cbn [eval]. symmetry. eapply call_body; eauto.
      clear -F Henv proj_ok elem_ok call_ok. induction F; cbn [map]; constructor; auto. eapply typed_dom; eauto.
    + intros i. unfold argsub. rewrite <- map_rev.
      change dflt with (ev rho (Prim ONot 1 false [])). symmetry. apply (map_nth (ev rho)).
  - (* a primitive is rebuilt from normalized operands *)
    pose proof (prim_args_typed G o w s ts T Ht) as A.
    assert (F : Forall2 (fun t t' => tyof G t' = tyof G t /\ ev rho t' = ev rho t) ts (map (norm sig body n) ts)).
    { clear -A IH Henv. induction A as [|a ts [U Ha] A IHA]; cbn [map]; constructor; auto.
      destruct (IH G rho a U Henv Ha) as [T' E']. rewrite T', Ha. auto. }
    destruct (prim_congr G rho o w s ts _ F) as [Tc Ec].
    rewrite <- Ec. apply nprim_sound; auto. rewrite Tc. exact Ht.
  - (* an observation normalizes its subject *)
    cbn [type_of] in Ht. destruct D as [| id obs |]; try discriminate.
    destruct (ty_ok (TValue id obs) && teq (tyof G u) (TValue id obs)) eqn:O; [|discriminate].
    apply andb_true_iff in O as [O1 O2]. apply teq_true in O2.
    destruct (IH G rho u _ Henv O2) as [Tu Eu]. split.
    + cbn [type_of]. rewrite O1, Tu. cbn [teq andb]. rewrite ty_eqb_refl. exact Ht.
    + cbn [eval]. rewrite Eu. reflexivity.
  - cbn [type_of] in Ht. destruct D as [| | U k]; try discriminate.
    match type of Ht with (if ?c then _ else _) = _ => destruct c eqn:O end; [|discriminate].
    repeat rewrite andb_true_iff in O. destruct O as [[O1 O2] O3]. apply teq_true in O2.
    destruct (tyof G x) as [X|] eqn:TX; [|discriminate].
    destruct (IH G rho u _ Henv O2) as [Tu Eu]. destruct (IH G rho x _ Henv TX) as [Tx Ex]. split.
    + cbn [type_of]. rewrite O1, Tu, Tx. cbn [teq andb]. rewrite ty_eqb_refl. cbn [andb]. rewrite O3. exact Ht.
    + cbn [eval]. rewrite Eu, Ex. reflexivity.
Qed.

(* M2, discharged: normalization keeps the meaning of every typed term. *)
Theorem nf_sound : forall G rho t T, env_ok Abs I G rho -> tyof G t = Some T -> ev rho (nf sig body t) = ev rho t.
Proof. intros G rho t T Henv Ht. apply (proj2 (norm_sound max_depth G rho t T Henv Ht)). Qed.

End Sound.

(* check_sound, with reflexivity deciding equality by the kernel's
   normalization: every premise about normalization is discharged, and what
   remains is that the interpretation models the definitions, observations and
   calls, and the M3 premise about linear arithmetic, which Linear.v
   discharges (check_sound_closed). *)
Theorem check_sound_normalized :
  forall (sig : nat -> option (list ty * ty)),
  (forall d ps R, sig d = Some (ps, R) -> ty_ok R = true) ->
  forall (body : nat -> option term),
  (forall d ps R b, sig d = Some (ps, R) -> body d = Some b -> type_of sig (rev ps) b = Some R) ->
  forall (cert : Type) (lin_ok : list ty -> list prop -> prop -> cert -> bool) (Abs : Type) (I : interp Abs),
  (forall id obs i v T,
      ty_ok (TValue id obs) = true -> dom Abs I (TValue id obs) v ->
      nth_error obs i = Some T -> dom Abs I T (i_proj Abs I (TValue id obs) i v)) ->
  (forall U k v X x,
      ty_ok (TIndexed U k) = true -> dom Abs I (TIndexed U k) v ->
      is_int X = true -> dom Abs I X x -> dom Abs I U (i_elem Abs I (TIndexed U k) v x)) ->
  (forall d ps R args,
      sig d = Some (ps, R) -> Forall2 (dom Abs I) ps args -> dom Abs I R (i_call Abs I d args)) ->
  (forall d ps R b args,
      sig d = Some (ps, R) -> body d = Some b -> Forall2 (dom Abs I) ps args ->
      i_call Abs I d args = eval Abs I (rev args) b) ->
  (forall G Fs P k rho,
      lin_ok G Fs P k = true ->
      Forall (fun F => valid sig G F = true) Fs -> valid sig G P = true ->
      env_ok Abs I G rho -> Forall (holds Abs I rho) Fs -> holds Abs I rho P) ->
  forall P (e : evid cert),
  check sig (nf sig body) cert lin_ok P e = true -> holds Abs I [] P.
Proof.
  intros sig sig_ok body body_typed cert lin_ok Abs I Hp He Hc Hb Hl P e E.
  eapply (check_sound sig sig_ok (nf sig body) cert lin_ok Abs I Hp He Hc); eauto.
  intros G rho t T Henv Ht. eapply (nf_sound sig sig_ok body body_typed Abs I Hp He Hc Hb); eauto.
Qed.
