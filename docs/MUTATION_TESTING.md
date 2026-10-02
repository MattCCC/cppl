# C++L Mutation Testing

**What the mutation suite measures, how an entry is written, and what counts as
an equivalent mutant**

A passing test suite says nothing on its own: it may check that the kernel
accepts what it should while never checking that it rejects what it must
(`AGENTS.md` 24). `make test-mutations` measures the other half. It breaks one
soundness check at a time in a disposable copy of the tree and requires a test
to fail. A mutation no test notices — a *survivor* — names a rule of the proof
system, the trust accounting or the refusal of unsupported C++ that nothing is
testing.

This document is the policy for that suite (`scripts/test-mutations.sh`). It
states no results: a run's numbers belong to the run.

---

## 1. What a run does

1. **Checks every anchor first.** Each entry names its check by an exact source
   string, which must occur exactly once in its file. An anchor that no longer
   matches means the check is silently untested, so the run refuses to start
   and names every stale entry before anything is built.
2. **Copies the tree.** The source is copied into `build/mutations/run-*/source`
   without `.git`, build trees, `tmp` or editor state. No mutation ever touches
   the checkout.
3. **Proves the control green.** The copy is configured with warnings as
   errors, built, and its whole test suite run. A failure here stops the run: a
   later failure must be the mutation's doing, not something already broken.
4. **Applies each mutation alone.** The anchor is replaced, the copy rebuilt
   incrementally, and the entry's tests run with a timeout of 120 seconds each;
   then the file is restored before the next entry.
5. **Rebuilds the unmutated copy** at the end, so no mutated compiler is left
   runnable.

## 2. Writing an entry

An entry is one line of five tab-separated fields in `scripts/test-mutations.sh`:

```text
name    file    before    after    ctest regex
```

- **name** is stable and says which check is broken, not how
  (`hypothesis-scope`, `trust-json-closure-flag`).
- **before** is the exact text of the check, unique in its file. Prefer the
  condition itself (`if (!held) {`) over surrounding code, so the mutation
  disables the check and nothing else.
- **after** removes the check's effect while still compiling: a condition made
  `false` or `true`, a call made `(void)`, a result replaced by the weaker one.
  A mutation that does not compile tests nothing and is reported as a build
  error, never as caught.
- **ctest regex** selects the narrowest tests that state the invariant the check
  enforces. It must select at least one test; an empty selection is an error in
  the experiment, not a result. Several alternatives are joined with `|`.

A check spanning several lines is written in the `multiline_*` functions of the
script instead, with the same five parts.

Reformatting code an anchor names changes the anchor. The anchor is updated in
the same commit, and the anchor check (step 1) confirms it before the commit
lands.

## 3. When an entry is required

Every new check that stands between an input and a stronger result gets an
entry, and the entry is shown caught before the commit that adds the check
lands:

- a condition of a kernel rule, a limit, or a comparison of a derived conclusion
  with the goal;
- an obligation the correspondence layer generates, and each place a fact is
  invalidated;
- a refusal of unsupported C++ or of a malformed construct;
- anything that decides a claim's trust closure, status or report category;
- validation of anything read from outside: interfaces, their configuration,
  their integrity, their dependencies;
- erasure's structural checks;
- withdrawal of an artifact that would otherwise describe a failed build.

A check with no entry is a check the project cannot show it would miss.

## 4. Outcomes

Every entry ends in exactly one of these:

| Outcome                   | Meaning                                                          | Ends the run as |
| ------------------------- | ---------------------------------------------------------------- | --------------- |
| `caught`                  | a selected test failed (CTest status 8, a test marked `Failed`, none `Not Run`) | pass            |
| `killed (nontermination)` | a selected test timed out or crashed                             | pass            |
| `equivalent`              | the tests pass and the entry has a justification (section 5)     | pass            |
| `survived`                | the tests pass and there is no justification                     | **failure**     |
| `build-error`             | the mutated copy does not build                                  | **failure**     |
| `test-error`              | anything else: no test selected, a test not run, another status  | **failure**     |

A timeout or a crash kills the mutation: turning a program that terminates into
one that does not, or that crashes, is a change in required behavior. The
in-process budgets should report such a search as an ordinary failure first; a
kill by timeout means some path is not budgeted, which is worth seeing.

## 5. Equivalent mutants

A mutation is **equivalent** only when removing the check leaves the program's
required behavior indistinguishable — the same refusals, the same termination,
no crash and no proof that was not there before — because **another enforcement
still holds the same invariant**. An entry is equivalent only when all three
hold:

1. its justification in `equivalent_justification` names the enforcement that
   still holds the invariant, and why the mutated check is redundant with it;
2. that enforcement has an entry of its own, and that entry is caught, so
   removing it is noticed;
3. a test states the invariant directly, so removing every enforcement of it
   fails that test.

What is never equivalent:

- **"The harness cannot observe a difference."** A difference no test observes
  is a missing test. It is a survivor until a test observes it, or until the
  three conditions above are shown.
- **A slower or non-terminating search**, or a crash: those are kills.
- **A change only in which diagnostic is printed**, when the refusal itself
  still happens: the check was redundant for soundness but its diagnostic is
  part of its contract, so a test pinning the diagnostic is added instead.
- **A mutation that removes a check whose case no input can reach yet.** That
  check is guarding the future. A fixture that reaches it is written, or the
  check is removed as dead code; it is not declared equivalent.

No entry is equivalent today. The nearest is `call-precondition-gate`, and it
shows the diagnostic rule at work. The gate refuses a stage composed after a
call whose precondition is unproven. `Composition::spend` refuses the same
dependency before reading its evidence (caught as `spend-dependency-proven`), so
removing the gate changes no verdict. It does change the refusal: the gate names
the callee whose precondition is unproven, where `spend` names an obligation
number. That diagnostic is pinned by
`a_stage_after_a_call_with_an_unproven_precondition_is_refused_naming_the_callee`
in `tests/unit/contracts_test.cpp` and by the `blocked` cases of
`tests/negative/verified_calls.sh`, so the entry is caught. An equivalent entry
needs all three parts above and review as a soundness change.

## 6. Survivors

A survivor is a defect in the tests, and possibly in the check. It is resolved by
one of:

- a test that fails with the mutation applied and passes without it — the usual
  fix, and the test cites the rule it states;
- showing the check wrong or dead and removing or correcting it, with its entry;
- the three parts of section 5.

It is never resolved by deleting the entry, narrowing the anchor until it
mutates nothing important, or widening the regex until some unrelated test
happens to fail.

## 7. Running it

```sh
make test-mutations JOBS=4                               # every entry
scripts/test-mutations.sh --only hypothesis-scope --only substitution-capture
scripts/test-mutations.sh --list                         # every entry's name
```

- The control configure uses the host's default compilers with warnings as
  errors, so `CC` and `CXX` name the LLVM Clang the project is built with, and
  that installation's `bin` is on `PATH` so libclang is found. GCC's warnings
  differ from Clang's, and the control build stops on them (the `ci-linux-gcc`
  preset turns warnings-as-errors off for that reason).
- A whole run is long — every entry rebuilds part of the tree and runs its
  tests — so it is not part of `make check` or of CI. It runs before a release
  and after a change to a check with an entry.
- Bound a run as every test is bounded, so a mutation that goes wrong cannot
  exhaust the machine: `bash tests/support/bounded.sh scripts/test-mutations.sh`.
- Artifacts are kept under `build/mutations/run-*`: each entry's diff, build log
  and test log, so a survivor can be reproduced without rerunning the suite.

`--only` runs the named entries, after checking their anchors and the same
control run; the pre-commit check for a new entry is `--only` with its name.

`--reuse <run directory>` builds in the copy an earlier run left, brought up to
date with the checkout, so the build is incremental. A file the copy holds that
the checkout does not is refused. Every file whose content or time differs from
the checkout's is touched after the copy, so no object built from a mutation an
interrupted run left applied, or restored without rebuilding, survives; the copy
is written in pax format, which keeps each time to the nanosecond, so an
unchanged file keeps its time and is not rebuilt. The control run is repeated on
every invocation, so a run interrupted part way is resumed soundly by running,
with `--reuse` and `--only`, the entries that have no outcome yet.
