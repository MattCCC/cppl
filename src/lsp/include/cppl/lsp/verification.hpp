#pragma once

#include "cppl/driver/buffer_compile.hpp"
#include "cppl/lsp/document.hpp"
#include "cppl/lsp/hover.hpp"
#include "cppl/lsp/protocol.hpp"

#include <string>
#include <vector>

namespace cppl::lsp {

// Verification status as an editor shows it.
//
// Every status here is one the kernel's verdicts gave, copied out by the
// compile (driver::ObligationRecord); nothing here decides one, so an editor
// can say PROVEN only where the trust report would. A status is shown only for
// the version of the buffer it was computed for: after an edit, until the next
// compile, a declaration shows none rather than one for text that has changed.

// The obligations of a Law, a proof or a verified function, from what the last
// compile of `document` recorded.
[[nodiscard]] std::vector<const driver::ObligationRecord*> obligations_of(const Document& document,
                                                                          const CpplDeclaration& declaration);

// One lens per Law, proof and verified function the document writes, over its
// name, stating the verdicts of its obligations.
[[nodiscard]] std::vector<CodeLens> verification_lenses(const Document& document);

// The verdicts of `declaration`'s obligations, in markdown, for hover: each
// one's status, what it rests on, its goal, and why it is not proven when it is
// not. Empty when the declaration has no obligations of its own.
[[nodiscard]] std::string verification_markdown(const Document& document, const CpplDeclaration& declaration);

} // namespace cppl::lsp
