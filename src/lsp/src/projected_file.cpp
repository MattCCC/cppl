#include "cppl/lsp/projected_file.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::lsp {

std::optional<source::ByteSpan> declared_name(const frontend::TokenStream& tokens, const source::ByteSpan& range,
                                              std::string_view name) {
    for (const frontend::Token& token : tokens.tokens()) {
        if (token.span.offset < range.offset) {
            continue;
        }
        if (token.span.offset >= range.end() || token.kind == frontend::TokenKind::EndOfFile) {
            break;
        }
        if (token.is_identifier(name)) {
            return token.span;
        }
    }
    return std::nullopt;
}

ProjectedFile::ProjectedFile(std::string path, std::string text) : path_(std::move(path)), text_(std::move(text)) {
    tokens_ = std::make_unique<frontend::TokenStream>(frontend::lex(text_, path_));
    diagnostics::Engine engine;
    syntax_ = std::make_unique<frontend::Syntax>(frontend::recognize(*tokens_, engine));
    if (syntax_->empty()) {
        return;
    }
    frontend::ProjectionOptions options;
    // Generated names differ between files, as they do between the compiler's
    // units, so two projected files in one unit never declare the same name.
    options.unit_key = source::hash_bytes(std::filesystem::absolute(path_).string()).to_short_hex(12);
    projection_ = std::make_unique<frontend::Projection>(frontend::project(*tokens_, *syntax_, options));

    for (const frontend::SpecificationFunction& function : projection_->specification_functions) {
        if (function.law_index >= syntax_->laws.size()) {
            continue;
        }
        const frontend::LawDeclaration& law = syntax_->laws[function.law_index];
        if (const auto name = declared_name(*tokens_, law.range.span, law.name)) {
            anchors_.push_back(Anchor{function.analysis_offset, *name});
        }
    }
    for (const frontend::RefinementProbe& probe : projection_->refinement_probes) {
        if (probe.refinement_index >= syntax_->refinement_types.size()) {
            continue;
        }
        const frontend::RefinementType& refinement = syntax_->refinement_types[probe.refinement_index];
        if (const auto name = declared_name(*tokens_, refinement.range.span, refinement.name)) {
            anchors_.push_back(Anchor{probe.alias_offset, *name});
        }
    }
}

ProjectedFile::~ProjectedFile() = default;

const std::string& ProjectedFile::analysis() const noexcept {
    if (projection_ != nullptr) {
        return projection_->analysis;
    }
    return text_;
}

bool ProjectedFile::projected() const noexcept {
    return projection_ != nullptr;
}

std::optional<std::size_t> ProjectedFile::to_analysis(std::size_t written) const {
    if (projection_ == nullptr) {
        return written <= text_.size() ? std::optional<std::size_t>{written} : std::nullopt;
    }
    for (const frontend::Projection::Segment& segment : projection_->segments) {
        if (written >= segment.original && written < segment.original + segment.length) {
            return segment.analysis + (written - segment.original);
        }
    }
    // A Law's or a refinement type's own name: the generated declaration that
    // stands for it.
    for (const Anchor& anchor : anchors_) {
        if (written >= anchor.written.offset && written < anchor.written.end()) {
            return anchor.analysis + (written - anchor.written.offset);
        }
    }
    // Written inside a C++L declaration: its first copy, which for a Law's or
    // a proof's parameters is the declaration standing for it rather than a
    // probe that repeats them.
    for (const frontend::Projection::Copy& copy : projection_->copies) {
        if (written >= copy.original.offset && written < copy.original.end()) {
            return copy.analysis + (written - copy.original.offset);
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> ProjectedFile::to_written(std::size_t analysis) const {
    if (projection_ == nullptr) {
        return analysis <= text_.size() ? std::optional<std::size_t>{analysis} : std::nullopt;
    }
    for (const Anchor& anchor : anchors_) {
        if (analysis >= anchor.analysis && analysis <= anchor.analysis + anchor.written.length) {
            return anchor.written.offset + (analysis - anchor.analysis);
        }
    }
    // An offset at a segment's end is the end of what it copied, which is
    // where an extent ending on the segment's last byte ends.
    for (const frontend::Projection::Segment& segment : projection_->segments) {
        if (analysis >= segment.analysis && analysis <= segment.analysis + segment.length) {
            return segment.original + (analysis - segment.analysis);
        }
    }
    for (const frontend::Projection::Copy& copy : projection_->copies) {
        if (analysis >= copy.analysis && analysis <= copy.analysis + copy.original.length) {
            return copy.original.offset + (analysis - copy.analysis);
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> ProjectedFile::kept(std::size_t analysis) const {
    if (projection_ == nullptr) {
        return analysis < text_.size() ? std::optional<std::size_t>{analysis} : std::nullopt;
    }
    for (const frontend::Projection::Segment& segment : projection_->segments) {
        if (analysis >= segment.analysis && analysis < segment.analysis + segment.length) {
            return segment.original + (analysis - segment.analysis);
        }
    }
    return std::nullopt;
}

} // namespace cppl::lsp
