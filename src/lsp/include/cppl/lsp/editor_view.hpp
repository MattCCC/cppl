#pragma once

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/hover.hpp"
#include "cppl/lsp/projected_file.hpp"
#include "cppl/lsp/protocol.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cppl::lsp {

// An open buffer, as the view needs it: where it is, what it says now.
struct OpenBuffer {
    std::string uri;
    std::string path;
    const std::string* text = nullptr;
};

// One document as Clang sees it for editor services: the translation unit Clang
// parsed from the document's projection, and every file in it that was read as
// its projection too (tools/cppl-lsp/README.md, "Navigation").
//
// Clang answers every question about C++, and the view only asks and maps the
// answer back to where it was written. A position Clang reports that cannot be
// traced to text an author wrote is not reported at all.
class EditorView {
  public:
    struct Options {
        std::string driver;
        std::vector<std::string> arguments;
    };

    // Brings the view up to date with `document` and every other open buffer.
    // False when Clang could not parse the unit, which leaves every query
    // empty rather than answered from an older unit.
    bool refresh(const OpenBuffer& document, const std::vector<OpenBuffer>& open, const Options& options);

    [[nodiscard]] std::vector<Location> navigate(clangbridge::Destination destination, const Position& position) const;

    // What the name at `position` denotes, in a form every open document's
    // view can look up in its own unit.
    struct Target {
        std::vector<std::string> usrs;
        std::string name;
        // Where it was declared, when that is traced to written text.
        std::optional<Location> declaration;
    };
    [[nodiscard]] std::optional<Target> target_at(const Position& position) const;

    struct Mention {
        Location location;
        clangbridge::Role role = clangbridge::Role::Read;
    };

    // Every place this view's unit writes the target's name, traced to written
    // text. A parameter the projection repeats in several generated
    // declarations -- a proof's, in each probe it has -- is one name the author
    // wrote once, so a declaration Clang reports at the same written position
    // is the target too.
    [[nodiscard]] std::vector<Mention> mentions(const Target& target) const;

    // What hover shows for the name at `position`, and the C++L declaration it
    // shows when the name stands for one. Such a name is shown as that
    // declaration, never as what the projection generated for it; a name only
    // generated code declares shows what it means (`result`, `self`) or
    // nothing.
    struct HoverAnswer {
        Hover hover;
        std::optional<CpplDeclaration> declaration;
    };
    [[nodiscard]] std::optional<HoverAnswer> hover(const Position& position) const;

    // The C++L declaration whose name is written at `location`.
    [[nodiscard]] std::optional<CpplDeclaration> cppl_declaration_at(const Location& location) const;

    // What may be written at `position`: Clang's names and keywords, and C++L's
    // own words where the grammar admits them (completion.hpp).
    [[nodiscard]] CompletionList complete(const Position& position, bool snippets) const;

    // The declarations the call whose arguments are being written at
    // `position` could resolve to, and the argument being written. Nothing
    // outside a call's parentheses.
    [[nodiscard]] std::optional<SignatureHelp> signature_help(const Position& position) const;

    // The document's outline: each declaration Clang finds whose name the
    // author wrote as C++, nested as it is declared, and each C++L declaration
    // among them where it is written (symbols.hpp). A declaration the
    // projection generated is never in it.
    [[nodiscard]] std::vector<DocumentSymbol> outline() const;

    // Where the document may fold (structure.hpp): each body, `#include` run and
    // conditional branch Clang parsed, each C++L body and bodiless declaration
    // the recognizer found, and each comment block or run of whole-line
    // comments. A body folds between its braces; a client that folds whole
    // lines keeps the line of its `}` in view.
    [[nodiscard]] std::vector<FoldingRange> folding_ranges(bool line_folding_only) const;

    // What selecting outward from `position` selects, innermost first: the
    // token there, then each construct Clang parsed and each C++L construct
    // the recognizer found that holds it, each holding the one before.
    [[nodiscard]] std::vector<Range> selection(const Position& position) const;

    // The hints Clang gives for the part of the document `range` covers, each
    // where the text it annotates was written: an argument inside a Law's
    // proposition is annotated where the Law writes it, and nothing the
    // projection generated is annotated at all.
    [[nodiscard]] std::vector<InlayHint> inlay_hints(const Range& range) const;

    // The number of refreshes that had to parse the unit from scratch, for
    // tests that check an edit reuses what Clang kept.
    [[nodiscard]] std::size_t parses() const noexcept {
        return parses_;
    }

  private:
    // The byte a request at `position` names: the one under it, or the one
    // before it when the position is just past a name.
    [[nodiscard]] std::optional<std::size_t> request_offset(const Position& position) const;

    // Where text inserted at `written` of the document would go in the
    // analysis text: at that byte, or just past the byte before it.
    [[nodiscard]] std::optional<std::size_t> insertion_point(std::size_t written) const;

    // Where an extent Clang reported was written, when it can be traced there.
    [[nodiscard]] std::optional<Location> locate(const clangbridge::Extent& extent) const;

    // The file an open buffer, the document or a projected header is, by URI.
    [[nodiscard]] const ProjectedFile* file_for(const std::string& uri) const;

    // Headers the unit includes that hold C++L, read as their projection, and
    // headers found to hold none, each with the write time it was read at.
    struct Header {
        std::filesystem::file_time_type written;
        std::shared_ptr<const ProjectedFile> file; // null when it holds no C++L
    };

    [[nodiscard]] std::vector<clangbridge::FileContent> unsaved() const;
    // Adds every included C++L header the view has not read yet; true when one
    // was added.
    bool read_included_headers();

    std::string uri_;
    std::unique_ptr<ProjectedFile> main_;
    std::map<std::string, std::shared_ptr<const ProjectedFile>> buffers_; // other open buffers, by normal path
    std::map<std::string, std::string> buffer_uris_;
    std::map<std::string, Header> headers_;
    std::unique_ptr<clangbridge::EditorUnit> unit_;
    Options options_;
    std::size_t parses_ = 0;
};

// The normal form of a path Clang reports, so two spellings of one file compare
// equal.
[[nodiscard]] std::string normal_path(const std::string& path);

// The Clang driver named by `configured`, by path: a bare name is looked up on
// PATH, as a shell would, since Clang finds its own headers relative to where
// its driver is.
[[nodiscard]] std::string resolve_driver(const std::string& configured);

} // namespace cppl::lsp
