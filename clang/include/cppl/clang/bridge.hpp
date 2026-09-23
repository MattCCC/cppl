#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/source/projection.hpp"

#include <expected>
#include <string>
#include <vector>

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
    std::vector<std::size_t> verified_offsets;
    struct PropositionProbe {
        std::string name;
        source::ProjectionShape shape;

        // The clause probe whose proposition this one states. A memory
        // capability is attributed through it, because the capability
        // constrains the function that clause belongs to.
        std::string owner;
    };
    std::vector<PropositionProbe> proposition_probes;

    // Which verified function each contract clause probe belongs to, by the
    // analysis offset of that function's declaration. A memory capability a
    // clause states constrains that function's body and no other
    // (SPEC.md VERIFIED-043).
    struct ClauseOwner {
        std::string probe;
        std::size_t function_offset = 0;
    };
    std::vector<ClauseOwner> clause_owners;

    // The refinement types declared in this unit (SPEC.md 17). Clang resolves
    // their aliases like any other, so the bridge needs the names to tell a
    // refinement apart from an ordinary alias to the same base type.
    struct Refinement {
        std::string name;
        std::string probe; // the generated function stating its predicate
        std::size_t index_count = 0;
        std::size_t alias_offset = 0;
    };
    std::vector<Refinement> refinements;
};

struct ParseRequest {
    std::string path;
    std::vector<std::string> arguments;
    Selection selection;
    std::optional<std::string> content;
    bool recover_bindings = false;
    bool recover_contract_types = false;
};

// Parses with Clang and returns the resolved semantic facts.
//
// An error result means the bridge itself could not run. A translation unit
// that Clang rejected is returned normally, with has_errors set and Clang's
// diagnostics attached: those are the user's C++ errors, not bridge failures.
// Such a unit describes no declarations.
[[nodiscard]] std::expected<TranslationUnit, std::string> parse(const ParseRequest& request);

// The libclang version backing this build, for trust reporting.
[[nodiscard]] std::string clang_version();

} // namespace cppl::clangbridge
