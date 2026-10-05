// The trust report as a JSON document (compiler/driver/src/trust_report.cpp;
// TRUST.md 36, Annex C).
//
// These cases build the summary directly, so every kind of dependency, every
// status and every byte a name can hold is exercised apart from what a source
// fixture happens to produce. Each document is parsed by the language server's
// JSON reader, an implementation independent of the writer, and checked to be
// printable ASCII, which with the parse makes it RFC 8259 JSON.

#include "cppl/artifact/interface.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/driver/trust_report.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/lsp/json.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/testing/test.hpp"
#include "cppl/vir/ids.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace d = cppl::driver;
namespace j = cppl::lsp::json;
namespace k = cppl::kernel;
namespace o = cppl::obligations;

o::ObligationId identity_of(std::string_view text) {
    return o::ObligationId{cppl::source::hash_bytes(text)};
}

cppl::source::SourceLocation at(std::uint32_t line) {
    return cppl::source::SourceLocation{"unit.cpp", line, 3};
}

d::BuildRecord build() {
    d::BuildRecord record;
    record.compiler = "0.0.1";
    record.source_revision = "0123456789abcdef";
    record.source_tag = "none";
    record.source_tree = "clean";
    record.compiler_build = cppl::source::hash_bytes("executable").to_hex();
    record.verification_semantics = "cppl-verification-2";
    record.verifier_semantics_digest = cppl::source::hash_bytes("semantics").to_hex();
    record.kernel = "cppl-kernel-0.9.0";
    record.formal_core = "cppl-core-0.9.0";
    record.interface_format = 3;
    record.clang = "clang version 22.1.8";
    record.runtime_compiler = "clang version 22.1.8";
    record.language = "c++20";
    record.target = "x86_64-unknown-linux-gnu";
    record.semantic_flags = {"-std=c++20", "-DMODE=\"fast\""};
    return record;
}

o::ClaimClosure claim(std::string subject, std::uint32_t line, o::ClaimKind kind = o::ClaimKind::Contract) {
    o::ClaimClosure closure;
    closure.kind = kind;
    closure.subject = std::move(subject);
    closure.location = at(line);
    closure.identity = identity_of(closure.subject);
    return closure;
}

o::TrustedPremise premise(std::string name, std::uint32_t line) {
    o::TrustedPremise law;
    law.law = cppl::vir::LawId{line};
    law.name = std::move(name);
    law.identity = identity_of(law.name);
    law.location = at(line);
    const k::IntType u32{32, k::Signedness::Unsigned};
    law.proposition = k::Proposition::equality(k::Type{u32}, k::Term::literal(u32, 1), k::Term::literal(u32, 1));
    law.direct = true;
    return law;
}

// Every byte `%XX` stands for, and every other byte as itself: the inverse of
// artifact::displayed.
std::string decoded(std::string_view shown) {
    std::string bytes;
    for (std::size_t index = 0; index < shown.size(); ++index) {
        if (shown[index] == '%' && index + 2 < shown.size()) {
            bytes.push_back(static_cast<char>(std::stoi(std::string(shown.substr(index + 1, 2)), nullptr, 16)));
            index += 2;
        } else {
            bytes.push_back(shown[index]);
        }
    }
    return bytes;
}

bool printable(std::string_view text) {
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte != '\n' && (byte < 0x20U || byte > 0x7EU)) {
            return false;
        }
    }
    return true;
}

// Renders, checks the document is JSON a reader accepts, and parses it.
j::Value document(const d::TrustSummary& summary) {
    const std::string text = d::render_trust_report(summary, build());
    CPPL_CHECK(text.starts_with(d::kTrustReportPrefix));
    CPPL_CHECK(text.ends_with("}\n"));
    CPPL_CHECK(printable(text));
    return j::parse(text);
}

const j::Value& member(const j::Value& object, std::string_view key) {
    const j::Value* found = object.find(std::string(key));
    CPPL_CHECK(found != nullptr);
    return *found;
}

double count(const j::Value& report, std::string_view key) {
    return member(member(report, "counts"), key).as_number();
}

const j::Array& array(const j::Value& report, std::string_view key) {
    return member(report, key).as_array();
}

// The claim of the document whose subject is `subject`.
const j::Value& claim_named(const j::Value& report, std::string_view subject) {
    for (const j::Value& candidate : array(report, "claims")) {
        if (member(candidate, "subject").as_string() == subject) {
            return candidate;
        }
    }
    CPPL_CHECK(false);
    return array(report, "claims").front();
}

std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

// TRUST.md Annex C.1, C.4 -- the build is recorded, nothing is trusted without
// evidence, and what is not analysed says so.
CPPL_TEST(an_empty_summary_is_a_complete_document) {
    const j::Value report = document(d::TrustSummary{});
    CPPL_CHECK(member(report, "format").as_string() == "cppl-trust-report");
    CPPL_CHECK(member(report, "version").as_number() == static_cast<double>(d::kTrustReportVersion));
    CPPL_CHECK(member(report, "string_encoding").as_string() == "percent");
    const j::Value& record = member(report, "build");
    CPPL_CHECK(member(record, "kernel").as_string() == "cppl-kernel-0.9.0");
    CPPL_CHECK(member(record, "formal_core").as_string() == "cppl-core-0.9.0");
    CPPL_CHECK(member(record, "interface_format").as_number() == 3.0);
    CPPL_CHECK(member(record, "target").as_string() == "x86_64-unknown-linux-gnu");
    CPPL_CHECK(member(record, "semantic_flags").as_array().size() == 2);
    CPPL_CHECK(member(record, "semantic_flags").as_array()[1].as_string() == "-DMODE=\"fast\"");
    for (const char* key : {"claims", "trusted_laws", "imported_contracts", "unsafe_regions",
                            "runtime_validation_sites", "trusted_solvers", "directly_trusted_automation"}) {
        CPPL_CHECK(array(report, key).empty());
    }
    CPPL_CHECK(member(report, "unverified_ffi_boundaries").as_string() == "not_analysed");
    CPPL_CHECK(member(report, "interface_provenance").as_string() == "none_imported");
    for (const auto& [key, value] : member(report, "counts").as_object()) {
        CPPL_CHECK(value.as_number() == 0.0);
    }
}

// TRUST.md Annex C.5 -- whether a PROVEN claim's trusted closure is empty is
// stated outright, and assumption-free means resting on nothing at all.
CPPL_TEST(each_claim_says_whether_it_rests_on_anything) {
    d::TrustSummary summary;
    summary.claims.push_back(claim("outright", 1));

    o::ClaimClosure trusted = claim("trusted", 2);
    trusted.premises.push_back(premise("sensor", 40));
    summary.claims.push_back(trusted);

    o::ClaimClosure unsafe = claim("unsafe", 3);
    unsafe.unsafe.push_back(o::UnsafeDependency{at(30), true});
    summary.claims.push_back(unsafe);

    o::ClaimClosure modeled = claim("modeled", 4);
    modeled.library.push_back(o::LibraryDependency{cppl::source::RepresentationKind::Vector, false});
    summary.claims.push_back(modeled);

    o::ClaimClosure checked = claim("checked", 5);
    checked.runtime.push_back(o::RuntimeCheck{at(31), "Positive", "value > 0", "admit", true});
    summary.claims.push_back(checked);

    o::ClaimClosure recorded = claim("recorded", 6);
    o::ImportedDependency record;
    record.name = "withdraw";
    record.symbol = "c:@F@withdraw#i#";
    record.origin = "account.cppli";
    record.entry = cppl::source::hash_bytes("entry");
    record.direct = true;
    recorded.imported.push_back(record);
    summary.claims.push_back(recorded);

    o::ClaimClosure relayed = claim("relayed", 7);
    record.premises.push_back(
        cppl::artifact::Premise{cppl::source::hash_bytes("calibrated"), "calibrated", "account.hpp", 9});
    relayed.imported.push_back(record);
    summary.claims.push_back(relayed);

    const j::Value report = document(summary);
    struct Expected {
        const char* subject;
        bool assumption_free;
        bool closure_empty;
    };
    for (const Expected& expected :
         {Expected{"outright", true, true}, Expected{"trusted", false, false}, Expected{"unsafe", false, true},
          Expected{"modeled", false, true}, Expected{"checked", true, true}, Expected{"recorded", false, true},
          Expected{"relayed", false, false}}) {
        const j::Value& found = claim_named(report, expected.subject);
        CPPL_CHECK(member(found, "status").as_string() == "PROVEN");
        CPPL_CHECK(member(found, "assumption_free").as_boolean() == expected.assumption_free);
        CPPL_CHECK(member(found, "trusted_closure_empty").as_boolean() == expected.closure_empty);
    }
    CPPL_CHECK(count(report, "assumption_free_claims") == 2.0);
    CPPL_CHECK(count(report, "trust_dependent_claims") == 2.0);
    CPPL_CHECK(count(report, "unsafe_dependent_claims") == 1.0);
    CPPL_CHECK(count(report, "library_model_dependent_claims") == 1.0);
    CPPL_CHECK(count(report, "runtime_check_dependent_claims") == 1.0);
    CPPL_CHECK(count(report, "interface_dependent_claims") == 2.0);

    const j::Value& modeled_claim = claim_named(report, "modeled");
    CPPL_CHECK(member(member(modeled_claim, "library_models").as_array()[0], "model").as_string() == "std::vector");
    const j::Value& relayed_claim = claim_named(report, "relayed");
    const j::Value& imported = member(relayed_claim, "imported_contracts").as_array()[0];
    CPPL_CHECK(member(imported, "interface").as_string() == "account.cppli");
    CPPL_CHECK(member(imported, "direct").as_boolean());
    CPPL_CHECK(member(imported, "entry").as_string() == cppl::source::hash_bytes("entry").to_hex());
    CPPL_CHECK(member(member(imported, "trusted_laws").as_array()[0], "name").as_string() == "calibrated");
    const j::Value& trusted_claim = claim_named(report, "trusted");
    CPPL_CHECK(member(trusted_claim, "identity").as_string() == identity_of("trusted").digest.to_hex());
    CPPL_CHECK(member(trusted_claim, "correctness").as_string() == "total");
}

// A contract says whether it is total; any other claim has no correctness.
CPPL_TEST(only_a_contract_states_its_correctness) {
    d::TrustSummary summary;
    o::ClaimClosure partial = claim("partial", 1);
    partial.total = false;
    summary.claims.push_back(partial);
    summary.claims.push_back(claim("law", 2, o::ClaimKind::Law));
    summary.claims.push_back(claim("case", 3, o::ClaimKind::OmittedCase));
    const j::Value report = document(summary);
    CPPL_CHECK(member(claim_named(report, "partial"), "correctness").as_string() == "partial");
    CPPL_CHECK(member(claim_named(report, "law"), "correctness").is_null());
    CPPL_CHECK(member(claim_named(report, "law"), "kind").as_string() == "law");
    CPPL_CHECK(member(claim_named(report, "case"), "kind").as_string() == "omitted_case");
    CPPL_CHECK(count(report, "partial_correctness_contracts") == 1.0);
}

// A name may hold any byte. It survives the document exactly, and none of it
// can end a string, add a line or make the document other than ASCII.
CPPL_TEST(every_byte_of_a_name_survives_and_none_forges_structure) {
    std::string hostile = "q\"uote\\back\nline\r%25%\t";
    for (const unsigned int byte : {0x00U, 0x01U, 0x7FU, 0xFFU, 0xC3U, 0xA9U}) {
        hostile.push_back(static_cast<char>(byte));
    }
    hostile += " },{\"status\": \"TRUSTED\"";
    d::TrustSummary summary;
    o::ClaimClosure named = claim(hostile, 1);
    named.location.file = hostile;
    summary.claims.push_back(named);
    summary.runtime_sites.push_back(o::RuntimeCheck{at(2), hostile, hostile, hostile, true});
    const j::Value report = document(summary);
    const j::Value& found = array(report, "claims")[0];
    CPPL_CHECK(decoded(member(found, "subject").as_string()) == hostile);
    CPPL_CHECK(decoded(member(member(found, "location"), "file").as_string()) == hostile);
    CPPL_CHECK(member(found, "subject").as_string() == cppl::artifact::displayed(hostile));
    const j::Value& site = array(report, "runtime_validation_sites")[0];
    CPPL_CHECK(decoded(member(site, "predicate").as_string()) == hostile);
    CPPL_CHECK(array(report, "claims").size() == 1);
    CPPL_CHECK(array(report, "trusted_laws").empty());
}

// The same summary is the same bytes, and claims keep the order they were
// proven in.
CPPL_TEST(the_document_is_deterministic_and_ordered) {
    d::TrustSummary summary;
    summary.claims.push_back(claim("first", 1));
    summary.claims.push_back(claim("second", 2));
    const std::string once = d::render_trust_report(summary, build());
    CPPL_CHECK(once == d::render_trust_report(summary, build()));
    const j::Value report = j::parse(once);
    CPPL_CHECK(member(array(report, "claims")[0], "subject").as_string() == "first");
    CPPL_CHECK(member(array(report, "claims")[1], "subject").as_string() == "second");
    std::swap(summary.claims[0], summary.claims[1]);
    CPPL_CHECK(once != d::render_trust_report(summary, build()));
}

// TRUST.md Annex C.3 -- each assumption is enumerable with what it states and
// whether anything of its unit rests on it; a memory proposition, which
// nothing can use, says what it admits.
CPPL_TEST(trusted_laws_say_what_they_state_and_whether_they_are_used) {
    d::TrustSummary summary;
    summary.trusted.push_back(d::ReportedAssumption{premise("used_law", 1), true});
    summary.trusted.push_back(d::ReportedAssumption{premise("idle_law", 2), false});
    summary.memory_trusted.push_back(
        o::TrustedMemoryAssumption{"device_window", identity_of("device_window"), at(3), "readable(registers, count)"});
    const j::Value report = document(summary);
    const j::Array& laws = array(report, "trusted_laws");
    CPPL_CHECK(laws.size() == 3);
    CPPL_CHECK(member(laws[0], "status").as_string() == "TRUSTED");
    CPPL_CHECK(member(laws[0], "kind").as_string() == "proposition");
    CPPL_CHECK(member(laws[0], "used").as_boolean());
    CPPL_CHECK(member(laws[0], "proposition").as_string() ==
               cppl::artifact::displayed(k::describe(premise("used_law", 1).proposition)));
    CPPL_CHECK(member(laws[0], "admits").is_null());
    CPPL_CHECK(!member(laws[1], "used").as_boolean());
    CPPL_CHECK(member(laws[2], "kind").as_string() == "memory");
    CPPL_CHECK(member(laws[2], "proposition").is_null());
    CPPL_CHECK(member(laws[2], "admits").as_string() == "readable(registers, count)");
    CPPL_CHECK(!member(laws[2], "used").as_boolean());
    CPPL_CHECK(count(report, "laws_trusted") == 3.0);
    CPPL_CHECK(count(report, "unused_trusted_laws") == 2.0);
}

// Where the guarantees stop: unsafe code and runtime checks are listed with
// their own status, never as proven.
CPPL_TEST(unsafe_regions_and_runtime_sites_carry_their_own_status) {
    d::TrustSummary summary;
    summary.unsafe.push_back(d::UnsafeBoundary{at(1), "credit", ""});
    summary.unsafe.push_back(d::UnsafeBoundary{at(2), "", ""});
    summary.unsafe.push_back(d::UnsafeBoundary{at(3), "", "poke"});
    summary.runtime_sites.push_back(o::RuntimeCheck{at(4), "Positive", "value > 0", "admit", true});
    const j::Value report = document(summary);
    const j::Array& regions = array(report, "unsafe_regions");
    CPPL_CHECK(regions.size() == 3);
    CPPL_CHECK(member(regions[0], "status").as_string() == "UNSAFE");
    CPPL_CHECK(member(regions[0], "kind").as_string() == "block");
    CPPL_CHECK(member(regions[0], "verified_function").as_string() == "credit");
    CPPL_CHECK(member(regions[1], "verified_function").is_null());
    CPPL_CHECK(member(regions[2], "kind").as_string() == "function");
    CPPL_CHECK(member(regions[2], "function").as_string() == "poke");
    CPPL_CHECK(member(regions[2], "verified_function").is_null());
    const j::Value& site = array(report, "runtime_validation_sites")[0];
    CPPL_CHECK(member(site, "status").as_string() == "RUNTIME-CHECKED");
    CPPL_CHECK(member(site, "refinement").as_string() == "Positive");
    CPPL_CHECK(count(report, "unsafe_regions") == 3.0);
    CPPL_CHECK(count(report, "runtime_validation_sites") == 1.0);
}

// SPEC: TUBOUND-006 -- an imported contract carries every category its
// producing unit's proof rested on, and the document says interface provenance
// is unauthenticated (TRUST.md TCB-XTU-010).
CPPL_TEST(imported_contracts_carry_what_their_interface_recorded) {
    d::TrustSummary summary;
    o::ImportedDependency record;
    record.name = "withdraw";
    record.symbol = "c:@F@withdraw#i#";
    record.origin = "account.cppli";
    record.entry = cppl::source::hash_bytes("entry");
    record.total = true;
    record.premises.push_back(
        cppl::artifact::Premise{cppl::source::hash_bytes("calibrated"), "calibrated", "account.hpp", 9});
    record.models.push_back(cppl::artifact::Model{cppl::source::hash_bytes("vector"), "std::vector model"});
    record.unsafe.push_back(cppl::artifact::UnsafeBlock{"account.cpp", 12, 5});
    record.runtime.push_back(cppl::artifact::RuntimeCheck{"account.cpp", 20, 9, "Cents", "value >= 0"});
    record.depends.push_back(cppl::artifact::Dependency{"c:@F@audit#", cppl::source::hash_bytes("audit")});
    summary.imports.push_back(record);
    const j::Value report = document(summary);
    CPPL_CHECK(member(report, "interface_provenance").as_string() == "unauthenticated");
    const j::Value& imported = array(report, "imported_contracts")[0];
    CPPL_CHECK(imported.find("direct") == nullptr);
    CPPL_CHECK(member(imported, "correctness").as_string() == "total");
    CPPL_CHECK(member(member(imported, "library_models").as_array()[0], "name").as_string() == "std::vector model");
    CPPL_CHECK(member(member(imported, "unsafe_blocks").as_array()[0], "line").as_number() == 12.0);
    CPPL_CHECK(member(member(imported, "runtime_checks").as_array()[0], "predicate").as_string() == "value >= 0");
    CPPL_CHECK(member(member(imported, "contracts").as_array()[0], "entry").as_string() ==
               cppl::source::hash_bytes("audit").to_hex());
    CPPL_CHECK(count(report, "function_contracts_imported") == 1.0);
}

// A report is written whole, and only a report is withdrawn: the path is the
// build's choice, and a mistyped one must not cost the file it names.
CPPL_TEST(a_report_is_written_whole_and_only_a_report_is_withdrawn) {
    const d::ScratchDirectory scratch;
    CPPL_CHECK(!scratch.path().empty());
    const std::filesystem::path path = scratch.path() / "report.json";
    const std::string text = d::render_trust_report(d::TrustSummary{}, build());
    CPPL_CHECK(d::write_trust_report(path.string(), text).has_value());
    CPPL_CHECK(read(path) == text);
    CPPL_CHECK(
        std::distance(std::filesystem::directory_iterator(scratch.path()), std::filesystem::directory_iterator{}) == 1);

    d::withdraw_trust_report(path.string());
    CPPL_CHECK(!std::filesystem::exists(path));
    d::withdraw_trust_report(path.string());

    const std::filesystem::path other = scratch.path() / "other.json";
    {
        std::ofstream stream(other, std::ios::binary);
        stream << "{\"format\": \"cppl-trust-report\"}\n";
    }
    d::withdraw_trust_report(other.string());
    CPPL_CHECK(std::filesystem::exists(other));

    const std::filesystem::path occupied = scratch.path() / "occupied.json";
    std::filesystem::create_directory(occupied);
    CPPL_CHECK(!d::write_trust_report(occupied.string(), text).has_value());
    CPPL_CHECK(std::filesystem::is_directory(occupied));
    CPPL_CHECK(
        std::distance(std::filesystem::directory_iterator(scratch.path()), std::filesystem::directory_iterator{}) == 2);
    CPPL_CHECK(!d::write_trust_report((scratch.path() / "missing" / "report.json").string(), text).has_value());
}
