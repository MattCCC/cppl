#!/usr/bin/env bash
# TRUST.md TCB-REPRO-001; docs/INSTALL.md
#
# A user installs C++L and uses it without the repository.
#
# The build is installed to a fresh prefix, the prefix is moved somewhere else,
# and from there, in an environment that holds nothing but a minimal PATH and
# its own home and temporary directories, the installed tools, which name no
# file of the source or build tree, must:
#
#   - print the release record, with one Clang release for analysis and code;
#   - compile ordinary C++ and verified C++L into programs that run;
#   - refuse a false Law, with the kernel's reason, and produce no program;
#   - format a file with the canonical C++L style, which the formatter carries
#     rather than reading from the source tree;
#   - serve an editor: initialize, publish the same refusal for an open
#     document, shut down and exit cleanly, over standard input and output.
#
# The release archive CPack writes is then checked the same way a download
# would be: its SHA-256 file matches it, and it holds exactly what the install
# holds.
set -euo pipefail
export LC_ALL=C

CMAKE="$1"
CPACK="$2"
BUILD="$3"
SOURCE="$4"
WORK="$5"

fail() {
    echo "$1" >&2
    exit 1
}

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/installed-package.XXXXXX")
mkdir -p "$run/home" "$run/tmp" "$run/project"

# Installed, then moved: nothing may depend on where it was installed.
"$CMAKE" --install "$BUILD" --prefix "$run/staged" --strip > "$run/install.log"
mv "$run/staged" "$run/prefix"
prefix="$run/prefix"
for tool in cppl cppl-lsp cppl-format; do
    [ -x "$prefix/bin/$tool" ] || fail "the install holds no executable '$tool'"
done
[ -f "$prefix/share/doc/cppl/INSTALL.md" ] || fail "the install holds no installation guide"

# Nothing installed names the tree it was built from, so nothing installed can
# read a file of it: a style, a header, a model, a script.
for tool in cppl cppl-lsp cppl-format; do
    for tree in "$SOURCE" "$BUILD"; do
        if grep -aFq "$tree" "$prefix/bin/$tool"; then
            fail "the installed $tool names '$tree', the tree it was built in"
        fi
    done
done

# A clean environment: no LLVM on PATH, no library path, no compiler flags.
clean() {
    (cd "$run/project" && env -i HOME="$run/home" TMPDIR="$run/tmp" PATH=/usr/bin:/bin LC_ALL=C "$@")
}

# The release record.
clean "$prefix/bin/cppl" --cppl-version > "$run/version" 2> "$run/version.err" ||
    fail "the installed cppl cannot print its release record: $(cat "$run/version.err")"
[ ! -s "$run/version.err" ] || fail "the installed cppl warned about its toolchain: $(cat "$run/version.err")"
analysis=$(sed -n 's/^Clang: *//p' "$run/version")
driver=$(sed -n 's/^Clang driver: *//p' "$run/version")
[ -n "$analysis" ] && [ "$analysis" = "$driver" ] ||
    fail "the installed cppl pairs libclang '$analysis' with the driver '$driver'"
grep -Eq '^Target: +[^ ]+-[^ ]+$' "$run/version" || fail "the installed cppl names no target"

# Ordinary C++.
cat > "$run/project/hello.cpp" << 'EOF'
#include <iostream>

int main() {
    std::cout << "hello\n";
    return 0;
}
EOF
clean "$prefix/bin/cppl" -std=c++20 hello.cpp -o hello || fail "the installed cppl did not compile ordinary C++"
[ "$(clean ./hello)" = hello ] || fail "the ordinary program the installed cppl compiled does not run as written"

# Verified C++L.
cat > "$run/project/verified.cpp" << 'EOF'
#include <iostream>

pure unsigned twice(unsigned x) {
    return x + x;
}

law twice_is_a_sum(unsigned x)
    proves (twice(x) == x + x);

type Small = unsigned where (self < 10u);

verified Small clamp(unsigned x)
    ensures (result < 10u)
{
    if (x < 10u) {
        return x;
    }
    return 9u;
}

int main() {
    std::cout << twice(21u) << ' ' << clamp(3u) << ' ' << clamp(40u) << '\n';
    return 0;
}
EOF
clean "$prefix/bin/cppl" -std=c++20 verified.cpp -o verified --cppl-trust-report > "$run/report" ||
    fail "the installed cppl did not verify a correct C++L program"
grep -Eq '^Laws proven: +1$' "$run/report" || fail "the installed cppl did not prove the Law"
grep -Eq '^Function contracts proven: +1$' "$run/report" || fail "the installed cppl did not prove the contract"
grep -Eq '^Unresolved obligations: +0$' "$run/report" || fail "the installed cppl left an obligation unresolved"
[ "$(clean ./verified)" = '42 3 9' ] || fail "the verified program does not run as written"

# A false Law is refused, with the kernel's reason, and no program is written.
cat > "$run/project/false_law.cpp" << 'EOF'
pure unsigned add_one(unsigned x) {
    return x + 1u;
}

law add_one_changes_nothing(unsigned x)
    proves (add_one(x) == x);

int main() {
    return 0;
}
EOF
if clean "$prefix/bin/cppl" false_law.cpp -o false_law 2> "$run/false.err"; then
    fail "the installed cppl accepted a false Law"
fi
grep -q "law 'add_one_changes_nothing' is not proven" "$run/false.err" ||
    fail "the installed cppl refused the false Law without saying why: $(cat "$run/false.err")"
grep -q 'status UNRESOLVED' "$run/false.err" || fail "the refusal does not report the obligation UNRESOLVED"
[ ! -e "$run/project/false_law" ] || fail "the installed cppl wrote a program for a false Law"

# Formatting, in the canonical style the formatter carries.
printf 'verified int f(int x) ensures (result >= 0) {\n    return x < 0 ? 0 : x;\n}\n' > "$run/project/format.cpp"
if clean "$prefix/bin/cppl-format" --check format.cpp 2> /dev/null; then
    fail "the installed cppl-format accepted a clause on its declaration's line"
fi
clean "$prefix/bin/cppl-format" -i format.cpp || fail "the installed cppl-format could not format a file"
clean "$prefix/bin/cppl-format" --check format.cpp || fail "the installed cppl-format does not accept its own output"
expected_format=$'verified int f(int x)\n    ensures (result >= 0)\n{\n    return x < 0 ? 0 : x;\n}'
[ "$(cat "$run/project/format.cpp")" = "$expected_format" ] ||
    fail "the installed cppl-format wrote $(cat "$run/project/format.cpp")"

# The language server, over standard input and output.
coproc LSP { clean "$prefix/bin/cppl-lsp" 2> "$run/lsp.err"; }
lsp_in=${LSP[1]}
lsp_out=${LSP[0]}

send() {
    printf 'Content-Length: %d\r\n\r\n%s' "${#1}" "$1" >&"$lsp_in"
}

# Reads one message into MESSAGE, giving up after a minute.
receive() {
    local line length=""
    while IFS= read -r -t 60 line <&"$lsp_out"; do
        line=${line%$'\r'}
        if [ -z "$line" ]; then
            break
        fi
        case "$line" in
            Content-Length:*) length=${line#Content-Length: } ;;
        esac
    done
    [ -n "$length" ] || return 1
    IFS= read -r -N "$length" -t 60 MESSAGE <&"$lsp_out" || return 1
    printf '%s\n' "$MESSAGE" >> "$run/lsp.messages"
}

# Reads until a message matches the extended regular expression.
await() {
    local pattern="$1" what="$2"
    while receive; do
        if printf '%s' "$MESSAGE" | grep -Eq "$pattern"; then
            return 0
        fi
    done
    fail "the installed cppl-lsp never sent $what; it sent $(cat "$run/lsp.messages" 2> /dev/null) and said $(cat "$run/lsp.err")"
}

root="file://$run/project"
send '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"'"$root"'","capabilities":{}}}'
await '"id": ?1[,}].*"capabilities"|"capabilities".*"id": ?1[,}]' 'its capabilities'
printf '%s' "$MESSAGE" | grep -q '"textDocumentSync"' || fail "the installed cppl-lsp offers no document sync"
send '{"jsonrpc":"2.0","method":"initialized","params":{}}'

text=$(sed 's/\\/\\\\/g; s/"/\\"/g' "$run/project/false_law.cpp" | awk '{ printf "%s\\n", $0 }')
send '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"'"$root"'/false_law.cpp","languageId":"cpp","version":1,"text":"'"$text"'"}}}'
await 'publishDiagnostics.*add_one_changes_nothing.*is not proven' 'the kernel refusal for the open document'

send '{"jsonrpc":"2.0","id":2,"method":"shutdown"}'
await '"id": ?2[,}]' 'a response to shutdown'
send '{"jsonrpc":"2.0","method":"exit"}'
exec {lsp_in}>&-
lsp_status=0
wait "$LSP_PID" || lsp_status=$?
[ "$lsp_status" -eq 0 ] || fail "the installed cppl-lsp exited with status $lsp_status after shutdown and exit"

# The release archive: its checksum, and what it holds.
"$CPACK" --config "$BUILD/CPackConfig.cmake" -B "$run/package" > "$run/cpack.log" ||
    fail "CPack could not write the release archive: $(cat "$run/cpack.log")"
archives=("$run/package"/cppl-*.tar.gz)
[ "${#archives[@]}" -eq 1 ] && [ -f "${archives[0]}" ] || fail "CPack did not write exactly one archive"
archive=${archives[0]}
[ -f "$archive.sha256" ] || fail "the archive has no SHA-256 beside it"
recorded=$(awk '{ print $1 }' "$archive.sha256")
actual=$("$CMAKE" -E sha256sum "$archive" | awk '{ print $1 }')
[ "$recorded" = "$actual" ] || fail "the archive's recorded SHA-256 is $recorded, and it has $actual"
grep -Fq "$(basename "$archive")" "$archive.sha256" || fail "the checksum file does not name the archive"

mkdir "$run/unpacked"
(cd "$run/unpacked" && "$CMAKE" -E tar xzf "$archive")
unpacked=("$run/unpacked"/*)
[ "${#unpacked[@]}" -eq 1 ] || fail "the archive does not hold one top-level directory"
(cd "$prefix" && find . -type f | sort) > "$run/installed.files"
(cd "${unpacked[0]}" && find . -type f | sort) > "$run/archived.files"
cmp -s "$run/installed.files" "$run/archived.files" || {
    diff "$run/installed.files" "$run/archived.files" >&2 || true
    fail "the archive and the install hold different files"
}
clean "${unpacked[0]}/bin/cppl" --cppl-version > "$run/archived.version" ||
    fail "the cppl in the archive cannot print its release record"
cmp -s "$run/version" "$run/archived.version" || fail "the archive's cppl and the installed one print different records"

echo 'installed and archived C++L compile, verify, refuse, format and serve an editor from a clean environment'
