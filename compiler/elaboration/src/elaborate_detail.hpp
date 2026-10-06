#pragma once

// What the files of the elaboration share: the elaborator of expressions,
// the case analyses it resolves, and the conversions of places, types and
// projected functions.

#include "cppl/clang/ast.hpp"
#include "cppl/decomposition/decomposition.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace cppl::elaboration::detail::elaborator {

inline void report(diagnostics::Engine& engine, diagnostics::Category category, const source::SourceLocation& location,
                   std::string message, std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    diagnostic.location = location;
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), location});
    }
    engine.report(std::move(diagnostic));
}

// Defined in elaborate_expressions.cpp.
vir::Place convert_place(const clangbridge::Place& place);

std::optional<vir::Type> convert_type(const clangbridge::Type& type);

vir::BinaryOp convert_operator(clangbridge::BinaryOp op);

// The cases of a partition the arms of one statement have accounted for so far.
struct Coverage {
    std::vector<bool> named;
    bool residual = false;
};

// Which case of a partition one written arm denotes.
struct MatchedArm {
    std::optional<std::uint32_t> descriptor; // a named case, or none for the residual
    std::string label;
    const std::vector<decomposition::ProofBinding>* bindings = nullptr;
};

// Defined in elaborate_cases.cpp.
std::optional<MatchedArm> match_arm(const frontend::ProofArm& arm, const vir::Expr* label,
                                    const decomposition::SumDecomposition& sum, const decomposition::Provider& provider,
                                    const vir::Type& subject, Coverage& coverage, diagnostics::Engine& engine);

bool exhaustive(const decomposition::SumDecomposition& sum, const Coverage& coverage, const vir::Type& subject,
                const source::SourceLocation& location, diagnostics::Engine& engine);

bool product_arm(const frontend::ProofStatement& statement, const decomposition::ProductDecomposition& product,
                 diagnostics::Engine& engine);

void record_states(const frontend::ProofStatement& statement, const vir::Type& subject,
                   const decomposition::Decomposition& decomposed, std::vector<SubjectStates>* recorded);

// Converts resolved Clang expressions into VIR.
//
// Every expression C++L cannot represent stops the conversion with a reason at
// a source location. Nothing is dropped silently.
// What a claim that a path cannot occur names, by the marker of its block: the
// proof declaration, or nothing when no proof has that name.
struct ResolvedClaim {
    std::optional<vir::ProofId> proof;
    std::string evidence;
    // The case an omission in a split's arm accounts for (SPEC.md CASE-012).
    std::optional<std::string> omitted;
};
using ResolvedClaims = std::map<std::string, ResolvedClaim, std::less<>>;

// The statement each case split on a runtime path was written as, by the
// marker of its block (SPEC.md CASE-017).
using ResolvedSplits = std::map<std::string, const frontend::ProofStatement*, std::less<>>;

class ExpressionElaborator {
  public:
    explicit ExpressionElaborator(std::uint32_t& next_id, const ResolvedClaims* claims = nullptr,
                                  const ResolvedSplits* splits = nullptr, diagnostics::Engine* engine = nullptr,
                                  std::vector<SubjectStates>* states = nullptr)
        : next_id_(next_id),
          claims_(claims),
          splits_(splits),
          engine_(engine),
          states_(states) {}

    // Whether conversion stopped at a case split whose arms were refused. That
    // refusal was reported where it was found, in the words a proof-side split
    // would have been refused in, so nothing more general need be said.
    [[nodiscard]] bool refused() const noexcept {
        return refused_;
    }

    struct Failure {
        std::string reason;
        source::SourceLocation location;
    };

    [[nodiscard]] std::optional<vir::Expr> convert(const clangbridge::Expr& expr);

    [[nodiscard]] const std::optional<Failure>& failure() const noexcept {
        return failure_;
    }

  private:
    // A case split on a runtime path (SPEC.md CASE-017). The partition is asked
    // of the provider for the subject as elaborated, and the written arms are
    // matched to it by the same rules a proof-side split uses. Each arm's
    // continuation is elaborated with the arm's binders standing for the values
    // its case exposes, so a binder never reaches the formal core as anything
    // but that value.
    std::optional<vir::Expr> convert_split(const clangbridge::CaseSplit& split, const clangbridge::Expr& expr,
                                           vir::Expr result);

    std::uint32_t& next_id_;
    const ResolvedClaims* claims_;
    const ResolvedSplits* splits_;
    diagnostics::Engine* engine_;
    std::vector<SubjectStates>* states_;
    bool refused_ = false;
    // The value each binder of the arms being elaborated stands for, by the
    // split's marker, the arm's position and the binder's position.
    std::map<std::tuple<std::string, std::uint32_t, std::uint32_t>, vir::Expr> binders_;
    std::optional<Failure> failure_;
};

// Defined in elaborate_statements.cpp.
const clangbridge::Call* first_unmodeled_call(const clangbridge::Expr& expr,
                                              const std::function<bool(const std::string&)>& modeled);

// Generated helpers have distinct names. Repeated displayed locations must
// never make an ambiguous helper lookup pick the first declaration. Laws and
// executable declarations are linked separately by physical analysis offset.
inline const clangbridge::Function* find_projected(
    const clangbridge::TranslationUnit& unit, std::string_view name, const source::SourceLocation& declared_at,
    const std::vector<clangbridge::TemplateArgument>* arguments = nullptr) {
    const clangbridge::Function* found = nullptr;
    for (const clangbridge::Function& function : unit.functions) {
        if (function.name != name || function.location.file != declared_at.file) {
            continue;
        }
        // A specialization reports the line of the declaration it came from,
        // which is the template's own line rather than the clause's. Matching
        // by name and file is enough for one: generated probe names are unique
        // per clause, and the arguments below separate the specializations of
        // one probe from each other.
        if (function.template_arguments.empty() && function.location.line != declared_at.line) {
            continue;
        }
        // A probe declared under a template header is instantiated once per
        // specialization, so the one that states this specialization's contract
        // is the one instantiated at its arguments. Pairing them by anything
        // less would check `f<4>` against the proposition written for `f<5>`
        // (SPEC.md TEMPLATE-001, TEMPLATE-003).
        if (arguments != nullptr && function.template_arguments != *arguments) {
            continue;
        }
        if (found != nullptr)
            return nullptr;
        found = &function;
    }
    return found;
}

// Defined in elaborate_contracts.cpp.
std::optional<std::vector<vir::Parameter>> convert_parameters(const clangbridge::Function& function,
                                                              diagnostics::Engine& engine, std::string_view subject);

// Reads one projected expression back as VIR.
//
// The expression itself was resolved by Clang in the proof's own scope; what
// happens here is only the conversion of that resolved expression into the
// fragment C++L models.
inline const clangbridge::Function* proposition_function(
    const Request& request, std::string_view generated, const source::SourceLocation& written,
    const std::vector<clangbridge::TemplateArgument>* arguments = nullptr) {
    for (const auto& probe : request.projection.proposition_probes) {
        if (probe.owner == generated && probe.location.file == written.file && probe.location.line == written.line) {
            return find_projected(request.unit, probe.name, probe.location, arguments);
        }
    }
    return find_projected(request.unit, generated, written, arguments);
}

// Defined in elaborate_contracts.cpp.
bool conjoins_capabilities(const clangbridge::Function& function);

void report_conjoined_capabilities(diagnostics::Engine& engine, const source::SourceLocation& written,
                                   const std::string& subject);

inline std::optional<vir::Expr> convert_projected(const Request& request, std::string_view generated,
                                                  const source::SourceLocation& written,
                                                  std::uint32_t& next_expression_id, const std::string& subject,
                                                  diagnostics::Engine& engine,
                                                  const std::vector<clangbridge::TemplateArgument>* arguments = nullptr,
                                                  bool capabilities_read_apart = false) {
    const clangbridge::Function* function = proposition_function(request, generated, written, arguments);
    if (function != nullptr && !capabilities_read_apart && conjoins_capabilities(*function)) {
        report_conjoined_capabilities(engine, written, subject);
        return std::nullopt;
    }
    if (function != nullptr && !function->returned_value.has_value() && function->body_rejection.has_value()) {
        report(engine, diagnostics::Category::UnsupportedSemantics, written,
               subject + " is not modeled by this implementation: " + *function->body_rejection);
        return std::nullopt;
    }
    if (function == nullptr || !function->returned_value.has_value()) {
        report(engine, diagnostics::Category::Elaboration, written, subject + " was not resolved",
               "Clang did not resolve the projected expression");
        return std::nullopt;
    }

    ExpressionElaborator elaborator(next_expression_id);
    std::optional<vir::Expr> converted = elaborator.convert(*function->returned_value);
    if (!converted.has_value()) {
        const auto& failure = elaborator.failure();
        report(engine, diagnostics::Category::UnsupportedSemantics,
               failure.has_value() && failure->location.is_valid() ? failure->location : written,
               subject + " is not modeled by this implementation: " +
                   (failure.has_value() ? failure->reason : "it has no representation"));
        return std::nullopt;
    }
    converted->provenance.range.begin = written;
    return converted;
}

// Defined in elaborate_statements.cpp.
std::optional<std::vector<vir::ProofStep>> convert_statements(
    const Request& request, const frontend::ProofDeclaration& declaration, const frontend::ProofFunction& projected,
    const std::map<std::string, std::size_t>& declared,
    const std::map<std::string, std::vector<const vir::Law*>>& trusted_laws, const std::set<std::string>& memory_laws,
    const std::vector<vir::Parameter>& parameters, std::uint32_t& next_expression_id, diagnostics::Engine& engine,
    std::vector<SubjectStates>* subject_states, std::vector<ResolvedName>* names);

// Defined in elaborate_contracts.cpp.
void elaborate_contract(const Request& request, const frontend::VerifiedFunction& declaration,
                        const frontend::ContractFunctions& projected, const clangbridge::Function& function,
                        const std::string& body_rejection, bool rejection_reported, std::uint32_t& next_expression_id,
                        vir::Function& converted, diagnostics::Engine& engine);

void elaborate_memory_assumption(const Request& request, const frontend::SpecificationFunction& specification,
                                 const frontend::LawDeclaration& declaration, const clangbridge::Function& function,
                                 std::uint32_t& next_expression_id, Result& result, diagnostics::Engine& engine);

void elaborate_refinements(const Request& request, std::uint32_t& next_expression_id, Result& result,
                           diagnostics::Engine& engine);

void elaborate_proofs(const Request& request, const std::map<std::string, vir::LawId>& admitted_laws,
                      const std::map<std::string, std::string>& law_names, std::uint32_t& next_expression_id,
                      Result& result, diagnostics::Engine& engine);

} // namespace cppl::elaboration::detail::elaborator
