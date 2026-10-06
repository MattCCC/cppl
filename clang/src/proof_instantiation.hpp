#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"

#include <clang-c/Index.h>
#include <vector>

namespace cppl::clangbridge::detail {

// Every place where proof-only text may make the program verified instantiate
// a template the program run does not instantiate there, refused where it
// stands (SPEC.md ERASE-019, TRUST.md TCB-SOURCE-010).
//
// An instantiation is not local to the text that causes it: a class template's
// specialization is instantiated once, at its first use, and what it declares
// -- a friend's definition, above all -- and what it computes stay for the rest
// of the unit. A use only proof-only text makes therefore changes what ordinary
// C++ after it means in the program verified and nowhere in the program run.
//
// The proof-only text is what `selection.proof_only` spans. `specializations`
// are the function template specializations the unit instantiated whose bodies
// hold some of it: a verified template's, and the contract probes instantiated
// with it, which no walk of the declarations reaches.
[[nodiscard]] std::vector<Diagnostic> proof_only_instantiations(CXTranslationUnit unit, const Selection& selection,
                                                                const std::vector<CXCursor>& specializations);

} // namespace cppl::clangbridge::detail
