// Elaborating the statements of a verified body into VIR.

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
#include "cppl/vir/types.hpp"
#include "elaborate_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::elaboration {

namespace detail::elaborator {

// The first call a resolved body makes, wherever it stands, to a function
// `modeled` does not admit, for a diagnostic to name: the function, and the
// default argument it stands in when it stands in one a call relies on, since
// no call there shows it (SPEC.md R.16). A library summary and a validation's
// probe are no function of this unit, as in `collect_callees`.
const clangbridge::Call* first_unmodeled_call(const clangbridge::Expr& expr,
                                              const std::function<bool(const std::string&)>& modeled) {
    if (const auto* call = std::get_if<clangbridge::Call>(&expr.node);
        call != nullptr && !call->library.has_value() && !call->validation.has_value() && !modeled(call->callee_usr)) {
        return call;
    }
    const clangbridge::Call* found = nullptr;
    const auto search = [&](const std::vector<clangbridge::Expr>& children) {
        for (const clangbridge::Expr& child : children) {
            if (found == nullptr) {
                found = first_unmodeled_call(child, modeled);
            }
        }
    };
    std::visit(
        [&](const auto& node) {
            if constexpr (requires { node.operands; }) {
                search(node.operands);
            }
            if constexpr (requires { node.arguments; }) {
                search(node.arguments);
            }
            if constexpr (requires { node.extent; }) {
                search(node.extent);
            }
            if constexpr (requires { node.body; }) {
                search(node.body);
            }
        },
        expr.node);
    return found;
}

// Resolves a proof body into typed steps.
//
// A step's reference names a proof-level entity: a proof this translation unit
// declares, a trusted law it declares, or a premise an earlier `assume` in this
// same body bound. All are C++L bindings, resolved here and nowhere else. The
// terms a step is
// instantiated at, and the proposition an `assume` names, are ordinary C++ and
// are read back from the functions the projector emitted for them, so Clang
// alone decides what each one denotes.
std::optional<std::vector<vir::ProofStep>> convert_statements(
    const Request& request, const frontend::ProofDeclaration& declaration, const frontend::ProofFunction& projected,
    const std::map<std::string, std::size_t>& declared,
    const std::map<std::string, std::vector<const vir::Law*>>& trusted_laws, const std::set<std::string>& memory_laws,
    const std::vector<vir::Parameter>& parameters, std::uint32_t& next_expression_id, diagnostics::Engine& engine,
    std::vector<SubjectStates>* subject_states, std::vector<ResolvedName>* names) {
    const std::size_t parameter_count = parameters.size();
    std::vector<std::string> value_names;
    value_names.reserve(parameters.size());
    for (const auto& parameter : parameters)
        value_names.push_back(parameter.name);
    std::vector<std::string> assumed;
    // Where each name `assume` bound is written, beside it, for editors.
    std::vector<source::SourceLocation> assumed_at;
    const auto note_resolution = [names](ResolvedName::Kind kind, const frontend::ProofStatement& statement,
                                         const source::SourceLocation& declaration) {
        if (names != nullptr && statement.reference_location.is_valid()) {
            names->push_back(ResolvedName{kind, statement.reference, statement.reference_location, declaration});
        }
    };
    std::vector<std::vector<vir::Type>> assumed_types;
    const auto quantified_types = [](const vir::Expr& proposition) {
        std::vector<vir::Type> types;
        const vir::Expr* inner = &proposition;
        while (const auto* quantified = std::get_if<vir::Universal>(&inner->node)) {
            types.insert(types.end(), quantified->binders.begin(), quantified->binders.end());
            if (quantified->body.size() != 1)
                break;
            inner = &quantified->body[0];
        }
        return types;
    };
    std::size_t next_argument = 0;
    std::size_t next_assumption = 0;
    std::size_t next_case = 0;
    // A residual binder is an alias for the subject's underlying value. Probe
    // parameters give it C++ lookup/type checking; this map removes those
    // analysis-only parameters before formal lowering, including under binders.
    //
    // Aliases are kept as they were resolved, with every position counted as
    // though no induction had consumed a parameter; `relevel` below restates a
    // converted expression for the binders that stand where it is written, once,
    // after its aliases are in place.
    std::vector<vir::Expr> aliases;

    // The induction statements whose arms enclose the statement being converted,
    // outermost first (SPEC.md 21.4). An arm states its goal without the
    // subject's quantifier: in the zero arm it is gone and the parameters after
    // it move in by one, and in the successor arm the predecessor stands where
    // the subject stood. The subject itself is out of scope in both, so a
    // statement naming it is refused rather than read as zero, as the
    // predecessor, or as a parameter that has moved into its place.
    struct InductionFrame {
        std::uint32_t subject = 0;
        std::string name;
        bool zero = false;
        // The alias standing for the predecessor, in a successor arm.
        std::optional<std::size_t> predecessor;
    };
    std::vector<InductionFrame> inductions;
    std::optional<std::string> out_of_scope;
    const auto mentions = [](auto&& self, const vir::Expr& expression, std::uint32_t position) -> bool {
        if (const auto* parameter = std::get_if<vir::ParameterRef>(&expression.node))
            return parameter->parameter == position;
        return std::visit(
            [&](const auto& node) {
                if constexpr (requires { node.operands; }) {
                    return std::ranges::any_of(node.operands,
                                               [&](const vir::Expr& child) { return self(self, child, position); });
                } else if constexpr (requires { node.arguments; }) {
                    return std::ranges::any_of(node.arguments,
                                               [&](const vir::Expr& child) { return self(self, child, position); });
                } else if constexpr (requires { node.body; }) {
                    return std::ranges::any_of(node.body,
                                               [&](const vir::Expr& child) { return self(self, child, position); });
                } else {
                    return false;
                }
            },
            expression.node);
    };
    const auto remap = [&](auto&& self, vir::Expr& expression) -> void {
        if (auto* parameter = std::get_if<vir::ParameterRef>(&expression.node)) {
            if (parameter->parameter >= parameter_count) {
                const auto index = parameter->parameter - parameter_count;
                if (index < aliases.size()) {
                    // A binder's value that mentions an induction subject would
                    // carry the subject into an arm where it is out of scope; the
                    // predecessor is the one alias that stands for that position.
                    for (const InductionFrame& frame : inductions) {
                        if (frame.predecessor != index && mentions(mentions, aliases[index], frame.subject))
                            out_of_scope = frame.name;
                    }
                    expression = aliases[index];
                } else {
                    parameter->parameter -= static_cast<std::uint32_t>(aliases.size());
                }
            } else if (const auto frame = std::ranges::find(inductions, parameter->parameter, &InductionFrame::subject);
                       frame != inductions.end()) {
                out_of_scope = frame->name;
            }
            return;
        }
        std::visit(
            [&](auto& node) {
                if constexpr (requires { node.operands; }) {
                    for (auto& child : node.operands)
                        self(self, child);
                } else if constexpr (requires { node.arguments; }) {
                    for (auto& child : node.arguments)
                        self(self, child);
                } else if constexpr (requires { node.body; }) {
                    for (auto& child : node.body)
                        self(self, child);
                }
            },
            expression.node);
    };
    // Each zero arm enclosing the expression removed one quantifier, so every
    // position after its subject moves in by one. A reference to the subject of
    // a zero arm has no position left to denote.
    const auto relevel = [&](auto&& self, vir::Expr& expression) -> void {
        if (auto* parameter = std::get_if<vir::ParameterRef>(&expression.node)) {
            std::uint32_t consumed = 0;
            for (const InductionFrame& frame : inductions) {
                if (!frame.zero)
                    continue;
                if (parameter->parameter == frame.subject)
                    out_of_scope = frame.name;
                if (parameter->parameter > frame.subject)
                    ++consumed;
            }
            parameter->parameter -= consumed;
            return;
        }
        std::visit(
            [&](auto& node) {
                if constexpr (requires { node.operands; }) {
                    for (auto& child : node.operands)
                        self(self, child);
                } else if constexpr (requires { node.arguments; }) {
                    for (auto& child : node.arguments)
                        self(self, child);
                } else if constexpr (requires { node.body; }) {
                    for (auto& child : node.body)
                        self(self, child);
                }
            },
            expression.node);
    };
    // `restated` is false only for a case subject, whose resolved form the
    // provider decomposes into aliases; those stay as resolved, and the subject
    // is restated for the step separately.
    const auto convert_probe = [&](const std::vector<std::string>& names, std::size_t& next,
                                   const source::SourceLocation& location,
                                   bool restated = true) -> std::optional<vir::Expr> {
        if (next >= names.size())
            return std::nullopt;
        auto expression = convert_projected(request, names[next++], location, next_expression_id,
                                            "statement of proof '" + declaration.name + "'", engine);
        if (!expression)
            return expression;
        out_of_scope.reset();
        remap(remap, *expression);
        if (restated)
            relevel(relevel, *expression);
        if (out_of_scope.has_value()) {
            report(engine, diagnostics::Category::Elaboration, location,
                   "'" + *out_of_scope + "' is not in scope inside the arms of its own induction",
                   "the zero arm proves the claim at 0 and the successor arm at the predecessor plus one; name the "
                   "predecessor the successor arm binds (SPEC.md 21.4)");
            return std::nullopt;
        }
        return expression;
    };
    const auto restate = [&](vir::Expr expression) {
        out_of_scope.reset();
        relevel(relevel, expression);
        return expression;
    };
    const auto convert_steps =
        [&](auto&& self,
            const std::vector<frontend::ProofStatement>& statements) -> std::optional<std::vector<vir::ProofStep>> {
        std::vector<vir::ProofStep> steps;

        for (const frontend::ProofStatement& statement : statements) {
            vir::ProofStep step;
            step.location = statement.location;

            if (statement.kind == frontend::ProofStatementKind::Cases ||
                statement.kind == frontend::ProofStatementKind::Decompose) {
                auto subject = convert_probe(projected.case_names, next_case, statement.location, false);
                if (!subject)
                    return std::nullopt;
                const vir::Expr stated_subject = restate(*subject);
                if (out_of_scope.has_value()) {
                    report(engine, diagnostics::Category::Elaboration, statement.location,
                           "'" + *out_of_scope + "' is not in scope inside the arms of its own induction",
                           "name the predecessor the successor arm binds (SPEC.md 21.4)");
                    return std::nullopt;
                }

                // What the states are is the representation's business, not the
                // engine's. A representation no provider models fails here, at
                // the provider boundary, naming the resolved C++ type.
                const decomposition::Subject described{*subject, statement.location};
                const decomposition::Decomposition decomposed = decomposition::decompose(described);
                if (const auto* unsupported = std::get_if<decomposition::Unsupported>(&decomposed)) {
                    report(engine, diagnostics::Category::UnsupportedSemantics, statement.location,
                           "proof decomposition is not defined for '" + unsupported->representation + "'",
                           unsupported->reason);
                    return std::nullopt;
                }

                record_states(statement, subject->type, decomposed, subject_states);
                if (const auto* product = std::get_if<decomposition::ProductDecomposition>(&decomposed)) {
                    if (!product_arm(statement, *product, engine)) {
                        return std::nullopt;
                    }
                    const auto& arm = statement.arms[0];
                    const auto outer_aliases = aliases.size();
                    const auto outer_assumed = assumed.size();
                    for (std::size_t i = 0; i < arm.binders.size(); ++i) {
                        if (std::ranges::find(value_names, arm.binders[i]) != value_names.end()) {
                            report(engine, diagnostics::Category::Elaboration, arm.location,
                                   "component binder duplicates an enclosing name");
                            return std::nullopt;
                        }
                        aliases.push_back(product->fields[i].value);
                        value_names.push_back(arm.binders[i]);
                    }
                    auto nested = self(self, arm.statements);
                    aliases.resize(outer_aliases);
                    value_names.resize(parameter_count + outer_aliases);
                    assumed.resize(outer_assumed);
                    assumed_types.resize(outer_assumed);
                    assumed_at.resize(outer_assumed);
                    if (!nested)
                        return std::nullopt;
                    step.node = vir::ProductStep{stated_subject, std::move(*nested)};
                    steps.push_back(std::move(step));
                    continue;
                }
                if (statement.kind == frontend::ProofStatementKind::Decompose) {
                    report(engine, diagnostics::Category::Elaboration, statement.location,
                           "decompose requires a product; use cases for alternative states");
                    return std::nullopt;
                }
                const auto& sum = std::get<decomposition::SumDecomposition>(decomposed);
                const decomposition::Provider& provider = *decomposition::provider_for(subject->type);

                vir::CasesStep cases{stated_subject, {}};
                Coverage coverage{std::vector<bool>(sum.cases.size(), false), false};

                for (const auto& arm : statement.arms) {
                    vir::CaseArm converted;
                    converted.location = arm.location;
                    converted.omitted = arm.omitted;

                    std::optional<vir::Expr> label;
                    if (!arm.keyword_label) {
                        label = convert_probe(projected.case_names, next_case, arm.location);
                        if (!label)
                            return std::nullopt;
                    }
                    const std::optional<MatchedArm> matched =
                        match_arm(arm, label ? &*label : nullptr, sum, provider, subject->type, coverage, engine);
                    if (!matched)
                        return std::nullopt;
                    converted.descriptor = matched->descriptor;
                    converted.label = matched->label;
                    const std::vector<decomposition::ProofBinding>* bindings = matched->bindings;

                    // An omitted case has no body, so it binds nothing and
                    // introduces no binder scope. Its contradiction statement
                    // still elaborates here, under the enclosing scope, and the
                    // obligation layer checks it under the case's own
                    // discriminator premise (CASE-013).
                    if (arm.omitted) {
                        auto discharge = self(self, arm.statements);
                        if (!discharge)
                            return std::nullopt;
                        converted.steps = std::move(*discharge);
                        cases.arms.push_back(std::move(converted));
                        continue;
                    }

                    const auto enclosing_assumed = assumed.size();
                    const auto enclosing_aliases = aliases.size();
                    for (std::size_t index = 0; index < arm.binders.size(); ++index) {
                        if (std::ranges::find(value_names, arm.binders[index]) != value_names.end()) {
                            report(engine, diagnostics::Category::Elaboration, arm.location,
                                   "case binder '" + arm.binders[index] + "' duplicates an enclosing value name");
                            return std::nullopt;
                        }
                        auto value = (*bindings)[index].value;
                        value.type = (*bindings)[index].type;
                        aliases.push_back(std::move(value));
                        value_names.push_back(arm.binders[index]);
                    }

                    auto nested = self(self, arm.statements);
                    aliases.resize(enclosing_aliases);
                    value_names.resize(parameter_count + enclosing_aliases);
                    assumed.resize(enclosing_assumed);
                    assumed_types.resize(enclosing_assumed);
                    assumed_at.resize(enclosing_assumed);
                    if (!nested)
                        return std::nullopt;
                    converted.steps = std::move(*nested);
                    cases.arms.push_back(std::move(converted));
                }

                if (!exhaustive(sum, coverage, subject->type, statement.location, engine)) {
                    return std::nullopt;
                }
                step.node = std::move(cases);
                steps.push_back(std::move(step));
                continue;
            }

            if (statement.kind == frontend::ProofStatementKind::Reflexivity) {
                step.node = vir::ReflexivityStep{};
                steps.push_back(std::move(step));
                continue;
            }

            // `induction x { zero => ... successor(pred) => ... }` or
            // `induction x;` (GRAMMAR.md 5.8, SPEC.md 21). The subject is a
            // parameter of the proof, which is a quantifier the claim states;
            // Clang resolves the name, and which parameter it resolved to is
            // what is recorded. Its type must have a principle: the unsigned
            // machine integers are the only domain this implementation gives
            // one (INDUCT-002, INDUCT-004).
            if (statement.kind == frontend::ProofStatementKind::Induction) {
                if (next_case >= projected.case_names.size()) {
                    return std::nullopt;
                }
                const std::optional<vir::Expr> resolved = convert_projected(
                    request, projected.case_names[next_case++], statement.location, next_expression_id,
                    "the subject of induction in proof '" + declaration.name + "'", engine);
                if (!resolved) {
                    return std::nullopt;
                }
                const auto* named = std::get_if<vir::ParameterRef>(&resolved->node);
                if (named == nullptr || named->parameter >= parameter_count) {
                    report(engine, diagnostics::Category::Elaboration, statement.location,
                           "induction is over a parameter of proof '" + declaration.name + "', and '" +
                               statement.reference + "' is not one",
                           "an induction principle applies to a quantifier the claim states; an arm binder, a "
                           "quantifier binder or any other expression has none (SPEC.md INDUCT-004)");
                    return std::nullopt;
                }
                const std::uint32_t position = named->parameter;
                if (std::ranges::find(inductions, position, &InductionFrame::subject) != inductions.end()) {
                    report(engine, diagnostics::Category::Elaboration, statement.location,
                           "'" + statement.reference + "' is not in scope inside the arms of its own induction",
                           "name the predecessor the successor arm binds (SPEC.md 21.4)");
                    return std::nullopt;
                }
                const vir::Type& type = parameters[position].type;
                if (!type.is_integer() || type.integer_type().is_signed || type.representation.is_known() ||
                    type.is_refined()) {
                    report(engine, diagnostics::Category::ProofFailure, statement.location,
                           "induction over '" + statement.reference + "' has no principle: its type '" +
                               vir::spelled(type) + "' is not an unsigned integer type",
                           "C++L defines induction over the unsigned machine integers, from zero by successor below "
                           "the type's maximum; signed integers, enumerations, refinements, pointers and classes "
                           "acquire no principle from their type (SPEC.md INDUCT-003, INDUCT-004)");
                    return std::nullopt;
                }

                // What the principle's cases are, for editors, recorded as the
                // case engine records a partition: the language server offers
                // these labels and never derives them itself.
                if (subject_states != nullptr && std::ranges::none_of(*subject_states, [&](const SubjectStates& entry) {
                        return entry.location == statement.location;
                    })) {
                    SubjectStates record;
                    record.location = statement.location;
                    record.subject = statement.reference;
                    record.representation = vir::spelled(type);
                    record.provider = "the unsigned induction principle";
                    record.induction = true;
                    record.states.push_back({"zero", {}, false});
                    record.states.push_back({"successor", {"pred"}, false});
                    subject_states->push_back(std::move(record));
                }

                vir::InductionStep induction;
                induction.subject = statement.reference;
                induction.type = type;
                induction.automatic = statement.arms.empty();
                induction.level = position;
                for (const InductionFrame& frame : inductions) {
                    if (frame.zero && frame.subject < position)
                        --induction.level;
                }

                bool has_zero = false;
                bool has_successor = false;
                for (const frontend::ProofArm& arm : statement.arms) {
                    const bool zero = arm.spelling == "zero";
                    if (!zero && arm.spelling != "successor") {
                        report(engine, diagnostics::Category::ProofFailure, arm.location,
                               "induction over an unsigned integer has the cases 'zero' and 'successor(pred)', and '" +
                                   arm.spelling + "' is not one",
                               "there is no wildcard arm (SPEC.md INDUCT-003)");
                        return std::nullopt;
                    }
                    if (zero ? has_zero : has_successor) {
                        report(engine, diagnostics::Category::ProofFailure, arm.location,
                               "duplicate case '" + arm.spelling + "'");
                        return std::nullopt;
                    }
                    (zero ? has_zero : has_successor) = true;
                    if (arm.binders.size() != (zero ? 0u : 1u)) {
                        report(engine, diagnostics::Category::ProofFailure, arm.location,
                               zero ? "'zero' binds nothing: the case is the value 0"
                                    : "'successor' binds exactly one name, the predecessor",
                               "an induction hypothesis and a range premise are premises, named with 'assume' "
                               "(SPEC.md 21.4)");
                        return std::nullopt;
                    }
                    if (!zero && std::ranges::find(value_names, arm.binders[0]) != value_names.end()) {
                        report(engine, diagnostics::Category::Elaboration, arm.location,
                               "induction binder '" + arm.binders[0] + "' duplicates an enclosing value name");
                        return std::nullopt;
                    }

                    const auto enclosing_assumed = assumed.size();
                    const auto enclosing_aliases = aliases.size();
                    InductionFrame frame{position, statement.reference, zero, std::nullopt};
                    if (!zero) {
                        // The predecessor stands at the subject's own position.
                        vir::Expr predecessor;
                        predecessor.type = type;
                        predecessor.provenance.range.begin = arm.location;
                        predecessor.node = vir::ParameterRef{position, arm.binders[0]};
                        frame.predecessor = aliases.size();
                        aliases.push_back(std::move(predecessor));
                        value_names.push_back(arm.binders[0]);
                    }
                    inductions.push_back(std::move(frame));
                    auto nested = self(self, arm.statements);
                    inductions.pop_back();
                    aliases.resize(enclosing_aliases);
                    value_names.resize(parameter_count + enclosing_aliases);
                    assumed.resize(enclosing_assumed);
                    assumed_types.resize(enclosing_assumed);
                    assumed_at.resize(enclosing_assumed);
                    if (!nested)
                        return std::nullopt;
                    (zero ? induction.zero : induction.successor) = std::move(*nested);
                    (zero ? induction.zero_location : induction.successor_location) = arm.location;
                }
                if (!induction.automatic && (!has_zero || !has_successor)) {
                    report(engine, diagnostics::Category::ProofFailure, statement.location,
                           std::string("non-exhaustive induction: '") + (has_zero ? "successor" : "zero") +
                               "' has no arm",
                           "every case must prove the goal; write both arms, or 'induction " + statement.reference +
                               ";' to leave both to automation (SPEC.md INDUCT-005)");
                    return std::nullopt;
                }
                step.node = std::move(induction);
                steps.push_back(std::move(step));
                continue;
            }

            if (statement.kind == frontend::ProofStatementKind::Assume) {
                auto proposition =
                    convert_probe(projected.assumption_names, next_assumption, statement.proposition_location);
                if (!proposition.has_value()) {
                    return std::nullopt;
                }
                assumed_types.push_back(quantified_types(*proposition));
                step.node = vir::AssumeStep{statement.reference, std::move(*proposition)};
                assumed.push_back(statement.reference);
                assumed_at.push_back(statement.reference_location);
                steps.push_back(std::move(step));
                continue;
            }

            // A premise bound in this body is the more local binding, so it is
            // looked for first, and the innermost one of its name wins.
            std::optional<vir::Reference> evidence;
            std::vector<vir::Type> expected_arguments;
            for (std::size_t position = assumed.size(); position > 0; --position) {
                if (assumed[position - 1] == statement.reference) {
                    expected_arguments = assumed_types[position - 1];
                    evidence = vir::Reference{vir::HypothesisRef{static_cast<std::uint32_t>(position - 1)},
                                              statement.reference};
                    note_resolution(ResolvedName::Kind::Assumption, statement, assumed_at[position - 1]);
                    break;
                }
            }

            // A trusted law is named like a proof but has no evidence: naming it
            // makes this proof relative to it (SPEC.md TRUSTED-006). A name that
            // could mean more than one thing is refused, never resolved by
            // preference, so which assumption a proof rests on is never a guess
            // (TRUSTED-009).
            const auto trusted = trusted_laws.find(statement.reference);
            if (!evidence.has_value() && trusted != trusted_laws.end()) {
                if (declared.contains(statement.reference) || trusted->second.size() != 1) {
                    report(engine, diagnostics::Category::Elaboration, statement.location,
                           "'" + statement.reference + "' names more than one proof or trusted law",
                           "rename one of them, so that the evidence a statement names is never chosen by "
                           "preference");
                    return std::nullopt;
                }
                const vir::Law& law = *trusted->second.front();
                evidence = vir::Reference{vir::TrustedLawRef{law.id}, statement.reference};
                const auto written =
                    std::ranges::find_if(request.syntax.laws, [&law](const frontend::LawDeclaration& candidate) {
                        return candidate.range == law.range;
                    });
                if (written != request.syntax.laws.end()) {
                    note_resolution(ResolvedName::Kind::TrustedLaw, statement, written->name_location);
                }
                for (const auto& parameter : law.parameters)
                    expected_arguments.push_back(parameter.type);
                if (!law.premise.has_value()) {
                    auto quantified = quantified_types(law.proposition);
                    expected_arguments.insert(expected_arguments.end(), quantified.begin(), quantified.end());
                }
            }

            if (!evidence.has_value()) {
                const auto target = declared.find(statement.reference);
                // A trusted law admitting a memory proposition is an explicit
                // assumption, but a capability is not a proposition any proof
                // goal can be, so no statement can use it (SPEC.md TRUSTED-003,
                // TRUSTED-008, RFC 0014 §10).
                if (target == declared.end() && memory_laws.contains(statement.reference)) {
                    report(engine, diagnostics::Category::Elaboration, statement.location,
                           "trusted law '" + statement.reference +
                               "' admits a memory proposition, which no proof statement can use",
                           "'readable' and 'writable' are not propositions the kernel checks, so no goal is one and "
                           "no premise can be supposed for one");
                    return std::nullopt;
                }
                if (target == declared.end()) {
                    report(engine, diagnostics::Category::Elaboration, statement.location,
                           "no proof or assumed premise named '" + statement.reference + "' is in scope here",
                           "'" + describe(statement.kind) +
                               "' names a proof declaration, a trusted law or a name bound by 'assume'");
                    return std::nullopt;
                }
                if (statement.reference == declaration.name) {
                    report(engine, diagnostics::Category::ProofFailure, statement.location,
                           "proof '" + declaration.name + "' uses itself as its own evidence",
                           "a proof never obtains an induction hypothesis by naming itself; 'induction' supplies "
                           "one from its principle (SPEC.md 21.4)");
                    return std::nullopt;
                }
                evidence = vir::Reference{vir::ProofRef{vir::ProofId{static_cast<std::uint32_t>(target->second)}},
                                          statement.reference};
                note_resolution(ResolvedName::Kind::Proof, statement,
                                request.syntax.proofs[target->second].name_location);
                const auto projected_target =
                    std::ranges::find_if(request.projection.proof_functions, [&](const auto& candidate) {
                        return candidate.proof_index == target->second;
                    });
                if (projected_target != request.projection.proof_functions.end()) {
                    const auto& target_declaration = request.syntax.proofs[target->second];
                    const auto* function =
                        proposition_function(request, projected_target->name, target_declaration.keyword_location);
                    if (function) {
                        for (const auto& parameter : function->parameters)
                            if (auto type = convert_type(parameter.type))
                                expected_arguments.push_back(*type);
                        if (function->returned_value) {
                            ExpressionElaborator reader(next_expression_id);
                            if (auto proposition = reader.convert(*function->returned_value)) {
                                auto quantified = quantified_types(*proposition);
                                expected_arguments.insert(expected_arguments.end(), quantified.begin(),
                                                          quantified.end());
                            }
                        }
                    }
                }
            }

            std::vector<vir::Expr> arguments;
            for (const frontend::ProofArgument& written : statement.arguments) {
                auto argument = convert_probe(projected.argument_names, next_argument, written.location);
                if (!argument.has_value()) {
                    return std::nullopt;
                }
                // Two values of one machine layout are not interchangeable when
                // they stand for different C++ representations, so a proof is
                // not instantiated at a value of the wrong one.
                const auto index = arguments.size();
                if (index < expected_arguments.size() &&
                    (argument->type.representation.is_known() || expected_arguments[index].representation.is_known()) &&
                    !(argument->type == expected_arguments[index])) {
                    report(engine, diagnostics::Category::Elaboration, written.location,
                           "proof argument has type '" + describe(argument->type) +
                               "', but its quantified parameter has type '" + describe(expected_arguments[index]) +
                               "'");
                    return std::nullopt;
                }
                arguments.push_back(std::move(*argument));
            }

            switch (statement.kind) {
                case frontend::ProofStatementKind::Exact:
                    step.node = vir::ExactStep{std::move(*evidence), std::move(arguments)};
                    break;
                case frontend::ProofStatementKind::Rewrite:
                    step.node = vir::RewriteStep{std::move(*evidence), std::move(arguments)};
                    break;
                case frontend::ProofStatementKind::Contradiction:
                    step.node = vir::ContradictionStep{std::move(*evidence), std::move(arguments)};
                    break;
                default:
                    step.node = vir::ApplyStep{std::move(*evidence), std::move(arguments)};
                    break;
            }
            steps.push_back(std::move(step));
        }

        return steps;
    };
    return convert_steps(convert_steps, declaration.statements);
}

} // namespace detail::elaborator

} // namespace cppl::elaboration
