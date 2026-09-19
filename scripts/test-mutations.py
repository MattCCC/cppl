#!/usr/bin/env python3
"""Disable one soundness check at a time in a disposable source copy.

Run with the same CMake/Clang environment as scripts/check.sh. No mutation is
applied to the checkout. Build failures and timeouts are errors, not kills.
Logs, the mutation diff, and a JSON result remain under build/mutations/.
"""

import argparse
from dataclasses import dataclass
import difflib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


@dataclass(frozen=True)
class Mutation:
    name: str
    path: str
    before: str
    after: str
    tests: str = "^kernel_"


def guard(name, before, path="kernel/src/check.cpp", tests="^kernel_"):
    return Mutation(name, path, before, "false && (" + before + ")", tests)


MUTATIONS = [
    guard("reflexivity-equality", "!(*lhs == *rhs)"),
    guard("forall-binder-match", "!(introduction->binder == quantified->binder)"),
    guard("implication-premise-match", "!(*introduction->premise == *implication->premise)"),
    guard("hypothesis-proposition", "!(available == proposition)"),
    Mutation("hypothesis-scope", "kernel/src/check.cpp",
             "if (assumed->index.value >= assumptions.size()) {",
             "if (assumed->index.value >= assumptions.size()) { return {};"),
    guard("forall-result", "!(instantiated == proposition)"),
    guard("implication-result", "!(*implication->conclusion == proposition)"),
    guard("transport-result", "!(result == proposition)"),
    guard("conditional-motive", "!(instantiate(*branch->motive, selected) == proposition)"),
    Mutation("forall-recursive-evidence", "kernel/src/check.cpp",
             "*elimination->evidence, limits, depth + 1);\n            !evidence)",
             "*elimination->evidence, limits, depth + 1);\n            false && !evidence)"),
    Mutation("implication-recursive-evidence", "kernel/src/check.cpp",
             "*application->evidence, limits, depth + 1);\n            !evidence)",
             "*application->evidence, limits, depth + 1);\n            false && !evidence)"),
    Mutation("implication-premise-evidence", "kernel/src/check.cpp",
             "*application->premise, limits, depth + 1);\n            !premise)",
             "*application->premise, limits, depth + 1);\n            false && !premise)"),
    Mutation("transport-equality-evidence", "kernel/src/check.cpp",
             "*transport->equality, limits, depth + 1);\n            !checked)",
             "*transport->equality, limits, depth + 1);\n            false && !checked)"),
    Mutation("transport-recursive-evidence", "kernel/src/check.cpp",
             "*transport->evidence, limits, depth + 1);\n            !checked)",
             "*transport->evidence, limits, depth + 1);\n            false && !checked)"),
    Mutation("conditional-false-arm", "kernel/src/check.cpp",
             "return check_under(context, locals, assumptions, false_goal, *branch->false_case,\n"
             "                           limits, depth + 1);",
             "(void)check_under(context, locals, assumptions, false_goal, *branch->false_case,\n"
             "                           limits, depth + 1); return {};"),
    Mutation("arithmetic-fact-evidence", "kernel/src/check.cpp",
             "*fact.evidence, limits, depth + 1);\n                !checked)",
             "*fact.evidence, limits, depth + 1);\n                false && !checked)"),
    guard("arithmetic-certificate", "!refuted", tests="^kernel_arithmetic_test$"),
    Mutation("substitution-capture", "kernel/src/substitution.cpp",
             "return shift(argument, depth, 0);", "return argument;"),
    Mutation("hypothesis-binder-shift", "kernel/src/check.cpp",
             "static_cast<std::uint32_t>(locals.size() - assumption.binders)", "0u"),
    guard("verdict-goal-identity", "!(acceptance.proposition() == obligation.goal)",
          "compiler/obligations/src/status.cpp", "^unit_verdict_test$"),
    Mutation("callee-body-linkage", "compiler/automation/src/composition.cpp",
             "if (!checked) {\n            return std::unexpected(\"callee contract linkage failed:",
             "if (false && !checked) {\n            return std::unexpected(\"callee contract linkage failed:",
             "^unit_contracts_test$"),
    Mutation("call-precondition-gate", "compiler/automation/src/composition.cpp",
             "if (index < stage.prefix && call.precondition_obligation.has_value() &&",
             "if (false && index < stage.prefix && call.precondition_obligation.has_value() &&",
             "^unit_contracts_test$|^negative_verified_calls$|^negative_verified_locals$"),
]


def run(command, log, timeout=300):
    with log.open("w") as output:
        try:
            return subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                                  timeout=timeout, check=False).returncode
        except subprocess.TimeoutExpired:
            return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", action="append", choices=[m.name for m in MUTATIONS])
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    artifacts = root / "build" / "mutations"
    artifacts.mkdir(parents=True, exist_ok=True)
    run_root = Path(tempfile.mkdtemp(prefix="run-", dir=artifacts))
    source = run_root / "source"
    shutil.copytree(root, source, ignore=shutil.ignore_patterns(
        ".git", "build", "build-*", ".code-review-graph", ".claude", ".codex"))
    build = run_root / "build"
    selected = [m for m in MUTATIONS if not args.only or m.name in args.only]
    results = []
    print(f"Mutation artifacts: {run_root}", flush=True)
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCPPL_WARNINGS_AS_ERRORS=ON"]
    compile_command = ["cmake", "--build", str(build), "-j", str(args.jobs)]
    # A green control run establishes that a later test failure is introduced.
    for name, command in [("configure", configure), ("build", compile_command),
                          ("baseline", ["ctest", "--test-dir", str(build),
                                        "--output-on-failure", "-j", str(args.jobs)])]:
        if run(command, run_root / f"{name}.log") != 0:
            raise SystemExit(f"Control {name} failed; see {run_root}/{name}.log")
    for mutation in selected:
        target = source / mutation.path
        original = target.read_text()
        if original.count(mutation.before) != 1:
            results.append({"name": mutation.name, "result": "stale-anchor"})
            continue
        changed = original.replace(mutation.before, mutation.after, 1)
        log_dir = run_root / mutation.name
        log_dir.mkdir()
        (log_dir / "mutation.diff").write_text("".join(difflib.unified_diff(
            original.splitlines(True), changed.splitlines(True),
            fromfile=mutation.path, tofile=mutation.path)))
        try:
            target.write_text(changed)
            built = run(compile_command, log_dir / "build.log")
            if built != 0:
                outcome = "build-timeout" if built is None else "build-error"
            else:
                status = run(["ctest", "--test-dir", str(build), "--output-on-failure",
                              "--timeout", "45", "-R", mutation.tests], log_dir / "tests.log")
                output = (log_dir / "tests.log").read_text()
                # CTest uses 8 for ordinary test failures. Crashes, timeouts,
                # setup errors, and an empty test selection do not count as kills.
                if status == 0 and "No tests were found" not in output:
                    outcome = "survived"
                elif status == 8 and "***Failed" in output and not any(
                        word in output for word in ["***Exception", "***Timeout", "***Not Run"]):
                    outcome = "caught"
                else:
                    outcome = "test-error"
        finally:
            target.write_text(original)
        results.append({"name": mutation.name, "result": outcome})
        print(f"{mutation.name}: {outcome}", flush=True)
        (run_root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    # Rebuild the unmodified copy so no runnable mutated compiler is left behind.
    restored = run(compile_command, run_root / "restored-build.log")
    (run_root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"{sum(r['result'] == 'caught' for r in results)}/{len(results)} mutations caught", flush=True)
    return 0 if restored == 0 and all(r["result"] == "caught" for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
