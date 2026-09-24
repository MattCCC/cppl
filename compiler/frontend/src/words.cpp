#include "cppl/frontend/words.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace cppl::frontend {

namespace {

constexpr auto kWords = std::to_array<std::string_view>(
    {// SPEC.md 3
     "law", "proof", "proves", "pure", "verified", "ghost", "unsafe", "trusted", "type", "where", "expects", "ensures",
     "decreases", "invariant", "forall", "exists",
     // WORD-001
     "result", "old", "self", "readable", "writable", "size", "at", "contains",
     // WORD-002
     "refl", "exact", "apply", "assume", "rewrite", "contradiction", "cases", "decompose", "induction",
     // WORD-010
     "omit", "by"});

} // namespace

std::span<const std::string_view> cppl_words() noexcept {
    return kWords;
}

bool is_cppl_word(std::string_view word) noexcept {
    return std::ranges::find(kWords, word) != kWords.end();
}

} // namespace cppl::frontend
