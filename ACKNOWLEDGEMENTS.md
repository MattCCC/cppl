# Acknowledgements

C++L builds on decades of work in mathematical logic, type theory, formal verification, programming-language design, automated reasoning, and C++ systems engineering.

The mathematical foundations are described in detail in [FOUNDATIONS.md](FOUNDATIONS.md).

C++L does not claim invention of propositions-as-types, dependent type theory, Hoare logic, refinement typing, SMT solving, or the other established ideas on which its proof system is built. There are many amazing people with amazing projects out there who have been serving as inspiration for this.

C++L grows out of formal methods, dependent type theory, deductive verification, proof-carrying code, and program synthesis - applied specifically to a source-compatible C++ superset.

---

## Mathematical foundations

Particular intellectual credit is due to:

- **Gerhard Gentzen** - natural deduction and structural proof systems.
- **Alonzo Church** - lambda calculus and foundational work connecting logic and computation.
- **Haskell Curry** - early propositions/types correspondence.
- **William Alvin Howard** - the propositions-as-types and proofs-as-programs correspondence.
- **Nicolaas de Bruijn** - AUTOMATH and pioneering machine-checked formal mathematics.
- **Per Martin-Löf** - intuitionistic dependent type theory, identity types, and constructive type theory.
- **Robert W. Floyd** - formal reasoning about program correctness.
- **C. A. R. Hoare** - axiomatic program semantics and Hoare logic.
- **Edsger W. Dijkstra** - weakest preconditions and predicate-transformer semantics.
- **Thierry Coquand** and **Gérard Huet** - the Calculus of Constructions and foundational work leading to modern proof assistants.
- **Tim Freeman** and **Frank Pfenning** - refinement typing, together with the broader refinement-type research community.
- Researchers behind SAT, SMT, decision procedures, congruence closure, and automated theorem reasoning.

---

## Modern proof systems

C++L also benefits intellectually from the work embodied in modern proof and verification systems, including:

- **Rocq / Coq** - Thierry Coquand, Gérard Huet, Christine Paulin-Mohring, and the wider Rocq community.
- **Lean** - Leonardo de Moura, Soonho Kong, Jeremy Avigad, Floris van Doorn, Jakob von Raumer, and the wider Lean community.
- **Agda** - Ulf Norell, with earlier work by Catarina Coquand and Makoto Takeyama, and contributions from the wider Agda community.
- **Idris** - Edwin Brady and the Idris community.
- **F\*** - Nikhil Swamy and collaborators at Microsoft Research, Inria, and the wider F\* community.
- **Dafny** - K. Rustan M. Leino and the Dafny community.
- **VeriFast** - Bart Jacobs, Jan Smans, Frank Piessens, and contributors.
- **RefinedC** - Michael Sammler, Rodolphe Lepigre, Robbert Krebbers, Kayvan Memarian, Derek Dreyer, and Deepak Garg.
- **VCC** - Ernie Cohen, Michał Moskal, Wolfram Schulte, Stephan Tobies, and collaborators.
- **Frama-C** - Pascal Cuoq, Florent Kirchner, Nikolai Kosmatov, Virgile Prevosto, Julien Signoles, Boris Yakobowski, and the wider Frama-C team.
- **Liquid Haskell / Liquid Types** - Patrick Rondon, Ming Kawaguchi, Ranjit Jhala, Niki Vazou, Alexander Bakst, and collaborators.
- **Bend** - Victor Taelin and contributors, particularly for contemporary exploration of Laws and machine-checkable intent in an AI-oriented programming model.
- Other related proof assistants, verification languages, program logics, refinement-type systems, and research projects.

These systems demonstrate different approaches to:

- small trusted proof kernels
- dependent typing
- refinement typing
- propositions as types
- program extraction
- contracts
- separation logic
- ownership reasoning
- memory verification
- automated theorem proving
- SMT-assisted verification
- proof automation
- verified systems programming

C++L is not intended to reproduce any one of them.

---

## C++ and LLVM ecosystem

C++L depends heavily on the work of the C++ language and compiler communities.

Particular credit belongs broadly to:

- the ISO C++ standards community;
- Clang contributors;
- LLVM contributors;
- libc++ contributors;
- researchers working on C++ memory, object lifetime, undefined behavior, and formal semantics.

C++L intends to reuse this ecosystem rather than replace it.

---

## Automated reasoning

C++L may use external automation such as:

- cvc5
- Z3
- SAT solvers
- rewriting engines
- arithmetic decision procedures
- proof search systems

These systems are tools for producing or discharging proof obligations.

C++L's architectural goal is that external automation should not silently become the sole definition of truth.

---

## Credit policy

When C++L adopts a substantial idea, algorithm, implementation technique, proof rule, or source contribution from another project, the project should:

1. preserve legally required attribution;
2. document the source clearly;
3. distinguish inspiration from copied implementation;
4. cite relevant papers or projects where practical;
5. comply with all applicable licenses.

C++L should be generous and precise with intellectual credit.

Formal methods is a cumulative field.

The project exists because of the work that came before it.
