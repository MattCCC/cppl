#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::lsp {

// The flags a build compiles each file with, as its build tool recorded them
// in a `compile_commands.json` (Clang's JSON compilation database), so the
// editor reads a document as its build does (tools/cppl-lsp/README.md,
// "Compile flags").
class CompileCommands {
  public:
    // The flags `path` is read with, as arguments Clang takes beside an input:
    // its own entry's, in the nearest database in its directory, a `build`
    // directory beside it, or a directory above; or, for a header or a file
    // the build does not compile, the entry for the file most like it -- the
    // same name with another extension, else the nearest in the tree. Empty
    // when no database is found.
    //
    // Only what changes how the text reads is kept: include and framework
    // paths (made absolute against the entry's directory), macros, forced
    // includes, the language standard and library, the target and system root,
    // and the few `-f` and `-m` switches that define macros or change the
    // language. Output, dependency and warning flags, and the input, are
    // dropped. A database edited since it was read is read again.
    [[nodiscard]] std::vector<std::string> flags_for(const std::string& path);

  private:
    struct Entry {
        std::filesystem::path file;
        std::filesystem::path directory;
        std::vector<std::string> arguments;
    };
    struct Database {
        std::filesystem::file_time_type written;
        std::vector<Entry> entries;
    };

    [[nodiscard]] const Database* database_at(const std::filesystem::path& file);

    std::map<std::filesystem::path, Database> databases_;
};

// A `command` string split into arguments as a POSIX shell splits it: on
// unquoted space, with single and double quotes grouping and a backslash
// escaping the character after it outside single quotes.
[[nodiscard]] std::vector<std::string> split_command(std::string_view command);

// The flags of `arguments` -- a compile command, compiler first -- that change
// how the text reads, with each path made absolute against `directory`.
[[nodiscard]] std::vector<std::string> reading_flags(const std::vector<std::string>& arguments,
                                                     const std::filesystem::path& directory);

} // namespace cppl::lsp
