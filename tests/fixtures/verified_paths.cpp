verified unsigned clamp(unsigned x)
    ensures (result <= 10u)
{
    if (x <= 10u)
        return x;
    return 10u;
}

verified unsigned clamp_call(unsigned x)
    ensures (result <= 10u)
{
    return clamp(x);
}

verified unsigned bounded(unsigned x)
    expects (x <= 10u)
    ensures (result <= 10u)
{
    return x;
}

verified unsigned guarded_call(unsigned x)
    ensures (result <= 10u)
{
    if (x <= 10u) {
        return bounded(x);
    } else {
        return bounded(10u);
    }
}

verified unsigned nested(unsigned x, unsigned y)
    ensures (result <= 10u)
{
    if (x <= 10u) {
        if (y <= 10u)
            return bounded(y);
        return bounded(x);
    } else if (y <= 10u) {
        return y;
    } else {
        return 10u;
    }
}

verified unsigned equal_branch(unsigned x)
    ensures (result == 0u)
{
    if (x == 0u)
        return x;
    else
        return 0u;
}

verified unsigned unequal_branch(unsigned x)
    ensures (result != 0u)
{
    if (x != 0u)
        return x;
    return 1u;
}

verified unsigned less_branch(unsigned x)
    ensures (result < 10u)
{
    if (x < 10u)
        return x;
    return 0u;
}

verified unsigned greater_branch(unsigned x)
    ensures (result > 10u)
{
    if (x > 10u)
        return x;
    return 11u;
}

verified unsigned at_least(unsigned x)
    ensures (result >= 10u)
{
    if (x >= 10u)
        return x;
    return 10u;
}

verified unsigned negative_path(unsigned x)
    expects (!(x <= 10u))
    ensures (result == x)
{
    if (!(x <= 10u))
        return x;
    return x;
}

verified unsigned identity(unsigned x)
    ensures (result == x)
{
    return x;
}

verified unsigned call_in_guard(unsigned x)
    ensures (result <= 10u)
{
    if (identity(x) <= 10u)
        return bounded(x);
    return 10u;
}

verified int signed_clamp(int x)
    ensures (result <= 10)
{
    if (x <= 10)
        return x;
    return 10;
}

verified unsigned above(unsigned x)
    expects (!(x <= 10u))
    ensures (result == x)
{
    return x;
}

verified unsigned false_guard_evidence(unsigned x)
    ensures (result == x)
{
    if (x <= 10u)
        return x;
    return above(x);
}

verified unsigned bounded_identity(unsigned x)
    expects (x <= 10u)
    ensures (result == x)
{
    return x;
}

verified unsigned guard_call_precondition(unsigned x)
    ensures (result <= 10u)
{
    if (x <= 10u) {
        if (bounded_identity(x) <= 10u)
            return x;
        return 10u;
    }
    return 10u;
}

verified unsigned sequential(unsigned x, unsigned y)
    ensures (result <= 10u)
{
    if (x <= 10u) {
        if (y <= 10u)
            return y;
    }
    if (y <= 10u)
        return y;
    return 10u;
}

verified unsigned empty_arm(unsigned x)
    ensures (result == x)
{
    if (x <= 10u) {
    } else {
        return above(x);
    }
    return x;
}

verified unsigned boolean_guard(bool b, unsigned x)
    ensures (result <= 10u)
{
    if (!b)
        return 0u;
    if (x <= 10u)
        return x;
    return 10u;
}

int main() {
    if (clamp(0u) != 0u || clamp(11u) != 10u || clamp(~0u) != 10u)
        return 1;
    if (clamp_call(20u) != 10u || guarded_call(5u) != 5u || guarded_call(20u) != 10u)
        return 2;
    if (nested(5u, 3u) != 3u || nested(5u, 20u) != 5u || nested(20u, 3u) != 3u || nested(20u, 30u) != 10u)
        return 3;
    if (equal_branch(0u) != 0u || equal_branch(7u) != 0u || unequal_branch(0u) != 1u || unequal_branch(7u) != 7u)
        return 4;
    if (less_branch(9u) != 9u || less_branch(10u) != 0u || greater_branch(11u) != 11u || greater_branch(0u) != 11u)
        return 5;
    if (at_least(9u) != 10u || at_least(12u) != 12u || negative_path(20u) != 20u)
        return 6;
    if (call_in_guard(3u) != 3u || call_in_guard(30u) != 10u)
        return 7;
    if (signed_clamp(-5) != -5 || signed_clamp(11) != 10)
        return 8;
    if (false_guard_evidence(5u) != 5u || false_guard_evidence(20u) != 20u)
        return 9;
    if (guard_call_precondition(5u) != 5u || guard_call_precondition(20u) != 10u)
        return 10;
    if (sequential(5u, 3u) != 3u || sequential(5u, 20u) != 10u || sequential(20u, 3u) != 3u ||
        sequential(20u, 30u) != 10u)
        return 11;
    if (empty_arm(5u) != 5u || empty_arm(20u) != 20u)
        return 12;
    if (boolean_guard(false, 5u) != 0u || boolean_guard(true, 5u) != 5u || boolean_guard(true, 20u) != 10u)
        return 13;
    return 0;
}
