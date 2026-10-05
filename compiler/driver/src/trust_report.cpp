#include "cppl/driver/trust_report.hpp"

#include "cppl/artifact/interface.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cppl::driver {

namespace {

// A JSON document written in one pass, members and elements each on a line of
// their own, indented two spaces a level. Keys and strings go through the same
// encoding, so nothing a caller passes can close a string early.
class Writer {
  public:
    void begin_object() {
        open('{', '}');
    }

    void end_object() {
        close();
    }

    void begin_array() {
        open('[', ']');
    }

    void end_array() {
        close();
    }

    void key(std::string_view name) {
        separate();
        quote(name);
        text_ += ": ";
        after_key_ = true;
    }

    void string(std::string_view value) {
        before_value();
        quote(value);
    }

    void number(std::uint64_t value) {
        before_value();
        text_ += std::to_string(value);
    }

    void boolean(bool value) {
        before_value();
        text_ += value ? "true" : "false";
    }

    void null() {
        before_value();
        text_ += "null";
    }

    [[nodiscard]] std::string take() && {
        text_ += '\n';
        return std::move(text_);
    }

  private:
    struct Frame {
        char closing = '}';
        std::size_t count = 0;
    };

    void open(char opening, char closing) {
        before_value();
        text_ += opening;
        frames_.push_back(Frame{closing, 0});
    }

    void close() {
        const Frame frame = frames_.back();
        frames_.pop_back();
        if (frame.count > 0) {
            newline();
        }
        text_ += frame.closing;
    }

    // A value directly after its key continues that line; any other value is
    // the next element of the array it stands in.
    void before_value() {
        if (after_key_) {
            after_key_ = false;
            return;
        }
        if (!frames_.empty()) {
            separate();
        }
    }

    void separate() {
        Frame& frame = frames_.back();
        if (frame.count++ > 0) {
            text_ += ',';
        }
        newline();
    }

    void newline() {
        text_ += '\n';
        text_.append(2 * frames_.size(), ' ');
    }

    // The displayed form leaves only printable ASCII other than `%`, so the
    // quotation mark and the backslash are the two characters JSON needs
    // escaped among what remains (RFC 8259, section 7).
    void quote(std::string_view value) {
        text_ += '"';
        for (const char character : artifact::displayed(value)) {
            if (character == '"' || character == '\\') {
                text_ += '\\';
            }
            text_ += character;
        }
        text_ += '"';
    }

    std::string text_;
    std::vector<Frame> frames_;
    bool after_key_ = false;
};

std::string_view claim_kind(obligations::ClaimKind kind) {
    switch (kind) {
        case obligations::ClaimKind::Law:
            return "law";
        case obligations::ClaimKind::Proof:
            return "proof";
        case obligations::ClaimKind::LawInstance:
            return "law_instance";
        case obligations::ClaimKind::Contract:
            return "contract";
        case obligations::ClaimKind::OmittedCase:
            return "omitted_case";
        case obligations::ClaimKind::ImpossiblePath:
            return "impossible_path";
    }
    return "unknown";
}

void location(Writer& json, const source::SourceLocation& where) {
    json.begin_object();
    json.key("file");
    json.string(where.file);
    json.key("line");
    json.number(where.line);
    json.key("column");
    json.number(where.column);
    json.end_object();
}

void optional_string(Writer& json, const std::string& value) {
    if (value.empty()) {
        json.null();
    } else {
        json.string(value);
    }
}

void build_record(Writer& json, const BuildRecord& build) {
    json.begin_object();
    json.key("compiler");
    json.string(build.compiler);
    json.key("source_revision");
    json.string(build.source_revision);
    json.key("source_tag");
    json.string(build.source_tag);
    json.key("source_tree");
    json.string(build.source_tree);
    json.key("compiler_build");
    json.string(build.compiler_build);
    json.key("verification_semantics");
    json.string(build.verification_semantics);
    json.key("verifier_semantics_digest");
    json.string(build.verifier_semantics_digest);
    json.key("kernel");
    json.string(build.kernel);
    json.key("formal_core");
    json.string(build.formal_core);
    json.key("interface_format");
    json.number(build.interface_format);
    json.key("clang");
    json.string(build.clang);
    json.key("runtime_compiler");
    json.string(build.runtime_compiler);
    json.key("language");
    json.string(build.language);
    json.key("target");
    json.string(build.target);
    json.key("semantic_flags");
    json.begin_array();
    for (const std::string& flag : build.semantic_flags) {
        json.string(flag);
    }
    json.end_array();
    json.end_object();
}

void counts(Writer& json, const TrustSummary& summary) {
    const auto claims_where = [&summary](auto predicate) {
        return static_cast<std::uint64_t>(std::ranges::count_if(summary.claims, predicate));
    };
    const auto unused = static_cast<std::uint64_t>(
        std::ranges::count_if(summary.trusted, [](const ReportedAssumption& law) { return !law.used; }));
    const auto rows = std::to_array<std::pair<std::string_view, std::uint64_t>>({
        {"laws_proven", summary.proven},
        {"laws_proven_by_written_proof", summary.proven_by_written_proof},
        {"proof_declarations_proven", summary.proofs_proven},
        {"laws_trusted", summary.trusted.size() + summary.memory_trusted.size()},
        {"function_contracts_proven", summary.contracts_proven},
        {"partial_correctness_contracts_proven", summary.partial_contracts_proven},
        {"function_contracts_imported", summary.imports.size()},
        {"call_preconditions_proven", summary.call_preconditions_proven},
        {"defined_operations_proven", summary.defined_operations_proven},
        {"loop_invariants_proven", summary.loop_invariants_proven},
        {"loop_measures_proven", summary.loop_measures_proven},
        {"recursive_call_measures_proven", summary.call_measures_proven},
        {"omitted_cases_proven", summary.omitted_cases_proven},
        {"impossible_paths_proven", summary.impossible_paths_proven},
        {"unresolved_obligations", summary.unresolved},
        {"trust_dependent_claims", claims_where(obligations::rests_on_trusted_laws)},
        {"unsafe_dependent_claims", claims_where(obligations::rests_on_unsafe_code)},
        {"assumption_free_claims", claims_where(assumption_free)},
        {"unused_trusted_laws", unused + summary.memory_trusted.size()},
        {"library_model_dependent_claims", claims_where(obligations::rests_on_library_models)},
        {"partial_correctness_contracts", claims_where([](const obligations::ClaimClosure& claim) {
             return claim.kind == obligations::ClaimKind::Contract && !claim.total;
         })},
        {"interface_dependent_claims",
         claims_where([](const obligations::ClaimClosure& claim) { return !claim.imported.empty(); })},
        {"runtime_check_dependent_claims", claims_where(obligations::rests_on_runtime_checks)},
        {"unsafe_regions", summary.unsafe.size()},
        {"runtime_validation_sites", summary.runtime_sites.size()},
    });
    json.begin_object();
    for (const auto& [name, value] : rows) {
        json.key(name);
        json.number(value);
    }
    json.end_object();
}

// A contract of another unit, as the interface that recorded it states it and
// what that unit's proof of it rests on, each kind apart. Every text here was
// read from an interface (TRUST.md TCB-XTU-010).
void imported_record(Writer& json, const obligations::ImportedDependency& imported, bool with_direct) {
    json.begin_object();
    json.key("name");
    json.string(imported.name);
    json.key("symbol");
    json.string(imported.symbol);
    json.key("interface");
    json.string(imported.origin);
    json.key("entry");
    json.string(imported.entry.to_hex());
    json.key("correctness");
    json.string(imported.total ? "total" : "partial");
    if (with_direct) {
        json.key("direct");
        json.boolean(imported.direct);
    }
    json.key("trusted_laws");
    json.begin_array();
    for (const artifact::Premise& premise : imported.premises) {
        json.begin_object();
        json.key("name");
        json.string(premise.name);
        json.key("file");
        json.string(premise.file);
        json.key("line");
        json.number(premise.line);
        json.key("identity");
        json.string(premise.identity.to_hex());
        json.end_object();
    }
    json.end_array();
    json.key("library_models");
    json.begin_array();
    for (const artifact::Model& model : imported.models) {
        json.begin_object();
        json.key("name");
        json.string(model.name);
        json.key("identity");
        json.string(model.identity.to_hex());
        json.end_object();
    }
    json.end_array();
    json.key("unsafe_blocks");
    json.begin_array();
    for (const artifact::UnsafeBlock& block : imported.unsafe) {
        json.begin_object();
        json.key("file");
        json.string(block.file);
        json.key("line");
        json.number(block.line);
        json.key("column");
        json.number(block.column);
        json.end_object();
    }
    json.end_array();
    json.key("runtime_checks");
    json.begin_array();
    for (const artifact::RuntimeCheck& check : imported.runtime) {
        json.begin_object();
        json.key("file");
        json.string(check.file);
        json.key("line");
        json.number(check.line);
        json.key("column");
        json.number(check.column);
        json.key("refinement");
        json.string(check.refinement);
        json.key("predicate");
        json.string(check.predicate);
        json.end_object();
    }
    json.end_array();
    json.key("contracts");
    json.begin_array();
    for (const artifact::Dependency& dependency : imported.depends) {
        json.begin_object();
        json.key("symbol");
        json.string(dependency.symbol);
        json.key("entry");
        json.string(dependency.entry.to_hex());
        json.end_object();
    }
    json.end_array();
    json.end_object();
}

// One proven claim and everything it rests on (TRUST.md Annex C.2). Whether
// its trusted closure is empty is stated outright, so no consumer has to read
// prose or recompute it to tell (Annex C.5).
void claim_record(Writer& json, const obligations::ClaimClosure& claim) {
    json.begin_object();
    json.key("kind");
    json.string(claim_kind(claim.kind));
    json.key("subject");
    json.string(claim.subject);
    json.key("symbol");
    optional_string(json, claim.symbol);
    json.key("location");
    location(json, claim.location);
    json.key("identity");
    json.string(claim.identity.digest.to_hex());
    json.key("status");
    json.string("PROVEN");
    json.key("correctness");
    if (claim.kind == obligations::ClaimKind::Contract) {
        json.string(claim.total ? "total" : "partial");
    } else {
        json.null();
    }
    json.key("assumption_free");
    json.boolean(assumption_free(claim));
    json.key("trusted_closure_empty");
    json.boolean(!obligations::rests_on_trusted_laws(claim));
    json.key("trusted_laws");
    json.begin_array();
    for (const obligations::TrustedPremise& premise : claim.premises) {
        json.begin_object();
        json.key("name");
        json.string(premise.name);
        json.key("location");
        location(json, premise.location);
        json.key("identity");
        json.string(premise.identity.digest.to_hex());
        json.key("direct");
        json.boolean(premise.direct);
        json.end_object();
    }
    json.end_array();
    json.key("unsafe_blocks");
    json.begin_array();
    for (const obligations::UnsafeDependency& dependency : claim.unsafe) {
        json.begin_object();
        json.key("location");
        location(json, dependency.location);
        json.key("direct");
        json.boolean(dependency.direct);
        json.end_object();
    }
    json.end_array();
    json.key("library_models");
    json.begin_array();
    for (const obligations::LibraryDependency& dependency : claim.library) {
        json.begin_object();
        json.key("model");
        json.string(source::describe_model(dependency.model));
        json.key("direct");
        json.boolean(dependency.direct);
        json.end_object();
    }
    json.end_array();
    json.key("runtime_checks");
    json.begin_array();
    for (const obligations::RuntimeCheck& check : claim.runtime) {
        json.begin_object();
        json.key("location");
        location(json, check.location);
        json.key("refinement");
        json.string(check.refinement);
        json.key("predicate");
        json.string(check.predicate);
        json.key("function");
        json.string(check.function);
        json.key("direct");
        json.boolean(check.direct);
        json.end_object();
    }
    json.end_array();
    json.key("imported_contracts");
    json.begin_array();
    for (const obligations::ImportedDependency& imported : claim.imported) {
        imported_record(json, imported, true);
    }
    json.end_array();
    json.key("proof_dependencies");
    json.begin_array();
    for (const obligations::ClaimUse& use : claim.uses) {
        json.begin_object();
        json.key("kind");
        json.string(claim_kind(use.kind));
        json.key("name");
        json.string(use.name);
        json.key("location");
        location(json, use.location);
        json.end_object();
    }
    json.end_array();
    json.end_object();
}

// A trusted assumption (TRUST.md Annex C.3): what it states, where, and
// whether a proven claim of its unit rests on it. The proposition is the one
// the kernel is given as a premise, in the kernel's notation.
void trusted_laws(Writer& json, const TrustSummary& summary) {
    json.begin_array();
    for (const ReportedAssumption& law : summary.trusted) {
        json.begin_object();
        json.key("name");
        json.string(law.premise.name);
        json.key("location");
        location(json, law.premise.location);
        json.key("identity");
        json.string(law.premise.identity.digest.to_hex());
        json.key("status");
        json.string("TRUSTED");
        json.key("kind");
        json.string("proposition");
        json.key("proposition");
        json.string(kernel::describe(law.premise.proposition));
        json.key("admits");
        json.null();
        json.key("used");
        json.boolean(law.used);
        json.end_object();
    }
    // A memory proposition is a capability rather than a proposition the kernel
    // checks, so nothing can rest on one (SPEC.md TRUSTED-003).
    for (const obligations::TrustedMemoryAssumption& law : summary.memory_trusted) {
        json.begin_object();
        json.key("name");
        json.string(law.name);
        json.key("location");
        location(json, law.location);
        json.key("identity");
        json.string(law.identity.digest.to_hex());
        json.key("status");
        json.string("TRUSTED");
        json.key("kind");
        json.string("memory");
        json.key("proposition");
        json.null();
        json.key("admits");
        json.string(law.statement);
        json.key("used");
        json.boolean(false);
        json.end_object();
    }
    json.end_array();
}

void unsafe_regions(Writer& json, const TrustSummary& summary) {
    json.begin_array();
    for (const UnsafeBoundary& boundary : summary.unsafe) {
        json.begin_object();
        json.key("status");
        json.string("UNSAFE");
        json.key("kind");
        json.string(boundary.function.empty() ? "block" : "function");
        json.key("location");
        location(json, boundary.location);
        json.key("function");
        optional_string(json, boundary.function);
        json.key("verified_function");
        optional_string(json, boundary.function.empty() ? boundary.owner : std::string{});
        json.end_object();
    }
    json.end_array();
}

void runtime_sites(Writer& json, const TrustSummary& summary) {
    json.begin_array();
    for (const obligations::RuntimeCheck& check : summary.runtime_sites) {
        json.begin_object();
        json.key("status");
        json.string("RUNTIME-CHECKED");
        json.key("location");
        location(json, check.location);
        json.key("refinement");
        json.string(check.refinement);
        json.key("predicate");
        json.string(check.predicate);
        json.key("function");
        json.string(check.function);
        json.end_object();
    }
    json.end_array();
}

} // namespace

bool assumption_free(const obligations::ClaimClosure& claim) {
    return claim.imported.empty() && !obligations::rests_on_trusted_laws(claim) &&
           !obligations::rests_on_unsafe_code(claim) && !obligations::rests_on_library_models(claim);
}

std::string render_trust_report(const TrustSummary& summary, const BuildRecord& build) {
    Writer json;
    json.begin_object();
    json.key("format");
    json.string("cppl-trust-report");
    json.key("version");
    json.number(kTrustReportVersion);
    json.key("string_encoding");
    json.string("percent");
    json.key("build");
    build_record(json, build);
    json.key("counts");
    counts(json, summary);
    json.key("claims");
    json.begin_array();
    for (const obligations::ClaimClosure& claim : summary.claims) {
        claim_record(json, claim);
    }
    json.end_array();
    json.key("trusted_laws");
    trusted_laws(json, summary);
    json.key("imported_contracts");
    json.begin_array();
    for (const obligations::ImportedDependency& imported : summary.imports) {
        imported_record(json, imported, false);
    }
    json.end_array();
    json.key("unsafe_regions");
    unsafe_regions(json, summary);
    json.key("runtime_validation_sites");
    runtime_sites(json, summary);
    // What this implementation does not look at is said to be unlooked at,
    // never counted as zero (TRUST.md 36.2).
    json.key("unverified_ffi_boundaries");
    json.string("not_analysed");
    // No solver, plugin or external checker is trusted without evidence the
    // kernel checks (TRUST.md Annex C.4).
    json.key("trusted_solvers");
    json.begin_array();
    json.end_array();
    json.key("directly_trusted_automation");
    json.begin_array();
    json.end_array();
    // An interface's integrity is checked and its origin is not (TRUST.md
    // TCB-XTU-010).
    json.key("interface_provenance");
    json.string(summary.imports.empty() ? "none_imported" : "unauthenticated");
    json.end_object();
    return std::move(json).take();
}

std::expected<void, std::string> write_trust_report(const std::string& path, std::string_view text) {
    // Named by its content, so two compiles writing the same report at once
    // write the same bytes to the same partial file.
    const std::filesystem::path destination(path);
    std::filesystem::path partial = destination;
    partial += ".partial-" + source::hash_bytes(text).to_short_hex(16);
    if (!write_scratch_file(partial, text)) {
        return std::unexpected("could not write '" + partial.string() + "'");
    }
    std::error_code error;
    std::filesystem::rename(partial, destination, error);
    if (error) {
        std::filesystem::remove(partial, error);
        return std::unexpected("could not replace '" + path + "'");
    }
    return {};
}

void withdraw_trust_report(const std::string& path) {
    // Only a file that is a report is removed: the option names a path the
    // build chose, and a mistyped one must not cost the file it names.
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return;
    }
    std::string first(kTrustReportPrefix.size(), '\0');
    stream.read(first.data(), static_cast<std::streamsize>(first.size()));
    first.resize(static_cast<std::size_t>(stream.gcount()));
    stream.close();
    if (first != kTrustReportPrefix) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(path, error);
}

} // namespace cppl::driver
