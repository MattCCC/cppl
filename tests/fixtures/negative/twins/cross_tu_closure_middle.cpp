// Refused twin of fixtures/cross_tu/closure_middle.cpp (tests/negative/refused_twins.sh): the same
// program, except that relayed_plain returns one more than plain.
#include "closure_middle.hpp"

#include "closure.hpp"

unsigned relayed_plain(unsigned x) {
    return plain(x) + 1u;
}

unsigned relayed_trusting(unsigned x) {
    return trusting(x);
}

unsigned relayed_unsafe() {
    return unsafe_read();
}
