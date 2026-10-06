#include "cppl/clang/bridge.hpp"

#include "access.hpp"
#include "call_objects.hpp"
#include "collect.hpp"
#include "conversions.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"
#include "cppl/source/storage.hpp"
#include "formal.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "proof_instantiation.hpp"
#include "refinements.hpp"
#include "signature.hpp"
#include "types.hpp"
#include "unsafe.hpp"

#include <algorithm>
#include <clang-c/CXDiagnostic.h>
#include <clang-c/CXErrorCode.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppl::clangbridge {

// The bridge's own functions that places.hpp declares for the lowering of
// whole struct values, so it reads types, places and accesses as the body
// lowering does.
namespace detail::bridge {

Type convert_type(CXType type, unsigned depth, ReferenceModel references,
                  const std::vector<Selection::Refinement>* known) {
    return detail::convert_type(type, depth, references, known);
}
bool same_modeled_value(const Type& outer, const Type& inner) {
    return detail::same_modeled_value(outer, inner);
}
bool same_term(const Expr& lhs, const Expr& rhs) {
    return detail::same_term(lhs, rhs);
}
std::string qualified_name_of(CXCursor cursor) {
    return detail::qualified_name_of(cursor);
}
Expr unsupported_expression(CXCursor cursor, std::string reason) {
    return detail::unsupported_expression(cursor, std::move(reason));
}
source::ParameterPassing passing_of(CXType written) {
    return detail::passing_of(written);
}
CXCursor designated_object(CXCursor expression) {
    return detail::designated_object(expression);
}
std::optional<ResolvedAccess> resolve_access(CXCursor cursor) {
    return detail::resolve_access(cursor);
}
std::optional<std::size_t> find_binding(const Locals& locals, CXCursor declaration, const std::vector<PlaceStep>& path,
                                        const Expr* index_term) {
    return detail::find_binding(locals, declaration, path, index_term);
}
std::optional<std::string> stale_borrow(const Locals& locals, std::size_t binding) {
    return detail::stale_borrow(locals, binding);
}
Place place_of(const Locals& locals, std::size_t entry) {
    return detail::place_of(locals, entry);
}
Expr read_place(const Locals& locals, std::size_t entry, CXCursor at) {
    return detail::read_place(locals, entry, at);
}
Place anonymous_place(std::string spelling) {
    return BodyLowering::anonymous_place(std::move(spelling));
}

} // namespace detail::bridge

// What the bridge's entry points use of its internals.
namespace {
using detail::collect;
using detail::collect_specializations;
using detail::Collector;
using detail::convert_type;
using detail::extract_body;
using detail::extract_formal;
using detail::member_standing;
using detail::MemberStanding;
using detail::mixes_capabilities;
using detail::parameters_of;
using detail::passing_of;
using detail::physical_offset;
using detail::qualified_name_of;
using detail::ReceiverLeaf;
using detail::reference_value_type;
using detail::ReferenceModel;
using detail::refined_template_argument;
using detail::refined_template_argument_refusal;
using detail::refinement_use;
using detail::refinements_of;
using detail::Signature;
using detail::specialized_template;
using detail::StatedCapability;
using detail::template_arguments_of;
using detail::UnsafeEffects;
using detail::bridge::presumed_location;
using detail::bridge::take;

Severity convert_severity(CXDiagnosticSeverity severity) {
    switch (severity) {
        case CXDiagnostic_Ignored:
        case CXDiagnostic_Note:
            return Severity::Note;
        case CXDiagnostic_Warning:
            return Severity::Warning;
        case CXDiagnostic_Error:
            return Severity::Error;
        case CXDiagnostic_Fatal:
            return Severity::Fatal;
    }
    return Severity::Error;
}

} // namespace

const Function* TranslationUnit::find_by_usr(std::string_view usr) const {
    for (const Function& function : functions) {
        if (function.usr == usr) {
            return &function;
        }
    }
    return nullptr;
}

const Function* TranslationUnit::find_by_name(std::string_view name) const {
    for (const Function& function : functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

// The one function declared at `offset`, or nothing when two different
// functions share it: an ambiguous declaration is not resolved by guessing.
//
// Two records of the *same* function are not two functions. An explicit
// specialization is reached both as a declaration and as its definition, and
// both carry one USR, so identity decides this rather than record count.
const Function* TranslationUnit::find_at_offset(std::size_t offset) const {
    const Function* found = nullptr;
    for (const Function& function : functions) {
        if (function.analysis_offset == offset) {
            if (found != nullptr && found->usr != function.usr)
                return nullptr;
            // Prefer the record carrying a body: the contract is discharged
            // from the definition.
            if (found == nullptr || (function.has_body && !found->has_body))
                found = &function;
        }
    }
    return found;
}

std::vector<const Function*> TranslationUnit::find_specializations_at_offset(std::size_t offset) const {
    std::vector<const Function*> found;
    for (const Function& function : functions) {
        if (function.analysis_offset == offset && !function.primary_usr.empty()) {
            found.push_back(&function);
        }
    }
    return found;
}

// Every specialization of a verified function template the unit references, by
// USR with one cursor for it, as Clang's indexer reports references from every
// body it instantiated, implicit instantiations included: a destructor's, a
// conversion's, or a template's reached only through another template. The
// cursor search in `collect_specializations` sees only what an expression of
// the written program names, and no instantiated body (SPEC.md TEMPLATE-001).
// Nothing when the indexer fails, which the caller refuses.
std::optional<std::vector<std::pair<std::string, CXCursor>>> indexed_specializations(CXIndex index,
                                                                                     CXTranslationUnit unit,
                                                                                     const Selection& selection) {
    struct Found {
        const Selection* selection = nullptr;
        std::vector<std::pair<std::string, CXCursor>> specializations;
    } found{&selection, {}};
    IndexerCallbacks callbacks{};
    callbacks.indexEntityReference = [](CXClientData data, const CXIdxEntityRefInfo* reference) {
        if (reference == nullptr || reference->referencedEntity == nullptr) {
            return;
        }
        auto& into = *static_cast<Found*>(data);
        // The indexer names a specialization by its template; the reference
        // itself, in the instantiated body, resolves to the specialization.
        const CXCursor referenced = clang_getCursorReferenced(reference->cursor);
        if (clang_Cursor_isNull(referenced) != 0) {
            return;
        }
        const auto primary = specialized_template(referenced);
        if (!primary.has_value()) {
            return;
        }
        const std::string name = take(clang_getCursorSpelling(referenced));
        const bool generated =
            !into.selection->specification_prefix.empty() && name.starts_with(into.selection->specification_prefix);
        if (!generated &&
            std::ranges::find(into.selection->offsets, physical_offset(*primary)) == into.selection->offsets.end()) {
            return;
        }
        std::string usr = take(clang_getCursorUSR(referenced));
        if (std::ranges::none_of(into.specializations, [&](const auto& known) { return known.first == usr; })) {
            into.specializations.emplace_back(std::move(usr), referenced);
        }
    };
    CXIndexAction action = clang_IndexAction_create(index);
    if (action == nullptr) {
        return std::nullopt;
    }
    const int status = clang_indexTranslationUnit(action, &found, &callbacks, sizeof(callbacks),
                                                  CXIndexOpt_IndexImplicitTemplateInstantiations, unit);
    clang_IndexAction_dispose(action);
    if (status != 0) {
        return std::nullopt;
    }
    return std::move(found.specializations);
}

std::expected<TranslationUnit, std::string> parse(const ParseRequest& request) {
    CXIndex index = clang_createIndex(/*excludeDeclarationsFromPCH=*/0, /*displayDiagnostics=*/0);
    if (index == nullptr) {
        return std::unexpected("could not create a Clang index");
    }

    struct ReleaseIndex {
        CXIndex index;
        ~ReleaseIndex() {
            clang_disposeIndex(index);
        }
    } release_index{index};

    std::vector<const char*> argv;
    argv.reserve(request.arguments.size());
    for (const std::string& argument : request.arguments) {
        argv.push_back(argument.c_str());
    }

    CXTranslationUnit unit = nullptr;
    CXUnsavedFile unsaved{};
    if (request.content) {
        unsaved.Filename = request.path.c_str();
        unsaved.Contents = request.content->data();
        unsaved.Length = static_cast<unsigned long>(request.content->size());
    }
    const CXErrorCode error = clang_parseTranslationUnit2(
        index, request.path.c_str(), argv.data(), static_cast<int>(argv.size()), request.content ? &unsaved : nullptr,
        request.content ? 1u : 0u, CXTranslationUnit_None, &unit);
    if (error != CXError_Success || unit == nullptr) {
        return std::unexpected("Clang failed to parse '" + request.path + "'");
    }

    struct ReleaseUnit {
        CXTranslationUnit unit;
        ~ReleaseUnit() {
            clang_disposeTranslationUnit(unit);
        }
    } release_unit{unit};

    TranslationUnit result;

    // What every type below was resolved for. The caller compares it with the
    // target the program is compiled for (TRUST.md TCB-CLANG-006).
    if (CXTargetInfo target = clang_getTranslationUnitTargetInfo(unit); target != nullptr) {
        result.target = take(clang_TargetInfo_getTriple(target));
        clang_TargetInfo_dispose(target);
    }

    const unsigned diagnostic_count = clang_getNumDiagnostics(unit);
    for (unsigned index_of_diagnostic = 0; index_of_diagnostic < diagnostic_count; ++index_of_diagnostic) {
        CXDiagnostic diagnostic = clang_getDiagnostic(unit, index_of_diagnostic);
        Diagnostic converted;
        converted.severity = convert_severity(clang_getDiagnosticSeverity(diagnostic));
        converted.category = Category::CppSemantic;
        converted.message = take(clang_getDiagnosticSpelling(diagnostic));
        converted.location = presumed_location(clang_getDiagnosticLocation(diagnostic));
        clang_disposeDiagnostic(diagnostic);

        if (converted.severity == Severity::Error || converted.severity == Severity::Fatal) {
            result.has_errors = true;
        }
        result.diagnostics.push_back(std::move(converted));
    }

    // A rejected unit is never verified, and libclang's layout queries can
    // crash on the error types of its recovery expressions.
    if (result.has_errors && !request.recover_bindings && !request.recover_contract_types)
        return result;

    Collector collector;
    collector.selection = &request.selection;
    clang_visitChildren(clang_getTranslationUnitCursor(unit), collect, &collector);
    // A second pass for template specializations, which are reached from their
    // uses rather than from the declaration list (SPEC.md 42).
    clang_visitChildren(clang_getTranslationUnitCursor(unit), collect_specializations, &collector);
    // Every specialization of a verified function template the unit
    // instantiates is verified: one the search above did not reach, from an
    // implicit call it cannot see, is refused rather than left unverified
    // (SPEC.md TEMPLATE-001).
    const bool verified_templates = std::ranges::any_of(collector.functions, [&](CXCursor function) {
        return clang_getCursorKind(function) == CXCursor_FunctionTemplate &&
               std::ranges::find(request.selection.offsets, physical_offset(function)) !=
                   request.selection.offsets.end();
    });
    if (verified_templates) {
        const auto indexed = indexed_specializations(index, unit, request.selection);
        if (!indexed.has_value()) {
            // libclang's indexer failed on a unit Clang accepted: the bridge
            // could not run, which says nothing about the program.
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error, Category::Internal,
                 "the instantiations of this unit's verified function templates could not be enumerated, so which "
                 "of them are verified cannot be shown (SPEC.md TEMPLATE-001)",
                 presumed_location(clang_getCursorLocation(clang_getTranslationUnitCursor(unit)))});
        } else {
            // A specialization the cursor search did not reach, such as one a
            // class template's destructor uses, is verified as every other is,
            // with whatever its own body instantiates.
            for (const auto& [usr, specialization] : *indexed) {
                if (std::ranges::any_of(collector.specializations, [&, usr = std::string_view(usr)](CXCursor known) {
                        return take(clang_getCursorUSR(known)) == usr;
                    })) {
                    continue;
                }
                collector.specializations.push_back(specialization);
                clang_visitChildren(specialization, collect_specializations, &collector);
            }
        }
    }
    // In a verified declaration or body, every declaration and reference that
    // names a template outside the standard library at a refined argument
    // (SPEC.md STDMODEL-020).
    for (const CXCursor& function : collector.selected) {
        if (const auto refined = refined_template_argument(function, request.selection)) {
            collector.refused_arguments.emplace_back(refined_template_argument_refusal(*refined), function);
        }
        clang_visitChildren(
            function,
            [](CXCursor cursor, CXCursor, CXClientData data) {
                auto& found = *static_cast<Collector*>(data);
                const CXCursorKind kind = clang_getCursorKind(cursor);
                if (kind == CXCursor_VarDecl || kind == CXCursor_ParmDecl || kind == CXCursor_DeclRefExpr ||
                    kind == CXCursor_TypeAliasDecl || kind == CXCursor_TypedefDecl) {
                    if (const auto refined = refined_template_argument(cursor, *found.selection)) {
                        found.refused_arguments.emplace_back(refined_template_argument_refusal(*refined), cursor);
                    }
                }
                return CXChildVisit_Recurse;
            },
            &collector);
    }
    std::vector<std::pair<std::string, source::SourceLocation>> reported;
    for (const auto& [message, at] : collector.refused_arguments) {
        std::pair<std::string, source::SourceLocation> refusal{message, presumed_location(clang_getCursorLocation(at))};
        if (std::ranges::find(reported, refusal) != reported.end()) {
            continue;
        }
        reported.push_back(refusal);
        result.has_errors = true;
        // Well-formed C++ whose instantiation this implementation does not
        // model with the predicate (SPEC.md STDMODEL-020).
        result.diagnostics.push_back({Severity::Error, Category::UnsupportedSemantics, refusal.first, refusal.second});
    }
    // Nothing proof-only text names may be instantiated in the program verified
    // where the program run does not instantiate it (SPEC.md ERASE-019). A unit
    // Clang already refused is not asked: its recovery AST is never verified.
    if (!result.has_errors) {
        for (Diagnostic& refusal :
             detail::proof_only_instantiations(unit, request.selection, collector.specializations)) {
            result.has_errors = true;
            result.diagnostics.push_back(std::move(refusal));
        }
    }
    for (const CXCursor& specialization : collector.specializations) {
        collector.selected.push_back(specialization);
    }
    if (result.has_errors && request.recover_contract_types && !request.recover_bindings) {
        // Recover only canonical void return identities, never bodies, layout,
        // obligations or facts from an erroneous AST. The corrected projection
        // must pass a fresh Clang analysis before verification can proceed.
        for (const auto cursor : collector.selected) {
            const auto offset = physical_offset(cursor);
            if (std::ranges::find(request.selection.verified_offsets, offset) ==
                    request.selection.verified_offsets.end() ||
                clang_getCanonicalType(clang_getCursorResultType(cursor)).kind != CXType_Void)
                continue;
            Function function;
            function.analysis_offset = offset;
            function.result.kind = TypeKind::Void;
            result.functions.push_back(std::move(function));
        }
        return result;
    }

    // An erased return alias is not evidence. Check even ordinary declarations
    // that were not selected for body elaboration. A verified redeclaration may
    // establish the same callable only through Clang's declaration identity.
    for (const auto cursor : collector.functions) {
        if (request.selection.refinements.empty())
            break;
        const auto name = take(clang_getCursorSpelling(cursor));
        if (!request.selection.specification_prefix.empty() && name.starts_with(request.selection.specification_prefix))
            continue;
        const auto refined = refinement_use(cursor, request.selection);
        if (!refined)
            continue;
        const auto canonical = clang_getCanonicalCursor(cursor);
        // A template's specializations are the functions that get verified, and
        // each reports the primary's location rather than its own. The
        // declaration the author marked `verified` is therefore the primary, so
        // a specialization is matched through it (SPEC.md TEMPLATE-001).
        const auto declared_offset = [](CXCursor candidate) {
            const auto primary = specialized_template(candidate);
            return physical_offset(primary.value_or(candidate));
        };
        const bool verified =
            std::ranges::find(request.selection.verified_offsets, declared_offset(cursor)) !=
                request.selection.verified_offsets.end() ||
            std::ranges::any_of(collector.selected, [&](CXCursor candidate) {
                return clang_equalCursors(canonical, clang_getCanonicalCursor(candidate)) &&
                       std::ranges::find(request.selection.verified_offsets, declared_offset(candidate)) !=
                           request.selection.verified_offsets.end();
            });
        // Ordinary C++ that Clang accepted, refused because no trusted
        // refinement boundary is modeled: not a C++ error.
        if (!verified) {
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error, Category::UnsupportedSemantics,
                 "ordinary function '" + qualified_name_of(cursor) + "' return cannot establish refinement '" +
                     *refined + "'; verify its definition (explicit trusted refinement boundaries are not implemented)",
                 presumed_location(clang_getCursorLocation(cursor))});
        }
    }
    for (const auto declaration : collector.unverified_storage) {
        if (request.selection.refinements.empty())
            break;
        // Ordinary C++ that Clang accepted, refused because an unverified
        // construction boundary is not modeled: not a C++ error.
        if (const auto refined = refinement_use(declaration, request.selection)) {
            result.has_errors = true;
            result.diagnostics.push_back(
                {Severity::Error, Category::UnsupportedSemantics,
                 "storage '" + take(clang_getCursorSpelling(declaration)) + "' uses refinement '" + *refined +
                     "' outside a modeled verified body, where ordinary C++ could establish it without proof; a "
                     "verified body checks its own construction and writes, but an unverified construction boundary "
                     "is not yet checked",
                 presumed_location(clang_getCursorLocation(declaration))});
        }
    }

    // The memory capabilities each verified function's contract states, keyed by
    // the analysis offset of the declaration they belong to.
    //
    // They are collected before any body is lowered because a probe is an
    // ordinary function of this unit and may be parsed after the body it
    // constrains. A body may rely only on what its own contract states
    // (SPEC.md VERIFIED-043).
    // Keyed by the analysis offset of the verified function the clause belongs
    // to, so a body may rely only on its own contract.
    std::unordered_map<std::size_t, std::vector<StatedCapability>> stated_capabilities;
    UnsafeEffects unsafe_effects(request.selection.specification_prefix, request.selection.unsafe_symbols);
    for (const auto& probe : request.selection.proposition_probes) {
        if (probe.shape.kind != source::ProjectionKind::Readable &&
            probe.shape.kind != source::ProjectionKind::Writable &&
            probe.shape.kind != source::ProjectionKind::Capabilities && !mixes_capabilities(probe.shape)) {
            continue;
        }
        const auto at = std::ranges::find_if(collector.functions, [&](CXCursor candidate) {
            return take(clang_getCursorSpelling(candidate)) == probe.name;
        });
        if (at == collector.functions.end()) {
            continue;
        }
        Function resolved;
        // A member function's probe is a member too, so a pointer it names
        // stands past the implicit object's leaves (SPEC.md CLASS-008).
        MemberStanding standing = member_standing(*at, request.selection.refinements);
        extract_formal(resolved, *at,
                       Signature{parameters_of(*at), std::move(standing.receiver), true, request.selection.refinements},
                       probe.shape);
        if (resolved.capabilities.empty()) {
            continue;
        }
        // The probe's parameters mirror the verified function's, so the index
        // each capability resolved against is the function's own parameter.
        const auto owner = std::ranges::find_if(request.selection.clause_owners,
                                                [&](const auto& candidate) { return candidate.probe == probe.owner; });
        if (owner == request.selection.clause_owners.end()) {
            continue;
        }
        for (const Capability& capability : resolved.capabilities) {
            StatedCapability stated;
            stated.parameter = capability.pointer.root.id;
            stated.kind = capability.kind;
            stated.extent = capability.extent;
            stated_capabilities[owner->function_offset].push_back(stated);
        }
    }

    for (const CXCursor& cursor : collector.selected) {
        Function function;
        function.usr = take(clang_getCursorUSR(cursor));
        function.external_linkage = clang_getCursorLinkage(cursor) == CXLinkage_External;
        function.name = take(clang_getCursorSpelling(cursor));
        function.qualified_name = qualified_name_of(cursor);
        // A projected proof expression returns `decltype(auto)` over a
        // parenthesized expression, so Clang gives it a reference type whenever
        // the expression is a glvalue. That reference is an artifact of how the
        // expression is handed to Clang, not something the author wrote, and the
        // value denoted is the subject's own. An ordinary declaration's result
        // and parameters keep reference types opaque, so a contract is never
        // proven about a value another object can change (AGENTS.md 11).
        const bool projected_expression = !request.selection.specification_prefix.empty() &&
                                          function.name.starts_with(request.selection.specification_prefix);
        function.result = convert_type(clang_getCursorResultType(cursor), 0,
                                       projected_expression ? ReferenceModel::Referent : ReferenceModel::Opaque,
                                       &request.selection.refinements);
        function.location = presumed_location(clang_getCursorLocation(cursor));
        function.analysis_offset = physical_offset(cursor);
        // A specialization records the template it came from and the arguments
        // it was instantiated at. Its `usr` already differs per specialization,
        // so this identifies which declaration's contract it carries without
        // ever merging two of them (SPEC.md TEMPLATE-001, TEMPLATE-003).
        if (const auto primary = specialized_template(cursor); primary.has_value()) {
            function.primary_usr = take(clang_getCursorUSR(*primary));
            function.template_arguments = template_arguments_of(cursor);
            // An implicit instantiation carries the contract written on the
            // primary, so it is keyed to the primary's declaration: the
            // contract written once is found for every specialization of it.
            //
            // An explicit specialization states its own contract at its own
            // location, and the projector recorded that declaration rather
            // than the primary's. Re-keying it to the primary would hand it a
            // contract written for a different body and would collide with the
            // primary's own declaration (SPEC.md TEMPLATE-001, TEMPLATE-003).
            //
            // An implicit instantiation reports the primary's own location,
            // while an explicit specialization is written somewhere else and
            // reports that. Comparing the two is what separates them: the C
            // API exposes no specialization-kind predicate.
            const bool states_own_contract =
                physical_offset(cursor) != physical_offset(*primary) &&
                std::ranges::find(request.selection.verified_offsets, physical_offset(cursor)) !=
                    request.selection.verified_offsets.end();
            if (!states_own_contract) {
                function.analysis_offset = physical_offset(*primary);
                function.location = presumed_location(clang_getCursorLocation(*primary));
            }
        }

        // A refinement on a parameter or a result is verification-level identity
        // Clang canonicalizes away, so it is recovered from the written type here
        // (SPEC.md 17.3): a refined parameter carries its predicate into the body,
        // and a refined result states one at every return.
        const auto attach_refinements = [&](Type& type, CXCursor declaration, CXType written) {
            auto resolved = refinements_of(declaration, written, request.selection.refinements);
            if (resolved) {
                type.refinements = std::move(*resolved);
            } else {
                result.has_errors = true;
                result.diagnostics.push_back({Severity::Error, resolved.error().category, resolved.error().message,
                                              presumed_location(clang_getCursorLocation(declaration))});
            }
        };
        attach_refinements(function.result, cursor, clang_getCursorResultType(cursor));

        // A member function's implicit object: one reference parameter per
        // leaf, standing before the written ones (SPEC.md CLASS-008). One this
        // implementation does not verify is refused by name, and its body is
        // not lowered as though it were verified without its object.
        const bool executable = std::ranges::find(request.selection.verified_offsets, function.analysis_offset) !=
                                request.selection.verified_offsets.end();
        MemberStanding standing = member_standing(cursor, request.selection.refinements);
        if (standing.rejection.has_value()) {
            if (executable) {
                function.member_rejection = std::move(standing.rejection);
            } else {
                function.has_body = true;
                function.body_rejection = std::move(standing.rejection);
            }
            result.functions.push_back(std::move(function));
            continue;
        }
        if (standing.receiver.has_value()) {
            for (const ReceiverLeaf& leaf : standing.receiver->leaves) {
                function.parameters.push_back(Parameter{leaf.spelling, leaf.type, standing.receiver->passing(leaf)});
            }
        }

        const std::vector<CXCursor> parameter_cursors = parameters_of(cursor);
        for (const CXCursor& parameter : parameter_cursors) {
            // A proof binder is projected as a reference parameter so Clang
            // resolves it without requiring a copy, a move, a default
            // constructor or any runtime object. It denotes the subject's own
            // value, so the referent is what it means.
            const auto written = clang_getCursorType(parameter);
            const auto passing = passing_of(written);
            Type parameter_type = convert_type(written, 0, ReferenceModel::Referent, &request.selection.refinements);
            attach_refinements(parameter_type, parameter,
                               source::aliases_storage(passing) ? reference_value_type(written) : written);
            function.parameters.push_back(
                Parameter{take(clang_getCursorSpelling(parameter)), std::move(parameter_type), passing});
        }

        // The projector's invariant declarations share the generated prefix,
        // which no ordinary declaration may use.
        const auto probe = std::ranges::find_if(request.selection.proposition_probes,
                                                [&](const auto& selected) { return selected.name == function.name; });
        if (probe != request.selection.proposition_probes.end()) {
            // A templated probe is reached through the reference that forced its
            // instantiation, which names a declaration; the proposition it
            // states lives in the definition. Clang owns which declaration is
            // the definition, so it is asked rather than assumed.
            const CXCursor defined = clang_getCursorDefinition(cursor);
            const CXCursor stating = clang_Cursor_isNull(defined) != 0 ? cursor : defined;
            extract_formal(
                function, stating,
                Signature{parameters_of(stating), std::move(standing.receiver), true, request.selection.refinements},
                probe->shape);
        } else {
            // Clang owns declaration/definition identity, including overloads
            // and parameter renaming. The public declaration supplies contract
            // metadata; the resolved definition supplies the executable body.
            const CXCursor definition = clang_getCursorDefinition(cursor);
            const CXCursor body_cursor = clang_Cursor_isNull(definition) ? cursor : definition;
            const auto stated = stated_capabilities.find(function.analysis_offset);
            // A body a verified function states is lowered as a body; anything
            // else selected here is a clause or a definition the formal core
            // may use, which reads a member of the implicit object as the
            // parameter it is.
            extract_body(function, body_cursor,
                         Signature{parameters_of(body_cursor), std::move(standing.receiver), !executable,
                                   request.selection.refinements, executable ? &unsafe_effects : nullptr},
                         request.selection.specification_prefix.empty() ? std::string()
                                                                        : request.selection.specification_prefix,
                         request.selection.refinements, executable,
                         stated == stated_capabilities.end() ? nullptr : &stated->second, unsafe_effects);
        }
        result.functions.push_back(std::move(function));
    }

    return result;
}

std::string clang_version() {
    return take(clang_getClangVersion());
}

} // namespace cppl::clangbridge
