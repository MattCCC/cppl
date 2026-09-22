#include "cppl/elaboration/elaborate.hpp"

#include "cppl/decomposition/decomposition.hpp"

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

// The bridge's place, as VIR carries it. Both layers describe the same storage
// (SPEC.md 12.10); they are separate types because the bridge boundary does not
// let a Clang-facing structure reach the logical core (ARCHITECTURE.md 12).
vir::Place convert_place(const clangbridge::Place& place) {
    using From = clangbridge::PlaceRoot::Kind;
    using To = vir::PlaceRoot::Kind;
    vir::Place converted;
    switch (place.root.kind) {
        case From::Parameter:
            converted.root.kind = To::Parameter;
            break;
        case From::Deref:
            converted.root.kind = To::Deref;
            break;
        case From::Local:
            converted.root.kind = To::Local;
            break;
    }
    converted.root.id = place.root.id;
    converted.root.version = place.root.version;
    converted.spelling = place.spelling;
    converted.path.reserve(place.path.size());
    for (const auto& step : place.path) {
        vir::PlaceStep converted_step;
        switch (step.kind) {
            case clangbridge::PlaceStep::Kind::Element:
                converted_step.kind = vir::PlaceStep::Kind::Element;
                break;
            case clangbridge::PlaceStep::Kind::SymbolicElement:
                converted_step.kind = vir::PlaceStep::Kind::SymbolicElement;
                break;
            case clangbridge::PlaceStep::Kind::Field:
                converted_step.kind = vir::PlaceStep::Kind::Field;
                break;
        }
        converted_step.index = step.index;
        converted_step.symbol = step.symbol;
        converted.path.push_back(converted_step);
    }
    return converted;
}

std::optional<vir::Type> convert_type(const clangbridge::Type& type) {
    std::optional<vir::Type> converted;
    switch (type.kind) {
        case clangbridge::TypeKind::Void:
            converted = vir::Type::void_type();
            break;
        case clangbridge::TypeKind::Int:
            converted = vir::Type::integer(type.width, type.is_signed);
            break;
        case clangbridge::TypeKind::Bool:
            converted = vir::Type::boolean();
            break;
        case clangbridge::TypeKind::Proposition:
            converted = vir::Type::proposition();
            break;
        case clangbridge::TypeKind::Value:
            converted = vir::Type::value();
            for (const auto& projection : type.projections) {
                auto component = convert_type(projection);
                if (!component)
                    return std::nullopt;
                std::get<vir::ValueType>(converted->node).projections.push_back(std::move(*component));
            }
            break;
        case clangbridge::TypeKind::Unsupported:
            return std::nullopt;
    }
    // The base type is what the value is; the refinements are what is known
    // about it (SPEC.md 17). Both are carried, so verification can tell
    // `Percentage` from `int` while code generation cannot.
    for (const clangbridge::Refinement& refinement : type.refinements) {
        converted->refinements.push_back(vir::Refinement{refinement.name, refinement.arguments, refinement.identity});
    }
    // What C++ representation the value stands for, when it is one a
    // decomposition provider may model. Nothing is inferred from a spelling:
    // this is the identity Clang resolved (SPEC.md 20.5).
    converted->representation.identity = type.representation.identity;
    converted->representation.kind = type.representation.kind;
    converted->representation.components = type.representation.components;
    converted->representation.rejection = type.representation.rejection;
    // The resolved C++ spelling is kept for every type, so a diagnostic can
    // name what the author wrote even when no provider models it.
    converted->representation.name = type.representation.name.empty() ? type.spelling : type.representation.name;
    for (const clangbridge::Enumerator& enumerator : type.representation.enumerators) {
        converted->representation.enumerators.push_back(vir::Enumerator{enumerator.name, enumerator.value});
    }
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

        if (const auto* projection = std::get_if<clangbridge::Projection>(&expr.node)) {
            if (projection->operands.size() != 1)
                return std::nullopt;
            auto operand = convert(projection->operands[0]);
            if (!operand)
                return std::nullopt;
            result.node = vir::Projection{projection->index, {std::move(*operand)}};
            return result;
        }
        if (const auto* quantified = std::get_if<clangbridge::Universal>(&expr.node)) {
            if (quantified->binders.empty() || quantified->body.size() != 1) {
                failure_ = Failure{"malformed universal proposition", expr.location};
                return std::nullopt;
            }
            vir::Universal converted;
            for (const auto& binder : quantified->binders) {
                auto binder_type = convert_type(binder);
                // Quantification ranges over a modeled domain of values. A
                // structural value is a decomposition subject, not such a
                // domain: nothing states what its inhabitants are.
                if (!binder_type || binder_type->is_proposition() || binder_type->is_value()) {
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
            if (!operand_type ||
                (!operand_type->is_integer() && !operand_type->is_boolean() && !operand_type->is_value()) ||
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

        if (const auto* completed = std::get_if<clangbridge::ReturnState>(&expr.node)) {
            vir::ReturnState converted;
            for (const auto& operand : completed->operands) {
                auto value = convert(operand);
                if (!value)
                    return std::nullopt;
                converted.operands.push_back(std::move(*value));
            }
            result.node = std::move(converted);
            return result;
        }
        if (const auto* unknown = std::get_if<clangbridge::UnknownVersion>(&expr.node)) {
            auto value_type = convert_type(unknown->value_type);
            if (!value_type || unknown->operands.size() != 1)
                return std::nullopt;
            auto body = convert(unknown->operands.front());
            if (!body)
                return std::nullopt;
            result.node =
                vir::UnknownVersion{unknown->version, convert_place(unknown->place), *value_type, {std::move(*body)}};
            return result;
        }

        if (const auto* parameter = std::get_if<clangbridge::ParameterRef>(&expr.node)) {
            result.node = vir::ParameterRef{parameter->index, parameter->name};
            return result;
        }

        if (const auto* literal = std::get_if<clangbridge::IntLiteral>(&expr.node)) {
            if (!type->is_integer() && !type->is_boolean() && !type->is_void()) {
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
            for (const auto& effect : call->effects) {
                auto declared = convert_type(effect.declared);
                if (!declared)
                    return std::nullopt;
                converted.effects.push_back({effect.argument, effect.version, *declared});
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

        if (const auto* bound = std::get_if<clangbridge::PlaceVersion>(&expr.node)) {
            vir::PlaceVersion converted;
            converted.version = bound->version;
            converted.place = convert_place(bound->place);
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

        if (const auto* place = std::get_if<clangbridge::PlaceRef>(&expr.node)) {
            result.node = vir::PlaceRef{place->version, convert_place(place->place)};
            return result;
        }

        if (const auto* loop = std::get_if<clangbridge::Loop>(&expr.node)) {
            std::vector<vir::Place> places;
            places.reserve(loop->places.size());
            for (const auto& place : loop->places)
                places.push_back(convert_place(place));
            vir::Loop converted{loop->loop, loop->heads, std::move(places), 0, 0, {}};
            // One surface invariant can state a conjunction. Each conjunct is
            // still an independent entry/preservation obligation of the one
            // loop rule; no new fact is introduced and disjunction is not split.
            const auto append_invariant = [&](auto&& self, vir::Expr value) -> void {
                if (auto* conjunction = std::get_if<vir::Binary>(&value.node);
                    conjunction && conjunction->op == vir::BinaryOp::And && conjunction->operands.size() == 2) {
                    for (auto& operand : conjunction->operands)
                        self(self, std::move(operand));
                } else {
                    ++converted.invariants;
                    converted.operands.push_back(std::move(value));
                }
            };
            // The measure and the head follow the invariants, and splitting a
            // conjunction changes how many of those there are, so they are held
            // back and appended once every invariant is in place.
            std::vector<vir::Expr> trailing;
            for (std::size_t index = 0; index < loop->operands.size(); ++index) {
                auto value = convert(loop->operands[index]);
                if (!value)
                    return std::nullopt;
                if (index >= loop->heads.size() && index < loop->heads.size() + loop->invariants)
                    append_invariant(append_invariant, std::move(*value));
                else if (index < loop->heads.size())
                    converted.operands.push_back(std::move(*value));
                else
                    trailing.push_back(std::move(*value));
            }
            converted.measures = loop->measures;
            for (auto& value : trailing)
                converted.operands.push_back(std::move(value));
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
    if (const auto* returned = std::get_if<vir::ReturnState>(&expr.node))
        for (const auto& operand : returned->operands)
            collect_callees(operand, callees);
    if (const auto* unknown = std::get_if<vir::UnknownVersion>(&expr.node))
        for (const auto& operand : unknown->operands)
            collect_callees(operand, callees);
    if (const auto* binary = std::get_if<vir::Binary>(&expr.node)) {
        for (const vir::Expr& operand : binary->operands) {
            collect_callees(operand, callees);
        }
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expr.node)) {
        for (const auto& operand : branch->operands)
            collect_callees(operand, callees);
    }
    if (const auto* bound = std::get_if<vir::PlaceVersion>(&expr.node)) {
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
        parameters.push_back(vir::Parameter{parameter.name, *type, parameter.passing});
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

// The memory capabilities a projected clause states, when it states any.
//
// A capability leaves elaboration on its own channel and never becomes a
// `vir::Expr`, because it is not a proposition the kernel can check: it is a
// property of the execution state, supposed by the obligation layer as a
// context hypothesis (RFC 0014 §10, SPEC.md 12.10).
std::vector<vir::Capability> convert_capabilities(const Request& request, std::string_view generated,
                                                  const source::SourceLocation& written,
                                                  std::uint32_t& next_expression_id, const std::string& subject,
                                                  diagnostics::Engine& engine) {
    const clangbridge::Function* function = proposition_function(request, generated, written);
    if (function == nullptr || function->capabilities.empty()) {
        return {};
    }
    std::vector<vir::Capability> converted_all;
    ExpressionElaborator elaborator(next_expression_id);
    for (const clangbridge::Capability& stated : function->capabilities) {
        vir::Capability capability;
        capability.kind = stated.kind == clangbridge::Capability::Kind::Readable ? vir::CapabilityKind::Readable
                                                                                : vir::CapabilityKind::Writable;
        // A capability stated by a contract is owed by the caller, so its
        // origin is the contract until a trusted law admits it.
        capability.origin = vir::CapabilityOrigin::Contract;
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
                return {};
            }
            capability.extent.push_back(std::move(*converted));
        }
        converted_all.push_back(std::move(capability));
    }
    return converted_all;
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
    value_names.reserve(parameters.size());
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
    std::vector<vir::Expr> aliases;
    const auto remap = [&](auto&& self, vir::Expr& expression) -> void {
        if (auto* parameter = std::get_if<vir::ParameterRef>(&expression.node)) {
            if (parameter->parameter >= parameter_count) {
                const auto index = parameter->parameter - parameter_count;
                if (index < aliases.size()) {
                    expression = aliases[index];
                } else {
                    parameter->parameter -= static_cast<std::uint32_t>(aliases.size());
                }
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

            if (statement.kind == frontend::ProofStatementKind::Cases ||
                statement.kind == frontend::ProofStatementKind::Decompose) {
                auto subject = convert_probe(projected.case_names, next_case, statement.location);
                if (!subject)
                    return std::nullopt;

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
                if (const auto* product = std::get_if<decomposition::ProductDecomposition>(&decomposed)) {
                    if (statement.kind != frontend::ProofStatementKind::Decompose || statement.arms.size() != 1 ||
                        statement.arms[0].spelling != "components") {
                        report(engine, diagnostics::Category::Elaboration, statement.location,
                               "product decomposition requires decompose subject { components(binders) => { proof } }");
                        return std::nullopt;
                    }
                    const auto& arm = statement.arms[0];
                    if (arm.binders.size() != product->fields.size()) {
                        report(engine, diagnostics::Category::Elaboration, arm.location,
                               "product binds " + std::to_string(product->fields.size()) +
                                   " value(s), but this arm names " + std::to_string(arm.binders.size()));
                        return std::nullopt;
                    }
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
                    if (!nested)
                        return std::nullopt;
                    step.node = vir::ProductStep{*subject, std::move(*nested)};
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
                const bool residual_required =
                    sum.exhaustiveness == decomposition::ExhaustivenessModel::ResidualRequired;

                vir::CasesStep cases{*subject, {}};
                std::vector<bool> covered(sum.cases.size(), false);
                bool residual = false;

                for (const auto& arm : statement.arms) {
                    vir::CaseArm converted;
                    converted.location = arm.location;

                    // A reserved label names the residual state, so it is
                    // matched against the partition's own residual label rather
                    // than resolved as an expression.
                    const std::vector<decomposition::ProofBinding>* bindings = nullptr;
                    if (arm.keyword_label) {
                        const std::string& written = arm.spelling;
                        const auto named = std::ranges::find_if(
                            sum.cases, [&](const auto& candidate) { return candidate.label.text == written; });
                        if (named != sum.cases.end()) {
                            const auto index = static_cast<std::size_t>(named - sum.cases.begin());
                            if (covered[index]) {
                                report(engine, diagnostics::Category::Elaboration, arm.location,
                                       "duplicate case '" + written + "'");
                                return std::nullopt;
                            }
                            covered[index] = true;
                            converted.descriptor = static_cast<std::uint32_t>(index);
                            converted.label = written;
                            bindings = &named->bindings;
                        } else {
                            if (!residual_required || written != sum.residual.text) {
                                report(engine, diagnostics::Category::Elaboration, arm.location,
                                       "'" + written + "' is not a case of '" + describe(subject->type) + "'");
                                return std::nullopt;
                            }
                            if (residual) {
                                report(engine, diagnostics::Category::Elaboration, arm.location,
                                       "duplicate case '" + written + "'");
                                return std::nullopt;
                            }
                            residual = true;
                            converted.label = sum.residual.text;
                            bindings = &sum.residual_bindings;
                        }
                    } else {
                        auto label = convert_probe(projected.case_names, next_case, arm.location);
                        if (!label)
                            return std::nullopt;
                        const std::optional<std::size_t> index = provider.resolve_label(sum, *label);
                        if (!index || !(label->type == subject->type)) {
                            report(engine, diagnostics::Category::Elaboration, arm.location,
                                   "this label does not name a case of '" + describe(subject->type) + "'");
                            return std::nullopt;
                        }
                        const decomposition::CaseDescriptor& descriptor = sum.cases[*index];
                        if (covered[*index]) {
                            report(engine, diagnostics::Category::Elaboration, arm.location,
                                   "duplicate case '" + descriptor.label.text + "'",
                                   "labels that denote one state name one case");
                            return std::nullopt;
                        }
                        covered[*index] = true;
                        converted.descriptor = static_cast<std::uint32_t>(*index);
                        converted.label = descriptor.label.text;
                        bindings = &descriptor.bindings;
                    }

                    // A case supplies exactly the bindings its provider
                    // describes, so an arm names exactly that many.
                    if (arm.binders.size() != bindings->size()) {
                        report(engine, diagnostics::Category::Elaboration, arm.location,
                               "case '" + converted.label + "' binds " + std::to_string(bindings->size()) +
                                   " value(s), but this arm names " + std::to_string(arm.binders.size()));
                        return std::nullopt;
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
                    if (!nested)
                        return std::nullopt;
                    converted.steps = std::move(*nested);
                    cases.arms.push_back(std::move(converted));
                }

                // Exhaustiveness comes from the partition the provider
                // described. A state it lists and no arm claims is a missing
                // case, so adding a state to a representation makes a proof
                // that did not account for it stop being exhaustive.
                for (std::size_t index = 0; index < sum.cases.size(); ++index) {
                    if (!covered[index]) {
                        report(engine, diagnostics::Category::ProofFailure, statement.location,
                               "non-exhaustive cases: '" + sum.cases[index].label.text + "' has no arm");
                        return std::nullopt;
                    }
                }
                if (residual_required && !residual) {
                    report(engine, diagnostics::Category::ProofFailure, statement.location,
                           "non-exhaustive cases: '" + sum.residual.text + "' has no arm",
                           "'" + describe(subject->type) +
                               "' has states beyond the ones it names, and "
                               "no wildcard absorbs them");
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

            // The syntax recognizes 'induction' (GRAMMAR.md 5.7) so the
            // formatter can lay out its arms, but this formal core has no
            // induction rule (SPEC.md 21) - the same rejection every other
            // self-referential proof dependency already receives below, made
            // explicit here rather than left to fall through to evidence
            // lookup, where the induction subject's own identifier would
            // otherwise be looked up as if it were a proof/premise name.
            if (statement.kind == frontend::ProofStatementKind::Induction) {
                report(engine, diagnostics::Category::ProofFailure, statement.location,
                       "proof '" + declaration.name + "' uses induction over '" + statement.reference + "'",
                       "this formal core has no induction rule, so a proof cannot depend on "
                       "an induction principle");
                return std::nullopt;
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
    std::optional<vir::Expr> ensured =
        convert_projected(request, projected.postcondition_name, postcondition_location, next_expression_id,
                          "the postcondition of verified function '" + function.qualified_name + "'", engine);
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
        std::vector<vir::Capability> capabilities =
            convert_capabilities(request, projected.precondition_names[index], preconditions[index]->location,
                                 next_expression_id, subject, engine);
        if (!capabilities.empty()) {
            contract.capabilities.insert(contract.capabilities.end(), std::make_move_iterator(capabilities.begin()),
                                         std::make_move_iterator(capabilities.end()));
            continue;
        }
        if (engine.has_errors()) {
            return;
        }
        std::optional<vir::Expr> expected =
            convert_projected(request, projected.precondition_names[index], preconditions[index]->location,
                              next_expression_id, subject, engine);
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
                           !std::holds_alternative<vir::PlaceVersion>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::Loop>(converted.returned_value->node) &&
                           !std::holds_alternative<vir::ReturnState>(converted.returned_value->node)) {
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
