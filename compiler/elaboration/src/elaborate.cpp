#include "cppl/elaboration/elaborate.hpp"

#include "body_walks.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"
#include "elaborate_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::elaboration {

using detail::elaborator::conjoins_capabilities;
using detail::elaborator::convert_parameters;
using detail::elaborator::convert_projected;
using detail::elaborator::convert_type;
using detail::elaborator::elaborate_contract;
using detail::elaborator::elaborate_memory_assumption;
using detail::elaborator::elaborate_proofs;
using detail::elaborator::elaborate_refinements;
using detail::elaborator::ExpressionElaborator;
using detail::elaborator::find_projected;
using detail::elaborator::first_unmodeled_call;
using detail::elaborator::report;
using detail::elaborator::report_conjoined_capabilities;
using detail::elaborator::ResolvedClaim;
using detail::elaborator::ResolvedClaims;
using detail::elaborator::ResolvedSplits;

namespace {

using detail::collect_callees;
using detail::collect_models;
using detail::contains_unsafe_region;
using detail::note_model;

} // namespace

const FunctionRejection* Result::rejection(const vir::SymbolId& symbol) const {
    for (const FunctionRejection& rejected : rejected_functions) {
        if (rejected.symbol == symbol) {
            return &rejected;
        }
    }
    return nullptr;
}

Result elaborate(const Request& request, diagnostics::Engine& engine) {
    Result result;
    // A draft keeps what the compiler refuses, for an editor to say where the
    // author is. None of it has meaning, so one that reached elaboration is
    // refused whole rather than any of it read as C++L (ARCH-LSP-007).
    if (const std::optional<source::SourceLocation> unfinished = frontend::draft_only(request.syntax)) {
        report(engine, diagnostics::Category::Internal, *unfinished,
               "an editor's draft of the text reached elaboration",
               "a draft holds declarations not written whole and statements that could not be read; only a "
               "compile's recognition of the text is elaborated");
        return result;
    }
    std::uint32_t next_expression_id = 0;
    std::uint32_t next_function_id = 0;

    // Functions the author marked `pure`, and functions marked `verified`. A
    // function may be both, and is converted once either way. Purity is claimed
    // here and checked below; the marker alone establishes nothing
    // (SPEC.md 13.3).
    struct Candidate {
        const clangbridge::Function* function = nullptr;
        bool pure = false;
        const frontend::ContractFunctions* contract = nullptr;
        const frontend::VerifiedFunction* declaration = nullptr;
        // Every later `verified` declaration of the same function, each
        // restating the contract (SPEC.md TU-003).
        std::vector<std::pair<const frontend::ContractFunctions*, const frontend::VerifiedFunction*>> redeclarations =
            {};
    };

    std::set<std::string> pure_symbols;
    std::set<std::string> verified_symbols;
    std::vector<Candidate> candidates;

    const auto candidate_for = [&candidates](const clangbridge::Function* function) -> Candidate& {
        for (Candidate& existing : candidates) {
            if (existing.function->usr == function->usr) {
                return existing;
            }
        }
        candidates.push_back(Candidate{function, false, nullptr, nullptr});
        return candidates.back();
    };

    for (const frontend::PureMarker& marker : request.syntax.pure_markers) {
        const auto offset = request.projection.declaration_offset(marker.function_offset);
        const clangbridge::Function* function = offset.has_value() ? request.unit.find_at_offset(*offset) : nullptr;
        if (function == nullptr) {
            report(engine, diagnostics::Category::Elaboration, marker.function_location,
                   "the declaration of '" + marker.function_name + "' marked pure was not resolved",
                   "Clang did not report a function declaration at this location");
            continue;
        }
        pure_symbols.insert(function->usr);
        candidate_for(function).pure = true;
    }

    for (const frontend::ContractFunctions& projected : request.projection.contract_functions) {
        const frontend::VerifiedFunction& declaration = request.syntax.verified_functions[projected.function_index];
        const auto offset = request.projection.declaration_offset(declaration.function_offset);
        const clangbridge::Function* function = offset.has_value() ? request.unit.find_at_offset(*offset) : nullptr;
        // A contract on a template is parameterized by the template's own
        // parameters, and the claim it makes is interpreted per specialization
        // after substitution (SPEC.md 42 TEMPLATE-001, Annex G.1). Clang
        // performs that substitution; what is checked here is each
        // specialization it produced, with its own instantiated contract and
        // its own proof identity.
        if (declaration.template_header.length != 0 && offset.has_value()) {
            const std::vector<const clangbridge::Function*> specializations =
                request.unit.find_specializations_at_offset(*offset);
            if (specializations.empty()) {
                // A template nothing instantiated has no specialization to
                // check. There is no obligation, and equally nothing was
                // proven: it must not be counted as a verified function
                // (SPEC.md TEMPLATE-001).
                report(engine, diagnostics::Category::UnsupportedSemantics, declaration.function_location,
                       "verified function template '" + declaration.function_name +
                           "' is not instantiated in this translation unit",
                       "a contract on a template is checked for each specialization, so a template that is never "
                       "used states nothing this unit can discharge");
                continue;
            }
            bool restated = false;
            for (const clangbridge::Function* specialization : specializations) {
                Candidate& candidate = candidate_for(specialization);
                // A template's contract is instantiated from the one
                // declaration whose body names its probes, so a second
                // `verified` declaration of it has no instantiated contract to
                // compare, and is refused rather than one of them dropped.
                if (candidate.contract != nullptr && candidate.contract != &projected) {
                    restated = true;
                    continue;
                }
                candidate.contract = &projected;
                candidate.declaration = &declaration;
                verified_symbols.insert(specialization->usr);
            }
            if (restated) {
                report(engine, diagnostics::Category::UnsupportedSemantics, declaration.function_location,
                       "verified function template '" + declaration.function_name +
                           "' is declared verified more than once",
                       "a template's contract is read from the one declaration that defines it; state it there "
                       "only (SPEC.md TU-003)");
            }
            continue;
        }
        if (function == nullptr) {
            report(engine, diagnostics::Category::Elaboration, declaration.function_location,
                   "the declaration of verified function '" + declaration.function_name + "' was not resolved",
                   "Clang did not report a function declaration at this location");
            continue;
        }
        Candidate& candidate = candidate_for(function);
        // A header declaration and the definition may both state the contract
        // of one function, which then has the first as its contract and must
        // state the same in every other (SPEC.md TU-003).
        if (candidate.contract != nullptr) {
            candidate.redeclarations.emplace_back(&projected, &declaration);
            ++result.redeclarations;
        } else {
            candidate.contract = &projected;
            candidate.declaration = &declaration;
        }
        verified_symbols.insert(function->usr);
    }

    // What each claim that a path cannot occur names. A name no proof declares
    // is reported once, here, where it was written; the claim still reaches the
    // verifier, as one with no evidence, so it is never silently dropped.
    ResolvedClaims claims;
    for (const frontend::PathContradictionMarker& marker : request.projection.path_contradictions) {
        const frontend::PathContradiction& written = request.syntax.path_contradictions[marker.claim_index];
        ResolvedClaim resolved{std::nullopt, written.statement.reference, written.omitted};
        const auto declared = std::ranges::find_if(request.syntax.proofs, [&](const frontend::ProofDeclaration& proof) {
            return proof.name == written.statement.reference;
        });
        if (declared == request.syntax.proofs.end()) {
            report(engine, diagnostics::Category::Elaboration, written.statement.location,
                   "no proof named '" + written.statement.reference + "' is in scope here",
                   "a claim that a path cannot occur names a proof declaration as its evidence");
        } else {
            resolved.proof =
                vir::ProofId{static_cast<std::uint32_t>(std::distance(request.syntax.proofs.begin(), declared))};
            if (written.statement.reference_location.is_valid()) {
                result.names.push_back(ResolvedName{ResolvedName::Kind::Proof, written.statement.reference,
                                                    written.statement.reference_location, declared->name_location});
            }
        }
        claims.emplace(marker.name, std::move(resolved));
    }
    ResolvedSplits splits;
    for (const frontend::PathSplitMarker& marker : request.projection.path_splits) {
        if (const frontend::ProofStatement* statement = frontend::split_statement(request.syntax, marker)) {
            splits.emplace(marker.name, statement);
        }
    }

    // Functions declared `unsafe`, by the identity Clang gave them, so that every
    // redeclaration and every call is recognized whatever it is spelled as
    // (SPEC.md UNSAFE-001, UNSAFE-002).
    std::map<std::string, std::string> unsafe_symbols;
    for (std::size_t index = 0; index < request.syntax.unsafe_functions.size(); ++index) {
        const frontend::UnsafeFunction& declared = request.syntax.unsafe_functions[index];
        const auto offset = request.projection.declaration_offset(declared.function_offset);
        const clangbridge::Function* function = offset.has_value() ? request.unit.find_at_offset(*offset) : nullptr;
        if (function == nullptr) {
            report(engine, diagnostics::Category::Elaboration, declared.function_location,
                   "the declaration of '" + declared.function_name + "' marked unsafe was not resolved",
                   "Clang did not report a function declaration at this location");
            continue;
        }
        if (unsafe_symbols.emplace(function->usr, function->qualified_name).second) {
            result.unsafe_functions.push_back(index);
        }
    }

    for (const Candidate& candidate : candidates) {
        const clangbridge::Function* function = candidate.function;
        // An unsafe function is not verified and not pure: it marks a boundary
        // whose safety is not established, so it cannot also claim either.
        if (unsafe_symbols.contains(function->usr)) {
            report(engine, diagnostics::Category::CpplSyntax, function->location,
                   "'" + function->qualified_name + "' is declared unsafe, so it cannot also be " +
                       (candidate.contract != nullptr ? "verified" : "pure"),
                   "an unsafe declaration states that calls cross an unverified boundary (SPEC.md UNSAFE-002)");
            continue;
        }
        // A member function this implementation does not verify is refused by
        // name where it is declared, never verified without its implicit
        // object or with a contract its dispatch does not honour (SPEC.md
        // CLASS-014, CLASS-015).
        if (function->member_rejection.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   candidate.declaration != nullptr ? candidate.declaration->function_location : function->location,
                   "verified member function '" + function->qualified_name +
                       "' is not verified by this "
                       "implementation: " +
                       *function->member_rejection);
            continue;
        }
        vir::Function converted;
        converted.id = vir::FunctionId{next_function_id++};
        converted.symbol = vir::SymbolId{function->usr};
        converted.qualified_name = function->qualified_name;
        converted.range.begin = function->location;
        converted.external_linkage = function->external_linkage;

        const std::optional<vir::Type> result_type = convert_type(function->result);
        const std::optional<std::vector<vir::Parameter>> parameters =
            convert_parameters(*function, engine, "'" + function->qualified_name + "'");

        if (!result_type.has_value() || !parameters.has_value()) {
            if (!result_type.has_value()) {
                report(engine, diagnostics::Category::UnsupportedSemantics, function->location,
                       "'" + function->qualified_name + "' returns '" + function->result.spelling +
                           "', which is not modeled",
                       "this implementation models built-in integer and boolean types only");
            }
            continue;
        }

        converted.result = *result_type;
        converted.parameters = *parameters;

        std::string rejection;
        bool rejection_reported = false;
        if (!function->has_body) {
            rejection = "it is declared but not defined in this translation unit";
            // A verified declaration whose body another unit defines still
            // states a contract. A caller may rely on it only once a validated
            // verification interface establishes it, which the obligation layer
            // decides; the declaration alone establishes nothing (SPEC.md
            // TUBOUND-003, TU-004).
            // One with internal linkage has no definition anywhere else.
            converted.defined_elsewhere = candidate.contract != nullptr && function->external_linkage;
        } else if (function->body_rejection.has_value()) {
            rejection = *function->body_rejection;
        } else if (!function->returned_value.has_value()) {
            rejection = "its body produced no value expression";
        }
        // Each error in the body's ghost state stands where it was written,
        // since the body itself may be one the implementation models.
        for (const clangbridge::Function::GhostError& error : function->ghost_errors) {
            report(engine, diagnostics::Category::CpplSyntax, error.location, error.message, error.note);
            rejection_reported = true;
        }
        // A ghost initializer never runs, so what it calls must be a function
        // the formal core defines rather than one whose call is modeled by its
        // contract (SPEC.md GHOST-001).
        for (const clangbridge::Function::GhostCall& call : function->ghost_calls) {
            if (!pure_symbols.contains(call.callee_usr)) {
                report(engine, diagnostics::Category::CpplSyntax, call.location,
                       "the initializer of ghost '" + call.ghost + "' calls '" + call.callee + "', which is not pure",
                       "a ghost initializer never runs, so it may call only functions the formal core defines "
                       "(SPEC.md GHOST-001)");
                rejection = "it initializes ghost state by a call that would have to run";
                rejection_reported = true;
            }
        }

        if (rejection.empty()) {
            ExpressionElaborator elaborator(next_expression_id, candidate.contract != nullptr ? &claims : nullptr,
                                            candidate.contract != nullptr ? &splits : nullptr, &engine,
                                            &result.subject_states);
            std::optional<vir::Expr> body = elaborator.convert(*function->returned_value);
            if (!body.has_value()) {
                const auto& failure = elaborator.failure();
                rejection = failure.has_value() ? failure->reason : "its body is not modeled";
                rejection_reported = elaborator.refused();
            } else {
                // The value the body produces is what a contract is about, so
                // it is kept whatever the function's purity. Purity decides
                // something else: whether the formal core may unfold it.
                converted.returned_value = std::move(body);

                std::vector<vir::SymbolId> callees;
                collect_callees(*converted.returned_value, callees);
                const bool calls_only_pure = std::ranges::all_of(callees, [&pure_symbols](const vir::SymbolId& callee) {
                    return pure_symbols.contains(callee.usr);
                });
                const bool calls_modeled = std::ranges::all_of(callees, [&](const vir::SymbolId& callee) {
                    return pure_symbols.contains(callee.usr) ||
                           (candidate.contract != nullptr && verified_symbols.contains(callee.usr));
                });
                // A call inside an unsafe block is never lowered, so one found
                // here stands on a path the body verifies.
                const auto unsafe_call = std::ranges::find_if(
                    callees, [&](const vir::SymbolId& callee) { return unsafe_symbols.contains(callee.usr); });
                if (unsafe_call != callees.end()) {
                    rejection = "it calls unsafe function '" + unsafe_symbols.at(unsafe_call->usr) +
                                "' outside an unsafe block; a verified body crosses that boundary only inside one "
                                "(SPEC.md UNSAFE-002)";
                } else if (candidate.pure && contains_unsafe_region(*converted.returned_value)) {
                    rejection = "it holds an unsafe block, whose effects are not checked, so it cannot be "
                                "established pure (SPEC.md PURE-005, UNSAFE-002)";
                    report(engine, diagnostics::Category::UnsupportedSemantics, function->location,
                           "pure function '" + function->qualified_name +
                               "' holds an unsafe block, whose effects are not checked, so it cannot be established "
                               "pure (SPEC.md PURE-005, UNSAFE-002)");
                    rejection_reported = true;
                } else if (!calls_modeled) {
                    rejection = "it calls a function that is not declared pure, so its value is not "
                                "a mathematical function of its arguments";
                    if (const clangbridge::Call* unmodeled = first_unmodeled_call(
                            *function->returned_value,
                            [&](const std::string& usr) {
                                return pure_symbols.contains(usr) ||
                                       (candidate.contract != nullptr && verified_symbols.contains(usr));
                            });
                        unmodeled != nullptr) {
                        rejection += ": '" + unmodeled->callee_name + "'";
                        if (!unmodeled->default_argument.empty()) {
                            rejection += ", called by the default argument of " + unmodeled->default_argument +
                                         ", which a call in it relies on (SPEC.md R.16)";
                        }
                    }
                } else if (candidate.pure && calls_only_pure &&
                           !std::holds_alternative<vir::Conditional>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::PlaceVersion>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::Loop>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::ReturnState>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::PathContradiction>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::CaseSplit>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::UnsafeRegion>(converted.returned_value->node)) {
                    converted.purity = vir::Purity::Pure;
                } else if (candidate.pure && candidate.contract == nullptr) {
                    rejection = "pure specification helpers require a single return expression";
                }
            }
        }

        // A function the author marked pure and that did not turn out to be a
        // definition is recorded, so a law that reaches for it can say why.
        if (candidate.pure && converted.purity != vir::Purity::Pure) {
            // A verified function whose body is not one return expression is
            // still verified from its contract; it is only never unfolded.
            result.rejected_functions.push_back(FunctionRejection{
                converted.symbol, function->qualified_name,
                rejection.empty() ? "its body is not a single return expression, so the formal core does not "
                                    "unfold it; its contract is verified and may be called"
                                  : rejection,
                function->location});
        }

        if (candidate.contract != nullptr) {
            elaborate_contract(request, *candidate.declaration, *candidate.contract, *function, rejection,
                               rejection_reported, next_expression_id, converted, engine);
            // Each restatement is read the same way, from its own clauses, and
            // compared with the first by meaning where the contract is stated.
            // One that cannot be read leaves the function without a contract,
            // so nothing relies on a statement that was not checked.
            for (const auto& [projected, declaration] : candidate.redeclarations) {
                vir::Function restated;
                restated.qualified_name = converted.qualified_name;
                restated.defined_elsewhere = converted.defined_elsewhere;
                restated.returned_value = converted.returned_value;
                elaborate_contract(request, *declaration, *projected, *function, rejection, true, next_expression_id,
                                   restated, engine);
                if (!restated.contract.has_value()) {
                    converted.contract.reset();
                    break;
                }
                converted.redeclared_contracts.push_back(std::move(*restated.contract));
            }
        }

        // The standard-library models the function rests on: those its body's
        // lowering used, and those its signature and contract mention, since a
        // contract stating `v.size()` means what the model says `size()` is
        // (RFC 0020 §10).
        std::set<source::RepresentationKind> models(function->library_models.begin(), function->library_models.end());
        for (const vir::Parameter& parameter : converted.parameters) {
            note_model(parameter.type, models);
        }
        note_model(converted.result, models);
        if (converted.contract.has_value()) {
            for (const vir::Expr& precondition : converted.contract->preconditions) {
                collect_models(precondition, models);
            }
            collect_models(converted.contract->postcondition, models);
        }
        converted.library_models.assign(models.begin(), models.end());

        result.module.functions.push_back(std::move(converted));
    }

    // Laws, and the C++ identity Clang gave each of them. A proof names a law
    // through ordinary C++ lookup, so the two meet here by symbol, never by
    // spelling.
    std::map<std::string, vir::LawId> admitted_laws;
    std::map<std::string, std::string> law_names;

    for (const frontend::SpecificationFunction& specification : request.projection.specification_functions) {
        const frontend::LawDeclaration& declaration = request.syntax.laws[specification.law_index];

        const clangbridge::Function* function = request.unit.find_at_offset(specification.analysis_offset);
        if (function != nullptr) {
            law_names.emplace(function->usr, declaration.name);
        }
        const std::string law_usr = function != nullptr ? function->usr : std::string{};
        if (function != nullptr && !specification.proposition_probe.empty()) {
            function = find_projected(request.unit, specification.proposition_probe, declaration.keyword_location);
        }
        // A memory proposition is admitted only as an explicit assumption: no
        // proof establishes one, because it is not a proposition the kernel
        // checks (SPEC.md TRUSTED-003, VERIFIED-044, RFC 0014 §10). One
        // conjoined with a predicate is refused whole, never admitted in part.
        if (function != nullptr && conjoins_capabilities(*function)) {
            report_conjoined_capabilities(engine, declaration.keyword_location, "law '" + declaration.name + "'");
            continue;
        }
        if (function != nullptr && !function->capabilities.empty()) {
            elaborate_memory_assumption(request, specification, declaration, *function, next_expression_id, result,
                                        engine);
            continue;
        }
        if (function == nullptr || !function->returned_value.has_value()) {
            report(engine, diagnostics::Category::Elaboration, declaration.range.begin,
                   "the proposition of law '" + declaration.name + "' was not resolved",
                   function == nullptr ? "Clang did not resolve the projected specification function"
                                       : "the specification expression produced no value");
            continue;
        }

        // A Law states its conclusion under its precondition. More than one
        // precondition conjoins them (GRAMMAR.md 3); naming separate clauses
        // with assume is not implemented, so require one explicit proposition.
        const auto preconditions = std::ranges::count_if(declaration.clauses, [](const frontend::Clause& clause) {
            return clause.kind == frontend::ClauseKind::Expects;
        });
        if (preconditions > 1) {
            report(engine, diagnostics::Category::UnsupportedSemantics, declaration.premise()->location,
                   "law '" + declaration.name + "' has " + std::to_string(preconditions) + " expects clauses",
                   "this implementation accepts one expects clause; combine its predicates with &&");
            continue;
        }

        const std::optional<std::vector<vir::Parameter>> parameters =
            convert_parameters(*function, engine, "law '" + declaration.name + "'");
        if (!parameters.has_value()) {
            continue;
        }

        ExpressionElaborator elaborator(next_expression_id);
        std::optional<vir::Expr> proposition = elaborator.convert(*function->returned_value);
        if (!proposition.has_value()) {
            const auto& failure = elaborator.failure();
            source::SourceLocation location = declaration.range.begin;
            std::string reason = "its proposition is not modeled";
            if (failure.has_value()) {
                reason = failure->reason;
                if (failure->location.is_valid()) {
                    location = failure->location;
                }
            }
            report(engine, diagnostics::Category::UnsupportedSemantics, location,
                   "law '" + declaration.name + "' cannot be given formal meaning: " + reason);
            continue;
        }

        vir::Law law;
        if (const frontend::Clause* written = declaration.premise(); written != nullptr) {
            std::optional<vir::Expr> premise =
                convert_projected(request, specification.premise_name, written->location, next_expression_id,
                                  "the precondition of law '" + declaration.name + "'", engine);
            if (!premise.has_value()) {
                continue;
            }
            law.premise = std::move(*premise);
            law.premise_range.begin = written->location;
        }

        law.id = vir::LawId{static_cast<std::uint32_t>(result.module.laws.size())};
        law.name = declaration.name;
        law.parameters = *parameters;
        law.proposition = std::move(*proposition);
        law.range = declaration.range;
        law.trusted = declaration.trusted;
        law.proposition_range.begin = declaration.proposition()->location;
        admitted_laws.emplace(law_usr, law.id);
        result.module.laws.push_back(std::move(law));
    }

    elaborate_refinements(request, next_expression_id, result, engine);
    elaborate_proofs(request, admitted_laws, law_names, next_expression_id, result, engine);
    return result;
}

std::optional<vir::Type> resolved_type(const clangbridge::Type& type) {
    return convert_type(type);
}

} // namespace cppl::elaboration
