// Proves the intake `intake.hpp` declares. A request's text is hostile until
// read: every character is read under the bound its range-based for keeps, and
// the units it names become a Quantity only through a validation the program
// runs, never by assumption.
#include "intake.hpp"

#include <cstdlib>

verified unsigned parse_units(const std::string& text)
    ensures (result <= 9999u)
{
    unsigned value = 0u;
    for (const char c : text)
        invariant (value <= 9999u)
    {
        if (c < '0' || c > '9' || value > 999u) {
            break;
        }
        value = value * 10u + static_cast<unsigned>(c - '0');
    }
    return value;
}

verified unsigned accepted_units(unsigned requested)
    ensures (result <= 1000u && (result == 0u || result == requested))
{
    if (validate<Quantity>(requested)) {
        const Quantity units = requested;
        return units;
    }
    return 0u;
}

verified Side side_of(const std::string& text)
    ensures (static_cast<unsigned>(result) <= 1u)
{
    if (text.empty()) {
        return Side::sell;
    }
    switch (const char code = text[0]) {
        case 'b':
            [[fallthrough]];
        case 'B':
            return Side::buy;
        default:
            return Side::sell;
    }
}

// The environment is outside the program, and nothing here can verify what it
// holds.
unsafe std::size_t slots_from_environment();

verified std::size_t configured_slots()
    ensures (result <= 8u)
{
    std::size_t slots = kSlots;
    unsafe {
        slots = slots_from_environment();
    }
    if (slots > kSlots) {
        return kSlots;
    }
    return slots;
}

unsafe std::size_t slots_from_environment() {
    const char* text = std::getenv("DESK_SLOTS");
    if (text == nullptr || text[0] < '0' || text[0] > '9') {
        return kSlots;
    }
    return static_cast<std::size_t>(text[0] - '0');
}
