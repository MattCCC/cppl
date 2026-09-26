// SPEC: CLASS-008
// A class declared in a function body has members the trust report cannot name
// as declarations of the unit, so `verified` on one is refused as it is on any
// other declaration in a block.
unsigned outer() {
    struct Local {
        unsigned value;

        verified unsigned get() const
            ensures (result == value)
        {
            return value;
        }
    };
    const Local local{1u};
    return local.get();
}

int main() {
    return static_cast<int>(outer());
}
