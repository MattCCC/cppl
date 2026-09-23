#pragma once

#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::lsp {

// The name a C++L declaration spells: the first identifier token inside its
// range that spells it.
[[nodiscard]] std::optional<source::ByteSpan> declared_name(const frontend::TokenStream& tokens,
                                                            const source::ByteSpan& range, std::string_view name);

// A file as Clang reads it for editor services.
//
// A file that holds C++L is given to Clang as the analysis projection the
// compiler makes (frontend::project), made here from the text as written
// rather than from the preprocessed unit: its `#include`s stay directives, so
// Clang reads the headers themselves and every position it reports in them is
// exact. A file with no C++L is read as it is.
//
// Every position Clang reports in the projection is traced back to the text as
// written, or to nothing: through a run the projection kept verbatim, through a
// run it copied into a generated declaration (Projection::copies), or through
// the generated declaration that stands for a Law or a refinement type.
// Generated text that is none of these, such as a probe's name, has no written
// position and is never shown.
class ProjectedFile {
  public:
    ProjectedFile(std::string path, std::string text);
    ~ProjectedFile();
    ProjectedFile(const ProjectedFile&) = delete;
    ProjectedFile& operator=(const ProjectedFile&) = delete;
    ProjectedFile(ProjectedFile&&) = delete;
    ProjectedFile& operator=(ProjectedFile&&) = delete;

    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }
    // The text as written.
    [[nodiscard]] const std::string& text() const noexcept {
        return text_;
    }
    // What Clang reads: the projection, or the text itself.
    [[nodiscard]] const std::string& analysis() const noexcept;

    // Whether the file holds C++L, and so is read as its projection.
    [[nodiscard]] bool projected() const noexcept;

    [[nodiscard]] const frontend::TokenStream& tokens() const noexcept {
        return *tokens_;
    }
    [[nodiscard]] const frontend::Syntax& syntax() const noexcept {
        return *syntax_;
    }

    // Where the byte at `written` is in the analysis text: where it was kept,
    // or its first copy in a generated declaration. Nothing when the
    // projection dropped it (a proof statement).
    [[nodiscard]] std::optional<std::size_t> to_analysis(std::size_t written) const;

    // Where the analysis text at `analysis` was written. A generated
    // declaration that stands for a Law or a refinement type maps its name to
    // the name the author wrote.
    [[nodiscard]] std::optional<std::size_t> to_written(std::size_t analysis) const;

    // Where the byte at `analysis` was written, when the projection kept it
    // where it was: never a byte of a generated declaration, even one copied
    // from what was written.
    [[nodiscard]] std::optional<std::size_t> kept(std::size_t analysis) const;

  private:
    std::string path_;
    // The tokens refer into it, which is why a projected file never moves.
    const std::string text_;
    std::unique_ptr<frontend::TokenStream> tokens_;
    std::unique_ptr<frontend::Syntax> syntax_;
    std::unique_ptr<frontend::Projection> projection_;

    // A generated name and the name it stands for.
    struct Anchor {
        std::size_t analysis = 0;
        source::ByteSpan written;
    };
    std::vector<Anchor> anchors_;
};

} // namespace cppl::lsp
