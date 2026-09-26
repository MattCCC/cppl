// The verified sequence subset erases to nothing (SPEC.md STDMODEL-022, ERASE-002,
// Annex M; RFC 0020 §11).
//
// Every container here is the ordinary type of the standard library the program
// is compiled against, and erased, this unit must compile to exactly the code
// `containers.reference.cpp` compiles to, where each contract, capability and
// refinement was removed by hand. A generation, a place, a length summary or a
// span's capability that reached the program would make the two differ.
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

type Positive = unsigned where (self > 0u);

verified std::size_t fill(std::size_t n)
    ensures (result == n)
{
    std::vector<Positive> v;
    std::size_t i = 0ul;
    while (i < n)
        invariant (v.size() == i && i <= n)
        decreases (n - i)
    {
        v.push_back(2u);
        ++i;
    }
    return v.size();
}

verified unsigned sum(std::span<const unsigned> in)
    expects (readable(in))
    ensures (result == result)
{
    unsigned total = 0u;
    std::size_t i = 0ul;
    while (i < in.size())
        invariant (i <= in.size())
        decreases (in.size() - i)
    {
        total += in[i];
        ++i;
    }
    return total;
}

verified void scale(std::span<unsigned> out, unsigned factor)
    expects (readable(out) && writable(out))
    ensures (true)
{
    std::size_t i = 0ul;
    while (i < out.size())
        invariant (i <= out.size())
        decreases (out.size() - i)
    {
        out[i] = out[i] * factor;
        ++i;
    }
}

verified std::size_t rebuild(std::vector<unsigned>& v)
    ensures (result == 2ul)
{
    v.clear();
    v.push_back(4u);
    v.push_back(5u);
    return v.size();
}

verified std::size_t text(const std::string& suffix)
    ensures (result == suffix.size() + 4ul)
{
    std::string s = "abc";
    s.push_back('d');
    s += suffix;
    return s.size();
}

verified unsigned pick(std::array<unsigned, 3> a, std::size_t i)
    expects (i < 3ul)
    ensures (result == result)
{
    a[i] = a[i] + 1u;
    return a[i];
}

verified std::size_t moved()
    ensures (result == 3ul)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    std::vector<unsigned> w = std::move(v);
    return w.size();
}

int main() {
    std::vector<unsigned> values{1u, 2u, 3u};
    scale(values, 3u);
    const unsigned total = sum(values);
    const std::size_t rebuilt = rebuild(values);
    std::printf("%zu %u %zu %zu %u %zu %u\n", fill(5ul), total, rebuilt, text("xy"), pick({7u, 8u, 9u}, 1ul), moved(),
                values[1]);
    return 0;
}
