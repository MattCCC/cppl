#!/usr/bin/env bash
set -euo pipefail

CPPL="$1"
WORK="$3"
export CPPL_TEST_CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/parallel-verification.XXXXXX")
export CPPL_TEST_SYNC="$run"

cat > "$run/input.cpp" <<'CPP'
verified unsigned value() ensures (result == VALUE) { return VALUE; }
int main() { return static_cast<int>(value()); }
CPP

cat > "$run/clang-wrapper" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
preprocess=false
for argument in "$@"; do
    if [ "$argument" = '-E' ]; then preprocess=true; fi
done
await_file() {
    for ((attempt=0; attempt<150; ++attempt)); do
        if [ -f "$1" ]; then return; fi
        sleep 0.1
    done
    echo 'timed out coordinating compiler invocations' >&2
    exit 1
}
if "$preprocess"; then
    if [ "$CPPL_TEST_LANE" = second ]; then
        await_file "$CPPL_TEST_SYNC/first.ready"
    fi
    "$CPPL_TEST_CLANG" "$@"
    touch "$CPPL_TEST_SYNC/$CPPL_TEST_LANE.ready"
    if [ "$CPPL_TEST_LANE" = first ]; then
        await_file "$CPPL_TEST_SYNC/second.ready"
    fi
else
    exec "$CPPL_TEST_CLANG" "$@"
fi
SH
chmod +x "$run/clang-wrapper"

CPPL_TEST_LANE=first "$CPPL" -std=c++17 -DVALUE=7u "$run/input.cpp" \
    "--cppl-clang=$run/clang-wrapper" -o "$run/first" > "$run/first.log" 2>&1 &
first_pid=$!
CPPL_TEST_LANE=second "$CPPL" -std=c++17 -DVALUE=9u "$run/input.cpp" \
    "--cppl-clang=$run/clang-wrapper" -o "$run/second" > "$run/second.log" 2>&1 &
second_pid=$!
status=0
wait "$first_pid" || status=1
wait "$second_pid" || status=1
if [ "$status" -ne 0 ]; then
    tail -30 "$run/first.log" "$run/second.log" >&2
    exit 1
fi

first_status=0
second_status=0
"$run/first" || first_status=$?
"$run/second" || second_status=$?
if [ "$first_status" -ne 7 ] || [ "$second_status" -ne 9 ]; then
    echo "crossed compilation results: $first_status, $second_status; expected 7, 9" >&2
    exit 1
fi
echo 'parallel verification preserves the executable of each invocation'
