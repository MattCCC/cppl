// SPEC: CASE-004, CASE-017
// `directed` of integration/ledger.cpp omitting the debit case as impossible.
// The precondition admits a debit, so no contradiction closes it, and the
// omission is refused rather than dropping that path.
enum class Direction : int { credit = 0, debit = 1 };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified long long directed(Direction direction, long long magnitude)
    expects (static_cast<int>(direction) >= 0 && static_cast<int>(direction) <= 1 && magnitude >= 0ll &&
            magnitude <= 999999ll)
    ensures (-999999ll <= result && result <= 999999ll)
{
    cases direction {
        Direction::credit => {
        }

        omit Direction::debit by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    if (direction == Direction::debit) {
        return -magnitude;
    }
    return magnitude;
}

int main() {
    return static_cast<int>(directed(Direction::credit, 0ll));
}
