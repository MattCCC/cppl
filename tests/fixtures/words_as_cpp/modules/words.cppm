// A module declaring entities named after C++L words, for main.cpp to import
// (SPEC.md MODULE-001, WORD-019).
module;
#include <cstdio>
export module words;

export struct contradiction {
    contradiction() {
        std::puts("contradiction constructed");
    }
};
export struct cases {
    int v;
};
export struct decompose {
    int v;
};
export struct ghost {
    int v = 6;
};
export struct unsafe {
    unsafe() {
        std::puts("unsafe constructed");
    }
};
export template <class T> T validate(T value) {
    return value * 2;
}
