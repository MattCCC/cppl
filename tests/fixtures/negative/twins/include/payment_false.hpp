#pragma once

// Refused twin of fixtures/include/payment.hpp (tests/negative/refused_twins.sh):
// the same header, except that its Law claims one more than scale returns.

pure int scale(int amount) {
    return amount;
}

law scale_preserves_amount(int amount)
    proves (scale(amount) == amount + 1);
