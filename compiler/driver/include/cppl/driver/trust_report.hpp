#pragma once

// What a compile reports about the trust of its results, and the
// machine-readable form of that report (TRUST.md 36, Annex C).
//
// The text report `--cppl-trust-report` prints and the JSON document
// `--cppl-emit-trust-report=<file>` writes are two renderings of one summary,
// and a claim is classified by the predicates below in both, so the two
// cannot disagree about what a claim rests on (TRUST.md TCB-REPORT-006).

#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::driver {

// An unsafe boundary a unit writes (SPEC.md 26): an outermost unsafe block,
// with the verified function it stands in if any, or a function declared
// unsafe. Listed whether or not a proven claim rests on it, so a report says
// where guarantees stop.
struct UnsafeBoundary {
    source::SourceLocation location;
    std::string owner;    // the verified function holding a block, if any
    std::string function; // the function an unsafe declaration marks
};

// A trusted law a unit declares, and whether a proven claim of that unit
// rests on it. An unused one could be removed without changing any result.
struct ReportedAssumption {
    obligations::TrustedPremise premise;
    bool used = false;
};

// Everything a compile of one or more units proved, assumed and left to
// runtime, unit by unit in the order the units were given.
struct TrustSummary {
    std::size_t laws = 0;
    std::size_t proven = 0;
    std::size_t contracts_proven = 0;
    std::size_t partial_contracts_proven = 0;
    std::size_t loop_invariants_proven = 0;
    std::size_t loop_measures_proven = 0;
    std::size_t call_measures_proven = 0;
    std::size_t omitted_cases_proven = 0;
    std::size_t impossible_paths_proven = 0;
    std::size_t call_preconditions_proven = 0;
    std::size_t defined_operations_proven = 0;
    std::size_t proven_by_written_proof = 0;
    std::size_t proofs_proven = 0;
    std::size_t unresolved = 0;

    // Every explicit assumption, and every proven claim with what it rests on.
    std::vector<ReportedAssumption> trusted;
    std::vector<obligations::ClaimClosure> claims;
    // Trusted laws admitting a memory proposition, which nothing can rest on.
    std::vector<obligations::TrustedMemoryAssumption> memory_trusted;
    // Every unsafe boundary written, whether or not a proven claim rests on it.
    std::vector<UnsafeBoundary> unsafe;
    // Every contract of another unit established from an interface.
    std::vector<obligations::ImportedDependency> imports;
    // Every runtime validation site of a proven contract (SPEC.md
    // RUNTIMECHECK-013).
    std::vector<obligations::RuntimeCheck> runtime_sites;
};

// Whether a proven claim rests on nothing at all: on no trusted law, no unsafe
// code, no standard-library model, whose statements are assumed of the library
// the program runs with (TRUST.md 28.1), and no contract of another unit, which
// only an interface nothing authenticates vouches for, however little that
// contract's own closure holds (SPEC.md TUBOUND-014, TRUST.md TCB-XTU-007). A
// runtime validation site is not an assumption (SPEC.md INTERACT-023), so a
// claim resting only on one is assumption-free.
[[nodiscard]] bool assumption_free(const obligations::ClaimClosure& claim);

// What a machine-readable report records about the build its claims were
// established by (TRUST.md Annex C.1). Every field is a function of the
// source, the toolchain and the arguments; none names a time, a user or a
// host.
struct BuildRecord {
    std::string compiler;                  // the C++L compiler version
    std::string source_revision;           // the commit it was built from
    std::string source_tag;                // the tag at that commit, if any
    std::string source_tree;               // whether a tracked file differed from it
    std::string compiler_build;            // the SHA-256 of its executable, provenance only
    std::string verification_semantics;    // declared by hand
    std::string verifier_semantics_digest; // computed from the semantic sources
    std::string kernel;
    std::string formal_core;
    std::uint32_t interface_format = 0;
    std::string clang;                       // the Clang that resolves C++ semantics
    std::string runtime_compiler;            // the Clang driver that compiles the runtime program
    std::string language;                    // the selected -std, or `default`
    std::string target;                      // the effective target triple
    std::vector<std::string> semantic_flags; // options that can change C++ meaning
};

// The first bytes of every report this compiler writes. A file at the report's
// path that does not begin with them is not a report.
inline constexpr std::string_view kTrustReportPrefix = "{\n  \"format\": \"cppl-trust-report\",\n";

// The version of the document's structure. A consumer refuses one it does not
// know rather than guess what a field means.
inline constexpr std::uint32_t kTrustReportVersion = 1;

// The report as a JSON document (RFC 8259), deterministic byte for byte for a
// given summary and build. Every string is shown as a report shows text read
// from outside: printable ASCII other than `%` as itself, and every other byte
// as `%XX` (artifact::displayed), so the document is ASCII, any byte of a name
// survives it, and a name cannot forge structure.
[[nodiscard]] std::string render_trust_report(const TrustSummary& summary, const BuildRecord& build);

// Writes a report so that no reader can observe a partial one: to a file beside
// the destination, renamed over it once complete.
[[nodiscard]] std::expected<void, std::string> write_trust_report(const std::string& path, std::string_view text);

// Removes a report left at `path` by an earlier compile, so a compile that no
// longer verifies does not leave one describing a build that did. A file at
// `path` that is not a report is left alone.
void withdraw_trust_report(const std::string& path);

} // namespace cppl::driver
