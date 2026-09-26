// A class whose verified member functions are defined in `counter.cpp` and
// called from `client.cpp`, which sees only this header and the verification
// interface `counter.cpp` writes (SPEC.md CLASS-008, TUBOUND-003, RFC 0017,
// RFC 0018). The contracts stand on the declarations in the class; the
// out-of-line definitions inherit them (SPEC.md CONTRACT-005).
#pragma once

struct Counter {
    unsigned value;
    unsigned limit;

    verified unsigned get() const
        ensures (result == value);

    verified unsigned headroom() const
        expects (value <= limit)
        ensures (result == limit - value);

    verified void reset()
        ensures (value == 0u);
};
