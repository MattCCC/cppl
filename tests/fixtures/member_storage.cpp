// SPEC: CLASS-008, CLASS-010, CLASS-011, STORAGE-005, STDMODEL-011
// The storage a realistic class holds and a caller hands it: a member array of
// the implicit object, std::array or built in, subscripted at a term against
// the extent its type states; an object a reference parameter designates,
// written member by member and through a member call that may write it; and a
// std::array handed by reference.
#include <array>
#include <cstddef>
#include <cstdio>

struct Stack {
    std::array<int, 16> items;
    std::size_t size;

    verified bool empty() const
        ensures (result == (size == 0u))
    {
        return size == 0u;
    }

    verified void push(int value)
        expects (size < 16u)
        ensures (size > 0u && size <= 16u)
    {
        items[size] = value;
        size = size + 1u;
    }

    verified int top() const
        expects (size > 0u && size <= 16u)
        ensures (true)
    {
        return items[size - 1u];
    }

    verified void pop()
        expects (size > 0u && size <= 16u)
        ensures (size < 16u)
    {
        size = size - 1u;
    }

    // An element written at a term is the one read at that term, and the size
    // beside the array is another member, which the write leaves alone.
    verified int put(std::size_t at, int value)
        expects (at < 16u && size == 3u)
        ensures (result == value && size == 3u)
    {
        items[at] = value;
        return items[at];
    }
};

// A built-in member array is followed the same way.
struct Ring {
    unsigned slots[8];
    unsigned head;

    verified void store(unsigned value)
        expects (head < 8u)
        ensures (head < 8u)
    {
        slots[head] = value;
        head = (head + 1u) % 8u;
    }

    verified unsigned newest() const
        expects (head < 8u)
        ensures (true)
    {
        return slots[head == 0u ? 7u : head - 1u];
    }
};

// The object a reference parameter designates is followed member by member:
// a member call that may write it leaves what that call's contract states.
verified int pop_top(Stack& s)
    expects (s.size > 0u && s.size <= 16u)
    ensures (s.size < 16u)
{
    const int value = s.top();
    s.pop();
    return value;
}

verified std::size_t push_one(Stack& s, int value)
    expects (s.size < 16u)
    ensures (result > 0u && result == s.size)
{
    s.push(value);
    return s.size;
}

struct Counter {
    unsigned hits;
    unsigned misses;
};

verified void record(Counter& c, bool hit)
    expects (c.hits < 1000u && c.misses < 1000u)
    ensures (c.hits <= 1000u && c.misses <= 1000u)
{
    if (hit) {
        c.hits = c.hits + 1u;
    } else {
        c.misses = c.misses + 1u;
    }
}

verified void reset(Counter& c)
    ensures (c.hits == 0u && c.misses == 0u)
{
    c.hits = 0u;
    c.misses = 0u;
}

verified unsigned reset_then_read()
    ensures (result == 0u)
{
    Counter c{4u, 5u};
    reset(c);
    return c.hits + c.misses;
}

// A std::array handed by reference: each element is the caller's storage,
// read and written at a term within the extent its type states.
verified void bump(std::array<unsigned, 10>& counts, unsigned bucket)
    expects (bucket < 10u)
    ensures (true)
{
    if (counts[bucket] < 1000000u) {
        counts[bucket] = counts[bucket] + 1u;
    }
}

verified void clear_first(std::array<unsigned, 10>& counts)
    ensures (counts[0] == 0u)
{
    counts[0] = 0u;
}

int main() {
    Stack s{{}, 0u};
    s.push(4);
    s.push(9);
    const int top = s.top();
    const int popped = pop_top(s);
    const std::size_t pushed = push_one(s, 7);
    s.push(1);
    const int put = s.put(2, 11);
    std::printf("%d %d %zu %d %d\n", top, popped, pushed, put, s.empty() ? 1 : 0);

    Ring r{{}, 0u};
    r.store(3u);
    r.store(5u);
    std::printf("%u %u\n", r.newest(), r.head);

    Counter c{1u, 2u};
    record(c, true);
    record(c, false);
    std::printf("%u %u %u\n", c.hits, c.misses, reset_then_read());

    std::array<unsigned, 10> counts{};
    bump(counts, 3u);
    bump(counts, 3u);
    counts[0] = 8u;
    clear_first(counts);
    std::printf("%u %u\n", counts[3], counts[0]);
    return 0;
}
