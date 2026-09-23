// The JSON-RPC body parser. Every message body the editor sends reaches it.
//
// Properties: malformed input is refused with json::Error and nothing else,
// and what the server writes back reads as what it wrote.

#include "cppl/lsp/json.hpp"
#include "cppl/testing/fuzz.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using cppl::testing::fuzz::require;
    namespace json = cppl::lsp::json;

    json::Value value;
    try {
        value = json::parse(cppl::testing::fuzz::text({data, size}));
    } catch (const json::Error&) {
        return 0;
    }

    const std::string written = value.dump();
    std::string rewritten;
    try {
        rewritten = json::parse(written).dump();
    } catch (const json::Error&) {
        require(false, "dump() writes JSON that parse() reads");
    }
    require(rewritten == written, "dump(parse(dump(v))) == dump(v)");
    return 0;
}
