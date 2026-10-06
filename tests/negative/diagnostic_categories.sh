#!/usr/bin/env bash
# SPEC: REFINE-019, STDMODEL-020, FORALL-001
# A refusal names what happened (AGENTS.md 35): only Clang's own diagnostics are
# C++ errors.
#
# The Clang bridge refuses some C++ that Clang accepted, because it lies outside
# the fragment this implementation models: an ordinary function that returns a
# refined value, refined storage built outside a verified body, a refinement
# written as a template argument, a type spelled so that a refinement in it
# cannot be followed. Each is `[unsupported-semantics]` in what the compiler
# prints, never `[cpp-semantic]`, which says Clang rejected the program. A real
# C++ error in a C++L unit stays `[cpp-semantic]`. The editor reads the same
# category (unit/lsp_server_test.cpp).
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/diagnostic-categories.XXXXXX")

# refused <name> <expected line>...: the unit read from stdin is refused, and
# prints each expected line exactly. It is compiled from its own directory, so
# each location begins with the file's own name.
refused() {
    local name="$1"
    shift
    cat > "$run/$name.cpp"
    if (cd "$run" && "$CPPL" -std=c++20 "$name.cpp" -o "$name") > "$run/$name.log" 2>&1; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    local line
    for line in "$@"; do
        if ! grep -Fqx -- "$line" "$run/$name.log"; then
            echo "$name did not report, exactly: $line" >&2
            cat "$run/$name.log" >&2
            exit 1
        fi
    done
}

# not_a_cpp_error <name>: nothing the unit was refused for is called a C++ error.
not_a_cpp_error() {
    if grep -q '\[cpp-semantic\]' "$run/$1.log"; then
        echo "$1 was refused for C++ that Clang accepted, reported as a C++ error:" >&2
        cat "$run/$1.log" >&2
        exit 1
    fi
}

# An ordinary function returning a refined member, and that member's storage
# built where no verified body checks it: well-formed C++, refused because no
# unverified refinement boundary is modeled (SPEC.md REFINE-019).
boundary="error [unsupported-semantics]: storage 'b' uses refinement 'Cap' outside a modeled verified body, \
where ordinary C++ could establish it without proof; a verified body checks its own construction and writes, but \
an unverified construction boundary is not yet checked"
refused unverified_boundary \
    "unverified_boundary.cpp:3:5: error [unsupported-semantics]: ordinary function 'make' return cannot establish \
refinement 'Cap'; verify its definition (explicit trusted refinement boundaries are not implemented)" \
    "unverified_boundary.cpp:3:18: $boundary" \
    "unverified_boundary.cpp:4:18: $boundary" <<'CPP'
type Cap = unsigned where (self <= 100u);
struct Box { Cap c; };
Box make() { Box b{1u}; return b; }
int main() { Box b = make(); return 0; }
CPP
not_a_cpp_error unverified_boundary

# A refinement written as a user template's argument (SPEC.md STDMODEL-020).
refused template_argument \
    "template_argument.cpp:4:19: error [unsupported-semantics]: refinement 'Positive' is written as a template \
argument of 'Box', whose instantiation holds it as its base type, where nothing charges its predicate; a \
refinement is a template argument only where the sequence model states its elements (SPEC.md STDMODEL-020)" <<'CPP'
type Positive = int where (self > 0);
template <class T> struct Box { T value; };
verified int boxed(int raw) ensures (result == raw) {
    Box<Positive> box{raw};
    return box.value;
}
int main() { return 0; }
CPP
not_a_cpp_error template_argument

# A parameter whose type may name a refinement through `decltype`, which this
# implementation does not follow (SPEC.md FORALL-001, STDMODEL-020).
refused hidden_refinement \
    "hidden_refinement.cpp:3:48: error [unsupported-semantics]: a type written as 'decltype(make_small())', which \
may name a refinement through a spelling this implementation does not follow; write the refinement or its base type \
directly" <<'CPP'
type Small = unsigned where (self < 10u);
verified Small make_small() ensures (result == 3u) { return 3u; }
verified unsigned twice(decltype(make_small()) s) ensures (result == s + s) { return s + s; }
int main() { return 0; }
CPP
not_a_cpp_error hidden_refinement

# The twin: a C++ error Clang reports in a C++L unit is a C++ error.
refused cpp_error <<'CPP'
type Cap = unsigned where (self <= 100u);
verified Cap clamp(unsigned x) ensures (result <= 100u) {
    if (x > 100u) {
        return 100u;
    }
    return x;
}
int main() {
    int broken = "not an int";
    return static_cast<int>(clamp(5u)) + broken;
}
CPP
if ! grep -Eq "^cpp_error\.cpp:9:[0-9]+: error \[cpp-semantic\]: .*'int'" "$run/cpp_error.log"; then
    echo "a C++ error in a C++L unit was not reported as one:" >&2
    cat "$run/cpp_error.log" >&2
    exit 1
fi
if grep -q '\[unsupported-semantics\]' "$run/cpp_error.log"; then
    echo "a C++ error was reported as a refusal of C++ outside the modeled fragment:" >&2
    cat "$run/cpp_error.log" >&2
    exit 1
fi

echo 'every refusal of C++ that Clang accepted says so, and a C++ error is still a C++ error'
