// The public contract of consume.cpp, proven through validate.cpp's
// positive_or_one (SPEC.md TUBOUND-006, RUNTIMECHECK-015).
#pragma once

verified int through_interface(int raw)
    ensures (result > 0);
