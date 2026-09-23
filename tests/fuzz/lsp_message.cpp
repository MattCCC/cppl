// The Content-Length framing the language server reads from its standard
// input: the first code any byte from the editor reaches.
//
// Properties: a malformed stream is refused with json::Error or ends the
// session, reading always makes progress, and what a header claims is never
// allocated ahead of the bytes that arrive (the target runs with a malloc
// limit far below what a Content-Length can state).

#include "cppl/lsp/json.hpp"
#include "cppl/lsp/transport.hpp"
#include "cppl/testing/fuzz.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using cppl::testing::fuzz::require;

    std::istringstream input(cppl::testing::fuzz::text({data, size}));

    // Every message consumes at least its blank separator line, so a stream of
    // `size` bytes holds at most `size` of them.
    for (std::size_t messages = 0; messages <= size; ++messages) {
        std::optional<std::string> body;
        try {
            body = cppl::lsp::read_message(input);
        } catch (const cppl::lsp::json::Error&) {
            return 0;
        }
        if (!body.has_value()) {
            return 0;
        }
        require(body->size() <= size, "a body is no larger than the stream that carried it");
    }
    require(false, "reading ends once the stream is consumed");
    return 0;
}
