#include "cppl/lsp/symbols.hpp"

#include "cppl/clang/editor.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/projected_file.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

// A proposition or a type as written, on one line, and no longer than an
// outline has room for.
std::string one_line(std::string_view text) {
    constexpr std::size_t kLimit = 60;
    std::string result;
    bool space = false;
    for (const char character : text) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            space = !result.empty();
            continue;
        }
        if (space) {
            result.push_back(' ');
            space = false;
        }
        result.push_back(character);
    }
    if (result.size() > kLimit) {
        std::size_t cut = kLimit;
        // Never inside a UTF-8 sequence.
        while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0U) == 0x80U) {
            --cut;
        }
        result.resize(cut);
        result += "...";
    }
    return result;
}

bool before(const Position& lhs, const Position& rhs) {
    return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.character < rhs.character);
}

bool holds(const Range& outer, const Range& inner) {
    return !before(inner.start, outer.start) && !before(outer.end, inner.end);
}

bool encloses(SymbolKind kind) {
    return kind == SymbolKind::Namespace || kind == SymbolKind::Class || kind == SymbolKind::Struct;
}

// A declaration the recognizer found, with its name where the recognizer
// found it.
DocumentSymbol written_symbol(const PositionMapper& mapper, source::ByteSpan range, source::ByteSpan name,
                              const std::string& spelled, SymbolKind kind, std::string detail) {
    DocumentSymbol symbol;
    symbol.name = spelled;
    symbol.detail = std::move(detail);
    symbol.kind = kind;
    symbol.range = mapper.byte_span_to_range(range);
    symbol.selection = mapper.byte_span_to_range(name);
    return symbol;
}

} // namespace

SymbolKind symbol_kind(clangbridge::Symbol::Kind kind) {
    switch (kind) {
        case clangbridge::Symbol::Kind::Namespace:
            return SymbolKind::Namespace;
        case clangbridge::Symbol::Kind::Class:
            return SymbolKind::Class;
        case clangbridge::Symbol::Kind::Struct:
        case clangbridge::Symbol::Kind::Union:
            return SymbolKind::Struct;
        case clangbridge::Symbol::Kind::Enum:
            return SymbolKind::Enum;
        case clangbridge::Symbol::Kind::Enumerator:
            return SymbolKind::EnumMember;
        case clangbridge::Symbol::Kind::Function:
            return SymbolKind::Function;
        case clangbridge::Symbol::Kind::Method:
            return SymbolKind::Method;
        case clangbridge::Symbol::Kind::Constructor:
            return SymbolKind::Constructor;
        case clangbridge::Symbol::Kind::Field:
            return SymbolKind::Field;
        case clangbridge::Symbol::Kind::Variable:
            return SymbolKind::Variable;
        case clangbridge::Symbol::Kind::TypeAlias:
            return SymbolKind::Class;
        case clangbridge::Symbol::Kind::Macro:
            return SymbolKind::Constant;
        case clangbridge::Symbol::Kind::Concept:
            return SymbolKind::Interface;
    }
    return SymbolKind::Variable;
}

std::vector<DocumentSymbol> cppl_symbols(const ProjectedFile& file) {
    std::vector<DocumentSymbol> symbols;
    if (!file.projected()) {
        return symbols;
    }
    const frontend::Syntax& syntax = file.syntax();
    const PositionMapper mapper(file.text());

    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        const frontend::LawDeclaration& law = syntax.laws[index];
        // A Law written with its proof owns the proof's body too.
        source::ByteSpan range = law.range.span;
        for (const frontend::ProofDeclaration& proof : syntax.proofs) {
            if (proof.inline_law == index && proof.range.span.end() > range.end()) {
                range.length = proof.range.span.end() - range.offset;
            }
        }
        symbols.push_back(written_symbol(mapper, range, law.name_span, law.name, SymbolKind::Interface,
                                         law.trusted ? "trusted law" : "law"));
    }
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        if (proof.inline_law.has_value()) {
            continue;
        }
        const std::string proposition =
            one_line(std::string_view(file.text().data() + proof.proposition.offset, proof.proposition.length));
        symbols.push_back(written_symbol(mapper, proof.range.span, proof.name_span, proof.name, SymbolKind::Function,
                                         proposition.empty() ? "proof" : "proves (" + proposition + ")"));
    }
    for (const frontend::RefinementType& refinement : syntax.refinement_types) {
        const std::string_view base(file.text().data() + refinement.base.offset, refinement.base.length);
        symbols.push_back(written_symbol(mapper, refinement.range.span, refinement.name_span, refinement.name,
                                         SymbolKind::Class, "refinement of " + one_line(base)));
    }
    return symbols;
}

std::string specifiers_of(const ProjectedFile& file, std::size_t offset) {
    std::string specifiers;
    if (!file.projected()) {
        return specifiers;
    }
    const frontend::Syntax& syntax = file.syntax();
    if (std::ranges::any_of(syntax.verified_functions, [offset](const frontend::VerifiedFunction& function) {
            return function.function_offset == offset;
        })) {
        specifiers += "verified ";
    }
    if (std::ranges::any_of(syntax.pure_markers, [offset](const frontend::PureMarker& marker) {
            return marker.function_offset == offset;
        })) {
        specifiers += "pure ";
    }
    return specifiers;
}

void place_symbol(std::vector<DocumentSymbol>& outline, DocumentSymbol symbol) {
    for (DocumentSymbol& candidate : outline) {
        if (encloses(candidate.kind) && holds(candidate.range, symbol.range)) {
            place_symbol(candidate.children, std::move(symbol));
            return;
        }
    }
    const auto later = std::ranges::find_if(
        outline, [&symbol](const DocumentSymbol& known) { return before(symbol.range.start, known.range.start); });
    outline.insert(later, std::move(symbol));
}

} // namespace cppl::lsp
