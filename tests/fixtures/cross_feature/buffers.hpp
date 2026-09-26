// A cursor whose member functions read spans, vectors and strings, proven in
// `buffers.cpp` and used by `client.cpp` through the verification interface
// `buffers.cpp` writes (RFC 0017, RFC 0018, RFC 0020). The contracts stand on
// the declarations in the class; the out-of-line definitions inherit them
// (SPEC.md CONTRACT-005). `tests/e2e/cross_feature.sh` drives them.
//
// The cursor's member is not refined. A refined member is caller storage a
// member function owes its refinement for at return, and a call that may write
// a container the function holds by reference, or an unsafe block, may reach
// it, so its refinement would not be known there
// (`negative/cross_feature_refined_receiver_push.cpp`,
// `negative/cross_feature_unsafe_refined_receiver.cpp`).
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

struct Cursor {
    std::size_t at;

    // The element of a span at the cursor, read under the span's capability.
    verified unsigned peek(std::span<const unsigned> in) const
        expects (readable(in) && at < in.size())
        ensures (true);

    // One more element of a vector the caller holds, which may reallocate it.
    verified std::size_t emit(std::vector<unsigned>& out, unsigned value) const
        ensures (result == out.size() && 1ul <= result);

    // A vector the caller holds, read only: its storage is left as it was.
    verified std::size_t length_of(const std::vector<unsigned>& in) const
        ensures (result == in.size());

    // The character of a string at the cursor.
    verified char character(const std::string& text) const
        expects (at < text.size())
        ensures (true);

    // Signed arithmetic over the member, converted: its bound and the
    // argument's make the sum fit.
    verified long long moved(long long delta) const
        expects (at <= 4096ul && delta >= -4096ll && delta <= 4096ll)
        ensures (result == static_cast<long long>(at) + delta);

    // A setting read in an unsafe block and clamped: the contract holds
    // whatever the block does, and rests on it.
    verified std::size_t setting() const
        ensures (result <= 4096ul);
};

// A string by value: the string model's summaries cross units with the
// contract.
verified std::size_t suffixed_length(std::string text)
    ensures (result == text.size() + 1ul);
