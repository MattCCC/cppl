// Elaborating contracts, memory assumptions, refinements and proofs from
// the functions the projection generated for them.

#include "cppl/clang/ast.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/capability.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "elaborate_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::elaboration {

using detail::elaborator::convert_place;
using detail::elaborator::ExpressionElaborator;
using detail::elaborator::proposition_function;
using detail::elaborator::report;

namespace detail::elaborator {

std::optional<std::vector<vir::Parameter>> convert_parameters(const clangbridge::Function& function,
                                                              diagnostics::Engine& engine, std::string_view subject) {
    std::vector<vir::Parameter> parameters;
    for (const clangbridge::Parameter& parameter : function.parameters) {
        const std::optional<vir::Type> type = convert_type(parameter.type);
        if (!type.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, function.location,
                   std::string(subject) + " has a parameter of type '" + parameter.type.spelling +
                       "', which is not modeled",
                   "this implementation models built-in integer and boolean types only");
            return std::nullopt;
        }
        parameters.push_back(vir::Parameter{parameter.name, *type, parameter.passing});
    }
    return parameters;
}

// Whether a projected clause conjoins memory capabilities with ordinary
// predicates. Only a verified function's `expects` clause reads such a clause
// apart (SPEC.md STDMODEL-016); anywhere else its predicates alone would be
// read and the capabilities silently lost, so every other reader refuses it.
bool conjoins_capabilities(const clangbridge::Function& function) {
    return !function.capabilities.empty() && function.returned_value.has_value();
}

void report_conjoined_capabilities(diagnostics::Engine& engine, const source::SourceLocation& written,
                                   const std::string& subject) {
    report(engine, diagnostics::Category::UnsupportedSemantics, written,
           subject + " conjoins a memory capability with a predicate, which only a verified function's expects "
                     "clause may do",
           "'readable' and 'writable' state storage permission; a precondition reads them on their own channel "
           "(SPEC.md STDMODEL-016), and nothing else can");
}

} // namespace detail::elaborator

namespace {

// The memory capabilities a projected clause states, when it states any.
//
// A capability leaves elaboration on its own channel and never becomes a
// `vir::Expr`, because it is not a proposition the kernel can check: it is a
// property of the execution state, supposed by the obligation layer as a
// context hypothesis (RFC 0014 §10, SPEC.md 12.10).
// Absent when a capability was stated and could not be read, which is reported
// here; empty when the clause states none.
std::optional<std::vector<vir::Capability>> convert_stated_capabilities(
    const clangbridge::Function& function, const source::SourceLocation& written, vir::CapabilityOrigin origin,
    const std::string& trusted_law, std::uint32_t& next_expression_id, const std::string& subject,
    diagnostics::Engine& engine) {
    std::vector<vir::Capability> converted_all;
    ExpressionElaborator elaborator(next_expression_id);
    for (const clangbridge::Capability& stated : function.capabilities) {
        vir::Capability capability;
        capability.kind = stated.kind == clangbridge::Capability::Kind::Readable ? vir::CapabilityKind::Readable
                                                                                 : vir::CapabilityKind::Writable;
        // A capability stated by a contract is owed by the caller; one a
        // trusted law states is admitted by it, and says so wherever it goes.
        capability.origin = origin;
        capability.trusted_law = trusted_law;
        capability.location = written;

        // The capability names the storage its pointer designates. The bridge
        // resolved which declaration that is, so the place is carried across
        // directly and the obligation layer can match a dereference against it.
        capability.place = convert_place(stated.pointer);

        for (const clangbridge::Expr& extent : stated.extent) {
            std::optional<vir::Expr> converted = elaborator.convert(extent);
            if (!converted.has_value()) {
                report(engine, diagnostics::Category::UnsupportedSemantics, written,
                       subject + " has an element count this implementation does not model");
                return std::nullopt;
            }
            capability.extent.push_back(std::move(*converted));
        }
        converted_all.push_back(std::move(capability));
    }
    return converted_all;
}

std::optional<std::vector<vir::Capability>> convert_capabilities(
    const Request& request, std::string_view generated, const source::SourceLocation& written,
    std::uint32_t& next_expression_id, const std::string& subject, diagnostics::Engine& engine,
    const std::vector<clangbridge::TemplateArgument>* arguments = nullptr) {
    const clangbridge::Function* function = proposition_function(request, generated, written, arguments);
    if (function == nullptr || function->capabilities.empty()) {
        return std::vector<vir::Capability>{};
    }
    return convert_stated_capabilities(*function, written, vir::CapabilityOrigin::Contract, {}, next_expression_id,
                                       subject, engine);
}

} // namespace

namespace detail::elaborator {

// Reads a verified function's contract back from the functions it was projected
// into.
//
// `result` is a parameter of the projected postcondition, in last position, so
// Clang resolves it as an ordinary name and the elaborated expression refers to
// it by position like any other parameter. Nothing named `result` exists in the
// program itself.
void elaborate_contract(const Request& request, const frontend::VerifiedFunction& declaration,
                        const frontend::ContractFunctions& projected, const clangbridge::Function& function,
                        const std::string& body_rejection, bool rejection_reported, std::uint32_t& next_expression_id,
                        vir::Function& converted, diagnostics::Engine& engine) {
    // A specialization of a template this unit declares without defining is
    // instantiated as a declaration only, so nothing instantiates the contract
    // at its arguments here, and no contract can be stated to compare with an
    // interface. An explicit specialization states its own (SPEC.md TUBOUND-004).
    if (converted.defined_elsewhere && !function.primary_usr.empty() && !declaration.explicit_specialization) {
        report(engine, diagnostics::Category::UnsupportedSemantics, declaration.function_location,
               "the contract of '" + function.qualified_name +
                   "' cannot be stated for this specialization: the template is declared but not defined in this "
                   "translation unit, so its contract is not instantiated here",
               "declare the specialization explicitly with its own contract, 'template <> verified ...;', so another "
               "unit's proof of it can be used here, or define the template where it is declared");
        return;
    }
    if (!converted.defined_elsewhere && (!body_rejection.empty() || !converted.returned_value.has_value())) {
        if (!rejection_reported) {
            report(engine, diagnostics::Category::UnsupportedSemantics, declaration.function_location,
                   "verified function '" + function.qualified_name +
                       "' has a body this implementation cannot state as a value" +
                       (body_rejection.empty() ? "" : ": " + body_rejection),
                   "a contract is discharged from the body, and this implementation models a body "
                   "using only modeled if/else, loops, blocks, and returned expressions");
        }
        return;
    }

    // Every case split written in this body, nested ones included, must have
    // been read on a path of it. One that was not stands in a lambda or a local
    // class, and dropping it would drop the obligations of its arms.
    for (const frontend::PathSplitMarker& marker : request.projection.path_splits) {
        if (marker.function_index == projected.function_index &&
            std::ranges::none_of(function.path_splits, [&marker](const clangbridge::Function::SplitSubject& read) {
                return read.marker == marker.name;
            })) {
            report(engine, diagnostics::Category::UnsupportedSemantics, marker.location,
                   "this case split is not on a runtime path of verified function '" + function.qualified_name + "'",
                   "a case split is checked as a statement of the function's own body, not inside a lambda or a "
                   "local class");
            return;
        }
    }

    // Every claim that a path cannot occur written in this body must have ended
    // a path of it. One that did not stands in a lambda or a local class, whose
    // body is not this function's, and dropping it would drop an obligation.
    for (const frontend::PathContradictionMarker& marker : request.projection.path_contradictions) {
        if (marker.function_index == projected.function_index &&
            std::ranges::find(function.path_contradictions, marker.name) == function.path_contradictions.end()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, marker.location,
                   "this claim that a path cannot occur is not on a runtime path of verified function '" +
                       function.qualified_name + "'",
                   "a contradiction is checked as a statement of the function's own body, not inside a lambda "
                   "or a local class");
            return;
        }
    }

    // Every unsafe block written in this body must have been passed through as
    // a region of one of its paths. One that was not stands in a lambda or a
    // local class, where it would weaken a claim nothing reports.
    for (const frontend::UnsafeBlockMarker& marker : request.projection.unsafe_blocks) {
        if (marker.function_index == std::optional<std::size_t>{projected.function_index} &&
            std::ranges::find(function.unsafe_regions, marker.name) == function.unsafe_regions.end()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, marker.location,
                   "this unsafe block is not on a runtime path of verified function '" + function.qualified_name + "'",
                   "an unsafe block in a verified body is modeled as a region of the function's own paths, not "
                   "inside a lambda or a local class");
            return;
        }
    }

    // Every invariant written in this body must have become an invariant of a
    // lowered loop. One that did not would be an obligation silently dropped.
    for (const frontend::LoopInvariantMarker& marker : request.projection.loop_invariants) {
        if (marker.function_index == projected.function_index &&
            std::ranges::find(function.loop_invariants, marker.name) == function.loop_invariants.end()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, marker.location,
                   "this loop invariant of verified function '" + function.qualified_name +
                       "' is not attached to a modeled loop",
                   "an invariant applies to the while or for loop whose body block follows it");
            return;
        }
    }

    // A `verified` function claims its contract is discharged, so there must be
    // something to discharge. A missing `ensures` read as a trivially true
    // postcondition would report a verified function that states nothing
    // (SPEC.md 12, VERIFIED-001 and VERIFIED-002).
    //
    // What counts as stating one depends on the result. A non-void function
    // establishes something about `result` (SPEC.md VERIFIED-009), so it states
    // `ensures` or returns a refinement type whose predicate it owes
    // (SPEC.md 17.2). A void function has no `result` (VERIFIED-010), so a
    // precondition it works under is a contract: its body may still owe
    // obligations, such as a write through a refined pointer.
    const frontend::Clause* postcondition = declaration.postcondition();
    if (postcondition == nullptr && function.result.refinements.empty() &&
        (function.result.kind != clangbridge::TypeKind::Void || declaration.preconditions().empty())) {
        const bool states_nothing = declaration.preconditions().empty();
        report(engine, diagnostics::Category::CpplSyntax, declaration.function_location,
               "verified function '" + function.qualified_name +
                   (states_nothing ? "' states no contract" : "' has no ensures clause"),
               "a verified function has exactly one ensures clause, or returns a refinement "
               "type whose predicate it owes");
        return;
    }
    const auto postcondition_location =
        postcondition != nullptr ? postcondition->location : declaration.function_location;
    // For a specialization, the contract to read back is the instantiation of
    // the probe at this specialization's own template arguments (SPEC.md
    // TEMPLATE-001). For an ordinary function there are none and the lookup is
    // unchanged.
    // An explicit specialization's probes are ordinary functions rather than
    // instantiations, because its arguments are already fixed and the contract
    // is written at them. It is matched like any other declaration.
    const std::vector<clangbridge::TemplateArgument>* arguments =
        function.primary_usr.empty() || declaration.explicit_specialization ? nullptr : &function.template_arguments;

    // A clause is read back from a probe restating the function's parameters,
    // without their default arguments (SPEC.md R.16, CONTRACTCOMP-002), and for
    // a postcondition of a function with a result `result` after them. A
    // position in the clause is the function's parameter at that position only
    // while the two lists agree, so a probe whose list does not is refused:
    // reading its clause could bind a parameter to another one, or `result` to
    // a parameter.
    const auto restates_parameters = [&](std::string_view probe_name, const source::SourceLocation& written,
                                         bool states_result, const std::string& subject) {
        const clangbridge::Function* probe = proposition_function(request, probe_name, written, arguments);
        if (probe == nullptr) {
            return true; // not resolved, which reading it reports
        }
        const std::size_t count = function.parameters.size();
        bool agrees = probe->parameters.size() ==
                      count + (states_result && function.result.kind != clangbridge::TypeKind::Void ? 1 : 0);
        // A member function's probe takes its implicit object's leaves first,
        // as the function does, read-only where the function may write them.
        // Types are compared by what they denote, never by how a declaration
        // restating the contract spells them.
        for (std::size_t index = 0; agrees && index < count; ++index) {
            const clangbridge::Type& stated = probe->parameters[index].type;
            const clangbridge::Type& declared = function.parameters[index].type;
            agrees = stated.kind == declared.kind && stated.width == declared.width &&
                     stated.is_signed == declared.is_signed && stated.representation == declared.representation;
        }
        if (!agrees) {
            report(engine, diagnostics::Category::Elaboration, written,
                   subject + " was not stated over the parameters of verified function '" + function.qualified_name +
                       "'",
                   "the declaration generated to state it does not restate the function's parameter list");
        }
        return agrees;
    };
    if (!restates_parameters(projected.postcondition_name, postcondition_location, true,
                             "the postcondition of verified function '" + function.qualified_name + "'")) {
        return;
    }
    const std::vector<const frontend::Clause*> written_preconditions = declaration.preconditions();
    for (std::size_t index = 0; index < projected.precondition_names.size() && index < written_preconditions.size();
         ++index) {
        if (!restates_parameters(projected.precondition_names[index], written_preconditions[index]->location, false,
                                 "the precondition of verified function '" + function.qualified_name + "'")) {
            return;
        }
    }

    std::optional<vir::Expr> ensured = convert_projected(
        request, projected.postcondition_name, postcondition_location, next_expression_id,
        "the postcondition of verified function '" + function.qualified_name + "'", engine, arguments);
    if (!ensured.has_value()) {
        return;
    }

    vir::Contract contract;
    contract.postcondition = std::move(*ensured);
    contract.range.begin = postcondition_location;

    const std::vector<const frontend::Clause*> preconditions = declaration.preconditions();
    if (preconditions.size() != projected.precondition_names.size()) {
        report(engine, diagnostics::Category::Elaboration, declaration.function_location,
               "the preconditions of verified function '" + function.qualified_name + "' were not all projected");
        return;
    }
    for (std::size_t index = 0; index < preconditions.size(); ++index) {
        const std::string subject = "the precondition of verified function '" + function.qualified_name + "'";
        // A memory capability is a precondition the caller owes, but it is not a
        // proposition: it leaves on the capability channel so it never reaches
        // the kernel (RFC 0014 §10).
        // Whether this clause's own capabilities failed to read is what decides
        // here. An error reported anywhere earlier in the unit says nothing
        // about this contract, and must not drop it unread.
        std::optional<std::vector<vir::Capability>> capabilities =
            convert_capabilities(request, projected.precondition_names[index], preconditions[index]->location,
                                 next_expression_id, subject, engine, arguments);
        if (!capabilities.has_value()) {
            return;
        }
        if (!capabilities->empty()) {
            contract.capabilities.insert(contract.capabilities.end(), std::make_move_iterator(capabilities->begin()),
                                         std::make_move_iterator(capabilities->end()));
            // A clause conjoining capabilities with ordinary predicates states
            // those predicates too, as a precondition like any other
            // (SPEC.md STDMODEL-016).
            const clangbridge::Function* stated = proposition_function(request, projected.precondition_names[index],
                                                                       preconditions[index]->location, arguments);
            if (stated == nullptr || !stated->returned_value.has_value()) {
                continue;
            }
        }
        std::optional<vir::Expr> expected =
            convert_projected(request, projected.precondition_names[index], preconditions[index]->location,
                              next_expression_id, subject, engine, arguments, true);
        if (!expected.has_value()) {
            return;
        }
        contract.preconditions.push_back(std::move(*expected));
    }

    // A `decreases` clause asks that the function terminate (SPEC.md
    // TERMINATION-004, TERMINATION-006): each component is read back as a term
    // over the parameters. A template's measure would have to be instantiated
    // at every specialization, which nothing forces yet, so it is refused rather
    // than dropped.
    if (const frontend::Clause* measure = declaration.measure(); measure != nullptr) {
        if (declaration.template_header.length != 0 && !declaration.explicit_specialization) {
            report(engine, diagnostics::Category::UnsupportedSemantics, measure->location,
                   "a 'decreases' clause on function template '" + function.qualified_name +
                       "' is not verified by this implementation",
                   "the requested termination obligation must not be accepted unchecked");
            return;
        }
        if (projected.measure_names.empty()) {
            report(engine, diagnostics::Category::Elaboration, measure->location,
                   "the measure of verified function '" + function.qualified_name + "' was not projected");
            return;
        }
        for (const std::string& name : projected.measure_names) {
            if (!restates_parameters(name, measure->location, false,
                                     "the measure of verified function '" + function.qualified_name + "'")) {
                return;
            }
            std::optional<vir::Expr> component =
                convert_projected(request, name, measure->location, next_expression_id,
                                  "the measure of verified function '" + function.qualified_name + "'", engine);
            if (!component.has_value()) {
                return;
            }
            contract.measures.push_back(std::move(*component));
        }
        contract.measure_range.begin = measure->location;
    }

    converted.contract = std::move(contract);
}

// A law whose conclusion is a memory proposition, `readable(p)` or
// `writable(p, n)` (SPEC.md TRUSTED-003, VERIFIED-044).
//
// Only a trusted law may state one. A capability is a property of the execution
// state, not a proposition the kernel checks (RFC 0014 §10), so no proof can
// establish it and an ordinary law stating one could never be proven. A trusted
// one is recorded as an explicit assumption on the capability channel, with the
// law's identity and location attached, and the trust report names it; nothing
// lowers it to a kernel proposition.
void elaborate_memory_assumption(const Request& request, const frontend::SpecificationFunction& specification,
                                 const frontend::LawDeclaration& declaration, const clangbridge::Function& function,
                                 std::uint32_t& next_expression_id, Result& result, diagnostics::Engine& engine) {
    const source::SourceLocation stated =
        declaration.proposition() != nullptr ? declaration.proposition()->location : declaration.range.begin;
    if (!declaration.trusted) {
        report(engine, diagnostics::Category::UnsupportedSemantics, stated,
               "law '" + declaration.name + "' states a memory proposition, which no proof can establish",
               "'readable' and 'writable' are not propositions the kernel checks; only a trusted law may admit one, as "
               "an explicit assumption (SPEC.md TRUSTED-003, VERIFIED-044)");
        return;
    }
    const std::string subject = "trusted law '" + declaration.name + "'";
    const std::optional<std::vector<vir::Parameter>> parameters = convert_parameters(function, engine, subject);
    if (!parameters.has_value()) {
        return;
    }
    vir::MemoryAssumption assumption;
    assumption.name = declaration.name;
    assumption.parameters = *parameters;
    assumption.range = declaration.range;
    if (const frontend::Clause* written = declaration.premise(); written != nullptr) {
        std::optional<vir::Expr> premise =
            convert_projected(request, specification.premise_name, written->location, next_expression_id,
                              "the precondition of " + subject, engine);
        if (!premise.has_value()) {
            return;
        }
        assumption.premise = std::move(*premise);
    }
    std::optional<std::vector<vir::Capability>> capabilities = convert_stated_capabilities(
        function, stated, vir::CapabilityOrigin::TrustedLaw, declaration.name, next_expression_id, subject, engine);
    if (!capabilities.has_value()) {
        return;
    }
    assumption.capabilities = std::move(*capabilities);
    result.module.memory_assumptions.push_back(std::move(assumption));
}

// Resolves the written proofs of a unit against the laws they claim to prove.
//
// Nothing here decides whether a proof holds. It decides only what the author
// wrote: which law is named, at which arguments, and which proof a step uses.
// A refinement type declaration: the base type and the predicate, as Clang
// resolved them (SPEC.md 17).
//
// The probe binds the declaration's indices and then `self`, in that order, so
// the base type is the last parameter's type and the indices are the ones before
// it. Nothing about the predicate is read from the source here: what it means is
// the expression Clang resolved in the probe's body.
void elaborate_refinements(const Request& request, std::uint32_t& next_expression_id, Result& result,
                           diagnostics::Engine& engine) {
    for (std::size_t index = 0; index < request.syntax.refinement_types.size(); ++index) {
        const frontend::RefinementType& declaration = request.syntax.refinement_types[index];
        const auto probe =
            std::ranges::find_if(request.projection.refinement_probes, [index](const frontend::RefinementProbe& entry) {
                return entry.refinement_index == index;
            });
        if (probe == request.projection.refinement_probes.end()) {
            continue;
        }

        const std::string subject = "refinement type '" + declaration.name + "'";
        const clangbridge::Function* function = find_projected(request.unit, probe->probe, probe->location);
        if (function != nullptr && conjoins_capabilities(*function)) {
            report_conjoined_capabilities(engine, declaration.predicate_location, "the predicate of " + subject);
            continue;
        }
        if (function == nullptr || !function->returned_value.has_value()) {
            report(engine, diagnostics::Category::Elaboration, declaration.predicate_location,
                   "the predicate of " + subject + " was not resolved",
                   function == nullptr ? "Clang did not resolve the projected predicate"
                                       : "the predicate produced no value");
            continue;
        }

        const std::optional<std::vector<vir::Parameter>> parameters = convert_parameters(*function, engine, subject);
        if (!parameters.has_value()) {
            continue;
        }
        if (parameters->empty()) {
            report(engine, diagnostics::Category::Elaboration, declaration.predicate_location,
                   "the predicate of " + subject + " states no value to refine");
            continue;
        }

        ExpressionElaborator elaborator(next_expression_id);
        std::optional<vir::Expr> predicate = elaborator.convert(*function->returned_value);
        if (!predicate.has_value()) {
            const auto& failure = elaborator.failure();
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   failure.has_value() && failure->location.is_valid() ? failure->location
                                                                       : declaration.predicate_location,
                   subject + " has a predicate this implementation does not model: " +
                       (failure.has_value() ? failure->reason : "it has no representation"));
            continue;
        }
        if (!predicate->type.is_boolean() && !predicate->type.is_proposition()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, declaration.predicate_location,
                   subject + " states a predicate of type '" + vir::describe(predicate->type) + "'",
                   "a refinement predicate states a proposition about 'self'");
            continue;
        }
        predicate->provenance.range.begin = declaration.predicate_location;

        vir::RefinementDeclaration refined;
        refined.name = declaration.name;
        refined.identity = probe->probe;
        refined.base = parameters->back().type;
        refined.indices.assign(parameters->begin(), parameters->end() - 1);
        refined.predicate = std::move(*predicate);
        refined.range = declaration.range;
        refined.predicate_range.begin = declaration.predicate_location;
        result.module.refinements.push_back(std::move(refined));
    }
}

void elaborate_proofs(const Request& request, const std::map<std::string, vir::LawId>& admitted_laws,
                      const std::map<std::string, std::string>& law_names, std::uint32_t& next_expression_id,
                      Result& result, diagnostics::Engine& engine) {
    // A proof is identified by its declaration position, so a step can name a
    // proof that is itself later found to be unsound: the reference resolves,
    // and the evidence still has to pass the kernel.
    std::map<std::string, std::size_t> declared;
    for (std::size_t index = 0; index < request.syntax.proofs.size(); ++index) {
        const frontend::ProofDeclaration& proof = request.syntax.proofs[index];
        if (!declared.emplace(proof.name, index).second) {
            report(engine, diagnostics::Category::CpplSyntax, proof.range.begin,
                   "proof '" + proof.name + "' is declared more than once",
                   "an earlier proof in this translation unit already has this name");
        }
    }
    // Only laws given formal meaning are here, so a trusted law whose
    // proposition could not be stated cannot be named as evidence either.
    std::map<std::string, std::vector<const vir::Law*>> trusted_laws;
    for (const vir::Law& law : result.module.laws) {
        if (law.trusted)
            trusted_laws[law.name].push_back(&law);
    }
    std::set<std::string> memory_laws;
    for (const vir::MemoryAssumption& assumption : result.module.memory_assumptions) {
        memory_laws.insert(assumption.name);
    }

    for (const frontend::ProofFunction& projected : request.projection.proof_functions) {
        const frontend::ProofDeclaration& declaration = request.syntax.proofs[projected.proof_index];

        const auto owner = declared.find(declaration.name);
        if (owner == declared.end() || owner->second != projected.proof_index) {
            continue; // a duplicate name, already reported
        }

        const clangbridge::Function* function =
            proposition_function(request, projected.name, declaration.keyword_location);
        if (function != nullptr && !function->capabilities.empty()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, declaration.proposition_location,
                   "proof '" + declaration.name + "' states a memory proposition, which no proof can establish",
                   "'readable' and 'writable' are not propositions the kernel checks; only a trusted law may admit "
                   "one, as an explicit assumption (SPEC.md TRUSTED-003, VERIFIED-044)");
            continue;
        }
        if (function == nullptr || !function->returned_value.has_value()) {
            report(engine, diagnostics::Category::Elaboration, declaration.range.begin,
                   "the proposition of proof '" + declaration.name + "' was not resolved",
                   function == nullptr ? "Clang did not resolve the projected proposition"
                                       : "the proposition produced no value");
            continue;
        }

        const std::optional<std::vector<vir::Parameter>> parameters =
            convert_parameters(*function, engine, "proof '" + declaration.name + "'");
        if (!parameters.has_value()) {
            continue;
        }

        ExpressionElaborator elaborator(next_expression_id);
        std::optional<vir::Expr> proposition = elaborator.convert(*function->returned_value);
        if (!proposition.has_value()) {
            const auto& failure = elaborator.failure();
            source::SourceLocation location = declaration.proposition_location;
            std::string reason = "its proposition is not modeled";
            if (failure.has_value()) {
                reason = failure->reason;
                if (failure->location.is_valid()) {
                    location = failure->location;
                }
            }
            report(engine, diagnostics::Category::UnsupportedSemantics, location,
                   "proof '" + declaration.name + "' cannot be given formal meaning: " + reason);
            continue;
        }

        std::optional<vir::LawId> law;
        if (declaration.inline_law) {
            const auto projected_law =
                std::ranges::find_if(request.projection.specification_functions, [&](const auto& candidate) {
                    return candidate.law_index == *declaration.inline_law;
                });
            const auto* resolved = projected_law == request.projection.specification_functions.end()
                                       ? nullptr
                                       : request.unit.find_at_offset(projected_law->analysis_offset);
            const auto admitted = resolved ? admitted_laws.find(resolved->usr) : admitted_laws.end();
            if (admitted == admitted_laws.end()) {
                report(engine, diagnostics::Category::Elaboration, declaration.range.begin,
                       "the Law of this explicit proof body was not given formal meaning");
                continue;
            }
            vir::Call claim{vir::SymbolId{resolved->usr}, resolved->qualified_name, {}};
            for (std::size_t index = 0; index < parameters->size(); ++index) {
                vir::Expr argument;
                argument.id = vir::ExprId{next_expression_id++};
                argument.type = (*parameters)[index].type;
                argument.node = vir::ParameterRef{static_cast<std::uint32_t>(index), (*parameters)[index].name};
                claim.arguments.push_back(std::move(argument));
            }
            proposition->node = std::move(claim);
        }
        if (const auto* claim = std::get_if<vir::Call>(&proposition->node)) {
            const auto admitted = admitted_laws.find(claim->callee.usr);
            if (admitted != admitted_laws.end()) {
                law = admitted->second;
            } else if (const auto known = law_names.find(claim->callee.usr); known != law_names.end()) {
                report(engine, diagnostics::Category::Elaboration, declaration.proposition_location,
                       "law '" + known->second + "' was not given formal meaning, so proof '" + declaration.name +
                           "' has no goal to discharge",
                       "the law itself was reported above");
                continue;
            }
        }

        // The claim's arguments need no check here. `proves (L(...))` is an
        // ordinary C++ call, so Clang has already settled their number and
        // their types; which proposition they state is worked out where the
        // law's own proposition is known, by instantiating it at them.
        std::optional<std::vector<vir::ProofStep>> steps =
            convert_statements(request, declaration, projected, declared, trusted_laws, memory_laws, *parameters,
                               next_expression_id, engine, &result.subject_states, &result.names);
        if (!steps.has_value()) {
            if (law)
                result.laws_with_refused_proofs.push_back(*law);
            continue;
        }

        vir::Proof proof;
        proof.id = vir::ProofId{static_cast<std::uint32_t>(projected.proof_index)};
        proof.name = declaration.name;
        proof.law = law;
        proof.parameters = *parameters;
        proof.proposition = std::move(*proposition);
        proof.steps = std::move(*steps);
        proof.range = declaration.range;
        result.module.proofs.push_back(std::move(proof));
    }
}

} // namespace detail::elaborator

} // namespace cppl::elaboration
