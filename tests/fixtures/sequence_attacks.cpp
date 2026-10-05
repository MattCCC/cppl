// The accepted twins of the storage and bounds attacks refused by
// `tests/negative/sequence_attacks.sh` (RFC 0020, SPEC.md J.17): each function
// here does what its refused twin does, short of the one step that makes the
// twin unsound. `tests/e2e/sequence_attacks.sh` checks that every contract is
// proven, rests on the library model it uses, and computes what it states.
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

// SPEC: STDMODEL-015, STDMODEL-025
// The span is used before `reserve`, never after. Refused twin:
// `negative/sequence_attack_stale_after_reserve.cpp`.
verified unsigned read_before_reserve(std::size_t i)
    ensures (result == 6u)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    std::span<unsigned> s(v);
    if (i < s.size()) {
        s[i] = 6u;
        const unsigned seen = v[i];
        v.reserve(100ul);
        return seen;
    }
    return 6u;
}

// SPEC: STDMODEL-013
// `reserve` keeps the length, which is all its summary states.
verified std::size_t length_after_reserve()
    ensures (result == 3ul)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    v.reserve(100ul);
    return v.size();
}

// SPEC: STDMODEL-025
// Refused twin: `negative/sequence_attack_reference_after_assign.cpp`.
verified unsigned reference_before_assign()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{3u};
    unsigned& r = v[0];
    const unsigned first = r;
    v = w;
    return first;
}

// SPEC: STDMODEL-025
// Refused twin: `negative/sequence_attack_reference_after_move_assign.cpp`.
verified unsigned reference_before_move_assign()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{3u};
    unsigned& r = v[0];
    const unsigned first = r;
    v = std::move(w);
    return first;
}

// SPEC: STDMODEL-013
// Refused twin: `negative/sequence_attack_resize.cpp`, which uses the
// unmodeled `resize`.
verified std::size_t grown_by_push()
    ensures (result == 4ul)
{
    std::vector<unsigned> v{1u, 2u};
    v.push_back(0u);
    v.push_back(0u);
    return v.size();
}

// SPEC: STDMODEL-012
// Refused twins: `negative/sequence_attack_iterator.cpp`, and
// `negative/sequence_attack_first_of_empty.cpp` for an empty vector.
verified unsigned first_by_index()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    return v[0];
}

// SPEC: STDMODEL-012
// Refused twin: `negative/sequence_attack_range_for.cpp`.
verified unsigned last_by_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned last = 0u;
    std::size_t i = 0ul;
    while (i < v.size())
        invariant (i <= v.size())
        decreases (v.size() - i)
    {
        last = v[i];
        ++i;
    }
    return last;
}

// SPEC: STDMODEL-012
// Refused twin: `negative/sequence_attack_off_by_one.cpp`, guarded by `<=`.
verified unsigned below_size(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (i < v.size()) {
        return v[i];
    }
    return 0u;
}

// SPEC: STDMODEL-012, ARITH-003
// Refused twin: `negative/sequence_attack_last_of_empty.cpp`.
verified unsigned last_of_nonempty(const std::vector<unsigned>& v)
    ensures (result == result)
{
    if (0ul < v.size()) {
        return v[v.size() - 1ul];
    }
    return 0u;
}

// SPEC: STDMODEL-012
// Refused twin: `negative/sequence_attack_first_of_empty.cpp`.
verified unsigned first_of_one()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u};
    return v[0];
}

// SPEC: STDMODEL-012, ARITH-003
// Refused twin: `negative/sequence_attack_wrapping_guard.cpp`.
verified unsigned exact_guard(std::span<const unsigned> s, std::size_t i)
    expects (readable(s))
    ensures (result == result)
{
    if (i < s.size()) {
        return s[i];
    }
    return 0u;
}

// SPEC: STDMODEL-016, VERIFIED-040
// The value written is read back through the view that wrote it. Refused
// twin: `negative/sequence_attack_two_views.cpp`, which reads it through another
// view that may alias.
verified unsigned written_view(std::span<unsigned> a, std::span<const unsigned> b)
    expects (readable(a) && writable(a) && readable(b) && 0ul < a.size() && 0ul < b.size())
    ensures (result == 7u)
{
    const unsigned before = b[0];
    a[0] = 7u;
    return a[0] + (before - before);
}

// SPEC: STDMODEL-025, UNSAFE-003
// Refused twin: `negative/sequence_attack_reference_across_unsafe.cpp`.
void grow(std::vector<unsigned>& v);

verified unsigned read_before_unsafe()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u};
    unsigned& r = v[0];
    const unsigned first = r;
    unsafe {
        grow(v);
    }
    return first;
}

void grow(std::vector<unsigned>& v) {
    v.push_back(2u);
}

// SPEC: STDMODEL-021
// Refused twin: `negative/sequence_attack_moved_from_element.cpp`.
verified unsigned moved_to_element()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    return w[0];
}

// SPEC: STDMODEL-025
// Refused twin: `negative/sequence_attack_string_reference_after_append.cpp`.
verified char character_before_append()
    ensures (result == result)
{
    std::string s = "ab";
    char& c = s[0];
    const char first = c;
    s += 'c';
    return first;
}

int main() {
    const std::vector<unsigned> input{4u, 5u};
    std::vector<unsigned> storage{8u, 9u};
    const std::vector<unsigned> other{3u};
    std::printf("%u %zu %u %u %zu %u %u %u %u %u %u %u %u %u %c\n", read_before_reserve(1ul), length_after_reserve(),
                reference_before_assign(), reference_before_move_assign(), grown_by_push(), first_by_index(),
                last_by_index(), below_size(input, 2ul), last_of_nonempty(input), first_of_one(),
                exact_guard(input, 1ul), written_view(storage, other), read_before_unsafe(), moved_to_element(),
                character_before_append());
    return 0;
}
