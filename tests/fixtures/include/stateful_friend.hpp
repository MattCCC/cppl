// Stateful templates, shared by the matched pairs of
// `negative/proof_instantiation.sh` (SPEC.md ERASE-019).
//
// Instantiating `Set<N>` defines the friend `adl(Tag<N>)`, which `Tag<N>` only
// declares, and `defined<N>()` asks anew at each use whether that has happened
// yet: each use has a default argument of a type of its own. A unit whose
// proof-only text instantiates `Set<0>` before its `E::A` is initialized would
// verify `E::A` as 1 and run it as 0.
#pragma once

#include <type_traits>

template <int N> struct Tag {
    friend constexpr int adl(Tag<N>);
};

template <int N> struct Set {
    int unused;
    enum class Limit : unsigned { value = 10u };
    friend constexpr int adl(Tag<N>) {
        return 1;
    }
};

template <int N, class Fresh = decltype([] {})> consteval bool defined() {
    return requires { typename std::integral_constant<int, adl(Tag<N>{})>; };
}
