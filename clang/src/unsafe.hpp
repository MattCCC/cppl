#pragma once

#include <clang-c/Index.h>

// Unsafe blocks (SPEC.md 26) and the callees whose unsafe code may write what
// they are handed (TRUST.md TCB-UNSAFE-004).
namespace cppl::clangbridge::detail {

class UnsafeEffects;

// Whether a call of `callee` may write, through unsafe code, storage it is handed
// by a `const` access path: a reference or a pointer parameter, a view, or its
// implicit object (TRUST.md TCB-UNSAFE-004). Defined beside `UnsafeEffects`,
// which decides which callees hold such code.
bool writes_unsafely(UnsafeEffects* effects, CXCursor callee);

} // namespace cppl::clangbridge::detail
