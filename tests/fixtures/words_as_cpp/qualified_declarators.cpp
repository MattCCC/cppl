// Ordinary C++ naming types after C++L specifiers, then defining a member or a
// namespace member through a qualified declarator (SPEC.md 3.1, WORD-008).
//
// `verified Factory::make()` defines `Factory::make`, returning a `verified`.
// The word is a type here, so nothing after it is a verified function: no
// return type stands between it and the declarator.
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

struct Factory {
    verified make();
    static verified make_static();
    pure make_pure();
    static pure make_pure_static();
    unsafe make_unsafe();
};

verified Factory::make() {
    return {};
}
verified Factory::make_static() {
    return {5};
}
pure Factory::make_pure() {
    return {};
}
pure Factory::make_pure_static() {
    return {6};
}
unsafe Factory::make_unsafe() {
    return {};
}

namespace ns {
extern verified value;
extern pure other;
} // namespace ns
verified ns::value;
pure ns::other{7};

int main() {
    Factory factory;
    std::printf("%d %d %d %d %d %d %d\n", factory.make().v, Factory::make_static().v, factory.make_pure().v,
                Factory::make_pure_static().v, factory.make_unsafe().v, ns::value.v, ns::other.v);
    return 0;
}
