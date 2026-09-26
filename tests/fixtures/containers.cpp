// The verified sequence subset (SPEC.md J.17, STDMODEL-010 to STDMODEL-022;
// RFC 0020).
//
// Every function here verifies, and each is the accepted twin of a refused
// program written out in `negative/container_*.cpp`: the two differ in the one
// thing the refusal is about. `tests/e2e/containers.sh` checks the trust report
// and runs the program; the values it prints are the ones the contracts state.
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

type Positive = unsigned where (self > 0u);

// --- std::array: a fixed product, its elements places (STDMODEL-011) -------

// SPEC: STDMODEL-011
// A guarded subscript owes nothing it does not prove; twin of
// `container_array_out_of_bounds`.
verified unsigned array_guarded(std::size_t i)
    ensures (result == 5u)
{
    std::array<unsigned, 3> a{1u, 2u, 3u};
    if (i < a.size()) {
        a[i] = 5u;
        return a[i];
    }
    return 5u;
}

// A by-value array parameter is the callee's own copy, element by element.
verified std::size_t array_parameter(std::array<unsigned, 4> a)
    ensures (result == 4ul)
{
    a[0] = 9u;
    return a.size();
}

// SPEC: STDMODEL-020
// A refined element's predicate is owed where a value enters and supplied
// where one is read.
verified Positive array_refined(std::size_t i)
    ensures (result > 0u)
{
    std::array<Positive, 2> a{3u, 4u};
    if (i < 2ul) {
        a[i] = 7u;
        return a[i];
    }
    return a[0];
}

// --- std::vector: a length, and elements at a generation (STDMODEL-012) -----

// SPEC: STDMODEL-013
// Construction and every mutator state the length they leave.
verified std::size_t vector_lengths()
    ensures (result == 3ul)
{
    std::vector<unsigned> v{1u, 2u};
    v.push_back(3u);
    v.push_back(4u);
    v.pop_back();
    v.reserve(16ul);
    return v.size();
}

// A listed element is the place its constant subscript names.
verified unsigned vector_listed()
    ensures (result == 7u)
{
    std::vector<unsigned> v{7u, 8u};
    return v[0];
}

// SPEC: STDMODEL-012
// An element written is read back while the generation stands. Twin of
// `container_vector_out_of_bounds`.
verified unsigned vector_write_read(std::size_t n, std::size_t i)
    ensures (result == 5u)
{
    std::vector<unsigned> v(n);
    if (i < v.size()) {
        v[i] = 5u;
        return v[i];
    }
    return 5u;
}

// SPEC: STDMODEL-015
// After `pop_back` the element place is formed again and its bound is owed
// against the length current there. Twin of `container_element_after_pop`.
verified unsigned vector_after_pop(std::size_t i)
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    v.pop_back();
    if (i < v.size()) {
        return v[i];
    }
    return 0u;
}

// SPEC: STDMODEL-013
// `pop_back` owes a non-empty vector. Twin of `container_pop_empty`.
verified std::size_t vector_pop_guarded(std::vector<unsigned>& v)
    ensures (result == result)
{
    if (!v.empty()) {
        v.pop_back();
    }
    return v.size();
}

// SPEC: STDMODEL-015
// A loop that appends carries the vector: at its head it is a fresh value of
// which only the invariant is known.
verified std::size_t vector_count(std::size_t n)
    ensures (result == n)
{
    std::vector<unsigned> v;
    std::size_t i = 0ul;
    while (i < n)
        invariant (v.size() == i && i <= n)
        decreases (n - i)
    {
        v.push_back(1u);
        ++i;
    }
    return v.size();
}

// A caller's vector, cleared and appended to: the contract states the post-state.
verified void vector_reset(std::vector<unsigned>& out)
    ensures (out.size() == 1ul)
{
    out.clear();
    out.push_back(1u);
}

// A reference parameter's length, read.
verified std::size_t vector_length(const std::vector<unsigned>& v)
    ensures (result == v.size())
{
    return v.size();
}

// SPEC: STDMODEL-023
// A by-value parameter is the callee's own copy: `v` in the contract is the
// copy as passed, and growing it grows nothing of the caller's.
verified std::size_t vector_grown_copy(std::vector<unsigned> v)
    ensures (result == v.size() + 1ul)
{
    v.push_back(1u);
    return v.size();
}

// Twin of `container_by_value_call`.
verified std::size_t vector_copy_call()
    ensures (result == 5ul)
{
    std::vector<unsigned> v{1u, 2u};
    std::size_t grown = vector_grown_copy(v);
    return grown + v.size();
}

// SPEC: STDMODEL-020
// A refined vector owes the predicate at every entry and supplies it at every
// read. Twins of `container_refined_push` and `container_refined_write`.
verified Positive vector_refined(std::size_t i)
    ensures (result > 0u)
{
    std::vector<Positive> v{1u, 2u};
    v.push_back(3u);
    if (i < v.size()) {
        v[i] = 9u;
        return v[i];
    }
    return v[0];
}

// SPEC: STDMODEL-021
// A move takes the length; the moved-from vector is not read. Twin of
// `container_moved_from_size`.
verified std::size_t vector_moved()
    ensures (result == 2ul)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    return w.size();
}

// SPEC: STDMODEL-015
// An element reference is used before the storage may change. Twin of
// `container_stale_reference`.
verified unsigned vector_reference()
    ensures (result == 3u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[1];
    r = 3u;
    unsigned seen = r;
    v.push_back(4u);
    return seen;
}

// SPEC: STDMODEL-012, ARITH-008
// A signed index is converted to the size type as C++ converts it, modularly,
// and the bound is owed on the converted value. Twin of
// `container_signed_index`.
verified unsigned signed_guarded(const std::vector<unsigned>& v, int i)
    ensures (result == result)
{
    if (0 <= i && static_cast<std::size_t>(i) < v.size()) {
        return v[i];
    }
    return 0u;
}

// SPEC: STDMODEL-012, ARITH-004, ARITH-009
// Dividing by a length owes a non-empty container. Twin of
// `container_divide_by_length`.
verified std::size_t per_element(const std::vector<unsigned>& v, std::size_t total)
    ensures (result == result)
{
    if (!v.empty()) {
        return total / v.size();
    }
    return 0ul;
}

// --- std::string: characters of a string (STDMODEL-013) --------------------

// SPEC: STDMODEL-013
verified std::size_t string_lengths()
    ensures (result == 6ul)
{
    std::string s = "abc";
    std::string t = "de";
    s.push_back('x');
    s += t;
    return s.length();
}

// Twin of `container_string_out_of_bounds`.
verified char string_character(const std::string& s, std::size_t i)
    ensures (true)
{
    if (i < s.size()) {
        return s[i];
    }
    return 'a';
}

// --- std::span: a borrowed view (STDMODEL-014, STDMODEL-016) ---------------

// SPEC: STDMODEL-016
// A span parameter's elements are read under the capability its contract
// states, conjoined with an ordinary predicate. Twins of
// `container_span_without_capability` and `container_span_out_of_bounds`.
verified unsigned span_at(std::span<const unsigned> in, std::size_t i)
    expects (readable(in) && i < in.size())
    ensures (result == result)
{
    return in[i];
}

// A parser's shape: read a span, append to a vector the caller holds.
// Reallocating `out` leaves the span's capability intact, because a capability
// never designates storage of a container the function holds by reference.
verified std::size_t span_collect(std::span<const unsigned> in, std::vector<unsigned>& out)
    expects (readable(in))
    ensures (result == in.size())
{
    std::size_t i = 0ul;
    while (i < in.size())
        invariant (i <= in.size())
        decreases (in.size() - i)
    {
        out.push_back(in[i]);
        ++i;
    }
    return i;
}

// SPEC: STDMODEL-016
// Writing through a span needs `writable`.
verified void span_fill(std::span<unsigned> s, unsigned value)
    expects (writable(s))
    ensures (true)
{
    std::size_t i = 0ul;
    while (i < s.size())
        invariant (i <= s.size())
        decreases (s.size() - i)
    {
        s[i] = value;
        ++i;
    }
}

// SPEC: STDMODEL-014, STDMODEL-015
// A span local views its vector: a write through it is a write to the vector.
// Twins of `container_stale_span` and `container_stale_span_in_loop`.
verified unsigned span_local(std::size_t i)
    ensures (result == 6u)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    std::span<unsigned> s(v);
    if (i < s.size()) {
        s[i] = 6u;
        return v[i];
    }
    return 6u;
}

// SPEC: STDMODEL-016
// A caller hands its own vector, a live span of it, and a span it holds a
// capability for. Twins of `container_self_view_call` and
// `container_forward_without_capability`.
verified std::size_t span_calls(std::span<const unsigned> in)
    expects (readable(in))
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    std::vector<unsigned> out;
    std::span<const unsigned> view = v;
    std::size_t taken = span_collect(v, out);
    std::size_t again = span_collect(view, out);
    std::size_t forwarded = span_collect(in, out);
    return taken + again + forwarded + out.size();
}

// SPEC: STDMODEL-017
// A container's data pointer passed to a capability, over its length. Twin of
// `container_data_overrun`.
verified unsigned zero_prefix(unsigned* p, std::size_t n)
    expects (writable(p, n))
    ensures (result == 0u)
{
    std::size_t i = 0ul;
    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        p[i] = 0u;
        ++i;
    }
    return 0u;
}

verified unsigned data_argument()
    ensures (result == 0u)
{
    std::vector<unsigned> v{4u, 5u, 6u};
    return zero_prefix(v.data(), v.size());
}

// --- The parser shape RFC 0020 is for ---------------------------------------

// SPEC: STDMODEL-016, ARITH-008
// Characters of a span, compared after the promotions C++ applies. An element
// is read into a local, since a condition reads no storage (STATUS.md).
verified std::size_t skip_digits(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (result <= in.size())
{
    std::size_t i = at;
    while (i < in.size())
        invariant (at <= i && i <= in.size())
        decreases (in.size() - i)
    {
        const char c = in[i];
        if (c < '0' || c > '9') {
            return i;
        }
        ++i;
    }
    return i;
}

// SPEC: STDMODEL-016
// The prefix a callee found is read and appended to a vector the caller holds;
// the callee's postcondition bounds every index.
verified std::size_t collect_digits(std::span<const char> in, std::vector<char>& out)
    expects (readable(in))
    ensures (result <= in.size())
{
    const std::size_t end = skip_digits(in, 0ul);
    std::size_t i = 0ul;
    while (i < end)
        invariant (i <= end && end <= in.size())
        decreases (end - i)
    {
        out.push_back(in[i]);
        ++i;
    }
    return end;
}

// SPEC: STDMODEL-018
// A contract that names no container rests on the model of one it calls.
verified std::size_t via_call()
    ensures (result == 3ul)
{
    return vector_lengths();
}

int main() {
    std::vector<unsigned> caller{1u, 2u};
    vector_reset(caller);
    const std::size_t reset = vector_length(caller);
    const std::size_t popped = vector_pop_guarded(caller);
    std::vector<unsigned> buffer{7u, 8u};
    span_fill(buffer, 2u);
    const std::vector<unsigned> input{5u, 6u};
    std::printf("%u %zu %u %zu %u %u %zu %zu %zu %zu %u %zu %u %zu %c %u %u %zu %u %zu %zu %u %zu\n",
                array_guarded(1ul), array_parameter({1u, 2u, 3u, 4u}), array_refined(1ul), vector_lengths(),
                vector_listed(), vector_write_read(3ul, 2ul), vector_count(4ul), reset, vector_moved(), popped,
                vector_reference(), string_lengths(), vector_refined(0ul), span_calls(input),
                string_character("xyz", 1ul), span_at(input, 1ul), buffer[1], vector_after_pop(1ul) == 2u ? 1ul : 0ul,
                span_local(0ul) + data_argument(), via_call(), vector_copy_call(), signed_guarded(input, 1),
                per_element(input, 10ul));
    const std::string date = "2024-09";
    std::vector<char> digits;
    const std::size_t year = collect_digits(date, digits);
    std::printf("%zu %zu\n", year, digits.size());
    return 0;
}
