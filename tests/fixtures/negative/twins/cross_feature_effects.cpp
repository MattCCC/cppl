// Refused twin of fixtures/cross_feature/effects.cpp (tests/negative/refused_twins.sh): the same
// program, except that set_five stores 50u into a Small.
#include "effects.hpp"

void set_five(Small& x) {
    x = 50u;
}

void set_fifty(unsigned& x) {
    x = 50u;
}
