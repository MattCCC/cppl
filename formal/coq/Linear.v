(* The translation of linear arithmetic (KERNEL.md 12; Builder and
   arithmetic_system in kernel/src/linear.cpp), modelled, and proven to keep
   every machine valuation that makes the facts true and the goal false as an
   integer solution of the system it builds. With check_certificate_sound
   (Certificate.v) that is the M3 premise Checker.v states as lin_sound,
   discharged for this translation: check_sound_closed below is check_sound
   with reflexivity deciding by the model's normalizer (Normalize.v) and
   linear arithmetic by this translation and the certificate checker, with no
   premise about the checker left.

   The model follows the kernel step for step: variables and constraints are
   made in the kernel's order, and one normal term at one type gets one
   variable and one value, so a certificate naming the kernel's positions and
   variables names the model's. It differs from the kernel only where the
   model accepts more: it has no limits, its recursion bounded by fuel alone,
   and its arithmetic is unbounded where the kernel's is 128-bit and refuses
   on overflow. A coefficient list may repeat a variable or hold a zero where
   the kernel's map would not; a constraint means the same either way. Where
   normal forms differ in placement (Normalize.v), so can the sharing of
   variables; that, as for the rest, rests on the transcription. *)

From Coq Require Import ZArith List Bool Lia.
Import ListNotations.
From CppL Require Import Syntax Semantics Typing Checker Normalize Certificate.

Open Scope Z_scope.

(* ------------------------------------------------------------------------ *)
(* Expressions, variables and the builder's state.                          *)

(* Expression in kernel/src/linear.cpp: sum(coefficient * variable) + constant. *)
Record expr : Type := mkE { eterms : list (nat * Z); econst : Z }.

Definition enone (c : Z) : expr := mkE [] c.
Definition evar (i : nat) : expr := mkE [(i, 1)] 0.
Definition eneg (e : expr) : expr := mkE (map (fun vc => (fst vc, - snd vc)) (eterms e)) (- econst e).
Definition esub (a b : expr) : expr := mkE (eterms a ++ eterms (eneg b)) (econst a - econst b).
Definition escale (e : expr) (k : Z) : expr := mkE (map (fun vc => (fst vc, snd vc * k)) (eterms e)) (econst e * k).
Definition eplus (e : expr) (i : nat) (c : Z) : expr := mkE (eterms e ++ [(i, c)]) (econst e).

(* finish: expression + extra <= 0. *)
Definition finish (e : expr) (extra : Z) : constraint := {| terms := eterms e; constant := econst e + extra |}.

(* A value variable stands for the machine value of its term at its type. A
   wrap variable stands for the multiple of 2^w by which its ghost expression,
   read over the integers, exceeds the machine value of its term. The ghost is
   the model's: it records what the kernel's comment says the variable is. *)
Inductive role : Type := RValue | RWrap.
Record avar : Type := mkV { arole : role; aw : nat; asg : bool; aterm : term; aghost : expr }.

Record state : Type := mkS {
  svars : list avar;
  svalues : list (nat * bool * term * expr);   (* values_ *)
  svarmap : list (nat * bool * term * nat);    (* variables_ *)
  sdivs : list (nat * bool * term);            (* divisions_ *)
  sranges : list (nat * (Z * Z));              (* ranges_ *)
  scons : list constraint;
  sdisj : list (constraint * constraint)
}.

Definition key_eqb (w : nat) (s : bool) (t : term) (w' : nat) (s' : bool) (t' : term) : bool :=
  Nat.eqb w w' && Bool.eqb s s' && term_eqb t t'.

Fixpoint find_value (l : list (nat * bool * term * expr)) (w : nat) (s : bool) (t : term) : option expr :=
  match l with
  | [] => None
  | (w', s', t', e) :: l' => if key_eqb w s t w' s' t' then Some e else find_value l' w s t
  end.

Fixpoint find_var (l : list (nat * bool * term * nat)) (w : nat) (s : bool) (t : term) : option nat :=
  match l with
  | [] => None
  | (w', s', t', i) :: l' => if key_eqb w s t w' s' t' then Some i else find_var l' w s t
  end.

Fixpoint mem_div (l : list (nat * bool * term)) (w : nat) (s : bool) (t : term) : bool :=
  match l with
  | [] => false
  | (w', s', t') :: l' => key_eqb w s t w' s' t' || mem_div l' w s t
  end.

Fixpoint find_range (l : list (nat * (Z * Z))) (i : nat) : option (Z * Z) :=
  match l with
  | [] => None
  | (j, r) :: l' => if Nat.eqb i j then Some r else find_range l' i
  end.

Definition with_cons (S : state) (c : constraint) : state :=
  mkS (svars S) (svalues S) (svarmap S) (sdivs S) (sranges S) (scons S ++ [c]) (sdisj S).
Definition with_disj (S : state) (d : constraint * constraint) : state :=
  mkS (svars S) (svalues S) (svarmap S) (sdivs S) (sranges S) (scons S) (sdisj S ++ [d]).
Definition with_var (S : state) (v : avar) : state :=
  mkS (svars S ++ [v]) (svalues S) (svarmap S) (sdivs S) (sranges S) (scons S) (sdisj S).
Definition with_value (S : state) (w : nat) (s : bool) (t : term) (e : expr) : state :=
  mkS (svars S) (svalues S ++ [(w, s, t, e)]) (svarmap S) (sdivs S) (sranges S) (scons S) (sdisj S).
Definition with_varmap (S : state) (w : nat) (s : bool) (t : term) (i : nat) : state :=
  mkS (svars S) (svalues S) (svarmap S ++ [(w, s, t, i)]) (sdivs S) (sranges S) (scons S) (sdisj S).
Definition with_div (S : state) (w : nat) (s : bool) (t : term) : state :=
  mkS (svars S) (svalues S) (svarmap S) (sdivs S ++ [(w, s, t)]) (sranges S) (scons S) (sdisj S).
Definition with_range (S : state) (i : nat) (r : Z * Z) : state :=
  mkS (svars S) (svalues S) (svarmap S) (sdivs S) (sranges S ++ [(i, r)]) (scons S) (sdisj S).

(* constrain, bound, before, equal, differ, outside, and the `either` of
   define_division. *)
Definition constrain (S : state) (e : expr) (extra : Z) : state := with_cons S (finish e extra).
Definition bound (S : state) (e : expr) (w : nat) (s : bool) : state :=
  constrain (constrain S (eneg e) (min_int w s)) e (- max_int w s).
Definition before (S : state) (l r : expr) (margin : Z) : state := constrain S (esub l r) margin.
Definition equal (S : state) (l r : expr) : state := before (before S l r 0) r l 0.
Definition differ (S : state) (l r : expr) : state := with_disj S (finish (esub l r) 1, finish (esub r l) 1).
Definition outside (S : state) (e : expr) (w : nat) (s : bool) : state :=
  with_disj S (finish e (1 - min_int w s), finish (eneg e) (max_int w s + 1)).
Definition either (S : state) (a : expr) (x : Z) (b : expr) (y : Z) : state :=
  with_disj S (finish a x, finish b y).

(* centered: the representative of a residue closest to zero. *)
Definition centered (w : nat) (c : Z) : Z :=
  let r := c mod modulus w in if modulus w / 2 <? r then r - modulus w else r.

Definition is_comparison (o : op) : bool :=
  match o with OEq | ONe | OLt | OLe | OGt | OGe => true | _ => false end.
Definition is_fits (o : op) : bool :=
  match o with AddFits | SubFits | MulFits => true | _ => false end.
Definition is_division (o : op) : bool :=
  match o with OQuot | ORem => true | _ => false end.

(* unbounded: the integer an operation computes on its operands' values, with
   no bound; none for a product of two non-constants. *)
Definition exact (o : op) (l r : expr) : option expr :=
  match o with
  | AddFits => Some (esub l (eneg r))
  | SubFits => Some (esub l r)
  | _ =>
      match eterms l, eterms r with
      | [], _ => Some (escale r (econst l))
      | _, [] => Some (escale l (econst r))
      | _, _ => None
      end
  end.

(* static_range: the least and greatest value an expression can take, from the
   range each of its variables is always in. *)
Fixpoint range_terms (S : state) (ts : list (nat * Z)) (lo hi : Z) : option (Z * Z) :=
  match ts with
  | [] => Some (lo, hi)
  | (v, c) :: ts' =>
      match nth_error (svars S) v with
      | Some x =>
          match arole x with
          | RValue =>
              let r := match find_range (sranges S) v with
                       | Some r => r
                       | None => (min_int (aw x) (asg x), max_int (aw x) (asg x))
                       end in
              range_terms S ts' (lo + c * (if 0 <=? c then fst r else snd r))
                                (hi + c * (if 0 <=? c then snd r else fst r))
          | RWrap => None
          end
      | None => None
      end
  end.

Definition static_range (S : state) (e : expr) : option (Z * Z) := range_terms S (eterms e) (econst e) (econst e).

(* product_always_fits, given the operands' values. *)
Definition product_fits (S : state) (l r : expr) (w : nat) (s : bool) : bool :=
  match static_range S l, static_range S r with
  | Some (l0, l1), Some (r0, r1) =>
      forallb (fun p => (min_int w s <=? p) && (p <=? max_int w s)) [l0 * r0; l0 * r1; l1 * r0; l1 * r1]
  | _, _ => false
  end.

(* The constant case of value(): the residue read as a value of the type. *)
Definition single (ms : list mono) (c : Z) : option (list term) :=
  match ms with
  | [(fs, k)] => if (k =? 1) && (c =? 0) then Some fs else None
  | _ => None
  end.

(* define_division's constraints: the remainder has the dividend's sign,
   and is smaller than the divisor, which is a literal or an unknown. *)
Definition same_sign (S : state) (dividend rem : expr) : state :=
  either (either S dividend 1 (eneg rem) 0) (eneg dividend) 0 rem 0.

Definition known_divisor (S3 : state) (s : bool) (c : Z) (q r : nat) (dividend : expr) : option state :=
  let rem := evar r in
  if (-1 <=? c) && (c <=? 1) then Some S3
  else
    let largest := Z.abs c - 1 in
    let S5 := constrain (equal S3 dividend (mkE [(q, c); (r, 1)] 0)) rem (- largest) in
    if negb s then Some S5 else Some (same_sign (constrain S5 (eneg rem) (- largest)) dividend rem).

Definition unknown_divisor (found : option (state * expr)) (s : bool) (dividend rem : expr) : option state :=
  match found with
  | None => None
  | Some (S4, d) =>
      let S5 := either S4 d 0 (esub rem d) 1 in
      if negb s then Some (constrain S5 (esub rem dividend) 0)
      else Some (same_sign (either S5 d 0 (esub (eneg rem) d) 1) dividend rem)
  end.

(* The loop of value() over the monomials: each one's variable, its
   coefficient centered. *)
Fixpoint each_var (var : state -> list term -> option (state * nat)) (w : nat) (S : state) (ms : list mono)
    : option (state * list (nat * Z)) :=
  match ms with
  | [] => Some (S, [])
  | (fs, c) :: ms' =>
      match var S fs with
      | Some (S1, i) =>
          match each_var var w S1 ms' with
          | Some (S2, ts) => Some (S2, (i, centered w c) :: ts)
          | None => None
          end
      | None => None
      end
  end.

Section Translation.

Variable sig : nat -> option (list ty * ty).
Variable body : nat -> option term.
Variable G : list ty.

Notation nfn := (nf sig body).

(* value, variable, define_conversion, define_division and truth. The fuel
   bounds the depth of their recursion, which the kernel bounds by its limits
   on terms. *)
Fixpoint value (n : nat) (S : state) (t : term) (w : nat) (s : bool) {struct n} : option (state * expr) :=
  match n with
  | O => None
  | Datatypes.S n' =>
      let u := nfn t in
      match find_value (svalues S) w s u with
      | Some e => Some (S, e)
      | None =>
          let p := read w s u in
          match fst p with
          | [] => let e := enone (wrap w s (snd p)) in Some (with_value S w s u e, e)
          | ms =>
              match single ms (snd p) with
              | Some fs =>
                  match variable n' S w s fs with
                  | Some (S1, i) => Some (with_value S1 w s u (evar i), evar i)
                  | None => None
                  end
              | None =>
                  match each_var (fun S fs => variable n' S w s fs) w S ms with
                  | Some (S1, ts) =>
                      let e0 := mkE ts (centered w (snd p)) in
                      let k := length (svars S1) in
                      let e := eplus e0 k (- modulus w) in
                      let S2 := bound (with_var S1 (mkV RWrap w s u e0)) e w s in
                      Some (with_value S2 w s u e, e)
                  | None => None
                  end
              end
          end
      end
  end

with variable (n : nat) (S : state) (w : nat) (s : bool) (fs : list term) {struct n} : option (state * nat) :=
  match n with
  | O => None
  | Datatypes.S n' =>
      let k := rfactors w s fs in
      match find_var (svarmap S) w s k with
      | Some i => Some (S, i)
      | None =>
          let i := length (svars S) in
          let S2 := bound (with_varmap (with_var S (mkV RValue w s k (enone 0))) w s k i) (evar i) w s in
          match fs, k with
          | [_], Prim OConvert w' s' [a] =>
              if Nat.eqb w' w && Bool.eqb s' s then
                match define_conversion n' S2 i w s a with Some S3 => Some (S3, i) | None => None end
              else Some (S2, i)
          | [_], Prim o w' s' [a; b] =>
              if is_division o && Nat.eqb w' w && Bool.eqb s' s then
                match define_division n' S2 w s a b with Some S3 => Some (S3, i) | None => None end
              else Some (S2, i)
          | _, _ => Some (S2, i)
          end
      end
  end

with define_conversion (n : nat) (S : state) (i : nat) (w : nat) (s : bool) (a : term) {struct n} : option state :=
  match n with
  | O => None
  | Datatypes.S n' =>
      match type_of sig G a with
      | Some (TInt fw fs) =>
          match value n' S a fw fs with
          | Some (S1, op) =>
              if (min_int w s <=? min_int fw fs) && (max_int fw fs <=? max_int w s) then
                Some (equal (with_range S1 i (min_int fw fs, max_int fw fs)) (evar i) op)
              else
                let j := length (svars S1) in
                Some (equal (with_var S1 (mkV RWrap w s (Prim OConvert w s [a]) op)) (evar i) (eplus op j (- modulus w)))
          | None => None
          end
      | _ => None
      end
  end

with define_division (n : nat) (S : state) (w : nat) (s : bool) (a b : term) {struct n} : option state :=
  match n with
  | O => None
  | Datatypes.S n' =>
      let qt := Prim OQuot w s [a; b] in
      if mem_div (sdivs S) w s qt then Some S
      else
        match variable n' (with_div S w s qt) w s [qt] with
        | None => None
        | Some (S1, q) =>
            match variable n' S1 w s [Prim ORem w s [a; b]] with
            | None => None
            | Some (S2, r) =>
                match value n' S2 a w s with
                | None => None
                | Some (S3, dividend) =>
                    match b with
                    | Lit w' s' v =>
                        if Nat.eqb w' w && Bool.eqb s' s then known_divisor S3 s (wrap w s v) q r dividend
                        else unknown_divisor (value n' S3 b w s) s dividend (evar r)
                    | _ => unknown_divisor (value n' S3 b w s) s dividend (evar r)
                    end
                end
            end
        end
  end

with truth (n : nat) (S : state) (cond : term) (holds : bool) {struct n} : option state :=
  match n with
  | O => None
  | Datatypes.S n' =>
      let fallback := fun S =>
        match value n' S cond 1 false with
        | Some (S1, e) => Some (equal S1 e (enone (if holds then 1 else 0)))
        | None => None
        end in
      match cond with
      | Lit _ _ v => if Bool.eqb (negb (v =? 0)) holds then Some S else Some (constrain S (enone 0) 1)
      | Prim ONot _ _ [a] => truth n' S a (negb holds)
      | Prim o w s [a; b] =>
          if is_comparison o then
            match value n' S a w s with
            | None => None
            | Some (S1, l) =>
                match value n' S1 b w s with
                | None => None
                | Some (S2, r) =>
                    Some (match o with
                          | OLt => if holds then before S2 l r 1 else before S2 r l 0
                          | OLe => if holds then before S2 l r 0 else before S2 r l 1
                          | OGt => if holds then before S2 r l 1 else before S2 l r 0
                          | OGe => if holds then before S2 r l 0 else before S2 l r 1
                          | OEq => if holds then equal S2 l r else differ S2 l r
                          | _ => if holds then differ S2 l r else equal S2 l r
                          end)
                end
            end
          else if is_fits o then
            match value n' S a w s with
            | None => None
            | Some (S1, l) =>
                match value n' S1 b w s with
                | None => None
                | Some (S2, r) =>
                    match exact o l r with
                    | Some x => Some (if holds then bound S2 x w s else outside S2 x w s)
                    | None =>
                        if product_fits S2 l r w s then Some (if holds then S2 else constrain S2 (enone 0) 1)
                        else fallback S2
                    end
                end
            end
          else fallback S
      | _ => fallback S
      end
  end.

(* fact and refuted. At u1, a side that normalizes to a literal makes the
   fact about what the other side's condition evaluates to. *)
Definition fact (n : nat) (S : state) (F : prop) : option state :=
  match F with
  | PEq (TInt w s) a b =>
      let general :=
        match value n S a w s with
        | Some (S1, l) => match value n S1 b w s with Some (S2, r) => Some (equal S2 l r) | None => None end
        | None => None
        end in
      if Nat.eqb w 1 && negb s then
        match nfn b with
        | Lit _ _ v => truth n S (nfn a) (negb (v =? 0))
        | _ => match nfn a with Lit _ _ v => truth n S (nfn b) (negb (v =? 0)) | _ => general end
        end
      else general
  | _ => None
  end.

Definition refuted (n : nat) (S : state) (P : prop) : option state :=
  match P with
  | PFalse => Some S
  | PEq (TInt w s) a b =>
      let general :=
        match value n S a w s with
        | Some (S1, l) => match value n S1 b w s with Some (S2, r) => Some (differ S2 l r) | None => None end
        | None => None
        end in
      if Nat.eqb w 1 && negb s then
        match nfn b with
        | Lit _ _ v => truth n S (nfn a) (v =? 0)
        | _ => match nfn a with Lit _ _ v => truth n S (nfn b) (v =? 0) | _ => general end
        end
      else general
  | _ => None
  end.

Fixpoint facts (n : nat) (S : state) (Fs : list prop) : option state :=
  match Fs with
  | [] => Some S
  | F :: Fs' => match fact n S F with Some S1 => facts n S1 Fs' | None => None end
  end.

Definition empty : state := mkS [] [] [] [] [] [] [].

Definition arithmetic_system (n : nat) (Fs : list prop) (P : prop) : option system :=
  match facts n empty Fs with
  | Some S1 =>
      match refuted n S1 P with
      | Some S2 => Some {| nvars := length (svars S2); constraints := scons S2; disjunctions := sdisj S2 |}
      | None => None
      end
  | None => None
  end.

End Translation.

(* The depth of the translation's recursion: three levels per level of a term,
   whose depth the kernel bounds by max_term_depth. *)
Definition max_linear_depth : nat := 4096.

(* Rule 9's check (check_under in kernel/src/check.cpp): the facts and the
   negated goal translated, and the certificate refuting the system. *)
Definition lin_ok (sig : nat -> option (list ty * ty)) (body : nat -> option term)
    (G : list ty) (Fs : list prop) (P : prop) (k : certificate) : bool :=
  match arithmetic_system sig body G max_linear_depth Fs P with
  | Some sys => check_certificate sys k
  | None => false
  end.

(* ======================================================================== *)
(* Soundness.                                                                *)

(* ---- expressions ---- *)

Definition eval_e (sg : nat -> Z) (e : expr) : Z := linear sg (eterms e) + econst e.

Lemma linear_app : forall sg a b, linear sg (a ++ b) = linear sg a + linear sg b.
Proof. intros sg a b. induction a as [|[v c] a IH]; unfold linear in *; cbn [fold_right app fst snd] in *; [ring|]. rewrite IH. ring. Qed.

Lemma linear_neg : forall sg ts, linear sg (map (fun vc => (fst vc, - snd vc)) ts) = - linear sg ts.
Proof. intros sg ts. induction ts as [|[v c] ts IH]; unfold linear in *; cbn [fold_right map fst snd] in *; [ring|]. rewrite IH. ring. Qed.

Lemma linear_scale : forall sg ts k, linear sg (map (fun vc => (fst vc, snd vc * k)) ts) = linear sg ts * k.
Proof. intros sg ts k. induction ts as [|[v c] ts IH]; unfold linear in *; cbn [fold_right map fst snd] in *; [ring|]. rewrite IH. ring. Qed.

Lemma eval_enone : forall sg c, eval_e sg (enone c) = c.
Proof. intros. unfold eval_e. simpl. ring. Qed.

Lemma eval_evar : forall sg i, eval_e sg (evar i) = sg i.
Proof. intros. unfold eval_e, evar, linear. cbn [fold_right eterms econst fst snd]. ring. Qed.

Lemma eval_eneg : forall sg e, eval_e sg (eneg e) = - eval_e sg e.
Proof. intros. unfold eval_e, eneg. simpl. rewrite linear_neg. ring. Qed.

Lemma eval_esub : forall sg a b, eval_e sg (esub a b) = eval_e sg a - eval_e sg b.
Proof.
  intros. unfold esub. unfold eval_e at 1. simpl. rewrite linear_app.
  change (linear sg (eterms (eneg b))) with (linear sg (map (fun vc => (fst vc, - snd vc)) (eterms b))).
  rewrite linear_neg. unfold eval_e. ring.
Qed.

Lemma eval_escale : forall sg e k, eval_e sg (escale e k) = eval_e sg e * k.
Proof. intros. unfold eval_e, escale. simpl. rewrite linear_scale. ring. Qed.

Lemma eval_eplus : forall sg e i c, eval_e sg (eplus e i c) = eval_e sg e + c * sg i.
Proof. intros. unfold eval_e, eplus. cbn [eterms econst]. rewrite linear_app. unfold linear at 2. cbn [fold_right fst snd]. ring. Qed.

Lemma sat_finish : forall sg e x, sat sg (finish e x) <-> eval_e sg e + x <= 0.
Proof. intros. unfold sat, Certificate.value, finish, eval_e. cbn [terms constant]. lia. Qed.

Definition bounded (n : nat) (e : expr) : Prop := Forall (fun vc => (fst vc < n)%nat) (eterms e).

Lemma bounded_mono : forall n n' e, (n <= n')%nat -> bounded n e -> bounded n' e.
Proof. intros n n' e L H. unfold bounded in *. eapply Forall_impl; [|exact H]. intros a. simpl. lia. Qed.

Lemma linear_ext : forall sg sg' ts, Forall (fun vc => sg (fst vc) = sg' (fst vc)) ts -> linear sg ts = linear sg' ts.
Proof.
  intros sg sg' ts H. induction H as [|[v c] ts Hv _ IH]; unfold linear in *; cbn [fold_right fst snd] in *;
    [reflexivity|]. rewrite Hv, IH. reflexivity.
Qed.

Lemma eval_ext : forall n sg sg' e, bounded n e -> (forall i, (i < n)%nat -> sg i = sg' i) -> eval_e sg e = eval_e sg' e.
Proof.
  intros n sg sg' e B H. unfold eval_e. f_equal. apply linear_ext.
  unfold bounded in B. eapply Forall_impl; [|exact B]. intros [v c] L. simpl in *. auto.
Qed.

Section Sound.

Variable sig : nat -> option (list ty * ty).
Hypothesis sig_ok : forall d ps R, sig d = Some (ps, R) -> ty_ok R = true.
Variable body : nat -> option term.
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
Hypothesis call_body : forall d ps R b args,
  sig d = Some (ps, R) -> body d = Some b -> Forall2 (dom Abs I) ps args ->
  i_call Abs I d args = eval Abs I (rev args) b.

Variable G : list ty.
Variable rho : list (val Abs).
Hypothesis Henv : env_ok Abs I G rho.

Notation tyof := (type_of sig G).
Notation fv := (fvr Abs I rho).
Notation nfn := (nf sig body).

Lemma typed_range : forall t w s, tyof t = Some (TInt w s) -> in_range w s (fv t).
Proof. intros t w s H. apply (typed_int sig Abs I proj_ok elem_ok call_ok G rho t w s Henv H). Qed.

Lemma typed_vz : forall t w s, tyof t = Some (TInt w s) -> eval Abs I rho t = VZ (fv t).
Proof. intros t w s H. apply (typed_int sig Abs I proj_ok elem_ok call_ok G rho t w s Henv H). Qed.

Lemma nf_typed : forall t T, tyof t = Some T -> tyof (nfn t) = Some T.
Proof.
  intros t T H. exact (proj1 (norm_sound sig sig_ok body body_typed Abs I proj_ok elem_ok call_ok call_body
                                max_depth G rho t T Henv H)).
Qed.

Lemma nf_val : forall t T, tyof t = Some T -> fv (nfn t) = fv t.
Proof.
  intros t T H. unfold fvr.
  rewrite (nf_sound sig sig_ok body body_typed Abs I proj_ok elem_ok call_ok call_body G rho t T Henv H).
  reflexivity.
Qed.

Lemma in_range_int_ok : forall w s z, int_ok w = true -> in_range w s z -> (1 <= w)%nat.
Proof. intros w s z H _. apply int_ok_width. exact H. Qed.

Lemma cong_in_range : forall w s x y, int_ok w = true -> in_range w s x -> in_range w s y ->
  cong (modulus w) x y -> x = y.
Proof. intros w s x y Hw Hx Hy C. apply (range_unique w s); auto. apply int_ok_width. exact Hw. Qed.

(* ---- the intended assignment ---- *)

Definition var_value (acc : list Z) (v : avar) : Z :=
  match arole v with
  | RValue => fv (aterm v)
  | RWrap => (eval_e (fun j => nth j acc 0) (aghost v) - fv (aterm v)) / modulus (aw v)
  end.

Definition build (vs : list avar) : list Z := fold_left (fun acc v => acc ++ [var_value acc v]) vs [].

Lemma build_app : forall vs v, build (vs ++ [v]) = build vs ++ [var_value (build vs) v].
Proof. intros. unfold build. rewrite fold_left_app. reflexivity. Qed.

Lemma build_length : forall vs, length (build vs) = length vs.
Proof.
  intros vs. induction vs as [|v vs IH] using rev_ind; [reflexivity|].
  rewrite build_app, !app_length, IH. reflexivity.
Qed.

Definition agrees (vs : list avar) (sg : nat -> Z) : Prop :=
  forall i, (i < length vs)%nat -> sg i = nth i (build vs) 0.

Lemma agrees_app : forall vs more sg, agrees (vs ++ more) sg -> agrees vs sg.
Proof.
  intros vs more sg H i L. rewrite H by (rewrite app_length; lia).
  induction more as [|v more IH] using rev_ind; [rewrite app_nil_r; reflexivity|].
  rewrite app_assoc, build_app, app_nth1 by (rewrite build_length, app_length; lia).
  apply IH. intros j Lj. rewrite (H j) by (rewrite !app_length in *; simpl in *; lia).
  rewrite app_assoc, build_app, app_nth1 by (rewrite build_length; lia). reflexivity.
Qed.

Lemma agrees_last : forall vs v sg, agrees (vs ++ [v]) sg -> sg (length vs) = var_value (build vs) v.
Proof.
  intros vs v sg H. rewrite H by (rewrite app_length; simpl; lia).
  rewrite build_app, app_nth2 by (rewrite build_length; lia). rewrite build_length, Nat.sub_diag. reflexivity.
Qed.

Lemma agrees_build : forall vs, agrees vs (fun i => nth i (build vs) 0).
Proof. intros vs i L. reflexivity. Qed.

Lemma agrees_nth : forall vs sg i v, agrees vs sg -> nth_error vs i = Some v ->
  sg i = var_value (build (firstn i vs)) v.
Proof.
  intros vs sg i v H N. assert (L : (i < length vs)%nat) by (apply nth_error_Some; congruence).
  apply nth_error_split in N as [l1 [l2 [E L1]]]. subst vs.
  assert (A : agrees (l1 ++ [v]) sg).
  { replace (l1 ++ v :: l2) with ((l1 ++ [v]) ++ l2) in H by (rewrite <- app_assoc; reflexivity).
    eapply agrees_app. exact H. }
  subst i. rewrite (agrees_last l1 v sg A). rewrite firstn_app, firstn_all, Nat.sub_diag. simpl.
  rewrite app_nil_r. reflexivity.
Qed.

Lemma build_prefix : forall vs more, exists tail, build (vs ++ more) = build vs ++ tail.
Proof.
  intros vs more. induction more as [|v more [tail IH]] using rev_ind.
  - exists []. rewrite !app_nil_r. reflexivity.
  - exists (tail ++ [var_value (build (vs ++ more)) v]). rewrite app_assoc, build_app, IH, app_assoc. reflexivity.
Qed.

Lemma build_firstn : forall vs i j, (j < i)%nat -> (i <= length vs)%nat ->
  nth j (build (firstn i vs)) 0 = nth j (build vs) 0.
Proof.
  intros vs i j Lj Li. rewrite <- (firstn_skipn i vs) at 2.
  destruct (build_prefix (firstn i vs) (skipn i vs)) as [tail E]. rewrite E.
  rewrite app_nth1; [reflexivity|]. rewrite build_length, firstn_length. lia.
Qed.

(* ---- the invariant ---- *)

Definition holds_all (S : state) (sg : nat -> Z) : Prop :=
  Forall (sat sg) (scons S) /\
  (forall c0 c1, In (c0, c1) (sdisj S) -> sat sg c0 \/ sat sg c1) /\
  Forall (fun r => fst (snd r) <= sg (fst r) <= snd (snd r)) (sranges S).

Definition var_ok (vs : list avar) (i : nat) (v : avar) : Prop :=
  int_ok (aw v) = true /\ tyof (aterm v) = Some (TInt (aw v) (asg v)) /\
  match arole v with
  | RValue => True
  | RWrap => bounded i (aghost v) /\
             cong (modulus (aw v)) (eval_e (fun j => nth j (build (firstn i vs)) 0) (aghost v)) (fv (aterm v))
  end.

Definition Good (S : state) : Prop :=
  (forall sg, agrees (svars S) sg -> holds_all S sg) /\
  (forall i v, nth_error (svars S) i = Some v -> var_ok (svars S) i v) /\
  (forall w s u e, In (w, s, u, e) (svalues S) ->
     int_ok w = true /\ tyof u = Some (TInt w s) /\ bounded (length (svars S)) e /\
     forall sg, agrees (svars S) sg -> eval_e sg e = fv u) /\
  (forall w s k i, In (w, s, k, i) (svarmap S) ->
     exists v, nth_error (svars S) i = Some v /\ arole v = RValue /\ aw v = w /\ asg v = s /\ aterm v = k).

Definition extends (S S' : state) : Prop := exists more, svars S' = svars S ++ more.

Lemma extends_refl : forall S, extends S S.
Proof. intros S. exists []. rewrite app_nil_r. reflexivity. Qed.

Lemma extends_trans : forall S1 S2 S3, extends S1 S2 -> extends S2 S3 -> extends S1 S3.
Proof. intros S1 S2 S3 [m1 E1] [m2 E2]. exists (m1 ++ m2). rewrite E2, E1, app_assoc. reflexivity. Qed.

Lemma extends_agrees : forall S S' sg, extends S S' -> agrees (svars S') sg -> agrees (svars S) sg.
Proof. intros S S' sg [more E] H. rewrite E in H. eapply agrees_app. exact H. Qed.

Lemma extends_length : forall S S', extends S S' -> (length (svars S) <= length (svars S'))%nat.
Proof. intros S S' [more E]. rewrite E, app_length. lia. Qed.

Lemma Good_empty : Good (mkS [] [] [] [] [] [] []).
Proof.
  split; [|split; [|split]]; cbn [svars scons sdisj sranges svalues svarmap].
  - intros sg _. split; [constructor|split; [intros c0 c1 []|constructor]].
  - intros i v N. destruct i; discriminate.
  - intros w s u e [].
  - intros w s k i [].
Qed.

(* The value of a variable under an agreeing assignment. *)
Lemma value_var : forall S sg i v, Good S -> agrees (svars S) sg -> nth_error (svars S) i = Some v ->
  arole v = RValue -> sg i = fv (aterm v).
Proof.
  intros S sg i v _ A N R. rewrite (agrees_nth _ _ _ _ A N). unfold var_value. rewrite R. reflexivity.
Qed.

Lemma wrap_var : forall S sg i v, Good S -> agrees (svars S) sg -> nth_error (svars S) i = Some v ->
  arole v = RWrap -> eval_e sg (aghost v) - modulus (aw v) * sg i = fv (aterm v).
Proof.
  intros S sg i v [_ [Hv _]] A N R.
  destruct (Hv i v N) as [Hw [_ Hr]]. rewrite R in Hr. destruct Hr as [B C].
  assert (L : (i < length (svars S))%nat) by (apply nth_error_Some; congruence).
  rewrite (agrees_nth _ _ _ _ A N). unfold var_value. rewrite R.
  assert (E : eval_e sg (aghost v) = eval_e (fun j => nth j (build (firstn i (svars S))) 0) (aghost v)).
  { apply (eval_ext i); [exact B|]. intros j Lj. rewrite (A j) by lia. symmetry. apply build_firstn; lia. }
  rewrite E. set (x := eval_e (fun j => nth j (build (firstn i (svars S))) 0) (aghost v)) in *.
  apply cong_iff in C. pose proof (modulus_pos (aw v)) as Hm.
  assert (D : (x - fv (aterm v)) mod modulus (aw v) = 0).
  { rewrite Zminus_mod, C, Z.sub_diag. reflexivity. }
  pose proof (Z.div_mod (x - fv (aterm v)) (modulus (aw v))) as Q. rewrite D in Q. lia.
Qed.

(* ---- preservation by each update ---- *)

Lemma Good_cons : forall S c, Good S -> (forall sg, agrees (svars S) sg -> sat sg c) -> Good (with_cons S c).
Proof.
  intros S c [H1 [H2 [H3 H4]]] Hc. split; [|split; [|split]]; cbn [with_cons svars svalues svarmap]; auto.
  intros sg A. destruct (H1 sg A) as [C [D R]]. split; [|split; auto]. cbn [scons]. apply Forall_app. split; auto.
Qed.

Lemma Good_disj : forall S c0 c1, Good S -> (forall sg, agrees (svars S) sg -> sat sg c0 \/ sat sg c1) ->
  Good (with_disj S (c0, c1)).
Proof.
  intros S c0 c1 [H1 [H2 [H3 H4]]] Hc. split; [|split; [|split]]; cbn [with_disj svars svalues svarmap]; auto.
  intros sg A. destruct (H1 sg A) as [C [D R]]. split; [auto|split; auto]. cbn [sdisj].
  intros d0 d1 Hin. apply in_app_or in Hin as [Hin|[Hin|[]]]; [auto|]. inversion Hin; subst. auto.
Qed.

Lemma Good_range : forall S i lo hi, Good S -> (forall sg, agrees (svars S) sg -> lo <= sg i <= hi) ->
  Good (with_range S i (lo, hi)).
Proof.
  intros S i lo hi [H1 [H2 [H3 H4]]] Hr. split; [|split; [|split]]; cbn [with_range svars svalues svarmap]; auto.
  intros sg A. destruct (H1 sg A) as [C [D R]]. split; [auto|split; auto]. cbn [sranges].
  apply Forall_app. split; auto.
Qed.

Lemma Good_div : forall S w s t, Good S -> Good (with_div S w s t).
Proof. intros S w s t [H1 [H2 [H3 H4]]]. split; [|split; [|split]]; cbn [with_div svars svalues svarmap]; auto. Qed.

Lemma Good_value : forall S w s u e, Good S -> int_ok w = true -> tyof u = Some (TInt w s) ->
  bounded (length (svars S)) e -> (forall sg, agrees (svars S) sg -> eval_e sg e = fv u) ->
  Good (with_value S w s u e).
Proof.
  intros S w s u e [H1 [H2 [H3 H4]]] Hw Hu B He. split; [|split; [|split]]; cbn [with_value svars svalues svarmap]; auto.
  intros w' s' u' e' Hin. apply in_app_or in Hin as [Hin|[Hin|[]]]; [auto|]. inversion Hin; subst. auto.
Qed.

Lemma Good_varmap : forall S w s k i v, Good S -> nth_error (svars S) i = Some v -> arole v = RValue ->
  aw v = w -> asg v = s -> aterm v = k -> Good (with_varmap S w s k i).
Proof.
  intros S w s k i v [H1 [H2 [H3 H4]]] N R Ew Es Ek. split; [|split; [|split]]; cbn [with_varmap svars svalues svarmap]; auto.
  intros w' s' k' i' Hin. apply in_app_or in Hin as [Hin|[Hin|[]]]; [auto|]. inversion Hin; subst. exists v. auto.
Qed.

Lemma Good_var : forall S v, Good S -> var_ok (svars S ++ [v]) (length (svars S)) v -> Good (with_var S v).
Proof.
  intros S v [H1 [H2 [H3 H4]]] Hv. split; [|split; [|split]]; cbn [with_var svars svalues svarmap].
  - intros sg A. apply H1. eapply agrees_app. exact A.
  - intros i x N. destruct (Nat.lt_ge_cases i (length (svars S))) as [L|L].
    + rewrite nth_error_app1 in N by exact L. destruct (H2 i x N) as [Hw [Ht Hr]]. split; [exact Hw|split; [exact Ht|]].
      destruct (arole x); [exact Logic.I|]. destruct Hr as [B C]. split; [exact B|].
      rewrite firstn_app, (proj2 (Nat.sub_0_le i (length (svars S)))) by lia. cbn [firstn]. rewrite app_nil_r.
      exact C.
    + rewrite nth_error_app2 in N by exact L. destruct (i - length (svars S))%nat eqn:D; [|destruct n; discriminate].
      inversion N; subst. replace i with (length (svars S)) by lia. exact Hv.
  - intros w s u e Hin. destruct (H3 w s u e Hin) as [Hw [Hu [B He]]]. split; [exact Hw|split; [exact Hu|split]].
    + eapply bounded_mono; [|exact B]. rewrite app_length. lia.
    + intros sg A. apply He. eapply agrees_app. exact A.
  - intros w s k i Hin. destruct (H4 w s k i Hin) as [x [N R]]. exists x. split; [|exact R].
    rewrite nth_error_app1; [exact N|]. apply nth_error_Some. congruence.
Qed.

Lemma extends_with_var : forall S v, extends S (with_var S v).
Proof. intros S v. exists [v]. reflexivity. Qed.

(* The constraints each update states. *)
Lemma Good_constrain : forall S e x, Good S -> (forall sg, agrees (svars S) sg -> eval_e sg e + x <= 0) ->
  Good (constrain S e x).
Proof. intros S e x HG H. apply Good_cons; [exact HG|]. intros sg A. apply sat_finish. auto. Qed.

Lemma Good_bound : forall S e w s, Good S -> (forall sg, agrees (svars S) sg -> in_range w s (eval_e sg e)) ->
  Good (bound S e w s).
Proof.
  intros S e w s HG H. unfold bound. apply Good_constrain.
  - apply Good_constrain; [exact HG|]. intros sg A. rewrite eval_eneg. specialize (H sg A). unfold in_range in H. lia.
  - intros sg A. specialize (H sg A). unfold in_range in H. lia.
Qed.

Lemma Good_before : forall S l r m, Good S -> (forall sg, agrees (svars S) sg -> eval_e sg l + m <= eval_e sg r) ->
  Good (before S l r m).
Proof. intros S l r m HG H. apply Good_constrain; [exact HG|]. intros sg A. rewrite eval_esub. specialize (H sg A). lia. Qed.

Lemma Good_equal : forall S l r, Good S -> (forall sg, agrees (svars S) sg -> eval_e sg l = eval_e sg r) ->
  Good (equal S l r).
Proof.
  intros S l r HG H. unfold equal. apply Good_before; [apply Good_before; [exact HG|]|].
  - intros sg A. rewrite (H sg A). lia.
  - intros sg A. rewrite (H sg A). lia.
Qed.

Lemma Good_differ : forall S l r, Good S -> (forall sg, agrees (svars S) sg -> eval_e sg l <> eval_e sg r) ->
  Good (differ S l r).
Proof.
  intros S l r HG H. apply Good_disj; [exact HG|]. intros sg A. rewrite !sat_finish, !eval_esub.
  specialize (H sg A). lia.
Qed.

Lemma Good_outside : forall S e w s, Good S -> (forall sg, agrees (svars S) sg -> ~ in_range w s (eval_e sg e)) ->
  Good (outside S e w s).
Proof.
  intros S e w s HG H. apply Good_disj; [exact HG|]. intros sg A. rewrite !sat_finish, eval_eneg.
  specialize (H sg A). unfold in_range in H. lia.
Qed.

Lemma Good_either : forall S a x b y, Good S ->
  (forall sg, agrees (svars S) sg -> eval_e sg a + x <= 0 \/ eval_e sg b + y <= 0) -> Good (either S a x b y).
Proof. intros S a x b y HG H. apply Good_disj; [exact HG|]. intros sg A. rewrite !sat_finish. auto. Qed.

(* Every update keeps the variables but with_var. *)
Lemma vars_with_cons : forall S c, svars (with_cons S c) = svars S. Proof. reflexivity. Qed.
Lemma vars_with_disj : forall S d, svars (with_disj S d) = svars S. Proof. reflexivity. Qed.
Lemma vars_with_value : forall S w s u e, svars (with_value S w s u e) = svars S. Proof. reflexivity. Qed.
Lemma vars_with_varmap : forall S w s k i, svars (with_varmap S w s k i) = svars S. Proof. reflexivity. Qed.
Lemma vars_with_div : forall S w s t, svars (with_div S w s t) = svars S. Proof. reflexivity. Qed.
Lemma vars_with_range : forall S i r, svars (with_range S i r) = svars S. Proof. reflexivity. Qed.
Lemma vars_constrain : forall S e x, svars (constrain S e x) = svars S. Proof. reflexivity. Qed.
Lemma vars_bound : forall S e w s, svars (bound S e w s) = svars S. Proof. reflexivity. Qed.
Lemma vars_before : forall S l r m, svars (before S l r m) = svars S. Proof. reflexivity. Qed.
Lemma vars_equal : forall S l r, svars (equal S l r) = svars S. Proof. reflexivity. Qed.
Lemma vars_differ : forall S l r, svars (differ S l r) = svars S. Proof. reflexivity. Qed.
Lemma vars_outside : forall S e w s, svars (outside S e w s) = svars S. Proof. reflexivity. Qed.
Lemma vars_either : forall S a x b y, svars (either S a x b y) = svars S. Proof. reflexivity. Qed.

(* ---- lookups ---- *)

Lemma key_eqb_true : forall w s t w' s' t', key_eqb w s t w' s' t' = true -> w = w' /\ s = s' /\ t = t'.
Proof.
  intros w s t w' s' t' K. unfold key_eqb in K. apply andb_true_iff in K as [K T]. apply andb_true_iff in K as [W B].
  apply Nat.eqb_eq in W. apply Bool.eqb_prop in B. apply term_eqb_eq in T. auto.
Qed.

Lemma find_value_in : forall l w s t e, find_value l w s t = Some e -> In (w, s, t, e) l.
Proof.
  induction l as [|[[[w' s'] t'] e'] l IH]; intros w s t e F; cbn [find_value] in F; [discriminate|].
  destruct (key_eqb w s t w' s' t') eqn:K.
  - injection F as <-. apply key_eqb_true in K as [-> [-> ->]]. left. reflexivity.
  - right. apply IH. exact F.
Qed.

Lemma find_var_in : forall l w s t i, find_var l w s t = Some i -> In (w, s, t, i) l.
Proof.
  induction l as [|[[[w' s'] t'] i'] l IH]; intros w s t i F; cbn [find_var] in F; [discriminate|].
  destruct (key_eqb w s t w' s' t') eqn:K.
  - injection F as <-. apply key_eqb_true in K as [-> [-> ->]]. left. reflexivity.
  - right. apply IH. exact F.
Qed.

Lemma find_range_in : forall l i r, find_range l i = Some r -> In (i, r) l.
Proof.
  induction l as [|[j r'] l IH]; intros i r F; cbn [find_range] in F; [discriminate|].
  destruct (Nat.eqb i j) eqn:E.
  - injection F as <-. apply Nat.eqb_eq in E. subst. left. reflexivity.
  - right. apply IH. exact F.
Qed.

Lemma extends_same : forall S S', svars S' = svars S -> extends S S'.
Proof. intros S S' E. exists []. rewrite app_nil_r. exact E. Qed.

Lemma nth_snoc : forall (l : list avar) x, nth_error (l ++ [x]) (length l) = Some x.
Proof. intros l x. rewrite nth_error_app2 by lia. rewrite Nat.sub_diag. reflexivity. Qed.

Lemma firstn_snoc : forall (l : list avar) x, firstn (length l) (l ++ [x]) = l.
Proof. intros l x. rewrite firstn_app, firstn_all, Nat.sub_diag. cbn [firstn]. apply app_nil_r. Qed.

(* ---- typing ---- *)

Lemma int_typed_ok : forall t w s, tyof t = Some (TInt w s) -> int_ok w = true.
Proof. intros t w s H. exact (type_of_ok sig sig_ok t G _ H). Qed.

Lemma prim2_typed : forall o w s a b T, tyof (Prim o w s [a; b]) = Some T ->
  int_ok w = true /\ tyof a = Some (TInt w s) /\ tyof b = Some (TInt w s).
Proof.
  intros o w s a b T H.
  destruct o; cbn [type_of ty_ok] in H;
    destruct (int_ok w) eqn:Hw; cbn [negb] in H; try discriminate;
    destruct (teq (tyof a) (TInt w s)) eqn:Ea; cbn [andb] in H; try discriminate;
    destruct (teq (tyof b) (TInt w s)) eqn:Eb; try discriminate;
    apply teq_true in Ea; apply teq_true in Eb; repeat split; assumption.
Qed.

Lemma div_typed : forall o w s a b, o = OQuot \/ o = ORem -> int_ok w = true ->
  tyof a = Some (TInt w s) -> tyof b = Some (TInt w s) -> tyof (Prim o w s [a; b]) = Some (TInt w s).
Proof.
  intros o w s a b [-> | ->] Hw Ha Hb; cbn [type_of ty_ok]; rewrite Hw; cbn [negb];
    rewrite Ha, Hb; cbn [teq]; rewrite ty_eqb_refl; reflexivity.
Qed.

Lemma convert_typed : forall w s a fw fs, int_ok w = true -> tyof a = Some (TInt fw fs) ->
  tyof (Prim OConvert w s [a]) = Some (TInt w s).
Proof. intros w s a fw fs Hw Ha. cbn [type_of ty_ok]. rewrite Hw. cbn [negb]. rewrite Ha. reflexivity. Qed.

Lemma not_typed : forall w s a T, tyof (Prim ONot w s [a]) = Some T -> tyof a = Some u1.
Proof.
  intros w s a T H. cbn [type_of ty_ok] in H. destruct (int_ok w); cbn [negb] in H; [|discriminate].
  destruct (ty_eqb (TInt w s) u1) eqn:E1; cbn [andb] in H; [|discriminate].
  destruct (teq (tyof a) (TInt w s)) eqn:E2; [|discriminate].
  apply ty_eqb_eq in E1. apply teq_true in E2. rewrite E2, E1. reflexivity.
Qed.

Lemma lit_typed : forall w' s' v w s, tyof (Lit w' s' v) = Some (TInt w s) -> w' = w /\ s' = s /\ in_range w s v.
Proof.
  intros w' s' v w s H. cbn [type_of] in H.
  destruct (ty_ok (TInt w' s') && in_rangeb w' s' v) eqn:E; [|discriminate].
  injection H as <- <-. apply andb_true_iff in E as [_ R]. apply in_rangeb_iff in R. auto.
Qed.

Lemma u1_range : forall t, tyof t = Some u1 -> fv t = 0 \/ fv t = 1.
Proof.
  intros t H. pose proof (typed_range t 1 false H) as R. unfold in_range, min_int, max_int in R. cbn in R. lia.
Qed.

(* ---- arithmetic ---- *)

Lemma centered_cong : forall w c, cong (modulus w) (centered w c) c.
Proof.
  intros w c. pose proof (modulus_pos w) as Hm. unfold centered.
  destruct (modulus w / 2 <? c mod modulus w).
  - constructor. rewrite Zminus_mod, Z_mod_same_full, Z.sub_0_r, !Z.mod_mod by lia. reflexivity.
  - apply (cong_mod (modulus w) Hm).
Qed.

Lemma corners : forall l0 l1 r0 r1 x y lo hi, l0 <= x <= l1 -> r0 <= y <= r1 ->
  lo <= l0 * r0 <= hi -> lo <= l0 * r1 <= hi -> lo <= l1 * r0 <= hi -> lo <= l1 * r1 <= hi ->
  lo <= x * y <= hi.
Proof.
  intros l0 l1 r0 r1 x y lo hi Hx Hy A B C D.
  assert (E0 : lo <= l0 * y <= hi) by (destruct (Z_le_gt_dec 0 l0); nia).
  assert (E1 : lo <= l1 * y <= hi) by (destruct (Z_le_gt_dec 0 l1); nia).
  destruct (Z_le_gt_dec 0 y); nia.
Qed.

Lemma quot_in_range : forall w s x c, (1 <= w)%nat -> in_range w s x -> in_range w s c -> 2 <= Z.abs c ->
  in_range w s (Z.quot x c).
Proof.
  intros w s x c Hw Rx Rc Hc.
  assert (Hc0 : c <> 0) by lia.
  pose proof (Z.quot_abs x c Hc0) as QA. rewrite Z.quot_div_nonneg in QA by lia.
  pose proof (Z.mul_div_le (Z.abs x) (Z.abs c) ltac:(lia)) as M.
  pose proof (Z.div_pos (Z.abs x) (Z.abs c) ltac:(lia) ltac:(lia)) as P.
  assert (Q2 : 2 * Z.abs (Z.quot x c) <= Z.abs x) by nia.
  pose proof (pow_split w Hw) as Hs.
  assert (Hp : 0 < 2 ^ (Z.of_nat w - 1)) by (apply Z.pow_pos_nonneg; lia).
  unfold in_range, min_int, max_int in *. destruct s.
  - lia.
  - assert (0 <= Z.quot x c) by (rewrite Z.quot_div_nonneg by lia; apply Z.div_pos; lia). lia.
Qed.

Lemma rem_sign : forall x y,
  (0 <= x -> 0 <= (if y =? 0 then x else Z.rem x y)) /\ (x <= 0 -> (if y =? 0 then x else Z.rem x y) <= 0).
Proof.
  intros x y. destruct (y =? 0) eqn:E; [lia|]. apply Z.eqb_neq in E.
  split; intros; [apply Z.rem_nonneg|apply Z.rem_nonpos]; assumption.
Qed.

Lemma exact_sound : forall o l r x sg, exact o l r = Some x ->
  eval_e sg x = match o with
                | AddFits => eval_e sg l + eval_e sg r
                | SubFits => eval_e sg l - eval_e sg r
                | _ => eval_e sg l * eval_e sg r
                end.
Proof.
  intros o l r x sg H.
  assert (Gen : match eterms l, eterms r with
                | [], _ => Some (escale r (econst l))
                | _, [] => Some (escale l (econst r))
                | _, _ => None
                end = Some x -> eval_e sg x = eval_e sg l * eval_e sg r).
  { intros E. destruct (eterms l) as [|t0 l0] eqn:El.
    - injection E as <-. rewrite eval_escale. unfold eval_e at 2. rewrite El. unfold linear. cbn [fold_right]. ring.
    - destruct (eterms r) as [|t1 r0] eqn:Er; [|discriminate].
      injection E as <-. rewrite eval_escale. unfold eval_e at 3. rewrite Er. unfold linear. cbn [fold_right]. ring. }
  destruct o; cbn [exact] in H; try (exact (Gen H)).
  - injection H as <-. rewrite eval_esub, eval_eneg. ring.
  - injection H as <-. rewrite eval_esub. ring.
Qed.

(* ---- static ranges ---- *)

Lemma range_terms_sound : forall S ts lo hi lo' hi', Good S -> range_terms S ts lo hi = Some (lo', hi') ->
  forall sg, agrees (svars S) sg -> forall z, lo <= z <= hi -> lo' <= z + linear sg ts <= hi'.
Proof.
  intros S ts. induction ts as [|[v c] ts IH]; intros lo hi lo' hi' HG E sg A z Hz; cbn [range_terms] in E.
  - injection E as <- <-. unfold linear. cbn [fold_right]. lia.
  - destruct (nth_error (svars S) v) as [x|] eqn:N; [|discriminate].
    destruct (arole x) eqn:R; [|discriminate].
    set (rr := match find_range (sranges S) v with
               | Some r => r
               | None => (min_int (aw x) (asg x), max_int (aw x) (asg x))
               end) in E.
    assert (Hr : fst rr <= sg v <= snd rr).
    { subst rr. destruct (find_range (sranges S) v) as [[r0 r1]|] eqn:F.
      - apply find_range_in in F. destruct HG as [H1 _]. destruct (H1 sg A) as [_ [_ H3]].
        rewrite Forall_forall in H3. exact (H3 _ F).
      - cbn [fst snd]. rewrite (value_var S sg v x HG A N R).
        destruct HG as [_ [H2 _]]. destruct (H2 v x N) as [_ [Ht _]]. exact (typed_range _ _ _ Ht). }
    assert (Hn : lo + c * (if 0 <=? c then fst rr else snd rr) <= z + c * sg v <=
                 hi + c * (if 0 <=? c then snd rr else fst rr)).
    { destruct (0 <=? c) eqn:C; [apply Z.leb_le in C|apply Z.leb_gt in C]; nia. }
    pose proof (IH _ _ lo' hi' HG E sg A _ Hn) as K.
    unfold linear in *. cbn [fold_right fst snd]. lia.
Qed.

Lemma static_range_sound : forall S e lo hi, Good S -> static_range S e = Some (lo, hi) ->
  forall sg, agrees (svars S) sg -> lo <= eval_e sg e <= hi.
Proof.
  intros S e lo hi HG E sg A. unfold static_range in E.
  pose proof (range_terms_sound _ _ _ _ _ _ HG E sg A (econst e) ltac:(lia)) as K. unfold eval_e. lia.
Qed.

Lemma product_fits_sound : forall S l r w s, Good S -> product_fits S l r w s = true ->
  forall sg, agrees (svars S) sg -> in_range w s (eval_e sg l * eval_e sg r).
Proof.
  intros S l r w s HG E sg A. unfold product_fits in E.
  destruct (static_range S l) as [[l0 l1]|] eqn:L; [|discriminate].
  destruct (static_range S r) as [[r0 r1]|] eqn:R; [|discriminate].
  pose proof (static_range_sound _ _ _ _ HG L sg A) as Bl.
  pose proof (static_range_sound _ _ _ _ HG R sg A) as Br.
  rewrite forallb_forall in E.
  assert (K : forall p, In p [l0 * r0; l0 * r1; l1 * r0; l1 * r1] -> min_int w s <= p <= max_int w s).
  { intros p Hp. specialize (E p Hp). apply andb_true_iff in E as [E1 E2]. apply Z.leb_le in E1, E2. lia. }
  unfold in_range. apply (corners l0 l1 r0 r1); auto; apply K; cbn; tauto.
Qed.

(* ---- a fresh value variable ---- *)

Lemma fresh_var : forall S w s k, Good S -> int_ok w = true -> tyof k = Some (TInt w s) ->
  let i := length (svars S) in
  let S2 := bound (with_varmap (with_var S (mkV RValue w s k (enone 0))) w s k i) (evar i) w s in
  Good S2 /\ extends S S2 /\ (i < length (svars S2))%nat /\ (forall sg, agrees (svars S2) sg -> sg i = fv k).
Proof.
  intros S w s k HG Hw Hk i S2.
  assert (G1 : Good (with_var S (mkV RValue w s k (enone 0)))).
  { apply Good_var; [exact HG|]. unfold var_ok. cbn [aw asg aterm arole]. auto. }
  assert (V1 : forall sg, agrees (svars S ++ [mkV RValue w s k (enone 0)]) sg -> sg i = fv k).
  { intros sg A. exact (value_var _ sg i _ G1 A (nth_snoc _ _) eq_refl). }
  assert (G2 : Good (with_varmap (with_var S (mkV RValue w s k (enone 0))) w s k i)).
  { eapply Good_varmap; [exact G1|apply nth_snoc|reflexivity..]. }
  split; [|split; [|split]].
  - apply Good_bound; [exact G2|]. intros sg A. rewrite eval_evar, (V1 sg A). apply typed_range. exact Hk.
  - exists [mkV RValue w s k (enone 0)]. reflexivity.
  - cbn [S2 bound constrain with_cons with_varmap with_var svars]. rewrite app_length. cbn. lia.
  - exact V1.
Qed.

(* ---- the divisor's constraints ---- *)

Lemma same_sign_sound : forall S dv r a b, Good S ->
  (forall sg, agrees (svars S) sg -> eval_e sg dv = fv a /\ sg r = (if fv b =? 0 then fv a else Z.rem (fv a) (fv b))) ->
  Good (same_sign S dv (evar r)).
Proof.
  intros S dv r a b HG Sem. unfold same_sign.
  destruct (rem_sign (fv a) (fv b)) as [Pn Np].
  apply Good_either; [apply Good_either; [exact HG|]|].
  - intros sg A. destruct (Sem sg A) as [D R]. rewrite eval_eneg, eval_evar, D, R. lia.
  - intros sg A. destruct (Sem sg A) as [D R]. rewrite eval_eneg, eval_evar, D, R. lia.
Qed.

Lemma unknown_sound : forall S4 d s w dv r S5 a b,
  Good S4 -> tyof a = Some (TInt w s) -> tyof b = Some (TInt w s) ->
  (forall sg, agrees (svars S4) sg -> eval_e sg d = fv b /\ eval_e sg dv = fv a /\
     sg r = (if fv b =? 0 then fv a else Z.rem (fv a) (fv b))) ->
  unknown_divisor (Some (S4, d)) s dv (evar r) = Some S5 -> Good S5 /\ extends S4 S5.
Proof.
  intros S4 d s w dv r S5 a b HG Ta Tb Sem E. unfold unknown_divisor in E.
  pose proof (typed_range _ _ _ Ta) as Ra. pose proof (typed_range _ _ _ Tb) as Rb.
  assert (G5 : Good (either S4 d 0 (esub (evar r) d) 1)).
  { apply Good_either; [exact HG|]. intros sg A. destruct (Sem sg A) as [D [_ R]].
    rewrite eval_esub, eval_evar, D, R.
    destruct (fv b =? 0) eqn:Z0; [left; apply Z.eqb_eq in Z0; lia|]. apply Z.eqb_neq in Z0.
    pose proof (Z.rem_bound_abs (fv a) (fv b) Z0). lia. }
  destruct s; cbn [negb] in E; injection E as <-.
  - split; [|apply extends_same; reflexivity].
    apply same_sign_sound with (a := a) (b := b); [apply Good_either; [exact G5|]|].
    + intros sg A. destruct (Sem sg A) as [D [_ R]]. rewrite eval_esub, eval_eneg, eval_evar, D, R.
      destruct (fv b =? 0) eqn:Z0; [left; apply Z.eqb_eq in Z0; lia|]. apply Z.eqb_neq in Z0.
      pose proof (Z.rem_bound_abs (fv a) (fv b) Z0). lia.
    + intros sg A. destruct (Sem sg A) as [_ [D R]]. auto.
  - split; [|apply extends_same; reflexivity].
    apply Good_constrain; [exact G5|]. intros sg A. destruct (Sem sg A) as [_ [D R]].
    rewrite eval_esub, eval_evar, D, R. unfold in_range, min_int in Ra, Rb.
    destruct (fv b =? 0) eqn:Z0; [lia|]. apply Z.eqb_neq in Z0.
    pose proof (Z.rem_le (fv a) (fv b) ltac:(lia) ltac:(lia)). lia.
Qed.

Lemma known_sound : forall S3 s w c q r dv a S5,
  Good S3 -> tyof a = Some (TInt w s) -> in_range w s c ->
  (forall sg, agrees (svars S3) sg -> eval_e sg dv = fv a /\
     sg q = (if c =? 0 then 0 else wrap w s (Z.quot (fv a) c)) /\
     sg r = (if c =? 0 then fv a else Z.rem (fv a) c)) ->
  known_divisor S3 s c q r dv = Some S5 -> Good S5 /\ extends S3 S5.
Proof.
  intros S3 s w c q r dv a S5 HG Ta Rc Sem E. unfold known_divisor in E.
  destruct ((-1 <=? c) && (c <=? 1)) eqn:Ec.
  { injection E as <-. split; [exact HG|apply extends_refl]. }
  assert (C2 : 2 <= Z.abs c).
  { destruct (Z_le_gt_dec (-1) c); destruct (Z_le_gt_dec c 1); lia. }
  assert (Hc : (c =? 0) = false) by (apply Z.eqb_neq; lia).
  pose proof (typed_range _ _ _ Ta) as Ra. pose proof (int_typed_ok _ _ _ Ta) as Hw.
  assert (Qr : in_range w s (Z.quot (fv a) c)) by (apply quot_in_range; auto; apply int_ok_width; exact Hw).
  assert (Sem' : forall sg, agrees (svars S3) sg ->
            eval_e sg dv = fv a /\ sg q = Z.quot (fv a) c /\ sg r = Z.rem (fv a) c).
  { intros sg A. destruct (Sem sg A) as [D [Q R]]. rewrite Hc in Q, R.
    rewrite wrap_small in Q by (auto; apply int_ok_width; exact Hw). auto. }
  pose proof (Z.rem_bound_abs (fv a) c ltac:(lia)) as Rb.
  assert (G5 : Good (constrain (equal S3 dv (mkE [(q, c); (r, 1)] 0)) (evar r) (- (Z.abs c - 1)))).
  { apply Good_constrain; [apply Good_equal; [exact HG|]|].
    - intros sg A. destruct (Sem' sg A) as [D [Q R]]. rewrite D. unfold eval_e, linear. cbn [eterms econst fold_right fst snd].
      rewrite Q, R. pose proof (Z.quot_rem' (fv a) c). lia.
    - intros sg A. destruct (Sem' sg A) as [_ [_ R]]. rewrite eval_evar, R. lia. }
  destruct s; cbn [negb] in E; injection E as <-; (split; [|apply extends_same; reflexivity]).
  - apply same_sign_sound with (a := a) (b := Lit w true c); [apply Good_constrain; [exact G5|]|].
    + intros sg A. destruct (Sem' sg A) as [_ [_ R]]. rewrite eval_eneg, eval_evar, R. lia.
    + intros sg A. destruct (Sem' sg A) as [D [_ R]]. split; [exact D|]. cbn [fvr eval zv]. rewrite Hc. exact R.
  - exact G5.
Qed.

(* ---- what each function keeps ---- *)

Definition value_ok (n : nat) : Prop :=
  forall S t w s S' e, Good S -> tyof t = Some (TInt w s) -> value sig body G n S t w s = Some (S', e) ->
    Good S' /\ extends S S' /\ bounded (length (svars S')) e /\
    forall sg, agrees (svars S') sg -> eval_e sg e = fv t.

Definition variable_ok (n : nat) : Prop :=
  forall S w s fs S' i, Good S -> int_ok w = true -> Forall (fun f => tyof f = Some (TInt w s)) fs ->
    variable sig body G n S w s fs = Some (S', i) ->
    Good S' /\ extends S S' /\ (i < length (svars S'))%nat /\
    forall sg, agrees (svars S') sg -> sg i = fv (rfactors w s fs).

Definition conv_ok (n : nat) : Prop :=
  forall S i w s a S', Good S -> int_ok w = true -> (i < length (svars S))%nat ->
    (forall sg, agrees (svars S) sg -> sg i = fv (Prim OConvert w s [a])) ->
    define_conversion sig body G n S i w s a = Some S' -> Good S' /\ extends S S'.

Definition div_ok (n : nat) : Prop :=
  forall S w s a b S', Good S -> tyof a = Some (TInt w s) -> tyof b = Some (TInt w s) ->
    define_division sig body G n S w s a b = Some S' -> Good S' /\ extends S S'.

Definition truth_ok (n : nat) : Prop :=
  forall S c (h : bool) S', Good S -> tyof c = Some u1 -> fv c = (if h then 1 else 0) ->
    truth sig body G n S c h = Some S' -> Good S' /\ extends S S'.

Lemma each_var_sound : forall n w s, variable_ok n -> int_ok w = true ->
  forall ms S S' ts, Good S -> Forall (fun kc => Forall (fun f => tyof f = Some (TInt w s)) (fst kc)) ms ->
  each_var (fun S fs => variable sig body G n S w s fs) w S ms = Some (S', ts) ->
  Good S' /\ extends S S' /\ Forall (fun vc => (fst vc < length (svars S'))%nat) ts /\
  forall sg, agrees (svars S') sg -> cong (modulus w) (linear sg ts) (lval fv ms).
Proof.
  intros n w s Hvar Hw. induction ms as [|[fs c] ms IH]; intros S S' ts HG Hms E; cbn [each_var] in E.
  - injection E as <- <-. split; [exact HG|split; [apply extends_refl|split; [constructor|]]].
    intros sg _. unfold linear. cbn [fold_right lval]. reflexivity.
  - inversion Hms as [|? ? Hfs Hms']; subst. cbn [fst] in Hfs.
    destruct (variable sig body G n S w s fs) as [[S1 i]|] eqn:Ev; [|discriminate].
    destruct (each_var (fun S fs => variable sig body G n S w s fs) w S1 ms) as [[S2 ts']|] eqn:Ee; [|discriminate].
    injection E as <- <-.
    destruct (Hvar S w s fs S1 i HG Hw Hfs Ev) as [G1 [X1 [L1 V1]]].
    destruct (IH S1 S2 ts' G1 Hms' Ee) as [G2 [X2 [B2 V2]]].
    split; [exact G2|split; [eapply extends_trans; eauto|split]].
    + constructor; [|exact B2]. cbn [fst]. pose proof (extends_length _ _ X2). lia.
    + intros sg A. specialize (V2 sg A). unfold linear in *. cbn [fold_right lval fst snd].
      rewrite (V1 sg (extends_agrees _ _ _ X2 A)), V2, centered_cong, rfactors_val. reflexivity.
Qed.

Ltac zbool := repeat match goal with
  | H : (_ <? _) = true |- _ => apply Z.ltb_lt in H
  | H : (_ <? _) = false |- _ => apply Z.ltb_ge in H
  | H : (_ <=? _) = true |- _ => apply Z.leb_le in H
  | H : (_ <=? _) = false |- _ => apply Z.leb_gt in H
  | H : (_ =? _) = true |- _ => apply Z.eqb_eq in H
  | H : (_ =? _) = false |- _ => apply Z.eqb_neq in H
  | H : negb _ = true |- _ => apply negb_true_iff in H
  | H : negb _ = false |- _ => apply negb_false_iff in H
  end.

Ltac refold E :=
  fold (value sig body G) (variable sig body G) (define_conversion sig body G) (define_division sig body G)
       (truth sig body G) in E.

Definition all_ok (n : nat) : Prop := value_ok n /\ variable_ok n /\ conv_ok n /\ div_ok n /\ truth_ok n.

Lemma value_step : forall n, all_ok n -> value_ok (Datatypes.S n).
Proof.
  intros n [IHv [IHx [IHc [IHd IHt]]]].
  intros S t w s S' e HG Ht E. cbn [value] in E. refold E.
  pose proof (int_typed_ok _ _ _ Ht) as Hw.
  pose proof (nf_typed _ _ Ht) as Hu. pose proof (nf_val _ _ Ht) as Eu.
  revert Hu Eu E. generalize (nf sig body t) as u. intros u Hu Eu E.
  destruct (find_value (svalues S) w s u) as [e0|] eqn:Fv.
  + injection E as <- <-. pose proof HG as [_ [_ [H3 _]]].
    destruct (H3 _ _ _ _ (find_value_in _ _ _ _ _ Fv)) as [_ [_ [B Ev]]].
    split; [exact HG|split; [apply extends_refl|split; [exact B|]]].
    intros sg A. rewrite (Ev sg A). exact Eu.
  + pose proof (read_val Abs I rho w s u) as Rp. pose proof (read_atoms sig G w s u Hu) as Ap.
    revert Rp Ap E. generalize (read w s u) as p. intros p Rp Ap E.
    assert (Same : forall z, tyof z = Some (TInt w s) -> cong (modulus w) (fv z) (pval fv p) -> fv z = fv u).
    { intros z Tz Cz. apply (range_unique w s); [apply int_ok_width; exact Hw|apply typed_range; exact Tz|
        apply typed_range; exact Hu|]. rewrite Cz, Rp. reflexivity. }
    assert (Fin : forall S1 e1, Good S1 -> extends S S1 -> bounded (length (svars S1)) e1 ->
              (forall sg, agrees (svars S1) sg -> eval_e sg e1 = fv u) ->
              Good (with_value S1 w s u e1) /\ extends S (with_value S1 w s u e1) /\
              bounded (length (svars (with_value S1 w s u e1))) e1 /\
              forall sg, agrees (svars (with_value S1 w s u e1)) sg -> eval_e sg e1 = fv t).
    { intros S1 e1 G1 X1 B1 V1. split; [apply Good_value; assumption|split; [|split; [exact B1|]]].
      - eapply extends_trans; [exact X1|]. apply extends_same. reflexivity.
      - intros sg A. rewrite (V1 sg A). exact Eu. }
    destruct (fst p) as [|m ms] eqn:Ep; cbn beta iota zeta in E.
    * (* a constant *)
      injection E as <- <-. apply Fin; [exact HG|apply extends_refl|constructor|].
      intros sg _. rewrite eval_enone.
      apply (range_unique w s); [apply int_ok_width; exact Hw|apply wrap_in_range, int_ok_width; exact Hw|
        apply typed_range; exact Hu|].
      rewrite wrap_cong, Rp. unfold pval. rewrite Ep. cbn [lval]. apply eq_cong. lia.
    * unfold atoms in Ap. rewrite Ep in Ap.
      destruct (single (m :: ms) (snd p)) as [fs|] eqn:Es.
      -- (* a single variable *)
         unfold single in Es. destruct m as [fs' k]. destruct ms as [|m2 ms2]; [|discriminate].
         destruct ((k =? 1) && (snd p =? 0)) eqn:K; [|discriminate]. injection Es as <-.
         apply andb_true_iff in K as [K1 K2]. apply Z.eqb_eq in K1, K2.
         inversion Ap as [|? ? Hfs _]; subst. cbn [fst] in Hfs. cbn beta iota in E.
         destruct (variable sig body G n S w s fs') as [[S1 i]|] eqn:Ex; [|discriminate].
         injection E as <- <-.
         destruct (IHx _ _ _ _ _ _ HG Hw Hfs Ex) as [G1 [X1 [L1 V1]]].
         apply Fin; [exact G1|exact X1|repeat constructor; exact L1|].
         intros sg A. rewrite eval_evar, (V1 sg A). apply Same; [apply rfactors_typed; assumption|].
         rewrite rfactors_val. unfold pval. rewrite Ep, K2. cbn [lval fst snd]. apply eq_cong. lia.
      -- (* a sum, wrapped *)
         cbn beta iota in E.
         destruct (each_var (fun S fs => variable sig body G n S w s fs) w S (m :: ms)) as [[S1 ts]|] eqn:Ee;
           [|discriminate].
         injection E as <- <-.
         destruct (each_var_sound n w s IHx Hw _ _ _ _ HG Ap Ee) as [G1 [X1 [B1 V1]]].
         set (e0 := mkE ts (centered w (snd p))).
         set (v := mkV RWrap w s u e0).
         assert (Gv : Good (with_var S1 v)).
         { apply Good_var; [exact G1|]. unfold var_ok. cbn [v aw asg aterm arole aghost].
           split; [exact Hw|split; [exact Hu|split; [exact B1|]]].
           rewrite firstn_snoc. unfold e0, eval_e. cbn [eterms econst].
           rewrite (V1 _ (agrees_build _)), centered_cong, Rp. unfold pval. rewrite Ep. reflexivity. }
         assert (Ve : forall sg, agrees (svars S1 ++ [v]) sg -> eval_e sg (eplus e0 (length (svars S1)) (- modulus w)) = fv u).
         { intros sg A. rewrite eval_eplus.
           pose proof (wrap_var (with_var S1 v) sg _ v Gv A (nth_snoc _ _) eq_refl) as W.
           cbn [v aghost aw aterm] in W. rewrite <- W. lia. }
         apply Fin.
         ++ apply Good_bound; [exact Gv|]. intros sg A. rewrite (Ve sg A). apply typed_range. exact Hu.
         ++ eapply extends_trans; [exact X1|]. exists [v]. reflexivity.
         ++ unfold bounded. cbn [eplus eterms bound constrain with_cons with_var svars].
            rewrite app_length. cbn [length]. apply Forall_app. split.
            ** eapply Forall_impl; [|exact B1]. intros [j cj]. cbn. lia.
            ** repeat constructor. cbn. lia.
         ++ exact Ve.
Qed.

Lemma variable_step : forall n, all_ok n -> variable_ok (Datatypes.S n).
Proof.
  intros n [IHv [IHx [IHc [IHd IHt]]]].
  intros S w s fs S' i HG Hw Hfs E. cbn [variable] in E. refold E.
  pose proof (rfactors_typed sig G w s Hw fs Hfs) as Hk.
  destruct (find_var (svarmap S) w s (rfactors w s fs)) as [j|] eqn:Fv.
  + injection E as <- <-. pose proof HG as [_ [_ [_ H4]]].
    destruct (H4 _ _ _ _ (find_var_in _ _ _ _ _ Fv)) as [v [N [R [_ [_ Ek]]]]].
    split; [exact HG|split; [apply extends_refl|split]].
    * apply nth_error_Some. congruence.
    * intros sg A. rewrite (value_var S sg j v HG A N R), Ek. reflexivity.
  + destruct (fresh_var S w s (rfactors w s fs) HG Hw Hk) as [G2 [X2 [L2 V2]]].
    cbv zeta in G2, X2, L2, V2.
    set (S2 := bound (with_varmap (with_var S (mkV RValue w s (rfactors w s fs) (enone 0))) w s
                       (rfactors w s fs) (length (svars S))) (evar (length (svars S))) w s) in *.
    set (i0 := length (svars S)) in *.
    assert (Fin : forall S3, Good S3 -> extends S2 S3 -> Good S3 /\ extends S S3 /\
              (i0 < length (svars S3))%nat /\ forall sg, agrees (svars S3) sg -> sg i0 = fv (rfactors w s fs)).
    { intros S3 G3 X3. split; [exact G3|split; [eapply extends_trans; eauto|split]].
      - pose proof (extends_length _ _ X3). lia.
      - intros sg A. apply V2. eapply extends_agrees; eauto. }
    clearbody S2 i0.
    destruct fs as [|f [|f2 fs2]];
      [cbn beta iota in E; injection E as <- <-; apply Fin; [exact G2|apply extends_refl]| |
       cbn beta iota in E; injection E as <- <-; apply Fin; [exact G2|apply extends_refl]].
    cbn [rfactors rmul] in *.
    destruct f as [vi|w' s' v|dc ts|o w' s' ts|D j x|D x y]; cbn beta iota in E;
      try (injection E as <- <-; apply Fin; [exact G2|apply extends_refl]).
    destruct ts as [|a [|b [|c ts]]]; destruct o; cbn [is_division andb] in E;
      try (injection E as <- <-; apply Fin; [exact G2|apply extends_refl]).
    * (* a conversion *)
      destruct (Nat.eqb w' w && Bool.eqb s' s) eqn:Ews;
        [|injection E as <- <-; apply Fin; [exact G2|apply extends_refl]].
      apply andb_true_iff in Ews as [E1 E2]. apply Nat.eqb_eq in E1. apply Bool.eqb_prop in E2. subst w' s'.
      destruct (define_conversion sig body G n S2 i0 w s a) as [S3|] eqn:Ec; [|discriminate].
      injection E as <- <-. destruct (IHc _ _ _ _ _ _ G2 Hw L2 V2 Ec) as [G3 X3]. apply Fin; assumption.
    * (* a quotient *)
      destruct (Nat.eqb w' w && Bool.eqb s' s) eqn:Ews;
        [|injection E as <- <-; apply Fin; [exact G2|apply extends_refl]].
      apply andb_true_iff in Ews as [E1 E2]. apply Nat.eqb_eq in E1. apply Bool.eqb_prop in E2. subst w' s'.
      destruct (prim2_typed _ _ _ _ _ _ Hk) as [_ [Ta Tb]].
      destruct (define_division sig body G n S2 w s a b) as [S3|] eqn:Ed; [|discriminate].
      injection E as <- <-. destruct (IHd _ _ _ _ _ _ G2 Ta Tb Ed) as [G3 X3]. apply Fin; assumption.
    * (* a remainder *)
      destruct (Nat.eqb w' w && Bool.eqb s' s) eqn:Ews;
        [|injection E as <- <-; apply Fin; [exact G2|apply extends_refl]].
      apply andb_true_iff in Ews as [E1 E2]. apply Nat.eqb_eq in E1. apply Bool.eqb_prop in E2. subst w' s'.
      destruct (prim2_typed _ _ _ _ _ _ Hk) as [_ [Ta Tb]].
      destruct (define_division sig body G n S2 w s a b) as [S3|] eqn:Ed; [|discriminate].
      injection E as <- <-. destruct (IHd _ _ _ _ _ _ G2 Ta Tb Ed) as [G3 X3]. apply Fin; assumption.
Qed.

Lemma conversion_step : forall n, all_ok n -> conv_ok (Datatypes.S n).
Proof.
  intros n [IHv [IHx [IHc [IHd IHt]]]].
  intros S i w s a S' HG Hw Li Hi E. cbn [define_conversion] in E. refold E.
  destruct (tyof a) as [[fw fs|id obs|U k]|] eqn:Ta; try discriminate.
  destruct (value sig body G n S a fw fs) as [[S1 op]|] eqn:Ev; [|discriminate].
  destruct (IHv _ _ _ _ _ _ HG Ta Ev) as [G1 [X1 [B1 V1]]].
  assert (Hc : forall sg, agrees (svars S1) sg -> sg i = wrap w s (fv a)).
  { intros sg A. rewrite (Hi sg (extends_agrees _ _ _ X1 A)). reflexivity. }
  destruct ((min_int w s <=? min_int fw fs) && (max_int fw fs <=? max_int w s)) eqn:Er.
  + injection E as <-. apply andb_true_iff in Er as [R1 R2]. apply Z.leb_le in R1, R2.
    pose proof (typed_range _ _ _ Ta) as Ra.
    assert (Hs : forall sg, agrees (svars S1) sg -> sg i = fv a).
    { intros sg A. rewrite (Hc sg A). apply wrap_small; [apply int_ok_width; exact Hw|].
      unfold in_range in *. lia. }
    split.
    * apply Good_equal.
      -- apply Good_range; [exact G1|]. intros sg A. rewrite (Hs sg A). exact Ra.
      -- intros sg A. rewrite eval_evar, (V1 sg A). exact (Hs sg A).
    * eapply extends_trans; [exact X1|]. apply extends_same. reflexivity.
  + injection E as <-.
    pose proof (convert_typed w s a fw fs Hw Ta) as Tc.
    set (v := mkV RWrap w s (Prim OConvert w s [a]) op).
    assert (Gv : Good (with_var S1 v)).
    { apply Good_var; [exact G1|]. unfold var_ok. cbn [v aw asg aterm arole aghost].
      split; [exact Hw|split; [exact Tc|split; [exact B1|]]].
      rewrite firstn_snoc, (V1 _ (agrees_build _)).
      change (fv (Prim OConvert w s [a])) with (wrap w s (fv a)). symmetry. apply wrap_cong. }
    split.
    * apply Good_equal; [exact Gv|]. intros sg A.
      rewrite eval_evar, eval_eplus.
      pose proof (wrap_var (with_var S1 v) sg _ v Gv A (nth_snoc _ _) eq_refl) as W.
      cbn [v aghost aw aterm] in W.
      rewrite (Hc sg (extends_agrees _ _ _ (extends_with_var _ _) A)).
      change (wrap w s (fv a)) with (fv (Prim OConvert w s [a])). rewrite <- W. lia.
    * eapply extends_trans; [exact X1|]. eapply extends_trans; [apply extends_with_var|].
      apply extends_same. reflexivity.
Qed.

Lemma division_step : forall n, all_ok n -> div_ok (Datatypes.S n).
Proof.
  intros n [IHv [IHx [IHc [IHd IHt]]]].
  intros S w s a b S' HG Ta Tb E. cbn [define_division] in E. refold E.
  pose proof (int_typed_ok _ _ _ Ta) as Hw.
  destruct (mem_div (sdivs S) w s (Prim OQuot w s [a; b])).
  { injection E as <-. split; [exact HG|apply extends_refl]. }
  assert (Tq : tyof (Prim OQuot w s [a; b]) = Some (TInt w s)) by (apply div_typed; auto).
  assert (Tr : tyof (Prim ORem w s [a; b]) = Some (TInt w s)) by (apply div_typed; auto).
  destruct (variable sig body G n (with_div S w s (Prim OQuot w s [a; b])) w s [Prim OQuot w s [a; b]])
    as [[S1 q]|] eqn:Eq; [|discriminate].
  destruct (IHx _ _ _ _ _ _ (Good_div _ _ _ _ HG) Hw (Forall_cons _ Tq (Forall_nil _)) Eq) as [G1 [X1 [_ Vq]]].
  destruct (variable sig body G n S1 w s [Prim ORem w s [a; b]]) as [[S2 r]|] eqn:Er; [|discriminate].
  destruct (IHx _ _ _ _ _ _ G1 Hw (Forall_cons _ Tr (Forall_nil _)) Er) as [G2 [X2 [_ Vr]]].
  destruct (value sig body G n S2 a w s) as [[S3 dv]|] eqn:Ea; [|discriminate].
  destruct (IHv _ _ _ _ _ _ G2 Ta Ea) as [G3 [X3 [_ V3]]].
  assert (X13 : extends S S3).
  { eapply extends_trans; [exact X1|]. eapply extends_trans; eauto. }
  assert (Sem : forall sg, agrees (svars S3) sg ->
            eval_e sg dv = fv a /\
            sg q = (if fv b =? 0 then 0 else wrap w s (Z.quot (fv a) (fv b))) /\
            sg r = (if fv b =? 0 then fv a else Z.rem (fv a) (fv b))).
  { intros sg A. split; [exact (V3 sg A)|split].
    - rewrite (Vq sg (extends_agrees _ _ _ X2 (extends_agrees _ _ _ X3 A))). reflexivity.
    - rewrite (Vr sg (extends_agrees _ _ _ X3 A)). reflexivity. }
  assert (Unk : forall S5, unknown_divisor (value sig body G n S3 b w s) s dv (evar r) = Some S5 ->
                Good S5 /\ extends S S5).
  { intros S5 E5. destruct (value sig body G n S3 b w s) as [[S4 d]|] eqn:Eb; [|discriminate].
    destruct (IHv _ _ _ _ _ _ G3 Tb Eb) as [G4 [X4 [_ V4]]].
    destruct (unknown_sound S4 d s w dv r S5 a b G4 Ta Tb) as [G5 X5]; [|exact E5|].
    - intros sg A. destruct (Sem sg (extends_agrees _ _ _ X4 A)) as [D [_ R]]. auto.
    - split; [exact G5|]. eapply extends_trans; [exact X13|]. eapply extends_trans; eauto. }
  destruct b as [vi|w' s' v|dc ts|o w' s' ts|D j x|D x y]; try (exact (Unk _ E)).
  destruct (Nat.eqb w' w && Bool.eqb s' s) eqn:Ews; [|exact (Unk _ E)].
  destruct (lit_typed _ _ _ _ _ Tb) as [-> [-> Rv]].
  rewrite (wrap_small w s v) in E by (auto; apply int_ok_width; exact Hw).
  destruct (known_sound S3 s w v q r dv a S' G3 Ta Rv) as [G5 X5]; [|exact E|].
  + intros sg A. exact (Sem sg A).
  + split; [exact G5|]. eapply extends_trans; eauto.
Qed.

Lemma truth_step : forall n, all_ok n -> truth_ok (Datatypes.S n).
Proof.
  intros n [IHv [IHx [IHc [IHd IHt]]]].
  intros S c h S' HG Tc Hc E. cbn [truth] in E. refold E.
  assert (FB : forall S0, Good S0 -> extends S S0 ->
            match value sig body G n S0 c 1 false with
            | Some (S1, e) => Some (equal S1 e (enone (if h then 1 else 0)))
            | None => None
            end = Some S' -> Good S' /\ extends S S').
  { intros S0 G0 X0 E0. destruct (value sig body G n S0 c 1 false) as [[S1 e]|] eqn:Ev; [|discriminate].
    injection E0 as <-. destruct (IHv _ _ _ _ _ _ G0 Tc Ev) as [G1 [X1 [_ V1]]].
    split.
    - apply Good_equal; [exact G1|]. intros sg A. rewrite (V1 sg A), eval_enone. exact Hc.
    - eapply extends_trans; [exact X0|]. eapply extends_trans; [exact X1|]. apply extends_same. reflexivity. }
  destruct c as [vi|w' s' v|dc ts|o w s ts|D j x|D x y]; cbn beta iota zeta in E;
    try (exact (FB S HG (extends_refl S) E)).
  + (* a literal *)
    change (fv (Lit w' s' v)) with v in Hc. subst v.
    destruct h; cbn in E; injection E as <-; split; [exact HG|apply extends_refl|exact HG|apply extends_refl].
  + destruct ts as [|a [|b [|c ts]]]; destruct o; cbn [is_comparison is_fits] in E;
      try (exact (FB S HG (extends_refl S) E)).
    all: try (apply (IHt S a (negb h) S' HG (not_typed _ _ _ _ Tc)); [|exact E];
              change (fv (Prim ONot w s [a])) with (1 - fv a) in Hc; destruct h; cbn [negb]; lia).
    all: destruct (prim2_typed _ _ _ _ _ _ Tc) as [Hw [Ta Tb]];
      destruct (value sig body G n S a w s) as [[S1 l]|] eqn:Ea; [|discriminate];
      destruct (IHv _ _ _ _ _ _ HG Ta Ea) as [G1 [X1 [_ Vl]]];
      destruct (value sig body G n S1 b w s) as [[S2 r]|] eqn:Eb; [|discriminate];
      destruct (IHv _ _ _ _ _ _ G1 Tb Eb) as [G2 [X2 [_ Vr]]];
      assert (X : extends S S2) by (eapply extends_trans; eauto);
      assert (Ev : forall sg, agrees (svars S2) sg -> eval_e sg l = fv a /\ eval_e sg r = fv b)
        by (intros sg A; split; [apply Vl; eapply extends_agrees; eauto|apply Vr; exact A]);
      clear Ea Eb Vl Vr;
      unfold fvr in Hc; cbn [eval map prim_eval zv b2v] in Hc;
      change (zv Abs (eval Abs I rho a)) with (fv a) in Hc;
      change (zv Abs (eval Abs I rho b)) with (fv b) in Hc.
    (* the comparisons *)
    1-6: destruct h; cbn beta iota in E; injection E as <-;
      (split; [|eapply extends_trans; [exact X|apply extends_same; reflexivity]]);
      (match type of Hc with (if ?c then _ else _) = _ => destruct c eqn:B end); try discriminate Hc; zbool;
      (lazymatch goal with
       | |- Good (equal _ _ _) => apply Good_equal
       | |- Good (differ _ _ _) => apply Good_differ
       | |- Good (before _ _ _ _) => apply Good_before
       end); try exact G2;
      intros sg A; destruct (Ev sg A) as [El Er]; rewrite El, Er; lia.
    (* the checks that an operation fits *)
    all: destruct (exact _ l r) as [x|] eqn:Ex;
      [pose proof (fun sg => exact_sound _ _ _ _ sg Ex) as Xs; cbn beta iota in Xs;
       destruct h; cbn beta iota in E; injection E as <-;
       (split; [|eapply extends_trans; [exact X|apply extends_same; reflexivity]]);
       (match type of Hc with (if ?c then _ else _) = _ => destruct c eqn:B end); try discriminate Hc;
       [apply Good_bound|apply Good_outside]; try exact G2;
       intros sg A; rewrite Xs; destruct (Ev sg A) as [El Er]; rewrite El, Er;
       [apply in_rangeb_iff; exact B|intros R; apply in_rangeb_iff in R; congruence]
      |try (cbn [exact] in Ex; discriminate Ex)].
    destruct (product_fits S2 l r w s) eqn:PF; [|exact (FB S2 G2 X E)].
    destruct h; cbn beta iota in E; injection E as <-; [split; [exact G2|exact X]|exfalso].
    pose proof (product_fits_sound S2 l r w s G2 PF _ (agrees_build _)) as R.
    destruct (Ev _ (agrees_build _)) as [El Er]. rewrite El, Er in R.
    apply in_rangeb_iff in R. rewrite R in Hc. discriminate Hc.
Qed.

Lemma translation_sound : forall n, all_ok n.
Proof.
  induction n as [|n IH].
  - split; [|split; [|split; [|split]]]; intros; discriminate.
  - split; [apply value_step|split; [apply variable_step|split; [apply conversion_step|split;
      [apply division_step|apply truth_step]]]]; exact IH.
Qed.

(* ---- the facts and the refuted goal ---- *)

Lemma lit_fact : forall n S x y w' s' v S', truth_ok n -> Good S ->
  tyof x = Some u1 -> tyof y = Some u1 -> fv x = fv y -> nfn y = Lit w' s' v ->
  truth sig body G n S (nfn x) (negb (v =? 0)) = Some S' -> Good S' /\ extends S S'.
Proof.
  intros n S x y w' s' v S' Ht HG Tx Ty Exy Ny E.
  apply (Ht S (nfn x) (negb (v =? 0)) S' HG (nf_typed _ _ Tx)); [|exact E].
  pose proof (nf_typed _ _ Ty) as Tn. rewrite Ny in Tn.
  destruct (lit_typed _ _ _ _ _ Tn) as [_ [_ Rv]]. unfold in_range, min_int, max_int in Rv. cbn in Rv.
  rewrite (nf_val _ _ Tx), Exy, <- (nf_val _ _ Ty), Ny. change (fv (Lit w' s' v)) with v.
  destruct (v =? 0) eqn:V; zbool; cbn [negb]; lia.
Qed.

Lemma lit_refuted : forall n S x y w' s' v S', truth_ok n -> Good S ->
  tyof x = Some u1 -> tyof y = Some u1 -> fv x <> fv y -> nfn y = Lit w' s' v ->
  truth sig body G n S (nfn x) (v =? 0) = Some S' -> Good S' /\ extends S S'.
Proof.
  intros n S x y w' s' v S' Ht HG Tx Ty Exy Ny E.
  apply (Ht S (nfn x) (v =? 0) S' HG (nf_typed _ _ Tx)); [|exact E].
  pose proof (nf_typed _ _ Ty) as Tn. rewrite Ny in Tn.
  destruct (lit_typed _ _ _ _ _ Tn) as [_ [_ Rv]]. unfold in_range, min_int, max_int in Rv. cbn in Rv.
  pose proof (nf_val _ _ Ty) as Vy. rewrite Ny in Vy. change (fv (Lit w' s' v)) with v in Vy.
  rewrite (nf_val _ _ Tx). pose proof (u1_range x Tx).
  destruct (v =? 0) eqn:V; zbool; lia.
Qed.

Lemma fact_sound : forall n S F S', value_ok n -> truth_ok n -> Good S -> valid sig G F = true ->
  holds Abs I rho F -> fact sig body G n S F = Some S' -> Good S' /\ extends S S'.
Proof.
  intros n S F S' Hv Ht HG VF HF E.
  destruct F as [T a b|T Q|A B|A B|A B|]; try discriminate E.
  destruct T as [w s|id obs|U k]; try discriminate E.
  cbn [valid] in VF. apply andb_true_iff in VF as [Va Vb]. apply teq_true in Va, Vb.
  cbn [holds] in HF.
  assert (Fab : fv a = fv b) by (unfold fvr; rewrite HF; reflexivity).
  assert (Gen : forall S0,
            match value sig body G n S a w s with
            | Some (S1, l) =>
                match value sig body G n S1 b w s with Some (S2, r) => Some (equal S2 l r) | None => None end
            | None => None
            end = Some S0 -> Good S0 /\ extends S S0).
  { intros S0 E0. destruct (value sig body G n S a w s) as [[S1 l]|] eqn:Ea; [|discriminate].
    destruct (Hv _ _ _ _ _ _ HG Va Ea) as [G1 [X1 [_ Vl]]].
    destruct (value sig body G n S1 b w s) as [[S2 r]|] eqn:Eb; [|discriminate].
    destruct (Hv _ _ _ _ _ _ G1 Vb Eb) as [G2 [X2 [_ Vr]]].
    injection E0 as <-. split.
    - apply Good_equal; [exact G2|]. intros sg A. rewrite (Vl sg (extends_agrees _ _ _ X2 A)), (Vr sg A). exact Fab.
    - eapply extends_trans; [exact X1|]. eapply extends_trans; [exact X2|]. apply extends_same. reflexivity. }
  unfold fact in E.
  destruct (Nat.eqb w 1 && negb s) eqn:U; [|exact (Gen _ E)].
  apply andb_true_iff in U as [U1 U2]. apply Nat.eqb_eq in U1. apply negb_true_iff in U2. subst w s.
  destruct (nf sig body b) as [vi|w' s' v|dc ts|o w' s' ts|D j x|D x y] eqn:Nb; cbn beta iota zeta in E.
  2: exact (lit_fact n S a b w' s' v S' Ht HG Va Vb Fab Nb E).
  all: destruct (nf sig body a) as [vi'|w'' s'' v'|dc' ts'|o' w'' s'' ts'|D' j' x'|D' x' y'] eqn:Na;
    cbn beta iota zeta in E; try (exact (Gen _ E)); rewrite <- Nb in E;
    exact (lit_fact n S b a w'' s'' v' S' Ht HG Vb Va (eq_sym Fab) Na E).
Qed.

Lemma refuted_sound : forall n S P S', value_ok n -> truth_ok n -> Good S -> valid sig G P = true ->
  ~ holds Abs I rho P -> refuted sig body G n S P = Some S' -> Good S' /\ extends S S'.
Proof.
  intros n S P S' Hv Ht HG VP HP E.
  destruct P as [T a b|T Q|A B|A B|A B|]; try discriminate E.
  2: { cbn [refuted] in E. injection E as <-. split; [exact HG|apply extends_refl]. }
  destruct T as [w s|id obs|U k]; try discriminate E.
  cbn [valid] in VP. apply andb_true_iff in VP as [Va Vb]. apply teq_true in Va, Vb.
  assert (Fab : fv a <> fv b).
  { intros Eq. apply HP. cbn [holds]. rewrite (typed_vz _ _ _ Va), (typed_vz _ _ _ Vb), Eq. reflexivity. }
  assert (Gen : forall S0,
            match value sig body G n S a w s with
            | Some (S1, l) =>
                match value sig body G n S1 b w s with Some (S2, r) => Some (differ S2 l r) | None => None end
            | None => None
            end = Some S0 -> Good S0 /\ extends S S0).
  { intros S0 E0. destruct (value sig body G n S a w s) as [[S1 l]|] eqn:Ea; [|discriminate].
    destruct (Hv _ _ _ _ _ _ HG Va Ea) as [G1 [X1 [_ Vl]]].
    destruct (value sig body G n S1 b w s) as [[S2 r]|] eqn:Eb; [|discriminate].
    destruct (Hv _ _ _ _ _ _ G1 Vb Eb) as [G2 [X2 [_ Vr]]].
    injection E0 as <-. split.
    - apply Good_differ; [exact G2|]. intros sg A. rewrite (Vl sg (extends_agrees _ _ _ X2 A)), (Vr sg A). exact Fab.
    - eapply extends_trans; [exact X1|]. eapply extends_trans; [exact X2|]. apply extends_same. reflexivity. }
  unfold refuted in E.
  destruct (Nat.eqb w 1 && negb s) eqn:U; [|exact (Gen _ E)].
  apply andb_true_iff in U as [U1 U2]. apply Nat.eqb_eq in U1. apply negb_true_iff in U2. subst w s.
  destruct (nf sig body b) as [vi|w' s' v|dc ts|o w' s' ts|D j x|D x y] eqn:Nb; cbn beta iota zeta in E.
  2: exact (lit_refuted n S a b w' s' v S' Ht HG Va Vb Fab Nb E).
  all: destruct (nf sig body a) as [vi'|w'' s'' v'|dc' ts'|o' w'' s'' ts'|D' j' x'|D' x' y'] eqn:Na;
    cbn beta iota zeta in E; try (exact (Gen _ E)); rewrite <- Nb in E;
    exact (lit_refuted n S b a w'' s'' v' S' Ht HG Vb Va (not_eq_sym Fab) Na E).
Qed.

Lemma facts_sound : forall n Fs S S', value_ok n -> truth_ok n -> Good S ->
  Forall (fun F => valid sig G F = true) Fs -> Forall (holds Abs I rho) Fs ->
  facts sig body G n S Fs = Some S' -> Good S'.
Proof.
  intros n Fs. induction Fs as [|F Fs IH]; intros S S' Hv Ht HG VF HF E; cbn [facts] in E.
  - injection E as <-. exact HG.
  - inversion VF as [|? ? VF1 VFs]; inversion HF as [|? ? HF1 HFs]; subst.
    destruct (fact sig body G n S F) as [S1|] eqn:E1; [|discriminate].
    destruct (fact_sound n S F S1 Hv Ht HG VF1 HF1 E1) as [G1 _].
    exact (IH S1 S' Hv Ht G1 VFs HFs E).
Qed.

(* M3: an accepted certificate means the facts entail the goal. This is
   Checker.v's lin_sound, for lin_ok. *)
Theorem lin_sound_model : forall Fs P k, lin_ok sig body G Fs P k = true ->
  Forall (fun F => valid sig G F = true) Fs -> valid sig G P = true ->
  Forall (holds Abs I rho) Fs -> holds Abs I rho P.
Proof.
  intros Fs P k E VF VP HF. unfold lin_ok, arithmetic_system in E.
  destruct (translation_sound max_linear_depth) as [Hv [_ [_ [_ Ht]]]].
  destruct (facts sig body G max_linear_depth empty Fs) as [S1|] eqn:E1; [|discriminate].
  pose proof (facts_sound _ _ _ _ Hv Ht Good_empty VF HF E1) as G1.
  destruct (refuted sig body G max_linear_depth S1 P) as [S2|] eqn:E2; [|discriminate].
  cbn beta iota in E.
  assert (Dec : holds Abs I rho P \/ ~ holds Abs I rho P).
  { destruct P as [T a b|T Q|A B|A B|A B|]; try discriminate E2.
    - destruct T as [w s|id obs|U k']; try discriminate E2.
      cbn [valid] in VP. apply andb_true_iff in VP as [Va Vb]. apply teq_true in Va, Vb.
      cbn [holds]. rewrite (typed_vz _ _ _ Va), (typed_vz _ _ _ Vb).
      destruct (Z.eq_dec (fv a) (fv b)) as [Eq|Ne]; [left; rewrite Eq; reflexivity|].
      right. intros Q. injection Q. exact Ne.
    - right. cbn [holds]. tauto. }
  destruct Dec as [Hp|Hn]; [exact Hp|exfalso].
  destruct (refuted_sound _ _ _ _ Hv Ht G1 VP Hn E2) as [[H1 _] _].
  destruct (H1 _ (agrees_build _)) as [C [D _]].
  exact (check_certificate_sound _ _ E _ C D).
Qed.

End Sound.

(* G13 (KERNEL.md 17): check_sound with reflexivity deciding by the model's
   normalizer and linear arithmetic by this translation and the certificate
   checker. Neither procedure is a premise any more: what is left are the
   interpretation's own obligations, that an observation, an element and a
   call give values of their types and a defined function means its body. *)
Section Closed.

Variable sig : nat -> option (list ty * ty).
Hypothesis sig_ok : forall d ps R, sig d = Some (ps, R) -> ty_ok R = true.
Variable body : nat -> option term.
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
Hypothesis call_body : forall d ps R b args,
  sig d = Some (ps, R) -> body d = Some b -> Forall2 (dom Abs I) ps args ->
  i_call Abs I d args = eval Abs I (rev args) b.

Theorem check_sound_closed : forall P e,
  check sig (nf sig body) certificate (lin_ok sig body) P e = true -> holds Abs I [] P.
Proof.
  intros P e E.
  apply (check_sound sig sig_ok (nf sig body) certificate (lin_ok sig body) Abs I proj_ok elem_ok call_ok)
    with (e := e); [| |exact E].
  - intros G rho t T Henv Ht. eapply nf_sound; eauto.
  - intros G Fs Q k rho Ek VF VQ Henv HF. eapply lin_sound_model with (G := G) (rho := rho); eauto.
Qed.

(* M4 for the closed checker: no evidence establishes False. *)
Corollary check_consistent_closed : forall e,
  check sig (nf sig body) certificate (lin_ok sig body) PFalse e = false.
Proof.
  intros e. destruct (check sig (nf sig body) certificate (lin_ok sig body) PFalse e) eqn:E; [|reflexivity].
  exfalso. exact (check_sound_closed PFalse e E).
Qed.

End Closed.

(* G13 (ROADMAP.md, V1 release gates): the checker of all fifteen rules is
   sound with the acceptance of the evidence its only premise. A context is a
   set of definitions as Context::define admits them (KERNEL.md 6): each with a
   supported result type and a body of that type over its parameters. A model
   of a context is an interpretation of it: one that gives every observation,
   element and call a value of its type and every definition the meaning of
   its body. A proposition holds when it holds in every model. *)
Record context : Type := {
  c_sig : nat -> option (list ty * ty);
  c_body : nat -> option term;
  c_sig_ok : forall d ps R, c_sig d = Some (ps, R) -> ty_ok R = true;
  c_body_typed : forall d ps R b, c_sig d = Some (ps, R) -> c_body d = Some b ->
    type_of c_sig (rev ps) b = Some R
}.

Record model (C : context) : Type := {
  m_carrier : Type;
  m_interp : interp m_carrier;
  m_proj_ok : forall id obs i v T,
    ty_ok (TValue id obs) = true -> dom m_carrier m_interp (TValue id obs) v ->
    nth_error obs i = Some T -> dom m_carrier m_interp T (i_proj m_carrier m_interp (TValue id obs) i v);
  m_elem_ok : forall U k v X x,
    ty_ok (TIndexed U k) = true -> dom m_carrier m_interp (TIndexed U k) v ->
    is_int X = true -> dom m_carrier m_interp X x ->
    dom m_carrier m_interp U (i_elem m_carrier m_interp (TIndexed U k) v x);
  m_call_ok : forall d ps R args,
    c_sig C d = Some (ps, R) -> Forall2 (dom m_carrier m_interp) ps args ->
    dom m_carrier m_interp R (i_call m_carrier m_interp d args);
  m_call_body : forall d ps R b args,
    c_sig C d = Some (ps, R) -> c_body C d = Some b -> Forall2 (dom m_carrier m_interp) ps args ->
    i_call m_carrier m_interp d args = eval m_carrier m_interp (rev args) b
}.

Definition kernel_check (C : context) : prop -> evid certificate -> bool :=
  check (c_sig C) (nf (c_sig C) (c_body C)) certificate (lin_ok (c_sig C) (c_body C)).

Theorem kernel_sound : forall (C : context) (M : model C) P e,
  kernel_check C P e = true -> holds (m_carrier C M) (m_interp C M) [] P.
Proof.
  intros [sig body sig_ok body_typed] [Abs I proj_ok elem_ok call_ok call_body] P e E.
  unfold kernel_check in E. cbn [c_sig c_body m_carrier m_interp] in *.
  eapply check_sound_closed; eauto.
Qed.

Corollary kernel_consistent : forall (C : context) (M : model C) e, kernel_check C PFalse e = false.
Proof.
  intros C M e. destruct (kernel_check C PFalse e) eqn:E; [|reflexivity].
  exfalso. exact (kernel_sound C M PFalse e E).
Qed.
