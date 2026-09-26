#include "closure_middle.hpp"

#include "closure.hpp"

unsigned relayed_plain(unsigned x) {
    return plain(x);
}

unsigned relayed_trusting(unsigned x) {
    return trusting(x);
}

unsigned relayed_unsafe() {
    return unsafe_read();
}
