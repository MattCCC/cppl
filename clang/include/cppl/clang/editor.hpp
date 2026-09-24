#pragma once

#include "cppl/source/location.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// How a name is used where it is written.
enum class Role : std::uint8_t {
    Declaration,
    Read,
    // The target of an assignment, a compound assignment or an increment.
    Write,
};

// One place a name is written.
struct Occurrence {
    Extent name;
    std::string usr;
    Role role = Role::Read;
};

// What a written name denotes: Clang's identity for it, which every unit that
// declares it gives it, its spelling, and where it is first declared.
struct Entity {
    std::string usr;
    std::string name;
    std::optional<Extent> declaration;
};

// What Clang knows about a name: what hover shows.
struct Description {
    // What kind of declaration it is, in words: "function", "variable", ...
    std::string kind;
    std::string name;
    // Its name with every enclosing namespace and class.
    std::string qualified_name;
    // The declaration as Clang prints it, without a body.
    std::string declaration;
    // The type of a variable, parameter, field or alias, and of what `auto`
    // was deduced as.
    std::string type;
    // A constant's value, where Clang can evaluate it.
    std::string value;
    // A type's size and alignment in bytes, where it has them.
    std::optional<long long> size;
    std::optional<long long> alignment;
    // The comment written for it, markers removed.
    std::string documentation;
    // Where its name is declared, and the extent of the name the request was
    // made on.
    std::optional<Extent> declared;
    std::optional<Extent> named;
};

// A name or keyword Clang would accept at a position.
struct Completion {
    enum class Kind : std::uint8_t {
        Function,
        Method,
        Constructor,
        Field,
        Variable,
        Parameter,
        Class,
        Struct,
        Enum,
        Enumerator,
        Namespace,
        TypeAlias,
        TemplateParameter,
        Macro,
        Concept,
        Keyword,
        Other,
    };
    Kind kind = Kind::Other;
    // The text that chooses it.
    std::string typed;
    // `typed` followed by its parameters, where it has them.
    std::string label;
    // Its type, or what it returns.
    std::string result;
    // `typed` with its parameters as numbered placeholders, in LSP snippet
    // syntax.
    std::string snippet;
    std::string documentation;
    // Clang's ranking: lower is likelier.
    unsigned priority = 0;
    bool deprecated = false;
};

// One declaration a call's arguments could be resolving to.
struct Signature {
    std::string label;
    // Each parameter as a byte range of `label`.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> parameters;
    // The parameter the argument being written stands for, where Clang says.
    std::optional<std::uint32_t> active;
    std::string documentation;
};

// One declaration of the main file's outline, with the declarations nested in
// it.
struct Symbol {
    enum class Kind : std::uint8_t {
        Namespace,
        Class,
        Struct,
        Union,
        Enum,
        Enumerator,
        Function,
        Method,
        Constructor,
        Field,
        Variable,
        TypeAlias,
        Macro,
        Concept,
    };
    Kind kind = Kind::Function;
    std::string name;
    // Its type, or a function's signature.
    std::string detail;
    // Where its name is written: for an unnamed namespace or type, which has
    // none, an empty extent at its first byte.
    Extent name_extent;
    Extent extent;
    std::vector<Symbol> children;
};

// A run of the main file's text a reader may fold away.
struct Fold {
    enum class Kind : std::uint8_t {
        // A body from its `{` through its `}`: a block, a function's, a
        // lambda's, a class's, an enumeration's, a namespace's, an `extern`
        // block's, a braced initializer.
        Braces,
        // One `#include` directive. Consecutive ones fold together.
        Include,
        // A branch of a conditional directive: from its `#if`, `#ifdef`,
        // `#ifndef`, `#elif` or `#else` up to the directive that ends it.
        Conditional,
    };
    Kind kind = Kind::Braces;
    Extent extent;
};

// What encloses a position, as far as what may be declared there goes.
enum class Scope : std::uint8_t {
    Namespace,
    Class,
    Function,
    Other,
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

    // What the name written at `offset` of the main file's text denotes: one
    // entity, or each candidate of a name a template has not yet resolved.
    // Empty where no name is written.
    [[nodiscard]] std::vector<Entity> entities_at(std::size_t offset) const;

    // Every place, in the main file and the headers it includes that are not
    // system headers, where a name denoting one of `usrs` is written: each
    // declaration, and each reference as a read or a write. A name written
    // nowhere, such as an implicit call, is no occurrence: every extent
    // reported spells the name in the text Clang read.
    [[nodiscard]] std::vector<Occurrence> occurrences(const std::vector<std::string>& usrs) const;

    // Every declaration spelled `name` in those files.
    [[nodiscard]] std::vector<Occurrence> declarations_named(std::string_view name) const;

    // What Clang knows about the name written at `offset`: a declaration, a
    // use, a macro, or the type `auto` was deduced as.
    [[nodiscard]] std::optional<Description> describe(std::size_t offset) const;

    // Every name and keyword Clang would accept at `offset` of the main file's
    // text, which is where the name being written starts. Clang parses the
    // text afresh up to there, reusing the preamble.
    [[nodiscard]] std::vector<Completion> complete(std::size_t offset) const;

    // Every declaration the call whose arguments are being written at `offset`
    // could resolve to.
    [[nodiscard]] std::vector<Signature> signatures(std::size_t offset) const;

    // What encloses `offset`: a namespace, a class, a function body.
    [[nodiscard]] Scope scope_at(std::size_t offset) const;

    // Every declaration whose name the main file writes outside a function
    // body -- namespaces, types, functions, variables, fields, enumerators,
    // aliases, macros, concepts -- nested as they are declared.
    [[nodiscard]] std::vector<Symbol> outline() const;

    // Every body, `#include` and conditional branch the main file's text
    // holds. Bodies are what Clang parsed as one; a conditional directive has
    // no cursor, so its branches are paired from the directives Clang lexed.
    [[nodiscard]] std::vector<Fold> folds() const;

    // The extent of every construct Clang parsed that holds `offset` of the
    // main file's text, an extent's end included, from the innermost out: an
    // expression, the statements and blocks around it, the declaration, the
    // classes and namespaces around that.
    [[nodiscard]] std::vector<Extent> enclosing(std::size_t offset) const;

  private:
    struct State;
    explicit EditorUnit(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace cppl::clangbridge
