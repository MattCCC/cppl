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
spend-dependency-proven	compiler/automation/src/composition.cpp	dependency.has_value() && !proven_.contains(*dependency)	false && (dependency.has_value() && !proven_.contains(*dependency))	^unit_contracts_test$
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
        call-precondition-gate) echo '^unit_contracts_test$|^negative_verified_calls$|^negative_verified_paths$' ;;
        *) echo '^kernel_' ;;
    esac
}

# A mutation whose removal leaves externally observable behavior
# indistinguishable: the same refusal, the same termination, no crash, and no
# proof that was not there before.
#
# This is a claim about the program, justified by naming the enforcement that
# still holds the invariant. It is never a way to record that this harness
# cannot observe a difference -- a search that stops terminating, or that
# crashes, has observably different behavior and is killed, not equivalent.
#
# Each entry names the tests that state the invariant directly, so removing
# every enforcement of it is still caught.
equivalent_justification() {
    case "$1" in
        # `Composition::spend` is charged against the dependency whose evidence
        # is read on the next line, and refuses an unproven one there. The gate
        # refuses the same stage earlier. With both present, removing the gate
        # changes when the search stops, never whether it stops or what it
        # concludes: it refuses, terminates, and builds no proof either way.
        #
        # Stated directly in tests/unit/contracts_test.cpp by
        # an_unproven_dependency_is_refused_before_its_evidence_is_read, so
        # removing spend's check -- the enforcement that remains -- fails that
        # test rather than going unnoticed.
        call-precondition-gate)
            echo "Composition::spend refuses the same unproven dependency before reading its evidence;" \
                 "that enforcement is itself mutated as 'spend-dependency-proven', which is caught" ;;
        *) return 1 ;;
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
# A hard backstop only. The in-process transition budget is what should stop a
# search that cannot make progress, and it reports an ordinary failure when it
# does. This exists so a path nothing budgets still ends the run on CI instead
# of hanging it, and so such a path is visible as 'killed (nontermination)'
# rather than passing silently.
ctest_timeout=120
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

caught=0
total=0
survivors=""
equivalents=""
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
        ctest --test-dir "$build" --output-on-failure --timeout "$ctest_timeout" \
              -R "$(spec_tests "$name")" > "$log_dir/tests.log" 2>&1 < /dev/null || status=$?
        justification=""
        # CTest uses 8 for ordinary test failures. An empty selection tests
        # nothing, so it is an error in the experiment rather than a result.
        if grep -qE '\*\*\*(Timeout|Exception)' "$log_dir/tests.log"; then
            # Turning a terminating program into one that does not terminate,
            # or into one that crashes, is a change in required behavior. It
            # kills the mutation. The in-process budget should reach this first
            # and report an ordinary failure; arriving here instead means some
            # path is still unbudgeted, which is worth seeing rather than
            # hiding.
            outcome="killed (nontermination)"
        elif [ "$status" -eq 0 ] && ! grep -q "No tests were found" "$log_dir/tests.log"; then
            # Passing under mutation is only acceptable where the mutated
            # program's required behavior is genuinely indistinguishable, and
            # the justification has to name what still enforces the invariant.
            if justification=$(equivalent_justification "$name"); then
                outcome="equivalent"
            else
                outcome="survived"
            fi
        elif [ "$status" -eq 8 ] && grep -q '\*\*\*Failed' "$log_dir/tests.log" &&
             ! grep -q '\*\*\*Not Run' "$log_dir/tests.log"; then
            outcome="caught"
        else
            outcome="test-error"
        fi
    fi

    cp "$log_dir/original" "$target"
    case "$outcome" in
        caught|killed*)
            caught=$((caught + 1))
            echo "$name: $outcome" ;;
        equivalent)
            equivalents="$equivalents $name"
            echo "$name: equivalent -- $justification" ;;
        *)
            survivors="$survivors $name($outcome)"
            echo "$name: $outcome" ;;
    esac
done 3<<< "$names"

# Leaves no runnable mutated compiler behind.
cmake --build "$build" -j "$jobs" > "$run/restored-build.log" 2>&1 ||
    { echo "Restoring the unmutated build failed; see $run/restored-build.log" >&2; exit 1; }

echo "$caught/$total mutations caught"
if [ -n "$equivalents" ]; then
    echo "equivalent (justified at equivalent_justification):$equivalents"
fi
if [ -n "$survivors" ]; then
    # Every mutation ends in exactly one of: caught, killed (nontermination), or
    # equivalent. A survivor is none of those, and means some rule of the proof
    # system is going untested.
    echo "not caught:$survivors" >&2
    exit 1
fi
