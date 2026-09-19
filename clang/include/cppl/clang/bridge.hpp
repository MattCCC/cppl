#pragma once

#include <expected>
#include <string>
#include <vector>

#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"

namespace cppl::clangbridge {

// Which declarations the bridge should describe in full.
//
// A preprocessed translation unit contains every declaration of every header it
// included. C++L only needs the declarations its formal layer refers to, so the
// caller names them: by the generated specification-function prefix, and by the
// physical analysis-buffer offset of declarations the frontend marked.
struct Selection {
    std::string specification_prefix;
    std::vector<std::size_t> offsets;
};

struct ParseRequest {
    std::string path;
    std::vector<std::string> arguments;
    Selection selection;
};

// Parses with Clang and returns the resolved semantic facts.
//
// An error result means the bridge itself could not run. A translation unit
// that Clang rejected is returned normally, with has_errors set and Clang's
// diagnostics attached: those are the user's C++ errors, not bridge failures.
[[nodiscard]] std::expected<TranslationUnit, std::string> parse(const ParseRequest& request);

// The libclang version backing this build, for trust reporting.
[[nodiscard]] std::string clang_version();

}  // namespace cppl::clangbridge
