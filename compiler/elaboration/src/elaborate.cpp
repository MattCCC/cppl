#include "cppl/elaboration/elaborate.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <variant>

namespace cppl::elaboration {

namespace {

void report(diagnostics::Engine& engine, diagnostics::Category category, const source::SourceLocation& location,
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

std::optional<vir::Type> convert_type(const clangbridge::Type& type) {
    std::optional<vir::Type> converted;
    switch (type.kind) {
        case clangbridge::TypeKind::Int:
            converted = vir::Type::integer(type.width, type.is_signed);
            break;
        case clangbridge::TypeKind::Bool:
            converted = vir::Type::boolean();
            break;
        case clangbridge::TypeKind::Proposition:
            converted = vir::Type::proposition();
            break;
        case clangbridge::TypeKind::Unsupported:
            return std::nullopt;
    }
    // The base type is what the value is; the refinements are what is known
    // about it (SPEC.md 17). Both are carried, so verification can tell
    // `Percentage` from `int` while code generation cannot.
    for (const clangbridge::Refinement& refinement : type.refinements) {
        converted->refinements.push_back(vir::Refinement{refinement.name, refinement.arguments});
    }
    converted->enumeration = type.enumeration;
    converted->enumerators = type.enumerators;
    return converted;
}

vir::BinaryOp convert_operator(clangbridge::BinaryOp op) {
    switch (op) {
        case clangbridge::BinaryOp::Add:
            return vir::BinaryOp::Add;
        case clangbridge::BinaryOp::Sub:
            return vir::BinaryOp::Sub;
        case clangbridge::BinaryOp::Mul:
            return vir::BinaryOp::Mul;
        case clangbridge::BinaryOp::Equal:
            return vir::BinaryOp::Equal;
        case clangbridge::BinaryOp::NotEqual:
            return vir::BinaryOp::NotEqual;
        case clangbridge::BinaryOp::Less:
            return vir::BinaryOp::Less;
        case clangbridge::BinaryOp::LessEqual:
            return vir::BinaryOp::LessEqual;
        case clangbridge::BinaryOp::Greater:
            return vir::BinaryOp::Greater;
        case clangbridge::BinaryOp::GreaterEqual:
            return vir::BinaryOp::GreaterEqual;
        case clangbridge::BinaryOp::And:
            return vir::BinaryOp::And;
        case clangbridge::BinaryOp::Or:
            return vir::BinaryOp::Or;
        case clangbridge::BinaryOp::Unsupported:
            break;
    }
    return vir::BinaryOp::Add;
}

// Converts resolved Clang expressions into VIR.
//
// Every expression C++L cannot represent stops the conversion with a reason at
// a source location. Nothing is dropped silently.
class ExpressionElaborator {
  public:
    explicit ExpressionElaborator(std::uint32_t& next_id) : next_id_(next_id) {}

    struct Failure {
        std::string reason;
        source::SourceLocation location;
    };

    [[nodiscard]] std::optional<vir::Expr> convert(const clangbridge::Expr& expr) {
        const std::optional<vir::Type> type = convert_type(expr.type);
        if (!type.has_value()) {
            failure_ = Failure{"type '" + expr.type.spelling + "' is not modeled", expr.location};
            return std::nullopt;
        }

        vir::Expr result;
        result.id = vir::ExprId{next_id_++};
        result.type = *type;
        result.provenance.range.begin = expr.location;

        if (const auto* quantified = std::get_if<clangbridge::Universal>(&expr.node)) {
            if (quantified->binders.empty() || quantified->body.size() != 1) {
                failure_ = Failure{"malformed universal proposition", expr.location};
                return std::nullopt;
            }
            vir::Universal converted;
            for (const auto& binder : quantified->binders) {
                auto binder_type = convert_type(binder);
                if (!binder_type || binder_type->is_proposition()) {
                    failure_ =
                        Failure{"quantifier binder type '" + binder.spelling + "' is not modeled", expr.location};
                    return std::nullopt;
                }
                converted.binders.push_back(*binder_type);
            }
            auto body = convert(quantified->body[0]);
            if (!body)
                return std::nullopt;
            converted.body.push_back(std::move(*body));
            result.node = std::move(converted);
            return result;
        }
        if (const auto* connective = std::get_if<clangbridge::Connective>(&expr.node)) {
            vir::Connective converted;
            switch (connective->kind) {
                case clangbridge::Connective::Kind::Conjunction:
                    converted.kind = vir::Connective::Kind::Conjunction;
                    break;
                case clangbridge::Connective::Kind::Disjunction:
                    converted.kind = vir::Connective::Kind::Disjunction;
                    break;
                case clangbridge::Connective::Kind::Equivalence:
                    converted.kind = vir::Connective::Kind::Equivalence;
                    break;
            }
            for (const auto& operand : connective->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }
        if (const auto* implication = std::get_if<clangbridge::Implication>(&expr.node)) {
            vir::Implication converted;
            for (const auto& operand : implication->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }

        if (const auto* equality = std::get_if<clangbridge::FormalEquality>(&expr.node)) {
            const auto operand_type = convert_type(equality->operand_type);
            if (!operand_type || (!operand_type->is_integer() && !operand_type->is_boolean()) ||
                equality->operands.size() != 2) {
                failure_ = Failure{"formal equality requires two operands of a modeled C++ type", expr.location};
                return std::nullopt;
            }
            vir::FormalEquality converted{*operand_type, {}};
            for (const auto& operand : equality->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                if (!(value->type == *operand_type)) {
                    failure_ = Failure{"formal equality operand has the wrong type", operand.location};
                    return std::nullopt;
                }
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }

        if (const auto* parameter = std::get_if<clangbridge::ParameterRef>(&expr.node)) {
            result.node = vir::ParameterRef{parameter->index, parameter->name};
            return result;
        }

        if (const auto* literal = std::get_if<clangbridge::IntLiteral>(&expr.node)) {
            if (!type->is_integer()) {
                failure_ = Failure{"a literal of non-integer type is not modeled", expr.location};
                return std::nullopt;
            }
            result.node = vir::IntLiteral{literal->value};
            return result;
        }

        if (const auto* call = std::get_if<clangbridge::Call>(&expr.node)) {
            vir::Call converted;
            converted.callee = vir::SymbolId{call->callee_usr};
            converted.callee_name = call->callee_name;
            for (const clangbridge::Expr& argument : call->arguments) {
                std::optional<vir::Expr> converted_argument = convert(argument);
                if (!converted_argument.has_value()) {
                    return std::nullopt;
                }
                converted.arguments.push_back(std::move(*converted_argument));
            }
            result.node = std::move(converted);
            return result;
        }

        if (const auto* binary = std::get_if<clangbridge::Binary>(&expr.node)) {
            if (binary->op == clangbridge::BinaryOp::Unsupported || binary->operands.size() != 2) {
                failure_ = Failure{"this operator is not modeled", expr.location};
                return std::nullopt;
            }
            vir::Binary converted;
            converted.op = convert_operator(binary->op);
            for (const clangbridge::Expr& operand : binary->operands) {
                std::optional<vir::Expr> converted_operand = convert(operand);
                if (!converted_operand.has_value()) {
                    return std::nullopt;
                }
                converted.operands.push_back(std::move(*converted_operand));
            }
            result.node = std::move(converted);
            return result;
        }

        if (const auto* negation = std::get_if<clangbridge::Negation>(&expr.node)) {
            vir::Negation converted;
            for (const auto& operand : negation->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }
        if (const auto* branch = std::get_if<clangbridge::Conditional>(&expr.node)) {
            vir::Conditional converted;
            for (const auto& operand : branch->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }

        if (const auto* bound = std::get_if<clangbridge::LocalVersion>(&expr.node)) {
            vir::LocalVersion converted;
            converted.version = bound->version;
            converted.name = bound->name;
            if (const std::optional<vir::Type> declared = convert_type(bound->declared)) {
                converted.declared = *declared;
            }
            for (const auto& operand : bound->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }

        if (const auto* local = std::get_if<clangbridge::LocalRef>(&expr.node)) {
            result.node = vir::LocalRef{local->version, local->name};
            return result;
        }

        if (const auto* loop = std::get_if<clangbridge::Loop>(&expr.node)) {
            vir::Loop converted{loop->loop, loop->heads, loop->names, loop->invariants, {}};
            for (const auto& operand : loop->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }

        if (const auto* next = std::get_if<clangbridge::Iterate>(&expr.node)) {
            vir::Iterate converted{next->loop, {}};
            for (const auto& operand : next->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }
        const auto& unsupported = std::get<clangbridge::Unsupported>(expr.node);
        failure_ = Failure{unsupported.reason, expr.location};
        return std::nullopt;
    }

    [[nodiscard]] const std::optional<Failure>& failure() const noexcept {
        return failure_;
    }

  private:
    std::uint32_t& next_id_;
    std::optional<Failure> failure_;
};

void collect_callees(const vir::Expr& expr, std::vector<vir::SymbolId>& callees) {
    if (const auto* call = std::get_if<vir::Call>(&expr.node)) {
        callees.push_back(call->callee);
        for (const vir::Expr& argument : call->arguments) {
            collect_callees(argument, callees);
        }
        return;
    }
    if (const auto* binary = std::get_if<vir::Binary>(&expr.node)) {
        for (const vir::Expr& operand : binary->operands) {
            collect_callees(operand, callees);
        }
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expr.node)) {
        for (const auto& operand : branch->operands)
            collect_callees(operand, callees);
    }
    if (const auto* bound = std::get_if<vir::LocalVersion>(&expr.node)) {
        for (const auto& operand : bound->operands)
            collect_callees(operand, callees);
    }
    if (const auto* negation = std::get_if<vir::Negation>(&expr.node)) {
        for (const auto& operand : negation->operands)
            collect_callees(operand, callees);
    }
    if (const auto* loop = std::get_if<vir::Loop>(&expr.node)) {
        for (const auto& operand : loop->operands)
            collect_callees(operand, callees);
    }
    if (const auto* next = std::get_if<vir::Iterate>(&expr.node)) {
        for (const auto& operand : next->operands)
            collect_callees(operand, callees);
    }
}

// Generated helpers have distinct names. Repeated displayed locations must
// never make an ambiguous helper lookup pick the first declaration. Laws and
// executable declarations are linked separately by physical analysis offset.
const clangbridge::Function* find_projected(const clangbridge::TranslationUnit& unit, std::string_view name,
                                            const source::SourceLocation& declared_at) {
    const clangbridge::Function* found = nullptr;
    for (const clangbridge::Function& function : unit.functions) {
        if (function.name == name && function.location.file == declared_at.file &&
            function.location.line == declared_at.line) {
            if (found != nullptr)
                return nullptr;
            found = &function;
        }
    }
    return found;
}

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
        parameters.push_back(vir::Parameter{parameter.name, *type});
    }
    return parameters;
}

// Reads one projected expression back as VIR.
//
// The expression itself was resolved by Clang in the proof's own scope; what
// happens here is only the conversion of that resolved expression into the
// fragment C++L models.
const clangbridge::Function* proposition_function(const Request& request, std::string_view generated,
                                                  const source::SourceLocation& written) {
    for (const auto& probe : request.projection.proposition_probes) {
        if (probe.owner == generated && probe.location.file == written.file && probe.location.line == written.line) {
            return find_projected(request.unit, probe.name, probe.location);
        }
    }
    return find_projected(request.unit, generated, written);
}

std::optional<vir::Expr> convert_projected(const Request& request, std::string_view generated,
                                           const source::SourceLocation& written, std::uint32_t& next_expression_id,
                                           const std::string& subject, diagnostics::Engine& engine) {
    const clangbridge::Function* function = proposition_function(request, generated, written);
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

// Resolves a proof body into typed steps.
//
// A step's reference names a proof-level entity: a proof this translation unit
// declares, or a premise an earlier `assume` in this same body bound. Both are
// C++L bindings, resolved here and nowhere else. The terms a step is
// instantiated at, and the proposition an `assume` names, are ordinary C++ and
// are read back from the functions the projector emitted for them, so Clang
// alone decides what each one denotes.
std::optional<std::vector<vir::ProofStep>> convert_statements(
    const Request& request, const frontend::ProofDeclaration& declaration, const frontend::ProofFunction& projected,
    const std::map<std::string, std::size_t>& declared, const std::vector<vir::Parameter>& parameters,
    std::uint32_t& next_expression_id, diagnostics::Engine& engine) {
    const std::size_t parameter_count = parameters.size();
    std::vector<std::string> value_names;
    for (const auto& parameter : parameters)
        value_names.push_back(parameter.name);
    std::vector<std::string> assumed;
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
    std::vector<std::uint32_t> aliases;
    const auto remap = [&](auto&& self, vir::Expr& expression) -> void {
        std::visit(
            [&](auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, vir::ParameterRef>) {
                    if (node.parameter >= parameter_count) {
                        const auto index = node.parameter - parameter_count;
                        node.parameter = index < aliases.size()
                                             ? aliases[index]
                                             : node.parameter - static_cast<std::uint32_t>(aliases.size());
                    }
                } else if constexpr (requires { node.operands; }) {
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
    const auto convert_probe = [&](const std::vector<std::string>& names, std::size_t& next,
                                   const source::SourceLocation& location) -> std::optional<vir::Expr> {
        if (next >= names.size())
            return std::nullopt;
        auto expression = convert_projected(request, names[next++], location, next_expression_id,
                                            "statement of proof '" + declaration.name + "'", engine);
        if (expression)
            remap(remap, *expression);
        return expression;
    };
    const auto convert_steps =
        [&](auto&& self,
            const std::vector<frontend::ProofStatement>& statements) -> std::optional<std::vector<vir::ProofStep>> {
        std::vector<vir::ProofStep> steps;

        for (const frontend::ProofStatement& statement : statements) {
            vir::ProofStep step;
            step.location = statement.location;

            if (statement.kind == frontend::ProofStatementKind::Cases) {
                auto subject = convert_probe(projected.case_names, next_case, statement.location);
                if (!subject)
                    return std::nullopt;
                const auto* parameter = std::get_if<vir::ParameterRef>(&subject->node);
                if (!parameter || subject->type.enumeration.empty() || !subject->type.is_integer()) {
                    report(engine, diagnostics::Category::UnsupportedSemantics, statement.location,
                           "cases currently requires a parameter of a defined scoped enum type");
                    return std::nullopt;
                }
                const std::uint32_t subject_parameter = parameter->parameter;
                vir::CasesStep cases{*subject, {}};
                std::set<std::int64_t> covered;
                bool residual = false;
                for (const auto& arm : statement.arms) {
                    vir::CaseArm converted;
                    converted.location = arm.location;
                    if (arm.residual) {
                        if (residual || arm.binders.size() != 1) {
                            report(engine, diagnostics::Category::Elaboration, arm.location,
                                   "a scoped enum has at most one unnamed(value) arm, with exactly one value binder");
                            return std::nullopt;
                        }
                        residual = true;
                    } else {
                        auto label = convert_probe(projected.case_names, next_case, arm.location);
                        if (!label)
                            return std::nullopt;
                        const auto* value = std::get_if<vir::IntLiteral>(&label->node);
                        if (!value || !(label->type == subject->type) || !arm.binders.empty()) {
                            report(engine, diagnostics::Category::Elaboration, arm.location,
                                   "a named enum arm requires an enumerator of the subject type and no binders");
                            return std::nullopt;
                        }
                        if (!covered.insert(value->value).second) {
                            report(engine, diagnostics::Category::Elaboration, arm.location,
                                   "duplicate enum case (enumerator aliases denote the same case)");
                            return std::nullopt;
                        }
                        converted.value = value->value;
                    }
                    const auto enclosing_assumed = assumed.size();
                    if (arm.residual) {
                        if (std::ranges::find(value_names, arm.binders[0]) != value_names.end()) {
                            report(engine, diagnostics::Category::Elaboration, arm.location,
                                   "case binder duplicates an enclosing value name");
                            return std::nullopt;
                        }
                        aliases.push_back(subject_parameter);
                        value_names.push_back(arm.binders[0]);
                    }
                    auto nested = self(self, arm.statements);
                    if (arm.residual) {
                        aliases.pop_back();
                        value_names.pop_back();
                    }
                    assumed.resize(enclosing_assumed);
                    assumed_types.resize(enclosing_assumed);
                    if (!nested)
                        return std::nullopt;
                    converted.steps = std::move(*nested);
                    cases.arms.push_back(std::move(converted));
                }
                // This prototype requires written arms, including the residual.
                // Omitted impossible cases await an explicit evidence-producing path.
                for (const auto value : subject->type.enumerators) {
                    if (!covered.contains(value)) {
                        report(engine, diagnostics::Category::ProofFailure, statement.location,
                               "non-exhaustive cases: a named enumerator has no arm");
                        return std::nullopt;
                    }
                }
                if (!residual) {
                    report(engine, diagnostics::Category::ProofFailure, statement.location,
                           "non-exhaustive cases: unnamed(value) needs an explicit arm in this prototype");
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

            if (statement.kind == frontend::ProofStatementKind::Assume) {
                auto proposition =
                    convert_probe(projected.assumption_names, next_assumption, statement.proposition_location);
                if (!proposition.has_value()) {
                    return std::nullopt;
                }
                assumed_types.push_back(quantified_types(*proposition));
                step.node = vir::AssumeStep{statement.reference, std::move(*proposition)};
                assumed.push_back(statement.reference);
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
                    break;
                }
            }

            if (!evidence.has_value()) {
                const auto target = declared.find(statement.reference);
                if (target == declared.end()) {
                    report(engine, diagnostics::Category::Elaboration, statement.location,
                           "no proof or assumed premise named '" + statement.reference + "' is in scope here",
                           "'" + describe(statement.kind) + "' names a proof declaration or a name bound by 'assume'");
                    return std::nullopt;
                }
                if (statement.reference == declaration.name) {
                    report(engine, diagnostics::Category::ProofFailure, statement.location,
                           "proof '" + declaration.name + "' uses itself as its own evidence",
                           "this formal core has no induction rule, so a proof cannot depend on "
                           "itself");
                    return std::nullopt;
                }
                evidence = vir::Reference{vir::ProofRef{vir::ProofId{static_cast<std::uint32_t>(target->second)}},
                                          statement.reference};
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
                const auto index = arguments.size();
                if (index < expected_arguments.size() &&
                    (!argument->type.enumeration.empty() || !expected_arguments[index].enumeration.empty()) &&
                    !(argument->type == expected_arguments[index])) {
                    report(engine, diagnostics::Category::Elaboration, written.location,
                           "proof argument has a different enum type from its quantified parameter");
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

// Reads a verified function's contract back from the functions it was projected
// into.
//
// `result` is a parameter of the projected postcondition, in last position, so
// Clang resolves it as an ordinary name and the elaborated expression refers to
// it by position like any other parameter. Nothing named `result` exists in the
// program itself.
void elaborate_contract(const Request& request, const frontend::VerifiedFunction& declaration,
                        const frontend::ContractFunctions& projected, const clangbridge::Function& function,
                        const std::string& body_rejection, std::uint32_t& next_expression_id, vir::Function& converted,
                        diagnostics::Engine& engine) {
    if (!body_rejection.empty() || !converted.returned_value.has_value()) {
        report(engine, diagnostics::Category::UnsupportedSemantics, declaration.function_location,
               "verified function '" + function.qualified_name +
                   "' has a body this implementation cannot state as a value" +
                   (body_rejection.empty() ? "" : ": " + body_rejection),
               "a contract is discharged from the body, and this implementation models a body "
               "using only modeled if/else, loops, blocks, and returned expressions");
        return;
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

    const frontend::Clause* postcondition = declaration.postcondition();
    std::optional<vir::Expr> ensured =
        convert_projected(request, projected.postcondition_name, postcondition->location, next_expression_id,
                          "the postcondition of verified function '" + function.qualified_name + "'", engine);
    if (!ensured.has_value()) {
        return;
    }

    vir::Contract contract;
    contract.postcondition = std::move(*ensured);
    contract.range.begin = postcondition->location;

    const std::vector<const frontend::Clause*> preconditions = declaration.preconditions();
    if (preconditions.size() != projected.precondition_names.size()) {
        report(engine, diagnostics::Category::Elaboration, declaration.function_location,
               "the preconditions of verified function '" + function.qualified_name + "' were not all projected");
        return;
    }
    for (std::size_t index = 0; index < preconditions.size(); ++index) {
        std::optional<vir::Expr> expected = convert_projected(
            request, projected.precondition_names[index], preconditions[index]->location, next_expression_id,
            "the precondition of verified function '" + function.qualified_name + "'", engine);
        if (!expected.has_value()) {
            return;
        }
        contract.preconditions.push_back(std::move(*expected));
    }

    converted.contract = std::move(contract);
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

    for (const frontend::ProofFunction& projected : request.projection.proof_functions) {
        const frontend::ProofDeclaration& declaration = request.syntax.proofs[projected.proof_index];

        const auto owner = declared.find(declaration.name);
        if (owner == declared.end() || owner->second != projected.proof_index) {
            continue; // a duplicate name, already reported
        }

        const clangbridge::Function* function =
            proposition_function(request, projected.name, declaration.keyword_location);
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

        // The claim's arguments need no check here. `proves(L(...))` is an
        // ordinary C++ call, so Clang has already settled their number and
        // their types; which proposition they state is worked out where the
        // law's own proposition is known, by instantiating it at them.
        std::optional<std::vector<vir::ProofStep>> steps =
            convert_statements(request, declaration, projected, declared, *parameters, next_expression_id, engine);
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
        if (function == nullptr) {
            report(engine, diagnostics::Category::Elaboration, declaration.function_location,
                   "the declaration of verified function '" + declaration.function_name + "' was not resolved",
                   "Clang did not report a function declaration at this location");
            continue;
        }
        Candidate& candidate = candidate_for(function);
        candidate.contract = &projected;
        candidate.declaration = &declaration;
        verified_symbols.insert(function->usr);
    }

    for (const Candidate& candidate : candidates) {
        const clangbridge::Function* function = candidate.function;
        vir::Function converted;
        converted.id = vir::FunctionId{next_function_id++};
        converted.symbol = vir::SymbolId{function->usr};
        converted.qualified_name = function->qualified_name;
        converted.range.begin = function->location;

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
        if (!function->has_body) {
            rejection = "it is declared but not defined in this translation unit";
        } else if (function->body_rejection.has_value()) {
            rejection = *function->body_rejection;
        } else if (!function->returned_value.has_value()) {
            rejection = "its body produced no value expression";
        }

        if (rejection.empty()) {
            ExpressionElaborator elaborator(next_expression_id);
            std::optional<vir::Expr> body = elaborator.convert(*function->returned_value);
            if (!body.has_value()) {
                const auto& failure = elaborator.failure();
                rejection = failure.has_value() ? failure->reason : "its body is not modeled";
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
                if (!calls_modeled) {
                    rejection = "it calls a function that is not declared pure, so its value is not "
                                "a mathematical function of its arguments";
                } else if (candidate.pure && calls_only_pure &&
                           !std::holds_alternative<vir::Conditional>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::LocalVersion>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::Loop>(converted.returned_value->node)) {
                    converted.purity = vir::Purity::Pure;
                } else if (candidate.pure && candidate.contract == nullptr) {
                    rejection = "pure specification helpers require a single return expression";
                }
            }
        }

        // A function the author marked pure and that did not turn out to be a
        // definition is recorded, so a law that reaches for it can say why.
        if (candidate.pure && converted.purity != vir::Purity::Pure) {
            result.rejected_functions.push_back(
                FunctionRejection{converted.symbol, function->qualified_name, rejection, function->location});
        }

        if (candidate.contract != nullptr) {
            elaborate_contract(request, *candidate.declaration, *candidate.contract, *function, rejection,
                               next_expression_id, converted, engine);
        }

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
        law.proposition_range.begin = declaration.proposition()->location;
        admitted_laws.emplace(law_usr, law.id);
        result.module.laws.push_back(std::move(law));
    }

    elaborate_refinements(request, next_expression_id, result, engine);
    elaborate_proofs(request, admitted_laws, law_names, next_expression_id, result, engine);
    return result;
}

} // namespace cppl::elaboration
