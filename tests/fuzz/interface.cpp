// The verification-interface reader (SPEC.md TUBOUND-005, TRUST.md TCB-XTU-005):
// every interface a unit imports reaches it from a file another build wrote.
//
// Each input is read twice: as it is, and as the body of an interface whose
// checksum line is recomputed for it, so the search reaches the structure
// behind the checksum instead of stopping at it.
//
// Properties: the reader accepts only canonical text, so whatever it accepts is
// exactly what the writer produces for what was read; and what was read reads
// back the same.

#include "cppl/artifact/interface.hpp"

#include "cppl/source/digest.hpp"
#include "cppl/testing/fuzz.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace {

void check(const std::string& text) {
    using cppl::testing::fuzz::require;

    const std::expected<cppl::artifact::Interface, cppl::artifact::ParseError> read = cppl::artifact::parse(text);
    if (!read) {
        require(!read.error().reason.empty(), "a refusal says why");
        return;
    }
    const std::expected<std::string, std::string> written = cppl::artifact::serialize(*read);
    require(written.has_value(), "what was read can be written");
    require(*written == text, "only canonical text is accepted");
    const auto again = cppl::artifact::parse(*written);
    require(again.has_value() && *again == *read, "what was written reads back the same");
    for (const cppl::artifact::Entry& entry : read->entries) {
        require(cppl::artifact::identify(entry) == cppl::artifact::identify(entry), "an entry identity is stable");
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string input = cppl::testing::fuzz::text({data, size});
    check(input);
    check(input + "checksum " + cppl::source::hash_bytes(input).to_hex() + "\n");
    return 0;
}
