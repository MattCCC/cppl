#include "cppl/elaboration/elaborate.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <variant>

namespace cppl::elaboration {

namespace {

void report(diagnostics::Engine& engine,
            diagnostics::Category category,
            const source::SourceLocation& location,
            std::string message,
            std::string note = {}) {
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
    switch (type.kind) {
        case clangbridge::TypeKind::Int:
            return vir::Type::integer(type.width, type.is_signed);
        case clangbridge::TypeKind::Bool:
            return vir::Type::boolean();
        case clangbridge::TypeKind::Unsupported:
            break;
    }
    return std::nullopt;
}

vir::BinaryOp convert_operator(clangbridge::BinaryOp op) {
    switch (op) {
        case clangbridge::BinaryOp::Add:
            return vir::BinaryOp::Add;
        case clangbridge::BinaryOp::Equal:
            return vir::BinaryOp::Equal;
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

        const auto& unsupported = std::get<clangbridge::Unsupported>(expr.node);
        failure_ = Failure{unsupported.reason, expr.location};
        return std::nullopt;
    }

    [[nodiscard]] const std::optional<Failure>& failure() const noexcept { return failure_; }

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
}

// The function a C++L declaration was projected into: it carries the declared
// name and stands at the line the declaration came from. Matching both keeps an
// ordinary C++ function of the same name from being mistaken for it.
const clangbridge::Function* find_projected(const clangbridge::TranslationUnit& unit,
                                            std::string_view name,
                                            const source::SourceLocation& declared_at) {
    for (const clangbridge::Function& function : unit.functions) {
        if (function.name == name && function.location.file == declared_at.file &&
            function.location.line == declared_at.line) {
            return &function;
        }
    }
    return nullptr;
}

std::optional<std::vector<vir::Parameter>> convert_parameters(
    const clangbridge::Function& function,
    diagnostics::Engine& engine,
    std::string_view subject) {
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

// Resolves one proof statement into a typed step.
//
// A step's reference is a proof name, which is a C++L entity: it is resolved
// against the proofs this translation unit declares, and nowhere else.
std::optional<vir::ProofStep> convert_statement(const frontend::ProofStatement& statement,
                                                const std::string& proof_name,
                                                const std::map<std::string, std::size_t>& declared,
                                                diagnostics::Engine& engine) {
    vir::ProofStep step;
    step.location = statement.location;

    if (statement.kind == frontend::ProofStatementKind::Reflexivity) {
        step.node = vir::ReflexivityStep{};
        return step;
    }

    const auto target = declared.find(statement.reference);
    if (target == declared.end()) {
        report(engine, diagnostics::Category::Elaboration, statement.location,
               "no proof named '" + statement.reference + "' is declared in this translation "
               "unit",
               "'" + describe(statement.kind) + "' names a proof declaration");
        return std::nullopt;
    }
    if (statement.reference == proof_name) {
        report(engine, diagnostics::Category::ProofFailure, statement.location,
               "proof '" + proof_name + "' uses itself as its own evidence",
               "this formal core has no induction rule, so a proof cannot depend on itself");
        return std::nullopt;
    }

    const vir::ProofId id{static_cast<std::uint32_t>(target->second)};
    if (statement.kind == frontend::ProofStatementKind::Exact) {
        step.node = vir::ExactStep{id, statement.reference};
    } else {
        step.node = vir::ApplyStep{id, statement.reference};
    }
    return step;
}

// Resolves the written proofs of a unit against the laws they claim to prove.
//
// Nothing here decides whether a proof holds. It decides only what the author
// wrote: which law is named, at which arguments, and which proof a step uses.
void elaborate_proofs(const Request& request,
                      const std::map<std::string, vir::LawId>& admitted_laws,
                      const std::map<std::string, std::string>& law_names,
                      std::uint32_t& next_expression_id,
                      Result& result,
                      diagnostics::Engine& engine) {
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

    std::map<vir::LawId, std::string> proved_by;

    for (const frontend::ProofFunction& projected : request.projection.proof_functions) {
        const frontend::ProofDeclaration& declaration =
            request.syntax.proofs[projected.proof_index];

        const auto owner = declared.find(declaration.name);
        if (owner == declared.end() || owner->second != projected.proof_index) {
            continue;  // a duplicate name, already reported
        }

        const clangbridge::Function* function =
            find_projected(request.unit, projected.name, declaration.keyword_location);
        if (function == nullptr || !function->returned_value.has_value()) {
            report(engine, diagnostics::Category::Elaboration, declaration.range.begin,
                   "the proposition of proof '" + declaration.name + "' was not resolved",
                   function == nullptr
                       ? "Clang did not resolve the projected proposition"
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

        const auto* claim = std::get_if<vir::Call>(&proposition->node);
        if (claim == nullptr) {
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   declaration.proposition_location,
                   "proof '" + declaration.name + "' does not state the law it proves",
                   "write proves(<law>(<parameters>)); this implementation proves a declared "
                   "law, and does not accept a proposition written out in place");
            continue;
        }

        const auto admitted = admitted_laws.find(claim->callee.usr);
        if (admitted == admitted_laws.end()) {
            const auto known = law_names.find(claim->callee.usr);
            report(engine, diagnostics::Category::Elaboration, declaration.proposition_location,
                   known == law_names.end()
                       ? "'" + claim->callee_name + "' is not a law in this translation unit"
                       : "law '" + known->second + "' was not given formal meaning, so proof '" +
                             declaration.name + "' has no goal to discharge",
                   known == law_names.end()
                       ? "a proof discharges a law declared with 'ensures'"
                       : "the law itself was reported above");
            continue;
        }

        const vir::Law& law = result.module.laws[admitted->second.value];
        const auto refuse = [&result, &law] {
            result.laws_with_refused_proofs.push_back(law.id);
        };

        if (law.parameters.size() != parameters->size()) {
            refuse();
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   declaration.proposition_location,
                   "proof '" + declaration.name + "' takes " +
                       std::to_string(parameters->size()) + " parameters, but law '" + law.name +
                       "' quantifies over " + std::to_string(law.parameters.size()),
                   "a proof discharges a law at its own parameters, so the two agree in number "
                   "and in type");
            continue;
        }

        // Parameter types need no check of their own: the claim is an ordinary
        // C++ call, so a type that does not match is a conversion, and a
        // conversion is not something this implementation models.
        //
        // The law must be claimed at the proof's own parameters, in order.
        // Instantiating a quantifier at an arbitrary term is a rule the formal
        // core does not have, so a proof cannot be written as if it did.
        bool arguments_agree = claim->arguments.size() == parameters->size();
        for (std::size_t index = 0; arguments_agree && index < claim->arguments.size(); ++index) {
            const auto* argument = std::get_if<vir::ParameterRef>(&claim->arguments[index].node);
            arguments_agree = argument != nullptr && argument->parameter == index;
        }
        if (!arguments_agree) {
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   declaration.proposition_location,
                   "proof '" + declaration.name + "' does not claim law '" + law.name +
                       "' at its own parameters",
                   "write proves(" + law.name +
                       "(...)) naming this proof's parameters in order; instantiating a "
                       "quantifier at another term is not part of this formal core");
            refuse();
            continue;
        }

        const auto [owner_of_law, first] = proved_by.emplace(law.id, declaration.name);
        if (!first) {
            report(engine, diagnostics::Category::CpplSyntax, declaration.range.begin,
                   "law '" + law.name + "' already has a proof",
                   "'" + owner_of_law->second + "' proves it; a law is discharged by exactly "
                   "one written proof");
            continue;
        }

        const frontend::ProofStatement& statement = declaration.statements.front();
        std::optional<vir::ProofStep> step =
            convert_statement(statement, declaration.name, declared, engine);
        if (!step.has_value()) {
            refuse();
            continue;
        }

        vir::Proof proof;
        proof.id = vir::ProofId{static_cast<std::uint32_t>(projected.proof_index)};
        proof.name = declaration.name;
        proof.law = law.id;
        proof.parameters = *parameters;
        proof.proposition = std::move(*proposition);
        proof.step = std::move(*step);
        proof.range = declaration.range;
        result.module.proofs.push_back(std::move(proof));
    }
}

}  // namespace

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

    // Functions the author marked `pure`. Purity is claimed here and checked
    // below; the marker alone establishes nothing (SPEC.md 13.3).
    std::set<std::string> pure_symbols;
    std::vector<const clangbridge::Function*> pure_functions;

    for (const frontend::PureMarker& marker : request.syntax.pure_markers) {
        const clangbridge::Function* function = request.unit.find_at(marker.function_location);
        if (function == nullptr) {
            report(engine, diagnostics::Category::Elaboration, marker.function_location,
                   "the declaration of '" + marker.function_name +
                       "' marked pure was not resolved",
                   "Clang did not report a function declaration at this location");
            continue;
        }
        pure_symbols.insert(function->usr);
        pure_functions.push_back(function);
    }

    for (const clangbridge::Function* function : pure_functions) {
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
                std::vector<vir::SymbolId> callees;
                collect_callees(*body, callees);
                for (const vir::SymbolId& callee : callees) {
                    if (!pure_symbols.contains(callee.usr)) {
                        rejection =
                            "it calls a function that is not declared pure, so its value is not "
                            "a mathematical function of its arguments";
                        break;
                    }
                }
                if (rejection.empty()) {
                    converted.returned_value = std::move(body);
                    converted.purity = vir::Purity::Pure;
                }
            }
        }

        if (converted.purity != vir::Purity::Pure) {
            result.rejected_functions.push_back(FunctionRejection{
                converted.symbol, function->qualified_name, rejection, function->location});
        }

        result.module.functions.push_back(std::move(converted));
    }

    // Laws, and the C++ identity Clang gave each of them. A proof names a law
    // through ordinary C++ lookup, so the two meet here by symbol, never by
    // spelling.
    std::map<std::string, vir::LawId> admitted_laws;
    std::map<std::string, std::string> law_names;

    for (const frontend::SpecificationFunction& specification :
         request.projection.specification_functions) {
        const frontend::LawDeclaration& declaration =
            request.syntax.laws[specification.law_index];

        const clangbridge::Function* function =
            find_projected(request.unit, specification.name, declaration.keyword_location);
        if (function != nullptr) {
            law_names.emplace(function->usr, declaration.name);
        }
        if (function == nullptr || !function->returned_value.has_value()) {
            report(engine, diagnostics::Category::Elaboration, declaration.range.begin,
                   "the proposition of law '" + declaration.name + "' was not resolved",
                   function == nullptr
                       ? "Clang did not resolve the projected specification function"
                       : "the specification expression produced no value");
            continue;
        }

        const bool has_precondition =
            std::ranges::any_of(declaration.clauses, [](const frontend::Clause& clause) {
                return clause.kind == frontend::ClauseKind::Expects;
            });
        if (has_precondition) {
            report(engine, diagnostics::Category::UnsupportedSemantics, declaration.range.begin,
                   "law '" + declaration.name + "' has an expects clause, which is not supported "
                   "by this implementation",
                   "implication is not part of the formal core yet, and the precondition will "
                   "not be assumed");
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
        law.id = vir::LawId{static_cast<std::uint32_t>(result.module.laws.size())};
        law.name = declaration.name;
        law.parameters = *parameters;
        law.proposition = std::move(*proposition);
        law.range = declaration.range;
        law.proposition_range.begin = declaration.proposition()->location;
        admitted_laws.emplace(function->usr, law.id);
        result.module.laws.push_back(std::move(law));
    }

    elaborate_proofs(request, admitted_laws, law_names, next_expression_id, result, engine);
    return result;
}

}  // namespace cppl::elaboration
