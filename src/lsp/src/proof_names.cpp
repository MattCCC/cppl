#include "cppl/lsp/proof_names.hpp"

#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/lsp/document.hpp"
#include "cppl/lsp/editor_view.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

bool is_name_byte(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '_' || byte >= 0x80;
}

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// What a file holds now: the editor's buffer when it is open, else the disk.
std::optional<std::string> current_text(const DocumentManager& documents, const std::string& file, std::string& uri) {
    const std::string key = normal_path(file);
    std::optional<std::string> found;
    documents.for_each([&](const Document& document) {
        if (!found.has_value() && normal_path(document.path()) == key) {
            found = document.text();
            uri = document.uri();
        }
    });
    if (found.has_value()) {
        return found;
    }
    uri = path_to_uri(key);
    return read_file(file);
}

// Every `assume` a proof body binds, in arms too.
void assumptions(const std::vector<frontend::ProofStatement>& statements,
                 std::vector<const frontend::ProofStatement*>& found) {
    for (const frontend::ProofStatement& statement : statements) {
        if (statement.kind == frontend::ProofStatementKind::Assume) {
            found.push_back(&statement);
        }
        for (const frontend::ProofArm& arm : statement.arms) {
            assumptions(arm.statements, found);
        }
    }
}

} // namespace

std::optional<std::size_t> spelled_at(const std::string& text, const source::SourceLocation& at,
                                      const std::string& name) {
    if (!at.is_valid() || at.column == 0 || name.empty()) {
        return std::nullopt;
    }
    std::size_t line_start = 0;
    for (std::uint32_t line = 1; line < at.line; ++line) {
        const std::size_t newline = text.find('\n', line_start);
        if (newline == std::string::npos) {
            return std::nullopt;
        }
        line_start = newline + 1;
    }
    const std::size_t offset = line_start + at.column - 1;
    if (offset + name.size() > text.size() || text.compare(offset, name.size(), name) != 0) {
        return std::nullopt;
    }
    if (offset + name.size() < text.size() && is_name_byte(text[offset + name.size()])) {
        return std::nullopt;
    }
    return offset;
}

bool same_place(const source::SourceLocation& lhs, const source::SourceLocation& rhs) {
    return lhs.line == rhs.line && lhs.column == rhs.column && normal_path(lhs.file) == normal_path(rhs.file);
}

ProofNames::ProofNames(const DocumentManager& documents) : documents_(documents) {}

std::optional<ProofNames::Declaration> ProofNames::declaration_at(const Document& document,
                                                                  const Position& position) const {
    const std::string& text = document.text();
    const std::size_t offset = PositionMapper(text).position_to_byte_offset(position);
    const std::string path = normal_path(document.path());
    const auto here = [&](const source::SourceLocation& at, const std::string& name) {
        if (normal_path(at.file) != path) {
            return false;
        }
        const std::optional<std::size_t> start = spelled_at(text, at, name);
        return start.has_value() && offset >= *start && offset <= *start + name.size();
    };

    // A use the compiler resolved here.
    for (const elaboration::ResolvedName& name : document.resolved_names()) {
        if (here(name.at, name.name)) {
            return Declaration{name.declaration, name.name};
        }
    }
    // A declaration written here, as the buffer reads now.
    if (const frontend::Syntax* syntax = document.syntax()) {
        for (const frontend::ProofDeclaration& proof : syntax->proofs) {
            if (here(proof.name_location, proof.name)) {
                return Declaration{proof.name_location, proof.name};
            }
            std::vector<const frontend::ProofStatement*> bound;
            assumptions(proof.statements, bound);
            for (const frontend::ProofStatement* statement : bound) {
                if (here(statement->reference_location, statement->reference)) {
                    return Declaration{statement->reference_location, statement->reference};
                }
            }
        }
        for (const frontend::LawDeclaration& law : syntax->laws) {
            if (here(law.name_location, law.name)) {
                return Declaration{law.name_location, law.name};
            }
        }
    }
    return std::nullopt;
}

std::optional<Location> ProofNames::locate(const Declaration& declaration) const {
    std::string uri;
    const std::optional<std::string> text = current_text(documents_, declaration.at.file, uri);
    if (!text.has_value()) {
        return std::nullopt;
    }
    const std::optional<std::size_t> start = spelled_at(*text, declaration.at, declaration.name);
    if (!start.has_value()) {
        return std::nullopt;
    }
    const PositionMapper mapper(*text);
    return Location{uri, Range{mapper.byte_offset_to_position(*start),
                               mapper.byte_offset_to_position(*start + declaration.name.size())}};
}

std::vector<Location> ProofNames::uses_of(const Declaration& declaration) const {
    std::vector<Location> uses;
    documents_.for_each([&](const Document& document) {
        for (const elaboration::ResolvedName& name : document.resolved_names()) {
            if (name.name != declaration.name || !same_place(name.declaration, declaration.at)) {
                continue;
            }
            std::optional<Location> use = locate(Declaration{name.at, name.name});
            if (!use.has_value()) {
                continue;
            }
            const bool repeated = std::ranges::any_of(uses, [&](const Location& known) {
                return known.uri == use->uri && known.range.start.line == use->range.start.line &&
                       known.range.start.character == use->range.start.character;
            });
            if (!repeated) {
                uses.push_back(std::move(*use));
            }
        }
    });
    return uses;
}

} // namespace cppl::lsp
