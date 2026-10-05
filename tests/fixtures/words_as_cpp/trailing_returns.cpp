// Ordinary C++ functions whose trailing return types name types after contract
// clauses (SPEC.md 3.1, GRAMMAR.md 43).
//
// A trailing return type names its type first, so `-> ensures (&)[3]` returns
// a reference to an array of `ensures`, and states no postcondition.
#include <cstdio>

struct ensures {
    int v = 4;
};
struct expects {
    int v = 5;
};
namespace ns {
struct decreases {
    int v = 6;
};
} // namespace ns

static ensures table[3];
static const ensures constant_table[3]{};
static ns::decreases measures[2];

ensures make_one(int) {
    return {};
}

auto pick() -> ensures (&)[3] {
    return table;
}
inline auto maker() -> ensures (*)(int) {
    return make_one;
}
static auto no_maker() -> expects (*)() {
    return nullptr;
}
auto constant() -> const ensures (&)[3] {
    return constant_table;
}
auto qualified() -> ns::decreases (&)[2] {
    return measures;
}

struct Holder {
    auto member() const -> ensures (&)[3] {
        return table;
    }
};

int main() {
    const Holder holder;
    std::printf("%d %d %d %d %d %d\n", pick()[1].v, maker()(0).v, no_maker() == nullptr, constant()[2].v,
                qualified()[1].v, holder.member()[0].v);
    return 0;
}
