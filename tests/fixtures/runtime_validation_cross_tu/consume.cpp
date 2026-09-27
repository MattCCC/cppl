// Uses validate.cpp's contracts through its verification interface. The claim
// proven through positive_or_one rests on the runtime check validate.cpp's
// proof took the fact from, named with where it is (SPEC.md RUNTIMECHECK-014,
// RUNTIMECHECK-015); the one proven through always_two rests on none.
#include "consume.hpp"

#include "validate.hpp"

#include <cstdio>
#include <cstdlib>

verified int through_interface(int raw)
    ensures (result > 0)
{
    return positive_or_one(raw);
}

verified int static_through_interface(int raw)
    ensures (result == 2)
{
    return always_two(raw);
}

int main(int argc, char** argv) {
    const int raw = argc > 1 ? std::atoi(argv[1]) : 0;
    std::printf("%d %d\n", through_interface(raw), static_through_interface(raw));
    return 0;
}
