#pragma once

#include "cppl/source/location.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace cppl::clangbridge {

// A file's text as Clang reads it in place of what is on disk: an editor's
// unsaved buffer, or the projection of a file that holds C++L.
struct FileContent {
    std::string path;
    std::string text;
};

// A position as Clang reports it.
struct FilePosition {
    // The file Clang read and the position in the text it read: 1-based line,
    // 1-based byte column and 0-based byte offset. Empty when Clang names none.
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::size_t offset = 0;

    // The same position as the file's line directives state it.
    source::SourceLocation presumed;

    bool in_main_file = false;
    bool in_system_header = false;
};

// A run of text in one file, from `begin` up to but not including `end`.
struct Extent {
    FilePosition begin;
    FilePosition end;
};

// What a navigation request asks for (LSP `textDocument/definition` and kin).
enum class Destination : std::uint8_t {
    Definition,
    Declaration,
    TypeDefinition,
    Implementation,
};

// One translation unit Clang parsed for editor services, kept so that the next
// request after an edit costs a reparse rather than a parse.
//
// Clang answers every question about ordinary C++ here, as it does for the
// compiler (AGENTS.md 14); this type only asks and reports where Clang says the
// answer is. It knows nothing of C++L: a file holding C++L reaches it as the
// projection the caller made of it, and mapping what Clang reports back to the
// text as written is the caller's.
//
// Not safe for concurrent use: one thread asks at a time.
class EditorUnit {
  public:
    struct Options {
        // The Clang driver whose installation supplies the standard library and
        // system headers, as it does when it compiles. It must name the
        // executable by path: Clang finds its own headers relative to it.
        std::string driver;
        // Compile flags: include paths, defines, `-std=`. Never the input or
        // an output.
        std::vector<std::string> arguments;
    };

    // Parses `main` as a C++ translation unit, reading `unsaved` files in place
    // of the disk. An error means Clang could not run at all; a program with
    // errors in it is parsed like any other, with recovery.
    [[nodiscard]] static std::expected<std::unique_ptr<EditorUnit>, std::string> parse(
        Options options, FileContent main, std::vector<FileContent> unsaved);

    ~EditorUnit();
    EditorUnit(const EditorUnit&) = delete;
    EditorUnit& operator=(const EditorUnit&) = delete;
    EditorUnit(EditorUnit&&) = delete;
    EditorUnit& operator=(EditorUnit&&) = delete;

    // Parses the unit again with new text for its main file and the files read
    // in place of the disk, reusing what Clang kept of the headers they include.
    [[nodiscard]] std::expected<void, std::string> reparse(FileContent main, std::vector<FileContent> unsaved);

    [[nodiscard]] const std::string& main_path() const noexcept;
    [[nodiscard]] const std::string& main_text() const noexcept;

    // Where the name written at `offset` of the main file's text is declared,
    // defined, typed or overridden, as `destination` asks, each as the extent
    // of the name at that declaration. An `#include` leads to the start of the
    // file it includes. Empty when no name is written there.
    [[nodiscard]] std::vector<Extent> navigate(Destination destination, std::size_t offset) const;

    // Every file the unit includes, directly or through another, that is not a
    // system header, by the path Clang opened it at.
    [[nodiscard]] std::vector<std::string> included_files() const;

  private:
    struct State;
    explicit EditorUnit(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace cppl::clangbridge
