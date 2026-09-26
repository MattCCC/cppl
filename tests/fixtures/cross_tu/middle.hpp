// The public contract of middle.cpp, which is itself proven through
// library.cpp's contract of clamp4 (SPEC.md TUBOUND-006).
#pragma once

verified unsigned doubled(unsigned y)
    expects (y < 50u)
    ensures (result < 7u);
