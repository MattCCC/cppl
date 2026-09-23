#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace cppl::driver {

// An owned, private temporary directory.
//
// The CLI uses one per process for every input's preprocessed and projected
// text; the buffer-compile path uses one per compile so a live editor buffer
// never touches the file the developer has open on disk (README.md,
// tools/cppl-lsp).
class ScratchDirectory {
  public:
    ScratchDirectory();

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    ~ScratchDirectory();

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

// A subdirectory of `root` unique to `input`'s absolute path, created on
// demand. Returns an empty path when it could not be created.
[[nodiscard]] std::filesystem::path scratch_directory(const std::filesystem::path& root, const std::string& input);

[[nodiscard]] bool write_scratch_file(const std::filesystem::path& path, std::string_view content);

} // namespace cppl::driver
