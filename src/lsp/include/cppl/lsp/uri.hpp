#pragma once

#include <optional>
#include <string>

namespace cppl::lsp {

// Converts an LSP `file://` URI to a native filesystem path.
//
// LSP URIs are percent-encoded and, on Windows, carry a drive letter after
// the authority slash (`file:///C:/path/to/file`). A naive `substr(7)` gets
// both wrong: it leaves percent-escapes undecoded and, on Windows, leaves a
// leading '/' before the drive letter. This performs full percent-decoding
// and drive-letter handling for macOS, Linux and Windows.
//
// Returns std::nullopt when `uri` is not a `file://` URI (e.g. `untitled:`
// for an unsaved buffer) or is otherwise malformed; the caller decides the
// fallback (cppl-lsp falls back to the raw URI text so diagnostics still
// have something stable to report against).
[[nodiscard]] std::optional<std::string> uri_to_path(const std::string& uri);

// The inverse: builds a `file://` URI from a native filesystem path,
// percent-encoding reserved characters. `path` is expected to be absolute;
// a relative path is encoded as given without being resolved.
[[nodiscard]] std::string path_to_uri(const std::string& path);

} // namespace cppl::lsp
