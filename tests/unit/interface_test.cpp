// The verification-interface reader and writer (SPEC.md TUBOUND-002, TUBOUND-005;
// TRUST.md TCB-XTU-005, TCB-ARTIFACT-001).
//
// An interface is read by units that did not produce it, so the reader treats
// it as hostile: it accepts exactly the canonical text of some interface of
// this format version, and names what is wrong with anything else. Each refusal
// below is paired with the text it was made from, which the reader accepts, so
// what is refused is the one thing changed.

#include "cppl/artifact/interface.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace {

namespace artifact = cppl::artifact;
using cppl::source::Digest;
using cppl::source::hash_bytes;

artifact::Interface sample() {
    artifact::Interface recorded;
    recorded.configuration.compiler = "0.0.1";
    recorded.configuration.build = hash_bytes("the compiler");
    recorded.configuration.kernel = "cppl-kernel-0.7.0";
    recorded.configuration.core = "cppl-core-0.7.0";
    recorded.configuration.clang = "clang version 22.1.8 (with spaces)";
    recorded.configuration.language = "c++20";
    recorded.configuration.target = "arm64-apple-macosx15.0.0";
    recorded.configuration.flags = {"-fwrapv", "-fno-exceptions"};
    recorded.unit = "/work/src/clamp library.cpp";
    recorded.sources = {{"/work/src/clamp library.cpp", hash_bytes("source")},
                        {"/work/include/clamp.hpp", hash_bytes("header")}};

    artifact::Entry clamp;
    clamp.symbol = "c:@F@clamp4#i#";
    clamp.name = "clamp4";
    clamp.statement = hash_bytes("statement");
    clamp.contract = "forall (x: u32). x < 100 -> result < 4";
    clamp.correctness = artifact::Correctness::Total;
    clamp.premises = {{hash_bytes("law"), "bounded", "/work/src/clamp library.cpp", 3}};
    clamp.unsafe = {{"/work/src/clamp library.cpp", 12, 5}};
    clamp.depends = {{"c:@F@helper#i#", hash_bytes("helper entry")}};

    artifact::Entry other;
    other.symbol = "c:@F@another#";
    other.name = "another";
    other.statement = hash_bytes("other statement");
    other.contract = "true";
    other.correctness = artifact::Correctness::Partial;

    recorded.entries = {clamp, other};
    return recorded;
}

std::string text_of(const artifact::Interface& recorded) {
    const std::expected<std::string, std::string> text = artifact::serialize(recorded);
    if (!text) {
        cppl::testing::fail(__FILE__, __LINE__, "serialize refused a valid interface: " + text.error());
    }
    return *text;
}

// The text with its checksum line recomputed, as a hand edit that knows the
// format would leave it: what is left for the reader to refuse is the edit.
std::string resealed(std::string_view body) {
    return std::string(body) + "checksum " + hash_bytes(body).to_hex() + "\n";
}

// Everything but the checksum line.
std::string body_of(const std::string& text) {
    const std::size_t last = text.rfind("checksum ");
    return text.substr(0, last);
}

std::string replaced(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) {
        cppl::testing::fail(__FILE__, __LINE__, "the sample has no '" + std::string(from) + "'");
    }
    text.replace(at, from.size(), to);
    return text;
}

void expect_refused(const std::string& text, std::string_view reason) {
    const std::expected<artifact::Interface, artifact::ParseError> read = artifact::parse(text);
    if (read) {
        cppl::testing::fail(__FILE__, __LINE__,
                            "accepted an interface that should be refused for: " + std::string(reason));
    }
    if (read.error().reason.find(reason) == std::string::npos) {
        cppl::testing::fail(__FILE__, __LINE__,
                            "refused for '" + read.error().reason + "', expected '" + std::string(reason) + "'");
    }
}

} // namespace

// SPEC: TUBOUND-002
CPPL_TEST(an_interface_reads_back_as_written) {
    const artifact::Interface recorded = sample();
    const std::string text = text_of(recorded);
    const auto read = artifact::parse(text);
    CPPL_CHECK(read.has_value());
    // Sources and entries come back in canonical order; everything else as given.
    artifact::Interface expected = recorded;
    std::swap(expected.sources[0], expected.sources[1]);
    std::swap(expected.entries[0], expected.entries[1]);
    CPPL_CHECK(*read == expected);
    CPPL_CHECK_EQ(text_of(*read), text);
}

// SPEC: TUBOUND-002
CPPL_TEST(the_text_of_an_interface_does_not_depend_on_the_order_it_was_assembled_in) {
    artifact::Interface first = sample();
    artifact::Interface second = sample();
    std::swap(second.sources[0], second.sources[1]);
    std::swap(second.entries[0], second.entries[1]);
    second.entries[1].premises.push_back(second.entries[1].premises.front());
    CPPL_CHECK_EQ(text_of(first), text_of(second));
}

// SPEC: TUBOUND-004
CPPL_TEST(an_entry_identity_covers_every_field) {
    const artifact::Entry base = sample().entries.front();
    const Digest identity = artifact::identify(base);
    CPPL_CHECK(artifact::identify(base) == identity);

    const auto differs = [&identity](const artifact::Entry& changed) {
        return !(artifact::identify(changed) == identity);
    };
    artifact::Entry changed = base;
    changed.symbol = "c:@F@clamp4#l#";
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.statement = hash_bytes("another statement");
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.correctness = artifact::Correctness::Partial;
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.premises.clear();
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.unsafe.clear();
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.depends.front().entry = hash_bytes("a different helper entry");
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.contract = "forall (x: u32). x < 100 -> result < 5";
    CPPL_CHECK(differs(changed));
}

// SPEC: TUBOUND-005
CPPL_TEST(any_edit_that_does_not_recompute_the_checksum_is_refused) {
    const std::string text = text_of(sample());
    expect_refused(replaced(text, "forall", "Forall"), "checksum does not match");
    expect_refused(replaced(text, "total", "partial"), "checksum does not match");
}

// SPEC: TUBOUND-005
CPPL_TEST(a_truncated_interface_is_refused) {
    const std::string text = text_of(sample());
    CPPL_CHECK(artifact::parse(text).has_value());
    expect_refused(text.substr(0, text.size() - 1), "truncated");
    expect_refused(body_of(text), "truncated");
    expect_refused(text.substr(0, text.find('\n') + 1), "truncated");
    expect_refused(text.substr(0, text.size() / 2), "truncated");
    expect_refused("", "not a C++L verification interface");
}

// SPEC: TUBOUND-005
CPPL_TEST(another_format_or_version_is_named_as_such) {
    const std::string body = body_of(text_of(sample()));
    expect_refused(resealed(replaced(body, "cppl-verification-interface 1", "cppl-verification-interface 2")),
                   "format version '2'");
    expect_refused(resealed(replaced(body, "cppl-verification-interface 1", "some-other-format 1")),
                   "not a C++L verification interface");
    expect_refused("\x7f"
                   "ELF",
                   "not a C++L verification interface");
}

// SPEC: TUBOUND-002
CPPL_TEST(a_status_other_than_proven_is_refused) {
    const std::string body = body_of(text_of(sample()));
    CPPL_CHECK(artifact::parse(resealed(body)).has_value());
    expect_refused(resealed(replaced(body, "status proven", "status refused")), "only a proven contract");
    expect_refused(resealed(replaced(body, "status proven", "status not-attempted")), "only a proven contract");
    expect_refused(resealed(replaced(body, "correctness total", "correctness maybe")), "neither 'total'");
}

// SPEC: TUBOUND-005
CPPL_TEST(missing_unknown_and_misplaced_fields_are_refused) {
    const std::string body = body_of(text_of(sample()));
    expect_refused(resealed(replaced(body, "kernel cppl-kernel-0.7.0\n", "")), "where 'kernel' was expected");
    expect_refused(resealed(replaced(body, "status proven\n", "")), "where 'status' was expected");
    expect_refused(resealed(replaced(body, "status proven\n", "status proven\nstatus proven\n")),
                   "where 'correctness' was expected");
    expect_refused(resealed(replaced(body, "end\n", "")), "where 'end' was expected");
    expect_refused(resealed(body + "surprise field\n"), "where an entry or the checksum was expected");
    expect_refused(resealed(replaced(body, "language c++20", "language c++20 extra")), "has 2 fields, not 1");
    expect_refused(resealed(replaced(body, "unit ", "unit  ")), "empty field");
    expect_refused(resealed(replaced(body, "language c++20", "language c++20 ")), "empty field");
    expect_refused(resealed(replaced(body, "language c++20", "language c++20\r")), "must be escaped");
}

// SPEC: TUBOUND-005
CPPL_TEST(a_token_has_exactly_one_spelling) {
    const std::string body = body_of(text_of(sample()));
    // `(with` is written with its space escaped; a raw one splits the field.
    CPPL_CHECK(body.find("clang clang%20version%2022.1.8%20%28with") == std::string::npos);
    CPPL_CHECK(body.find("clang clang%20version%2022.1.8%20(with%20spaces)") != std::string::npos);
    expect_refused(resealed(replaced(body, "clang%20version", "clang%2Fversion%20")), "not canonically encoded");
    expect_refused(resealed(replaced(body, "clang%20version", "clang%41version")), "not canonically encoded");
    expect_refused(resealed(replaced(body, "clang%20version", "clang%2aversion")), "not canonically encoded");
    expect_refused(resealed(replaced(body, "clang%20version", "clang%2version")), "not canonically encoded");
    expect_refused(resealed(replaced(body, "clang%20version%2022.1.8%20(with%20spaces)", "%")),
                   "not canonically encoded");
}

// SPEC: TUBOUND-005
CPPL_TEST(digests_and_numbers_are_canonical) {
    const std::string body = body_of(text_of(sample()));
    const std::string statement = "statement " + hash_bytes("statement").to_hex();
    std::string upper = statement;
    for (std::size_t index = std::string_view("statement ").size(); index < upper.size(); ++index) {
        if (upper[index] >= 'a' && upper[index] <= 'f') {
            upper[index] = static_cast<char>(upper[index] - 'a' + 'A');
        }
    }
    CPPL_CHECK(!(upper == statement));
    expect_refused(resealed(replaced(body, statement, upper)), "malformed digest");
    expect_refused(resealed(replaced(body, statement, statement.substr(0, statement.size() - 1))), "malformed digest");
    expect_refused(resealed(replaced(body, "unsafe 12 5", "unsafe 012 5")), "malformed number");
    expect_refused(resealed(replaced(body, "unsafe 12 5", "unsafe 0 5")), "malformed number");
    expect_refused(resealed(replaced(body, "unsafe 12 5", "unsafe 12 99999999999")), "malformed number");
    expect_refused(resealed(replaced(body, "unsafe 12 5", "unsafe -12 5")), "malformed number");
}

// SPEC: TUBOUND-005, TUBOUND-009
CPPL_TEST(repeated_or_reordered_items_are_refused) {
    const std::string body = body_of(text_of(sample()));
    const std::size_t first_entry = body.find("entry ");
    const std::size_t second_entry = body.find("entry ", first_entry + 1);
    const std::string entries = body.substr(first_entry);
    const std::string head = body.substr(0, first_entry);
    const std::string first = body.substr(first_entry, second_entry - first_entry);
    const std::string second = body.substr(second_entry);
    CPPL_CHECK(artifact::parse(resealed(head + first + second)).has_value());
    expect_refused(resealed(head + second + first), "one function is recorded twice");
    expect_refused(resealed(head + first + first + second), "one function is recorded twice");

    const std::size_t first_source = body.find("source ");
    const std::size_t second_source = body.find("source ", first_source + 1);
    const std::string source_a = body.substr(first_source, second_source - first_source);
    const std::string source_b = body.substr(second_source, first_entry - second_source);
    const std::string before_sources = body.substr(0, first_source);
    expect_refused(resealed(before_sources + source_b + source_a + entries), "source files are not in canonical order");
    expect_refused(resealed(before_sources + source_a + source_a + source_b + entries),
                   "source files are not in canonical order");

    const std::size_t premise = body.find("premise ");
    const std::string premise_line = body.substr(premise, body.find('\n', premise) + 1 - premise);
    expect_refused(resealed(replaced(body, premise_line, premise_line + premise_line)), "premises are not in");
}

// SPEC: TUBOUND-005
CPPL_TEST(sizes_past_the_bounds_are_refused_without_reading_them) {
    expect_refused(std::string(artifact::kMaxBytes + 1, 'x'), "larger than");
    const std::string body = body_of(text_of(sample()));
    const std::string long_name(artifact::kMaxLineBytes, 'n');
    expect_refused(resealed(replaced(body, "name another", "name " + long_name)), "longer than");
}

// SPEC: TUBOUND-002
CPPL_TEST(an_interface_that_could_not_be_read_back_is_not_written) {
    artifact::Interface recorded = sample();
    recorded.entries.push_back(recorded.entries.front());
    recorded.entries.back().statement = hash_bytes("a second statement");
    CPPL_CHECK(!artifact::serialize(recorded).has_value());

    recorded = sample();
    recorded.entries.front().contract.clear();
    CPPL_CHECK(!artifact::serialize(recorded).has_value());

    recorded = sample();
    recorded.sources.push_back({recorded.sources.front().path, hash_bytes("other content")});
    CPPL_CHECK(!artifact::serialize(recorded).has_value());

    recorded = sample();
    recorded.configuration.target.clear();
    CPPL_CHECK(!artifact::serialize(recorded).has_value());

    recorded = sample();
    recorded.entries.front().premises.front().line = 0;
    CPPL_CHECK(!artifact::serialize(recorded).has_value());
}

// SPEC: TUBOUND-002
CPPL_TEST(every_byte_of_a_field_survives_the_round_trip) {
    artifact::Interface recorded = sample();
    std::string every_byte;
    for (int byte = 1; byte < 256; ++byte) {
        every_byte.push_back(static_cast<char>(byte));
    }
    every_byte.push_back('\0');
    recorded.entries.front().name = every_byte;
    recorded.unit = std::string("/path with\nnewline and %25 escape");
    const std::string text = text_of(recorded);
    const auto read = artifact::parse(text);
    CPPL_CHECK(read.has_value());
    CPPL_CHECK_EQ(read->unit, recorded.unit);
    CPPL_CHECK_EQ(read->entries.back().name, every_byte);
}
