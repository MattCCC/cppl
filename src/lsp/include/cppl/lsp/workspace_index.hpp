#pragma once

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/compile_commands.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cppl::lsp {

// What every file of a workspace declares and where each name is written,
// kept current on a thread of its own, for the requests an open document
// cannot answer alone: workspace symbols, and references and renames that
// reach files no open document includes (tools/cppl-lsp/README.md,
// "Workspace index").
//
// The files are every C++ and C++L source and header under a root, skipping
// hidden directories, build trees and nested repositories, and every file the
// compilation database at a root lists. Each is read from disk as it is now,
// with the flags its build gives it, the way an open document is read: by
// Clang through its projection, and by the compile as far as elaboration,
// which says what each name a proof statement uses resolves to. A file edited
// on disk is read again at the next poll.
class WorkspaceIndex {
  public:
    struct Options {
        // The Clang driver, by path, that editor units are parsed with.
        std::string driver;
        // The Clang the compile runs, and the flags every file is read with
        // after its build's.
        std::string clang;
        std::vector<std::string> clang_arguments;
        // How often files are checked for edits, and the roots for new ones.
        std::chrono::milliseconds poll{2000};
    };

    // A declaration a workspace symbol search finds.
    struct Symbol {
        std::string name;
        std::string container;
        SymbolKind kind = SymbolKind::Function;
        Location location;
    };

    // A place a name with an identity is written.
    struct Mention {
        std::string usr;
        Location location;
        clangbridge::Role role = clangbridge::Role::Read;
    };

    // How far indexing has come: files read, out of files found to read.
    using Progress = std::function<void(std::size_t done, std::size_t total)>;

    WorkspaceIndex(std::vector<std::filesystem::path> roots, Options options, Progress progress = {});
    ~WorkspaceIndex();

    WorkspaceIndex(const WorkspaceIndex&) = delete;
    WorkspaceIndex& operator=(const WorkspaceIndex&) = delete;
    WorkspaceIndex(WorkspaceIndex&&) = delete;
    WorkspaceIndex& operator=(WorkspaceIndex&&) = delete;

    // The declarations whose name holds `query`'s characters in order,
    // ignoring case, best first, at most `limit` (`rank`).
    [[nodiscard]] std::vector<Symbol> symbols(std::string_view query, std::size_t limit) const;

    // How well `name` answers `query`, ignoring case: 4 for the name itself,
    // 3 for a prefix, 2 for a part of it, 1 for its characters in order, 0
    // not at all. Every name answers an empty query.
    [[nodiscard]] static int match(std::string_view name, std::string_view query);

    // Orders `symbols` best match first, then by name, each once.
    static void rank(std::vector<Symbol>& symbols, std::string_view query);

    // Every declaration of a document's `outline`, each with the declarations
    // it is nested in as its container (`a::b`).
    [[nodiscard]] static std::vector<Symbol> declared(const std::vector<DocumentSymbol>& outline,
                                                      const std::string& uri);

    // Every place a name with one of `usrs` is written in the files indexed,
    // once each.
    [[nodiscard]] std::vector<Mention> mentions(const std::vector<std::string>& usrs) const;

    // Every place, in the files indexed, a name with one of `usrs` is used
    // without being written there: where a macro whose body spells it is
    // expanded (EditorView::unwritten).
    [[nodiscard]] std::vector<Location> unwritten(const std::vector<std::string>& usrs) const;

    // Whether the file at `path` is one the index has read.
    [[nodiscard]] bool holds(const std::string& path) const;

    // Every proof statement, in the files indexed, that the compile resolved
    // to the declaration of `name` written at `declaration`.
    [[nodiscard]] std::vector<Location> proof_name_uses(const std::string& name,
                                                        const source::SourceLocation& declaration) const;

    // Looks at the files again now, not at the next poll, and returns once a
    // look begun after the call found every file read as it is on disk, true,
    // or once `timeout` has passed, false.
    [[nodiscard]] bool wait_until_current(std::chrono::milliseconds timeout);

  private:
    // A name a proof statement uses, where, and what it was resolved to.
    struct ProofNameUse {
        std::string name;
        source::SourceLocation declaration;
        Location use;
    };

    struct Entry {
        std::filesystem::file_time_type written;
        std::vector<Symbol> symbols;
        std::vector<Mention> mentions;
        std::vector<Mention> unwritten;
        std::vector<ProofNameUse> uses;
    };

    void run();
    [[nodiscard]] std::vector<std::filesystem::path> discover();
    [[nodiscard]] Entry read(const std::filesystem::path& file, std::filesystem::file_time_type written);

    std::vector<std::filesystem::path> roots_;
    Options options_;
    Progress progress_;
    // Read only by the thread that indexes.
    CompileCommands commands_;

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::filesystem::path, Entry> entries_;
    // Each look at the files is numbered; `current_` is the last that found
    // nothing to read.
    std::uint64_t looks_ = 0;
    std::uint64_t current_ = 0;
    bool look_now_ = false;
    bool stopping_ = false;
    // Last, so that it starts once everything it uses exists.
    std::thread thread_;
};

} // namespace cppl::lsp
