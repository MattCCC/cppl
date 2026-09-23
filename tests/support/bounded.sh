#!/usr/bin/env bash
# Runs one command under hard resource limits: `bounded.sh command [args...]`.
#
# A test that goes wrong must fail, never exhaust the machine running it. A
# compiler stuck writing to a redirected stream once grew one file in the build
# tree to hundreds of gigabytes, and a process orphaned by an interrupted run
# keeps going after CTest has stopped waiting for it. So no file may grow past
# the cap below - the writer is stopped with SIGXFSZ - and no process may use
# more CPU time than the budget below - it is stopped with SIGXCPU. Both limits
# are inherited by everything the command starts, and a limit already lower is
# kept.
set -euo pipefail

# 256 MiB per file, in bash's 1024-byte units. No test writes anything close.
readonly max_file_kib=262144
# Far past the slowest test's own timeout, so only a runaway reaches it.
readonly max_cpu_seconds=1800

lower() {
    local flag="$1" value="$2" current
    current=$(ulimit -S "$flag")
    if [ "$current" = unlimited ] || [ "$current" -gt "$value" ]; then
        ulimit "$flag" "$value"
    fi
}

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        # Windows has no per-process file-size or CPU limit bash can set; the
        # CTest timeout is the only bound there.
        ;;
    *)
        lower -f "$max_file_kib"
        lower -t "$max_cpu_seconds"
        ;;
esac

exec "$@"
