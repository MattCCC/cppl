#!/usr/bin/env bash
# Runs a script's independent cases side by side. Sourced, never run.
#
# A case is a command whose inputs no other case writes and whose outputs no
# other case reads: its own copies, its own logs, its own objects. Running such
# cases at once changes when each one runs and nothing else. Every case still
# runs to the same checks, and the script still sees their outcomes in the order
# it started them: what a case printed is replayed in that order, and the first
# case to fail ends the script with its own messages and status, exactly as it
# would have run alone. Cases started after it may already have run; what they
# printed is dropped, since the script would not have reached them.
#
#   cases_begin <directory>     where each case's output and status are kept
#   case_run <command> [args]   starts one case, in a subshell, under set -e
#   cases_end                   waits for every case still running
#
# A case runs in a subshell, so a variable it sets is lost: a script counts
# its cases where it starts them. CPPL_TEST_JOBS sets how many run at once,
# by default one per online processor. Each is one compiler invocation on one
# small unit, so running a processor's worth of them beside CTest's own jobs
# costs time slices, not memory.
#
# Written for the bash 3.2 macOS ships: no `wait -n`, and every array that may
# be empty is expanded with a guard, since `set -u` rejects an empty one there.

parallel_jobs() {
    local jobs="${CPPL_TEST_JOBS:-}"
    if [ -z "$jobs" ]; then
        jobs=$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 1)
    fi
    case "$jobs" in
        '' | *[!0-9]* | 0) jobs=1 ;;
    esac
    printf '%s\n' "$jobs"
}

cases_begin() {
    cases_dir="$1"
    cases_jobs=$(parallel_jobs)
    cases_started=0
    cases_pids=()
    cases_names=()
    mkdir -p "$cases_dir"
}

# A case's status is written, by rename, only once it has ended, so a status
# file that exists is complete. The case itself runs with errexit on and in no
# conditional context, which would switch errexit off inside it.
case_run() {
    local name="$cases_dir/$cases_started"
    cases_started=$((cases_started + 1))
    (
        set +e
        (
            set -e
            "$@"
        ) > "$name.out" 2> "$name.err" < /dev/null
        echo "$?" > "$name.status.part"
        mv "$name.status.part" "$name.status"
    ) &
    cases_pids+=("$!")
    cases_names+=("$name")
    cases_report
    while [ "$(cases_running)" -ge "$cases_jobs" ]; do
        sleep 0.02
        cases_report
    done
}

cases_running() {
    local name running=0
    for name in ${cases_names[@]+"${cases_names[@]}"}; do
        [ -e "$name.status" ] || running=$((running + 1))
    done
    printf '%s\n' "$running"
}

# Reports, oldest first, every case that has ended before the first that has
# not. A case that failed ends the script, and the cases still running are
# stopped, since their outcomes can no longer be reported.
cases_report() {
    local name status
    while [ "${#cases_names[@]}" -gt 0 ] && [ -e "${cases_names[0]}.status" ]; do
        name="${cases_names[0]}"
        status=$(cat "$name.status")
        cases_pids=(${cases_pids[@]+"${cases_pids[@]:1}"})
        cases_names=(${cases_names[@]+"${cases_names[@]:1}"})
        cat "$name.out"
        cat "$name.err" >&2
        if [ "$status" != 0 ]; then
            if [ "${#cases_pids[@]}" -gt 0 ]; then
                kill "${cases_pids[@]}" 2> /dev/null || true
            fi
            case "$status" in
                '' | *[!0-9]*) exit 1 ;;
            esac
            exit "$status"
        fi
    done
}

cases_end() {
    while [ "${#cases_names[@]}" -gt 0 ]; do
        sleep 0.02
        cases_report
    done
}
