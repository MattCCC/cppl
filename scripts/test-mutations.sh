#!/usr/bin/env bash
# Disables one soundness check at a time in a disposable source copy, and
# reports whether the suite noticed.
#
# A test suite that passes says nothing on its own: it may be checking that the
# kernel accepts what it should, while never checking that it rejects what it
# must. Breaking a check on purpose is how that is measured. If a mutation
# survives, some rule of the proof system is going untested (AGENTS.md 24).
#
# No mutation touches the checkout. Each one is applied to a copy, built, tested
# and thrown away. Build failures and timeouts are errors, not kills: only a
# genuine test failure counts as catching a mutation.
#
# Usage: scripts/test-mutations.sh [--only <name>]... [--jobs <n>] [--list]
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
root=$(pwd)

# Each mutation is: name | file | before | after | ctest regex
#
# 'before' is an exact source string, matched once. Anything a mutation cannot
# find is reported before any building starts, because an anchor that no longer
# matches means that check is silently untested.
#
# A tab separates the fields, so the source strings may contain anything else.
mutations=$(cat <<'MUTATIONS'
reflexivity-equality	kernel/src/check.cpp	!(*lhs == *rhs)	false && (!(*lhs == *rhs))	^kernel_
forall-binder-match	kernel/src/check.cpp	!(introduction->binder == quantified->binder)	false && (!(introduction->binder == quantified->binder))	^kernel_
implication-premise-match	kernel/src/check.cpp	!(*introduction->premise == *implication->premise)	false && (!(*introduction->premise == *implication->premise))	^kernel_
hypothesis-proposition	kernel/src/check.cpp	!(available == proposition)	false && (!(available == proposition))	^kernel_
forall-result	kernel/src/check.cpp	!(instantiated == proposition)	false && (!(instantiated == proposition))	^kernel_
implication-result	kernel/src/check.cpp	!(*implication->conclusion == proposition)	false && (!(*implication->conclusion == proposition))	^kernel_
transport-result	kernel/src/check.cpp	!(result == proposition)	false && (!(result == proposition))	^kernel_
conditional-motive	kernel/src/check.cpp	!(instantiate(*branch->motive, selected) == proposition)	false && (!(instantiate(*branch->motive, selected) == proposition))	^kernel_
arithmetic-certificate	kernel/src/check.cpp	!refuted	false && (!refuted)	^kernel_arithmetic_test$
hypothesis-scope	kernel/src/check.cpp	if (assumed->index.value >= assumptions.size()) {	if (assumed->index.value >= assumptions.size()) { return {};	^kernel_
substitution-capture	kernel/src/substitution.cpp	return shift(argument, depth, 0);	return argument;	^kernel_
hypothesis-binder-shift	kernel/src/check.cpp	static_cast<std::uint32_t>(locals.size() - assumption.binders)	0u	^kernel_
verdict-goal-identity	compiler/obligations/src/status.cpp	!(acceptance.proposition() == obligation.goal)	false && (!(acceptance.proposition() == obligation.goal))	^unit_verdict_test$
conjunction-side-identity	kernel/src/check.cpp	!(side == proposition)	false && (!(side == proposition))	^kernel_
conjunction-shape	kernel/src/check.cpp	const auto* conjunction = std::get_if<And>(&taken->conjunction->node);	const auto* conjunction = std::get_if<And>(&taken->conjunction->node); if (conjunction == nullptr) { return {}; }	^kernel_
disjunction-shape	kernel/src/check.cpp	const auto* disjunction = std::get_if<Or>(&cases->disjunction->node);	const auto* disjunction = std::get_if<Or>(&cases->disjunction->node); if (disjunction == nullptr) { return {}; }	^kernel_
MUTATIONS
)

# These carry embedded newlines, so they are held separately rather than being
# squeezed onto one line above.
multiline_names=(forall-recursive-evidence implication-recursive-evidence
                 implication-premise-evidence transport-equality-evidence
                 transport-recursive-evidence conditional-false-arm
                 arithmetic-fact-evidence callee-body-linkage
                 call-precondition-gate conjunction-introduction-right
                 disjunction-right-case conjunction-elimination-evidence)

multiline_file() {
    case "$1" in
        callee-body-linkage|call-precondition-gate) echo "compiler/automation/src/composition.cpp" ;;
        *) echo "kernel/src/check.cpp" ;;
    esac
}

multiline_tests() {
    case "$1" in
        callee-body-linkage) echo '^unit_contracts_test$' ;;
        # A known survivor, kept because the reason is worth stating. This is a
        # scheduling guard, not a check whose removal yields a wrong answer: it
        # is what makes the `proven_.at(...)` below it safe, by refusing a stage
        # whose call-site precondition is not proven yet. Remove it and the
        # search stops converging, so every fixture that compiles a call times
        # out rather than failing, and a timeout states nothing. Soundness here
        # is covered elsewhere -- the kernel still refuses the premise, which
        # `a_caller_that_does_not_establish_a_precondition_cannot_use_the_summary`
        # asserts -- so what survives is the liveness property, which this
        # harness cannot express as a failing test.
        call-precondition-gate) echo '^unit_contracts_test$' ;;
        *) echo '^kernel_' ;;
    esac
}

multiline_before() {
    case "$1" in
        forall-recursive-evidence)
            printf '%s' '*elimination->evidence,
                                        limits, depth + 1);
            !evidence)' ;;
        implication-recursive-evidence)
            printf '%s' '*application->evidence,
                                        limits, depth + 1);
            !evidence)' ;;
        implication-premise-evidence)
            printf '%s' '*application->premise,
                                       limits, depth + 1);
            !premise)' ;;
        transport-equality-evidence)
            printf '%s' '*transport->equality, limits, depth + 1);
            !checked)' ;;
        transport-recursive-evidence)
            printf '%s' '*transport->evidence, limits, depth + 1);
            !checked)' ;;
        arithmetic-fact-evidence)
            printf '%s' '*fact.evidence, limits, depth + 1);
                !checked)' ;;
        conditional-false-arm)
            printf '%s' 'return check_under(context, locals, assumptions, false_goal, *branch->false_case, limits, depth + 1);' ;;
        callee-body-linkage)
            printf '%s' 'if (!checked) {
            return std::unexpected("callee contract linkage failed:' ;;
        call-precondition-gate)
            printf '%s' 'if (index < stage.prefix && !std::ranges::all_of(' ;;
        conjunction-introduction-right)
            printf '%s' 'return check_under(context, locals, assumptions, *conjunction->right, *introduction->right, limits, depth + 1);' ;;
        disjunction-right-case)
            printf '%s' 'return check_under(context, locals, assumptions, from_right, *cases->right_case, limits, depth + 1);' ;;
        conjunction-elimination-evidence)
            printf '%s' '*taken->conjunction, *taken->evidence, limits, depth + 1);
            !evidence)' ;;
    esac
}

multiline_after() {
    case "$1" in
        conditional-false-arm)
            printf '%s' '(void)check_under(context, locals, assumptions, false_goal, *branch->false_case, limits, depth + 1); return {};' ;;
        call-precondition-gate)
            printf '%s' 'if (false && index < stage.prefix && !std::ranges::all_of(' ;;
        callee-body-linkage)
            printf '%s' 'if (false && !checked) {
            return std::unexpected("callee contract linkage failed:' ;;
        forall-recursive-evidence)
            printf '%s' '*elimination->evidence,
                                        limits, depth + 1);
            false && !evidence)' ;;
        implication-recursive-evidence)
            printf '%s' '*application->evidence,
                                        limits, depth + 1);
            false && !evidence)' ;;
        implication-premise-evidence)
            printf '%s' '*application->premise,
                                       limits, depth + 1);
            false && !premise)' ;;
        transport-equality-evidence)
            printf '%s' '*transport->equality, limits, depth + 1);
            false && !checked)' ;;
        transport-recursive-evidence)
            printf '%s' '*transport->evidence, limits, depth + 1);
            false && !checked)' ;;
        arithmetic-fact-evidence)
            printf '%s' '*fact.evidence, limits, depth + 1);
                false && !checked)' ;;
        conjunction-introduction-right)
            printf '%s' '(void)check_under(context, locals, assumptions, *conjunction->right, *introduction->right, limits, depth + 1); return {};' ;;
        disjunction-right-case)
            printf '%s' '(void)check_under(context, locals, assumptions, from_right, *cases->right_case, limits, depth + 1); return {};' ;;
        conjunction-elimination-evidence)
            printf '%s' '*taken->conjunction, *taken->evidence, limits, depth + 1);
            false && !evidence)' ;;
    esac
}

only=()
jobs=4
list_only=0
while [ $# -gt 0 ]; do
    case "$1" in
        --only) only+=("$2"); shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --list) list_only=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

all_names() {
    printf '%s\n' "$mutations" | cut -f1
    printf '%s\n' "${multiline_names[@]}"
}

if [ "$list_only" -eq 1 ]; then
    all_names
    exit 0
fi

# Validated here rather than inside the command substitution that collects the
# names, where a failure would exit only the subshell and be lost.
known=$(all_names)
for name in ${only+"${only[@]}"}; do
    if ! printf '%s\n' "$known" | grep -qx -- "$name"; then
        echo "no such mutation: $name" >&2
        exit 2
    fi
done

selected() {
    if [ ${#only[@]} -eq 0 ]; then
        printf '%s\n' "$known"
        return
    fi
    printf '%s\n' "${only[@]}"
}

field() {
    printf '%s\n' "$mutations" | awk -F'\t' -v n="$1" -v f="$2" '$1 == n { print $f }'
}

spec_file() {
    if field "$1" 2 | grep -q .; then field "$1" 2; else multiline_file "$1"; fi
}

spec_tests() {
    if field "$1" 5 | grep -q .; then field "$1" 5; else multiline_tests "$1"; fi
}

spec_before() {
    if field "$1" 3 | grep -q .; then field "$1" 3; else multiline_before "$1"; fi
}

spec_after() {
    if field "$1" 4 | grep -q .; then field "$1" 4; else multiline_after "$1"; fi
}

# Substitutes the single occurrence of "$2" with "$3" in the file "$1", writing
# the result to stdout. Both strings are taken literally: \Q..\E keeps every
# character of an anchor (which is C++, full of regex metacharacters) from being
# read as a pattern. -0777 reads the whole file, so an anchor may span lines.
substitute() {
    BEFORE="$2" AFTER="$3" perl -0777 -pe '
        BEGIN { $b = $ENV{BEFORE}; $a = $ENV{AFTER} }
        $n = ($_ =~ s/\Q$b\E/$a/);
        END { exit($n == 1 ? 0 : 1) }
    ' "$1"
}

# Counts occurrences of "$2" in the file "$1" as a literal string.
occurrences() {
    BEFORE="$2" perl -0777 -ne '
        BEGIN { $b = $ENV{BEFORE} }
        my $n = () = /\Q$b\E/g;
        print "$n\n";
    ' "$1"
}

names=$(selected)

# An anchor is an exact source string, so ordinary reformatting of the code it
# names silently stops that check from being tested. Saying so here costs a file
# read; finding out inside the loop costs a build and a test run.
stale=""
while IFS= read -r name; do
    [ -n "$name" ] || continue
    file=$(spec_file "$name")
    if [ ! -f "$file" ]; then
        stale="$stale $name(no $file)"
        continue
    fi
    count=$(occurrences "$file" "$(spec_before "$name")")
    if [ "$count" != "1" ]; then
        stale="$stale $name($count matches)"
    fi
done <<< "$names"

if [ -n "$stale" ]; then
    echo "Mutation anchors no longer match the source, so these checks are untested:$stale" >&2
    exit 1
fi

artifacts="$root/build/mutations"
mkdir -p "$artifacts"
run=$(mktemp -d "$artifacts/run-XXXXXX")
source_copy="$run/source"
build="$run/build"

echo "Mutation artifacts: $run"

# rsync keeps the copy cheap and leaves the checkout untouched.
rsync -a --exclude .git --exclude 'build' --exclude 'build-*' --exclude tmp \
      --exclude .code-review-graph --exclude .claude --exclude .codex \
      "$root/" "$source_copy/"

# A green control run establishes that a later test failure was introduced by
# the mutation rather than being there all along.
cmake -S "$source_copy" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCPPL_WARNINGS_AS_ERRORS=ON > "$run/configure.log" 2>&1 ||
    { echo "Control configure failed; see $run/configure.log" >&2; exit 1; }
cmake --build "$build" -j "$jobs" > "$run/build.log" 2>&1 ||
    { echo "Control build failed; see $run/build.log" >&2; exit 1; }
ctest --test-dir "$build" --output-on-failure -j "$jobs" > "$run/baseline.log" 2>&1 ||
    { echo "Control baseline failed; see $run/baseline.log" >&2; exit 1; }

# A mutation whose removal costs termination rather than soundness. The tests
# that exercise it then hang instead of failing, so it cannot be caught here.
# Each one is commented at its entry in multiline_tests with what covers the
# soundness property instead.
expected_survivor() {
    case "$1" in
        call-precondition-gate) return 0 ;;
        *) return 1 ;;
    esac
}

caught=0
total=0
survivors=""
expected=""
while IFS= read -r name <&3; do
    [ -n "$name" ] || continue
    total=$((total + 1))
    file=$(spec_file "$name")
    target="$source_copy/$file"
    log_dir="$run/$name"
    mkdir -p "$log_dir"
    cp "$target" "$log_dir/original"

    substitute "$target" "$(spec_before "$name")" "$(spec_after "$name")" > "$log_dir/mutated"
    diff -u "$log_dir/original" "$log_dir/mutated" > "$log_dir/mutation.diff" || true
    cp "$log_dir/mutated" "$target"

    # stdin is redirected away from these because the loop reads the remaining
    # mutation names from it, and a child that consumes it ends the run early.
    outcome="unknown"
    if ! cmake --build "$build" -j "$jobs" > "$log_dir/build.log" 2>&1 < /dev/null; then
        outcome="build-error"
    else
        status=0
        ctest --test-dir "$build" --output-on-failure --timeout 300 \
              -R "$(spec_tests "$name")" > "$log_dir/tests.log" 2>&1 < /dev/null || status=$?
        # CTest uses 8 for ordinary test failures. A crash, a timeout, or an
        # empty selection is an error in the experiment, not a detection.
        if [ "$status" -eq 0 ] && ! grep -q "No tests were found" "$log_dir/tests.log"; then
            outcome="survived"
        elif [ "$status" -eq 8 ] && grep -q '\*\*\*Failed' "$log_dir/tests.log" &&
             ! grep -qE '\*\*\*(Exception|Timeout|Not Run)' "$log_dir/tests.log"; then
            outcome="caught"
        else
            outcome="test-error"
        fi
    fi

    cp "$log_dir/original" "$target"
    if [ "$outcome" = "caught" ]; then
        caught=$((caught + 1))
    elif expected_survivor "$name"; then
        # Recorded above with the reason. Listed, never counted as caught, and
        # never a reason for the run to fail: a guard whose removal costs
        # termination rather than soundness cannot be stated as a failing test.
        expected="$expected $name"
        outcome="$outcome (expected)"
    else
        survivors="$survivors $name($outcome)"
    fi
    echo "$name: $outcome"
done 3<<< "$names"

# Leaves no runnable mutated compiler behind.
cmake --build "$build" -j "$jobs" > "$run/restored-build.log" 2>&1 ||
    { echo "Restoring the unmutated build failed; see $run/restored-build.log" >&2; exit 1; }

echo "$caught/$total mutations caught"
if [ -n "$expected" ]; then
    echo "expected survivors (see multiline_tests for what covers each):$expected"
fi
if [ -n "$survivors" ]; then
    echo "not caught:$survivors" >&2
    exit 1
fi
