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

#include <chrono>
#include <iosfwd>
#include <optional>
#include <string>

namespace cppl::lsp {

struct TransportOptions {
    // Compile each document on a thread of its own, so that requests are
    // answered while a compile runs, and report each compile as work in
    // progress to a client that shows it. A document is compiled as soon as
    // it opens, and a change once typing has paused for `quiet`. Off, a
    // document is compiled before the notification that opened or changed it
    // returns, as a test that reads what a notification produced needs.
    bool background_compiles = false;
    std::chrono::milliseconds quiet{300};
};

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
//
// With background compiles, the input is read on a thread of its own, so a
// request the client withdraws (`$/cancelRequest`) while it waits its turn is
// answered as cancelled, not run. After a compile, a client that supports it
// is asked to fetch its code lenses and semantic tokens again.
[[nodiscard]] int run_transport(Server& server, std::istream& input, std::ostream& output, std::ostream& log,
                                const TransportOptions& options = {});

} // namespace cppl::lsp
