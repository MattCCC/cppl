#pragma once

// A Law written in a header. It is visible because C++L reads the translation
// unit after preprocessing (SPEC.md 3.2, 44).

pure int scale(int amount) {
    return amount;
}

law scale_preserves_amount(int amount)
    ensures(scale(amount) == amount);
