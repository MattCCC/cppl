#pragma once

#include "cppl/lsp/protocol.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::lsp {

// What a rename may write, and what it may not change (tools/cppl-lsp/README.md,
// "Rename"). Which places a rename edits is what references finds; these say
// whether the edit is safe to make.

// Why `name` cannot be written in place of a name, or nothing when it can: it
// must be a C++ identifier, and not a C++ keyword or alternative token.
[[nodiscard]] std::optional<std::string> refuse_identifier(std::string_view name);

// `text` with `edits` applied, their ranges counted in UTF-16 units as the
// protocol counts them. The edits do not overlap.
[[nodiscard]] std::string apply_edits(const std::string& text, const std::vector<TextEdit>& edits);

// Why rewriting the file at `path` from `text` by `edits`, each of which
// writes `name`, would change the C++L the file holds, or nothing when it
// would not. The frontend answers, never the server. A C++L word may be written
// only outside every C++L construct, where it is an ordinary identifier
// (SPEC.md 3). And the recognizer must read the rewritten file as the same
// constructs, each proof with the same statements.
[[nodiscard]] std::optional<std::string> changes_cppl(const std::string& path, const std::string& text,
                                                      const std::vector<TextEdit>& edits, std::string_view name);

} // namespace cppl::lsp
