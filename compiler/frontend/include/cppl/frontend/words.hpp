#pragma once

#include <span>
#include <string_view>

namespace cppl::frontend {

// Every word the specification gives a C++L meaning somewhere: the contextual
// words (SPEC.md 3), the names with a meaning inside specification contexts
// (WORD-001), the proof statements (WORD-002), and the words of a case
// omission (WORD-010). None is reserved: outside its context each is an
// ordinary C++ identifier. Proof-arm labels (WORD-005) are not among them.
[[nodiscard]] std::span<const std::string_view> cppl_words() noexcept;

[[nodiscard]] bool is_cppl_word(std::string_view word) noexcept;

} // namespace cppl::frontend
