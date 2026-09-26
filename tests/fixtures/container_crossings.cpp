// The accepted twins of the container crossings `negative/containers.sh`
// refuses: each differs from its refused twin in the one thing the refusal is
// about. Driven by `e2e/containers.sh`.
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>

type Positive = unsigned where (self > 0u);

verified unsigned read_first(const unsigned& x, unsigned* p, std::size_t n)
    expects (writable(p, n))
    ensures (result == 0u)
{
    return 0u;
}

// SPEC: STDMODEL-017
// Twin of `container_element_and_data_call`: the element is only read.
verified unsigned element_by_const_reference_and_data()
    ensures (result == 0u)
{
    std::vector<unsigned> v{4u, 5u, 6u};
    return read_first(v[0], v.data(), v.size());
}

verified unsigned set_first(unsigned& x, const unsigned* p, std::size_t n)
    expects (readable(p, n))
    ensures (result == 0u && x == 1u)
{
    x = 1u;
    return 0u;
}

// SPEC: STDMODEL-017, VERIFIED-036
// Twin of `container_element_and_data_call` and
// `container_const_pointer_writable`: the data pointer is only read.
verified unsigned element_and_readable_data()
    ensures (result == 1u)
{
    std::vector<unsigned> v{4u, 5u, 6u};
    const unsigned zero = set_first(v[0], v.data(), v.size());
    return zero + v[0];
}

verified unsigned touch(unsigned& x, std::span<unsigned> s)
    ensures (result == 0u)
{
    return 0u;
}

// SPEC: STDMODEL-016
// Twin of `container_element_and_span_call`: the span is of another vector.
verified unsigned element_and_other_span()
    ensures (result == 0u)
{
    std::vector<unsigned> v{4u, 5u, 6u};
    std::vector<unsigned> w{7u};
    return touch(v[0], w);
}

verified std::size_t count(const std::vector<unsigned>& v)
    ensures (result == v.size())
{
    return v.size();
}

// SPEC: STDMODEL-020
// Twin of `container_refined_mutable_reference`: a callee that only reads the
// container leaves the content invariant in place.
verified unsigned refined_by_const_reference()
    ensures (result > 0u)
{
    std::vector<Positive> r{1u, 2u};
    const std::size_t n = count(r);
    if (n > 0ul && r.size() > 0ul) {
        return r[0];
    }
    return 1u;
}

verified std::size_t measure(const unsigned* p, std::size_t n)
    expects (readable(p, n))
    ensures (result == n)
{
    return n;
}

// SPEC: VERIFIED-036
// Twin of `capability_non_null`: an empty vector's data pointer, possibly
// null, is a valid capability over zero elements.
verified std::size_t empty_data()
    ensures (result == 0ul)
{
    std::vector<unsigned> v;
    return measure(v.data(), v.size());
}

int main() {
    std::printf("%u %u %u %u %zu\n", element_by_const_reference_and_data(), element_and_readable_data(),
                element_and_other_span(), refined_by_const_reference(), empty_data());
    return 0;
}
