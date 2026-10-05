// Ordinary C++ naming types after C++L specifiers, then declaring with the
// type before the decl-specifiers C++ admits after it, an operator function and
// an explicit specialization (SPEC.md 3.1, WORD-008).
//
// None of these declarations states a return type after the word, so none is a
// C++L function: `verified const c{};` declares `c`, `verified static s;` a
// variable `s`, and `verified operator*(...)` an operator returning a
// `verified`.
#include <cstdio>

struct verified {
    int v = 1;
};
struct pure {
    int v = 2;
};
struct unsafe {
    int v = 3;
};

verified const constant{};
pure const pure_constant{};
verified volatile volatile_value{};
unsafe const unsafe_constant{};
verified(parenthesized){};
pure(assigned) = {};
verified array[2]{};
verified (*pointer_to_function)() = nullptr;
verified static internal;
verified constexpr compile_time{};
verified typedef alias;
verified extern external;
verified external{};
pure static pure_internal;
pure typedef pure_alias;
verified thread_local per_thread;

verified constexpr make_constexpr() {
    return {4};
}
verified inline make_inline() {
    return {5};
}
pure static make_pure_static() {
    return {6};
}

template <class T> verified template_function(T) {
    return {};
}
template <> verified template_function<int>(int) {
    return {7};
}

verified operator*(verified a, verified b) {
    return {a.v * b.v + 1};
}
bool operator<(verified a, verified b) {
    return a.v < b.v;
}
verified operator&(verified a, verified b) {
    return {a.v & b.v};
}
pure operator+(pure a, pure b) {
    return {a.v + b.v};
}

struct Holder {
    verified mutable cached{};
    verified static shared;
    verified virtual made() const {
        return {8};
    }
    verified inline inline_member() const {
        return {9};
    }
    pure static pure_member() {
        return {10};
    }
    virtual ~Holder() = default;
};
verified Holder::shared{};

int main() {
    verified const local_constant{};
    pure const local_pure{};
    verified static local_static;
    verified a{3}, b{4};
    pure c{5}, d{6};
    alias aliased{};
    pure_alias pure_aliased{};
    const Holder holder{};
    std::printf("%d %d %d %d %d %d %d %d\n", constant.v, pure_constant.v, unsafe_constant.v, parenthesized.v,
                assigned.v, array[1].v, pointer_to_function == nullptr, internal.v);
    std::printf("%d %d %d %d %d %d %d\n", compile_time.v, aliased.v, external.v, pure_internal.v, pure_aliased.v,
                per_thread.v, volatile_value.v);
    std::printf("%d %d %d %d %d\n", make_constexpr().v, make_inline().v, make_pure_static().v, template_function(1.0).v,
                template_function(1).v);
    std::printf("%d %d %d %d\n", (a * b).v, a < b, (a & b).v, (c + d).v);
    std::printf("%d %d %d %d %d\n", holder.cached.v, Holder::shared.v, holder.made().v, holder.inline_member().v,
                Holder::pure_member().v);
    std::printf("%d %d %d\n", local_constant.v, local_pure.v, local_static.v);
    return 0;
}
