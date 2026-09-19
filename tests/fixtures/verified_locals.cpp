verified unsigned identity(unsigned x) ensures(result == x) { return x; }

verified unsigned bounded(unsigned x) expects(x <= 10u) ensures(result == x) { return x; }

verified unsigned chained(unsigned x) ensures(result == (x + 1u) + 1u) {
    unsigned y = x + 1u;
    unsigned z = y + 1u;
    return z;
}

verified unsigned latest(unsigned x) ensures(result == 7u) {
    unsigned y = x;
    y = 7u;
    return y;
}

verified unsigned preserved(unsigned x) ensures(result == x) {
    unsigned y = x;
    unsigned kept = y;
    y = 0u;
    return kept;
}

verified unsigned declared_together(unsigned x) ensures(result == x) {
    unsigned y = x, z = y;
    return z;
}

verified unsigned deduced(unsigned x) ensures(result == x) {
    auto y = x;
    const unsigned z = y;
    unsigned w = z;
    return w;
}

verified unsigned braced(unsigned x) ensures(result == x) {
    unsigned y{x};
    unsigned z(y);
    return z;
}

verified unsigned choose_then_adjust(unsigned x, bool b) ensures(result == x) {
    unsigned y = x;

    if (b) {
        unsigned z = y;
        y = z;
    }

    return y;
}

verified unsigned merged(bool b) ensures(result <= 2u) {
    unsigned x = 1u;
    if (b) x = 2u;
    return x;
}

verified unsigned both_arms(bool b, unsigned x) ensures(result <= 10u) {
    unsigned y = x;
    if (b) { y = 10u; } else { y = 0u; }
    return y;
}

verified unsigned nested_assignments(bool b, bool c, unsigned x) ensures(result <= 10u) {
    unsigned y = x;
    if (b) {
        if (c) y = 1u; else y = 2u;
    } else {
        y = 3u;
    }
    return y;
}

verified unsigned shadowed(unsigned x) ensures(result == x) {
    unsigned y = x;
    {
        unsigned y = 0u;
        y = y + 1u;
    }
    return y;
}

verified unsigned call_in_initializer(unsigned x) ensures(result == x) {
    unsigned y = identity(x);
    unsigned z = y;
    return z;
}

verified unsigned call_in_assignment(unsigned x) ensures(result == x) {
    unsigned y = 0u;
    y = identity(x);
    return y;
}

verified unsigned guarded_call(unsigned x) ensures(result <= 10u) {
    if (x <= 10u) {
        unsigned y = bounded(x);
        return y;
    }
    return 10u;
}

verified unsigned local_guard(unsigned x) ensures(result <= 10u) {
    unsigned y = x;
    if (y <= 10u) return y;
    return 10u;
}

verified unsigned flag_guard(unsigned x) ensures(result <= 10u) {
    bool small = x <= 10u;
    if (small) return x;
    return 10u;
}

verified unsigned clamp(unsigned x) ensures(result <= 10u) {
    if (x <= 10u) return x;
    return 10u;
}

// The false arm needs clamp's postcondition, reached through the local.
verified unsigned clamped_guard(unsigned x) ensures(result <= 10u) {
    unsigned y = clamp(x);
    if (y == 10u) return 10u;
    return y;
}

verified unsigned constant_arm(bool b) ensures(result == 1u) {
    const unsigned one = 1u;
    if (b) return one;
    return 1u;
}

verified unsigned declared_in_arm(unsigned x, bool b) ensures(result == x) {
    unsigned y = x;
    if (b) unsigned y = 0u;
    return y;
}

// Each update is the assignment it abbreviates, at the local's own type.
verified unsigned updated(unsigned x) ensures(result == 2u * x + 1u) {
    unsigned y = x;
    y += x;
    y++;
    ++y;
    y--;
    return y;
}

verified unsigned long scaled(unsigned long x) ensures(result == 6ul * x - 1ul) {
    unsigned long y = x;
    y *= 3ul;
    y *= 2ul;
    --y;
    y -= 0ul;
    return y;
}

int main() {
    if (updated(4u) != 9u || scaled(2ul) != 11ul || updated(4294967295u) != 4294967295u) return 16;
    if (chained(1u) != 3u || latest(1u) != 7u || declared_together(2u) != 2u) return 1;
    if (preserved(1u) != 1u) return 11;
    if (deduced(3u) != 3u || braced(4u) != 4u) return 2;
    if (choose_then_adjust(5u, true) != 5u || choose_then_adjust(5u, false) != 5u) return 3;
    if (merged(true) != 2u || merged(false) != 1u) return 4;
    if (both_arms(true, 3u) != 10u || both_arms(false, 3u) != 0u) return 5;
    if (nested_assignments(true, true, 9u) != 1u || nested_assignments(true, false, 9u) != 2u ||
        nested_assignments(false, true, 9u) != 3u) return 6;
    if (shadowed(6u) != 6u) return 7;
    if (call_in_initializer(7u) != 7u || call_in_assignment(8u) != 8u) return 8;
    if (guarded_call(9u) != 9u || guarded_call(90u) != 10u) return 9;
    if (local_guard(2u) != 2u || local_guard(20u) != 10u) return 10;
    if (flag_guard(3u) != 3u || flag_guard(30u) != 10u) return 12;
    if (clamped_guard(4u) != 4u || clamped_guard(40u) != 10u) return 13;
    if (constant_arm(true) != 1u || constant_arm(false) != 1u) return 14;
    if (declared_in_arm(5u, true) != 5u || declared_in_arm(5u, false) != 5u) return 15;
    return 0;
}
