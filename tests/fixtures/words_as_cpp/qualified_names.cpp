// Ordinary C++ naming namespaces after C++L words (SPEC.md 3.1, WORD-008).
//
// A word followed by `::` is the first component of a nested-name-specifier,
// whatever the word means elsewhere. Each namespace below declares an `inner`
// whose value differs from the global one, so a word misread as a specifier --
// `pure::inner g()` read as a pure `::inner g()` -- changes what the program
// prints rather than failing to compile.
#include <cstdio>

struct inner {
    int v = 1;
};

namespace law {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace law
namespace proof {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace proof
namespace pure {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
int value = 4;
} // namespace pure
namespace verified {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
using R = unsigned;
} // namespace verified
namespace unsafe {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace unsafe
namespace ghost {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace ghost
namespace trusted {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace trusted
namespace type {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace type
namespace cases {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace cases
namespace decompose {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace decompose
namespace contradiction {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace contradiction
namespace ensures {
struct inner {
    int v = 2;
};
inline int f() {
    return 3;
}
} // namespace ensures

using R = unsigned char;

// Return types, with and without the ordinary specifiers that may precede one.
pure::inner pure_returned() {
    return {};
}
static pure::inner pure_static() {
    return {};
}
inline pure::inner pure_inline() {
    return {};
}
constexpr pure::inner pure_constexpr() {
    return {};
}
template <class T> pure::inner pure_template(T) {
    return {};
}
pure ::inner pure_spaced() {
    return {};
}
pure::inner* pure_pointer() {
    static pure::inner kept;
    return &kept;
}
verified::inner verified_returned() {
    return {};
}
verified ::inner verified_spaced() {
    return {};
}
static verified::inner verified_static() {
    return {};
}
// `verified::R` is `unsigned`, where the global `R` would truncate to 44.
verified::R verified_scalar(unsigned x) {
    return x;
}
unsafe::inner unsafe_returned() {
    return {};
}
static unsafe::inner unsafe_static() {
    return {};
}
ghost::inner ghost_returned() {
    return {};
}
law::inner law_returned() {
    return {};
}
proof::inner proof_returned() {
    return {};
}
trusted::inner trusted_returned() {
    return {};
}
type::inner type_returned() {
    return {};
}
ensures::inner ensures_returned() {
    return {};
}

struct Holder {
    pure::inner member() const {
        return {};
    }
    static pure::inner static_member() {
        return {};
    }
    static verified::inner verified_member() {
        return {};
    }
    unsafe::inner unsafe_member() const {
        return {};
    }
};

// Variables of those types.
pure::inner pure_variable;
verified::inner verified_variable;
unsafe::inner unsafe_variable;
ghost::inner ghost_variable;

int main() {
    // Locals and calls in a block, where a statement may begin.
    pure::inner pure_local;
    verified::inner verified_local;
    unsafe::inner unsafe_local;
    ghost::inner ghost_local;
    cases::inner cases_local{};
    decompose::inner decompose_local{};
    contradiction::inner contradiction_local;
    pure::f();
    verified::f();
    unsafe::f();
    ghost::f();
    cases::f();
    int called = pure::f() + verified::f() + unsafe::f() + ghost::f() + cases::f() + decompose::f() +
                 contradiction::f() + law::f() + proof::f() + trusted::f() + type::f() + ensures::f();
    static pure::inner pure_static_local;

    const Holder holder;
    std::printf("%d %d %d %d %d %d %d\n", pure_returned().v, pure_static().v, pure_inline().v, pure_constexpr().v,
                pure_template(0).v, pure_spaced().v, pure_pointer()->v);
    std::printf("%d %d %d %u\n", verified_returned().v, verified_spaced().v, verified_static().v,
                static_cast<unsigned>(verified_scalar(300u)));
    std::printf("%d %d %d %d %d %d %d %d\n", unsafe_returned().v, unsafe_static().v, ghost_returned().v,
                law_returned().v, proof_returned().v, trusted_returned().v, type_returned().v, ensures_returned().v);
    std::printf("%d %d %d %d\n", holder.member().v, Holder::static_member().v, Holder::verified_member().v,
                holder.unsafe_member().v);
    std::printf("%d %d %d %d\n", pure_variable.v, verified_variable.v, unsafe_variable.v, ghost_variable.v);
    std::printf("%d %d %d %d %d %d %d %d\n", pure_local.v, verified_local.v, unsafe_local.v, ghost_local.v,
                cases_local.v, decompose_local.v, contradiction_local.v, pure_static_local.v);
    std::printf("%d %d\n", called, pure::value);
    return 0;
}
