// The conversion from the `file://` URIs an editor names documents by to the
// paths the server opens.
//
// Properties: a path never carries a NUL byte, which every C API would read
// as the end of a shorter path, and the URI the server reports a path by
// names the same path again.

#include "cppl/lsp/uri.hpp"
#include "cppl/testing/fuzz.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#ifdef _WIN32
#include <algorithm>
#endif

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using cppl::testing::fuzz::require;

    const std::optional<std::string> path = cppl::lsp::uri_to_path(cppl::testing::fuzz::text({data, size}));
    if (!path.has_value()) {
        return 0;
    }
    require(path->find('\0') == std::string::npos, "a path holds no NUL byte");

    const std::optional<std::string> again = cppl::lsp::uri_to_path(cppl::lsp::path_to_uri(*path));

#ifdef _WIN32
    // Both are separators on Windows, and a URI spells each as '/'.
    std::string expected = *path;
    std::ranges::replace(expected, '\\', '/');
#else
    const std::string& expected = *path;
#endif
    require(again.has_value() && *again == expected, "uri_to_path(path_to_uri(p)) == p");
    return 0;
}
