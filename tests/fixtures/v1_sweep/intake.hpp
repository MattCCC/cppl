// What the desk reads from outside the program: the requests its clients send,
// as text, and the number of slots its environment configures. Proven in
// `intake.cpp`, and used by `desk.cpp` through the verification interface
// `intake.cpp` writes.
#pragma once

#include "stock.hpp"

#include <cstddef>
#include <string>

// The units a request's text begins with: at most four digits, read until the
// first character that is not one.
verified unsigned parse_units(const std::string& text)
    ensures (result <= 9999u);

// A request's units where they are what one order may carry, tested at run
// time, and none otherwise.
verified unsigned accepted_units(unsigned requested)
    ensures (result <= 1000u && (result == 0u || result == requested));

// The side a request's text names by its first character: `b` or `B` buys,
// anything else sells.
verified Side side_of(const std::string& text)
    ensures (static_cast<unsigned>(result) <= 1u);

// How many slots of a shelf the desk is configured to use, read from its
// environment in an unsafe block and clamped to a shelf's size.
verified std::size_t configured_slots()
    ensures (result <= 8u);
