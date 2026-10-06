#pragma once

// The state the projection is built in, and the text helpers its passes
// share; each pass is defined in the projection_*.cpp file named for what it
// projects.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend::detail::projector {

// Defined in projection.cpp.
const Directive* directive_at(const TokenStream& stream, std::size_t offset);

// Blanks `span` of `buffer`, a text of `stream`, keeping every newline and every
// preprocessor directive line within it byte for byte. Neither is C++L: a
// newline removed would move every line below it, and a `#pragma` or a line
// marker removed would change what Clang makes of everything after it (SPEC.md
// ERASE-005). `buffer` holds the stream's text from byte `base` on.
inline void blank(std::string& buffer, const source::ByteSpan& span, const TokenStream& stream, std::size_t base = 0) {
    const std::size_t end = std::min(span.end(), buffer.size());
    for (std::size_t offset = span.offset; offset < end; ++offset) {
        if (buffer[offset] == '\n') {
            continue;
        }
        if (const Directive* directive = directive_at(stream, base + offset); directive != nullptr) {
            offset = std::min(directive->span.end() - base, end) - 1;
            continue;
        }
        buffer[offset] = ' ';
    }
}

// Defined in projection.cpp.
bool lowering_moves_columns(const TokenStream& stream, const source::ByteSpan& span, const std::string& lowered);

std::string directives_within(const TokenStream& stream, const source::ByteSpan& span);

// Generated analysis text, and every run of it copied byte for byte from the
// scanned text, at offsets relative to the start of `text`.
struct Generated {
    std::string text;
    std::vector<Projection::Copy> copies;

    Generated& operator+=(std::string_view plain) {
        text += plain;
        return *this;
    }
    Generated& operator+=(const Generated& more) {
        for (const Projection::Copy& copy : more.copies) {
            copies.push_back(Projection::Copy{text.size() + copy.analysis, copy.original});
        }
        text += more.text;
        return *this;
    }
    // Appends the scanned text of `span`, recorded as a copy of it.
    void copy(const TokenStream& stream, const source::ByteSpan& span) {
        if (span.length != 0) {
            copies.push_back(Projection::Copy{text.size(), span});
        }
        text += stream.spelling(span);
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return text.size();
    }
    [[nodiscard]] bool empty() const noexcept {
        return text.empty();
    }
};

struct Edit {
    source::ByteSpan span;
    std::string replacement;
    std::optional<std::size_t> specification_index = std::nullopt;
    std::optional<std::size_t> refinement_index = std::nullopt;
    std::vector<Projection::Copy> copies = {};
};

inline Edit generated_edit(const source::ByteSpan& span, Generated replacement,
                           std::optional<std::size_t> specification_index = std::nullopt,
                           std::optional<std::size_t> refinement_index = std::nullopt) {
    return Edit{span, std::move(replacement.text), specification_index, refinement_index,
                std::move(replacement.copies)};
}

// Defined in projection.cpp.
Generated at_written_position(const TokenStream& stream, const source::ByteSpan& expression);

std::string resume_at(const TokenStream& stream, std::size_t offset);

std::string spelled_tokens(const TokenStream& stream, const source::ByteSpan& span);

source::ByteSpan token_extent(const TokenStream& stream, const source::ByteSpan& span);

std::string spelled_indices(const TokenStream& stream, const RefinementType& refinement);

// The analysis text gives every Law, and every proof, a home no ordinary C++
// can look into: a namespace with a reserved name, nested in the namespace the
// declaration is written in. A Law then has no runtime callable identity even
// for the analysis (SPEC.md ERASE-005, LAW-003): ordinary code never names
// that namespace and no using-directive ordinary code can see nominates it, so
// ordinary name lookup, overload resolution and argument-dependent lookup find
// exactly the declarations the runtime program has.
//
// Laws and proofs see one another as C++ scoping would have them see ordinary
// declarations: a site's namespace nominates the formal namespace of every
// enclosing namespace, and of every unnamed or inline namespace whose members
// those namespaces see, that an earlier site created. Each is named after its
// path, so the name is the same for the same namespace in every text that
// declares it, and no other namespace's formal namespace can answer to it.
struct FormalScopes {
    std::vector<std::string> laws;   // the text opening each Law's namespace
    std::vector<std::string> proofs; // and each proof's
};

// Defined in projection_scopes.cpp.
FormalScopes formal_scopes(const TokenStream& stream, const Syntax& syntax, const ProjectionOptions& options);

void refuse_misplaced_directives(const TokenStream& stream, const Syntax& syntax,
                                 std::vector<diagnostics::Diagnostic>& diagnostics);

// What project() builds a projection in: the scanned text and its syntax,
// the runtime text and the edits that make the analysis text, and the
// template header the declaration being emitted stands under. Each pass is
// one member, run in the order project() runs them.
struct Projector {
    Projector(const TokenStream& scanned, const Syntax& recognized, const ProjectionOptions& requested)
        : stream(scanned),
          syntax(recognized),
          options(requested),
          text(stream.text()) {
        projection.runtime.assign(text);
        edits.reserve(syntax.laws.size() + syntax.proofs.size() + syntax.pure_markers.size());
    }

    const TokenStream& stream;
    const Syntax& syntax;
    const ProjectionOptions& options;
    const std::string_view text;

    Projection projection;

    std::vector<Edit> edits;

    // The template header the declaration being emitted stands under, where it
    // has one. A contract clause may name the template's parameters, so its
    // probe has to be declared under the same header; laws and proofs are not
    // templated and leave this empty.
    std::string template_header;

    // Whether the declaration being projected is a template, as opposed to an
    // explicit specialization whose `template <>` declares no parameters. A
    // specialization's probes are ordinary functions: its arguments are fixed,
    // so there is nothing to specialize and nothing to instantiate.
    bool template_parameters = false;

    // Whether the declaration being projected is a member function with an
    // implicit object. Its probes are then members of the same class, stated
    // `const`: a contract is resolved in the scope the body sees, with `this`
    // and ordinary member lookup (SPEC.md CONTRACT-008), and reads the object
    // without writing it. A static member's probes are static members, which
    // the ordinary `static` prefix already declares inside a class.
    bool implicit_object = false;

    // What every generated declaration is introduced by. A templated probe
    // cannot be `static`: it is a template, and its header has to precede the
    // declaration it introduces. Nor can a probe of a member function with an
    // implicit object: it has one too.
    [[nodiscard]] bool templated() const {
        return template_parameters;
    }
    [[nodiscard]] std::string declaration_prefix() const {
        std::string prefix;
        if (templated()) {
            prefix += template_header;
            prefix += " [[maybe_unused]] ";
            return prefix;
        }
        if (implicit_object) {
            return {"[[maybe_unused]] "};
        }
        prefix += "[[maybe_unused]] static ";
        return prefix;
    }
    // What follows a probe's parameter list: `const` for a member function's
    // probe, which reads the implicit object and never writes it.
    [[nodiscard]] std::string probe_qualifier() const {
        return implicit_object ? std::string(" const") : std::string();
    }

    // A declaration becomes an ordinary C++ function stating the proposition it
    // carries, emitted where the declaration stood. Everything after this point
    // in the analysis text is C++ that Clang resolves on its own.
    Generated emit(std::string_view name, const Generated& parameters, const source::ByteSpan& expression,
                   const source::SourceLocation& begin, std::uint32_t end_line, std::size_t* name_offset = nullptr,
                   std::string* proposition_name = nullptr);

    // A directive written inside the declaration follows the namespace, in the
    // namespace the declaration stands in, as it does in the program.
    [[nodiscard]] Generated in_formal_scope(const std::string& opening, const Generated& declarations,
                                            const source::ByteSpan& span) const;

    // A claim that a path cannot occur is proof syntax in runtime code. The
    // program keeps its `;`, so an empty statement stands where it was written
    // and whatever statement it was the body of still has one. Clang is given a
    // block at the same point instead, which resolves the evidence's arguments
    // in the scope the statement sees (SPEC.md VERIFIED-023).
    [[nodiscard]] std::string claim_marker(std::size_t index) const {
        return options.generated_prefix + "contradiction_" + std::to_string(index) +
               (options.unit_key.empty() ? "" : "_" + options.unit_key);
    }
    [[nodiscard]] Generated claim_block(const std::string& name, const ProofStatement& statement) const;

    // `pure`, `unsafe` and `ghost` markers.
    void project_markers();

    void project_refinements();

    void project_validations();

    void project_laws(const FormalScopes& formal);

    void project_proofs(const FormalScopes& formal);

    void project_contracts();

    void project_loops();

    void project_path_claims();

    void project_path_splits();

    void project_explicit_instantiations();

    void assemble();
};

} // namespace cppl::frontend::detail::projector
