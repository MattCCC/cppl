(* What each theorem rests on. tools/formal/check.sh requires every line below
   to report "Closed under the global context": no axiom, no admitted lemma,
   nothing but Coq's own kernel (TRUST.md TCB-META-003). The hypotheses of
   check_sound are its explicit premises, not axioms. *)

From CppL Require Import Syntax Semantics Typing Checker Normalize Consistency Certificate Linear.

Print Assumptions check_sound.
Print Assumptions check_consistent.
Print Assumptions syntactic_consistency.
Print Assumptions syntactic_soundness.
Print Assumptions check_certificate_sound.
Print Assumptions nf_sound.
Print Assumptions check_sound_normalized.
Print Assumptions lin_sound_model.
Print Assumptions check_sound_closed.
Print Assumptions check_consistent_closed.
Print Assumptions kernel_sound.
Print Assumptions kernel_consistent.
