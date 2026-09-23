#pragma once

// Content-Length-delimited JSON-RPC 2.0 framing over stdio, and the
// dispatch loop that connects it to Server (tools/cppl-lsp/main.cpp).
//
// This is the actual LSP wire protocol: everything else in cppl_lsp
// (Server, Document, Linter, PositionMapper) is transport-agnostic and is
// exercised directly by unit tests. Transport itself reads from an
// std::istream and writes to an std::ostream so it, too, can be driven by a
// test without a real subprocess and pipes.

#include "cppl/lsp/server.hpp"

#include <iosfwd>
#include <optional>
#include <string>

namespace cppl::lsp {

// Reads one Content-Length-delimited JSON-RPC message body from `input`.
// Returns std::nullopt at end of stream, including one that ends inside a
// body. Throws json::Error on a header line over 8 KiB, and on a
// Content-Length that is not plain decimal digits, is stated twice, or exceeds
// 64 MiB; the caller decides whether that is fatal (main.cpp treats it as end
// of session, since a corrupted stream cannot be recovered by skipping
// forward). Memory grows with the bytes that arrive, never with the length a
// header states.
[[nodiscard]] std::optional<std::string> read_message(std::istream& input);

// Writes one message with the required Content-Length header and no other
// framing (LSP requires exactly "\r\n\r\n" between the header block and the
// body, and no trailing separator after the body).
void write_message(std::ostream& output, const std::string& body);

// Runs the read-dispatch-write loop until `exit` is received (per the LSP
// spec, `exit` always ends the session; `shutdown` alone does not) or the
// input stream ends. Returns the process exit code the LSP spec specifies:
// 0 if `shutdown` was received before `exit`, 1 otherwise.
//
// Diagnostics the server produces are written to `output` as
// textDocument/publishDiagnostics notifications. Anything this loop itself
// needs to say (malformed input, unknown methods it chooses to log) goes to
// `log`, never to `output`, since `output` is the protocol channel and
// stdout must carry nothing but framed JSON-RPC.
[[nodiscard]] int run_transport(Server& server, std::istream& input, std::ostream& output, std::ostream& log);

} // namespace cppl::lsp
