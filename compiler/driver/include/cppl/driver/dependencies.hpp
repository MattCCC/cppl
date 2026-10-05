#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::driver {

// The files a Make dependency rule Clang wrote for `target` names as its
// prerequisites, in order, with Clang's escapes undone: `\ ` and `\#` for a
// space and a `#` in a name, `$$` for a `$`, a doubled backslash before an
// escaped space, and a backslash before a line break joining lines. Every other
// backslash is part of a name, as in a Windows path.
//
// The text is one rule, as `-M -MF` writes it for one input with `-MT target`
// and no `-MP`. Anything else -- another target, a second rule, no rule -- is
// refused rather than read in part, since a file left out would be a file an
// interface is not bound to (SPEC.md TUBOUND-005).
[[nodiscard]] std::expected<std::vector<std::string>, std::string> make_prerequisites(std::string_view text,
                                                                                      std::string_view target);

} // namespace cppl::driver
