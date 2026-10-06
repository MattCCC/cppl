#pragma once

#include <clang-c/Index.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Unsafe blocks and the callees whose unsafe code may write what they are
// handed (unsafe.cpp, where each is documented).
namespace cppl::clangbridge::detail {

// Which functions may write, through an unsafe block, storage they were handed
// through a `const` access path or a view of `const` elements: one whose body
// holds an unsafe block, or that calls such a function, to a fixed point over
// the calls this unit defines, and one of another unit whose verification
// interface records its contract resting on an unsafe block. A caller models a
// call to one as writing every reference, pointer and view it hands over: an
// unsafe block is not trusted to respect the signature of the function that
// holds it (TRUST.md TCB-UNSAFE-004).
class UnsafeEffects {
  public:
    UnsafeEffects(std::string prefix, const std::vector<std::string>& imported);

    [[nodiscard]] bool of(CXCursor callee);

  private:
    static std::string key_of(CXCursor function);

    std::string prefix_;
    std::unordered_set<std::string> imported_;
    std::unordered_map<std::string, bool> known_;
};

std::optional<CXCursor> unsafe_marker_of(CXCursor statement, const std::string& prefix);

std::vector<CXCursor> unsafe_blocks_in(CXCursor root, const std::string& prefix, unsigned depth = 0);

std::unordered_set<unsigned> named_declarations(CXCursor root);

bool may_rebind(CXCursor root, CXCursor declaration, unsigned depth = 0);

// Whether a call of `callee` may write, through unsafe code, storage it is handed
// by a `const` access path: a reference or a pointer parameter, a view, or its
// implicit object (TRUST.md TCB-UNSAFE-004). Defined beside `UnsafeEffects`,
// which decides which callees hold such code.
bool writes_unsafely(UnsafeEffects* effects, CXCursor callee);

} // namespace cppl::clangbridge::detail
