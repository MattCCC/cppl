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

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace artifact = cppl::artifact;
using cppl::source::Digest;
using cppl::source::hash_bytes;

artifact::Interface sample() {
    artifact::Interface recorded;
    recorded.configuration.compiler = "0.0.1";
    recorded.configuration.semantics = "cppl-verification-semantics-0.2.0";
    recorded.configuration.verifier = hash_bytes("the verifier's sources");
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
    clamp.models = {{hash_bytes("a model"), "std::vector model"}, {hash_bytes("another model"), "std::span model"}};
    clamp.unsafe = {{"/work/src/clamp library.cpp", 12, 5}};
    clamp.runtime = {{"/work/src/clamp library.cpp", 20, 9, "Small", "(self < 4)"},
                     {"/work/src/clamp library.cpp", 18, 13, "Positive", "(self > 0)"}};
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
    // Sources, entries and an entry's models come back in canonical order;
    // everything else as given.
    artifact::Interface expected = recorded;
    std::swap(expected.sources[0], expected.sources[1]);
    std::swap(expected.entries[0], expected.entries[1]);
    std::ranges::sort(expected.entries[1].models, {},
                      [](const artifact::Model& model) { return model.identity.to_hex(); });
    std::swap(expected.entries[1].runtime[0], expected.entries[1].runtime[1]);
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
    std::swap(second.entries[1].models[0], second.entries[1].models[1]);
    second.entries[1].models.push_back(second.entries[1].models.front());
    CPPL_CHECK_EQ(text_of(first), text_of(second));
}

// SPEC: TUBOUND-002, TUBOUND-009
CPPL_TEST(a_result_identity_covers_the_contract_and_every_dependency_of_every_kind) {
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
    // SPEC: STDMODEL-018 -- a record that drops a model it rested on is
    // another record, so nothing proven through the first is used with it.
    changed = base;
    changed.models.pop_back();
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.models.front().identity = hash_bytes("a different model");
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.premises.front().identity = hash_bytes("another law");
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.unsafe.clear();
    CPPL_CHECK(differs(changed));
    // SPEC: RUNTIMECHECK-015
    // A runtime validation site is where it is and what it enters.
    changed = base;
    changed.runtime.clear();
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.runtime.pop_back();
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.runtime.front().line = 21;
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.runtime.front().column = 10;
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.runtime.front().file = "/work/src/other.cpp";
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.runtime.front().refinement = "Positive";
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.depends.front().entry = hash_bytes("a different helper entry");
    CPPL_CHECK(differs(changed));
    changed = base;
    changed.depends.front().symbol = "c:@F@other_helper#i#";
    CPPL_CHECK(differs(changed));

    // One dependency of each kind never stands for one of another kind.
    artifact::Entry only_premise = base;
    only_premise.unsafe.clear();
    only_premise.models.clear();
    only_premise.depends.clear();
    only_premise.runtime.clear();
    artifact::Entry only_model = only_premise;
    only_model.premises.clear();
    only_model.models = {{only_premise.premises.front().identity, "a model with a law's identity"}};
    CPPL_CHECK(!(artifact::identify(only_premise) == artifact::identify(only_model)));
    // An unsafe block and a runtime check at one place are different
    // dependencies (SPEC.md RUNTIMECHECK-015).
    artifact::Entry only_unsafe = only_model;
    only_unsafe.models.clear();
    only_unsafe.unsafe = {{"/work/src/clamp library.cpp", 20, 9}};
    artifact::Entry only_check = only_unsafe;
    only_check.unsafe.clear();
    only_check.runtime = {{"/work/src/clamp library.cpp", 20, 9, "Small", "(self < 4)"}};
    CPPL_CHECK(!(artifact::identify(only_unsafe) == artifact::identify(only_check)));
}

// SPEC: TUBOUND-009
CPPL_TEST(a_result_identity_is_not_the_identity_of_the_entrys_bytes) {
    const artifact::Entry base = sample().entries.front();
    const Digest identity = artifact::identify(base);
    // What is recorded only to be shown does not change what a caller may
    // conclude, and so does not change the identity.
    artifact::Entry shown = base;
    shown.name = "clamp_to_four";
    shown.contract = "the same contract, described differently";
    shown.premises.front().name = "renamed_law";
    shown.premises.front().file = "/elsewhere/clamp.cpp";
    shown.premises.front().line = 30;
    // SPEC: RUNTIMECHECK-015
    shown.runtime.front().predicate = "(self <= 3)";
    CPPL_CHECK(artifact::identify(shown) == identity);
    // Order and repetition are not content either.
    artifact::Entry reordered = base;
    std::swap(reordered.models[0], reordered.models[1]);
    reordered.models.push_back(reordered.models.front());
    CPPL_CHECK(artifact::identify(reordered) == identity);
}

// SPEC: TUBOUND-002, TUBOUND-006
CPPL_TEST(a_library_model_this_compiler_does_not_have_is_refused) {
    const std::string body = body_of(text_of(sample()));
    CPPL_CHECK(body.find(" std::span%20model\n") != std::string::npos);
    expect_refused(resealed(replaced(body, " std::span%20model\n", " std::deque%20model\n")), "not a library model");
    artifact::Interface recorded = sample();
    recorded.entries.front().models.push_back({hash_bytes("a map model"), "std::map model"});
    CPPL_CHECK(!artifact::serialize(recorded).has_value());
}

// SPEC: RUNTIMECHECK-015, TUBOUND-005
CPPL_TEST(a_runtime_validation_site_is_read_only_as_written) {
    const std::string body = body_of(text_of(sample()));
    const std::string unsafe = "unsafe 12 5 /work/src/clamp%20library.cpp\n";
    const std::string small = "runtime 20 9 /work/src/clamp%20library.cpp Small (self%20<%204)\n";
    const std::string positive = "runtime 18 13 /work/src/clamp%20library.cpp Positive (self%20>%200)\n";
    CPPL_CHECK(body.find(positive + small) != std::string::npos);
    // Between the unsafe blocks and the dependencies, in canonical order.
    CPPL_CHECK(body.find(unsafe + positive) != std::string::npos);
    CPPL_CHECK(body.find(small + "depends ") != std::string::npos);
    expect_refused(resealed(replaced(body, positive + small, small + positive)), "runtime validation sites are not in");
    expect_refused(resealed(replaced(body, positive, positive + positive)), "runtime validation sites are not in");
    expect_refused(resealed(replaced(body, "runtime 18 13", "runtime 0 13")), "malformed number");
    expect_refused(resealed(replaced(body, " Small (self%20<%204)", " Small")), "has 4 fields, not 5");
    expect_refused(resealed(replaced(body, " Small (self%20<%204)", " Small (self%20<%204) extra")), "too many fields");
    // Out of its place, it is a field the reader did not expect there.
    expect_refused(resealed(replaced(body, unsafe + positive, positive + unsafe)), "was expected");
    artifact::Interface recorded = sample();
    recorded.entries.front().runtime.front().refinement.clear();
    CPPL_CHECK(!artifact::serialize(recorded).has_value());
    recorded = sample();
    recorded.entries.front().runtime.front().line = 0;
    CPPL_CHECK(!artifact::serialize(recorded).has_value());
}

// SPEC: TUBOUND-005
CPPL_TEST(a_digest_reads_back_only_from_its_own_spelling) {
    const Digest digest = hash_bytes("any content");
    CPPL_CHECK(Digest::from_hex(digest.to_hex()) == std::optional<Digest>{digest});
    std::string upper = digest.to_hex();
    for (char& character : upper) {
        if (character >= 'a' && character <= 'f') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    // Only the lower-case spelling is read, so a digest with a letter in it is
    // refused in upper case.
    if (upper != digest.to_hex()) {
        CPPL_CHECK(!Digest::from_hex(upper).has_value());
    }
    CPPL_CHECK(!Digest::from_hex(digest.to_hex().substr(1)).has_value());
    CPPL_CHECK(!Digest::from_hex(digest.to_hex() + "0").has_value());
    CPPL_CHECK(!Digest::from_hex(std::string(64, 'g')).has_value());
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
    CPPL_CHECK(body.starts_with("cppl-verification-interface 3\n"));
    expect_refused(resealed(replaced(body, "cppl-verification-interface 3", "cppl-verification-interface 4")),
                   "format version '4'");
    expect_refused(resealed(replaced(body, "cppl-verification-interface 3", "some-other-format 3")),
                   "not a C++L verification interface");
    // Version 1 recorded no models, and version 2 no runtime validation sites,
    // so their entries could not say whether a contract rested on one: each is
    // refused as another version, whatever it holds (SPEC.md STDMODEL-018,
    // RUNTIMECHECK-015).
    const auto without = [](std::string text, std::string_view key) {
        const std::string opening = "\n" + std::string(key) + " ";
        while (text.find(opening) != std::string::npos) {
            const std::size_t at = text.find(opening) + 1;
            text.erase(at, text.find('\n', at) + 1 - at);
        }
        return text;
    };
    const std::string version_two =
        without(replaced(body, "cppl-verification-interface 3", "cppl-verification-interface 2"), "runtime");
    expect_refused(resealed(version_two), "it is format version '2', and this compiler reads only version 3");
    const std::string version_one =
        without(replaced(version_two, "cppl-verification-interface 2", "cppl-verification-interface 1"), "model");
    expect_refused(resealed(version_one), "it is format version '1', and this compiler reads only version 3");
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

    const std::size_t first_model = body.find("model ");
    const std::size_t second_model = body.find("model ", first_model + 1);
    const std::string model_a = body.substr(first_model, second_model - first_model);
    const std::string model_b = body.substr(second_model, body.find('\n', second_model) + 1 - second_model);
    expect_refused(resealed(replaced(body, model_a + model_b, model_b + model_a)), "models are not in");
    expect_refused(resealed(replaced(body, model_a, model_a + model_a)), "models are not in");
    // A model belongs between the premises and the unsafe blocks.
    const std::size_t unsafe = body.find("unsafe ");
    const std::string unsafe_line = body.substr(unsafe, body.find('\n', unsafe) + 1 - unsafe);
    expect_refused(
        resealed(replaced(replaced(body, model_a + model_b, ""), unsafe_line, unsafe_line + model_a + model_b)),
        "found 'model' where 'end' was expected");
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

    recorded = sample();
    recorded.entries.front().models.front().name.clear();
    CPPL_CHECK(!artifact::serialize(recorded).has_value());
}

// A name read from an interface is shown with every byte a report could be
// forged with escaped, and a printable one as itself.
//
// SPEC: TUBOUND-005, TUBOUND-006
CPPL_TEST(a_recorded_text_is_shown_without_the_bytes_that_could_forge_a_line) {
    CPPL_CHECK_EQ(artifact::displayed("std::vector model"), std::string("std::vector model"));
    CPPL_CHECK_EQ(artifact::displayed("forall (x: u32). x < 100 -> result < 4"),
                  std::string("forall (x: u32). x < 100 -> result < 4"));
    CPPL_CHECK_EQ(artifact::displayed("x\nAssumption-free claims: 7"), std::string("x%0AAssumption-free claims: 7"));
    CPPL_CHECK_EQ(artifact::displayed(std::string("a\0b\r\x7f%", 6)), std::string("a%00b%0D%7F%25"));
    CPPL_CHECK_EQ(artifact::displayed("\xc3\xa9"), std::string("%C3%A9"));
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
