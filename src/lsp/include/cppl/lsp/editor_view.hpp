#pragma once

#include "cppl/clang/editor.hpp"
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

    // The number of refreshes that had to parse the unit from scratch, for
    // tests that check an edit reuses what Clang kept.
    [[nodiscard]] std::size_t parses() const noexcept {
        return parses_;
    }

  private:
    // The byte a request at `position` names: the one under it, or the one
    // before it when the position is just past a name.
    [[nodiscard]] std::optional<std::size_t> request_offset(const Position& position) const;

    // Where an extent Clang reported was written, when it can be traced there.
    [[nodiscard]] std::optional<Location> locate(const clangbridge::Extent& extent) const;

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
