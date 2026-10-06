// Projecting Laws, proofs and the contracts of verified functions.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "formal_projection.hpp"
#include "projector.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

using detail::projector::at_written_position;
using detail::projector::blank;
using detail::projector::Edit;
using detail::projector::FormalScopes;
using detail::projector::Generated;
using detail::projector::generated_edit;
using detail::projector::Projector;
using detail::projector::resume_at;
using detail::projector::spelled_tokens;

namespace {

using detail::line_directive;

// A verified function's parameter list as its probes declare it: the list as
// written, without its default arguments (SPEC.md R.16, CONTRACTCOMP-002).
//
// A default argument is a value a call relying on it evaluates at the call, so
// it belongs to the call, never to the contract: a probe states the clause over
// the parameters, and the bridge lowers a default where a call uses it. A probe
// that kept one would also need one for every parameter after it, `result`
// included, and a template's probes are declared twice, where C++ forbids
// stating a default again. The function's own declaration keeps its defaults in
// both texts; only the generated copies leave them out.
//
// A default runs from a `=` outside every bracket to the first comma outside
// every bracket after it, or to the end of the list. A comma after a `<` the
// default leaves open may instead separate the arguments of a template-id, and
// which it is depends on lookup Clang has not done yet: there the default is
// not delimited, and `ambiguous` holds the `=` it starts at. A comma before
// which every `<` is closed separates parameters, since a template-id that
// contains a comma has a `<` still open there whose `>` follows it. Everything
// kept is copied from the scanned text, comments and attributes included.
struct ProbeParameters {
    Generated text;
    std::optional<source::SourceLocation> ambiguous;
};

ProbeParameters without_default_arguments(const TokenStream& stream, const source::ByteSpan& span) {
    ProbeParameters parameters;
    std::size_t kept_from = span.offset;
    std::size_t default_end = span.offset; // the end of the default's last token
    source::SourceLocation default_at;     // and its `=`
    bool in_default = false;
    unsigned depth = 0;  // (), [], {} open within the list
    unsigned angles = 0; // `<` a default leaves open outside every bracket
    const std::vector<Token>& tokens = stream.tokens();
    for (auto at =
             std::ranges::lower_bound(tokens, span.offset, {}, [](const Token& token) { return token.span.offset; });
         at != tokens.end(); ++at) {
        const Token& token = *at;
        if (token.kind == TokenKind::EndOfFile || token.span.offset >= span.end()) {
            break;
        }
        if (token.is_punctuator("(") || token.is_punctuator("[") || token.is_punctuator("{")) {
            ++depth;
        } else if ((token.is_punctuator(")") || token.is_punctuator("]") || token.is_punctuator("}")) && depth > 0) {
            --depth;
        } else if (depth == 0 && !in_default && token.is_punctuator("=")) {
            parameters.text.copy(stream, source::ByteSpan{kept_from, token.span.offset - kept_from});
            in_default = true;
            angles = 0;
            default_end = token.span.end();
            default_at = stream.location_of(token);
            continue;
        } else if (depth == 0 && in_default && token.is_punctuator(",")) {
            if (angles != 0 && !parameters.ambiguous.has_value()) {
                parameters.ambiguous = default_at;
            }
            in_default = false;
            kept_from = default_end;
            continue;
        } else if (depth == 0 && in_default) {
            if (token.is_punctuator("<")) {
                ++angles;
            } else if (token.is_punctuator(">") || token.is_punctuator(">=")) {
                angles = angles > 0 ? angles - 1 : 0;
            } else if (token.is_punctuator(">>") || token.is_punctuator(">>=")) {
                angles = angles > 1 ? angles - 2 : 0;
            }
        }
        if (in_default) {
            default_end = token.span.end();
        }
    }
    if (in_default) {
        kept_from = default_end;
    }
    parameters.text.copy(stream, source::ByteSpan{kept_from, span.end() - kept_from});
    return parameters;
}

// The names a template header introduces, as an argument list: from
// `template <unsigned N, typename T>` this yields `N, T`.
//
// A probe declared under that header is a template too, and a template is
// instantiated only where it is used. Naming the probe at these arguments
// inside the body is what makes C++ instantiate it alongside each
// specialization of the function, at the same arguments (SPEC.md TEMPLATE-001).
//
// The name of each parameter is the last identifier before the `,` or `>` that
// ends it, which is where C++ puts it in every form this implementation
// accepts. A parameter pack or a defaulted parameter is not one of those forms,
// and yields no name, so the caller emits nothing rather than something wrong.
std::optional<std::string> template_parameter_names(const TokenStream& stream, const source::ByteSpan& header) {
    const std::string_view text = stream.spelling(header);
    const std::size_t open = text.find('<');
    if (open == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t close = text.rfind('>');
    if (close == std::string_view::npos || close <= open) {
        return std::nullopt;
    }
    const std::string_view inside = text.substr(open + 1, close - open - 1);
    std::string names;
    std::string candidate;
    int depth = 0;
    const auto flush = [&] {
        if (candidate.empty()) {
            return false;
        }
        if (!names.empty()) {
            names += ", ";
        }
        names += candidate;
        candidate.clear();
        return true;
    };
    for (std::size_t index = 0; index <= inside.size(); ++index) {
        const char character = index < inside.size() ? inside[index] : ',';
        if (character == '<' || character == '(') {
            ++depth;
            continue;
        }
        if (character == '>' || character == ')') {
            --depth;
            continue;
        }
        if (depth != 0) {
            continue;
        }
        if (character == ',') {
            if (!flush()) {
                return std::nullopt;
            }
            continue;
        }
        // `...` introduces a pack and `=` a default argument; neither is a form
        // whose instantiation can be forced by naming the parameters.
        if (character == '.' || character == '=') {
            return std::nullopt;
        }
        if ((std::isalnum(static_cast<unsigned char>(character)) != 0) || character == '_') {
            candidate += character;
            continue;
        }
        candidate.clear();
    }
    return names.empty() ? std::nullopt : std::optional<std::string>{names};
}

} // namespace

void Projector::project_laws(const FormalScopes& formal) {
    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        const LawDeclaration& law = syntax.laws[index];
        blank(projection.runtime, law.range.span, stream);

        const Clause* proposition = law.proposition();
        if (proposition == nullptr) {
            continue;
        }

        SpecificationFunction projected{law.name, index, {}};
        Generated parameters;
        parameters.copy(stream, law.parameters);
        Generated replacement = emit(law.name, parameters, proposition->expression, law.keyword_location, law.end_line,
                                     &projected.analysis_offset, &projected.proposition_probe);

        // A precondition is a specification expression of the Law's own
        // parameters, so it is projected exactly like the conclusion, under a
        // generated name: the Law's name states what the Law concludes.
        if (const Clause* premise = law.premise(); premise != nullptr) {
            projected.premise_name = options.generated_prefix + "premise_" + std::to_string(index) +
                                     (options.unit_key.empty() ? "" : "_" + options.unit_key);
            replacement +=
                emit(projected.premise_name, parameters, premise->expression, premise->location, law.end_line);
        }

        projected.analysis_offset += formal.laws[index].size();
        edits.push_back(generated_edit(law.range.span, in_formal_scope(formal.laws[index], replacement, law.range.span),
                                       projection.specification_functions.size()));
        projection.specification_functions.push_back(std::move(projected));
    }
}

void Projector::project_proofs(const FormalScopes& formal) {
    // An instantiation argument is an ordinary C++ expression written in the
    // proof's own scope, so it is projected as a function returning it. The
    // deduced return type is the type Clang gives the expression, with no
    // conversion imposed on the way out.
    const auto emit_expression = [this](std::string_view name, const Generated& parameters,
                                        const source::ByteSpan& expression) {
        Generated head;
        head += "[[maybe_unused]] static decltype(auto) ";
        head += name;
        head += "(";
        head += parameters;
        head += ") { return (";
        head += at_written_position(stream, expression);
        head += "); }\n";
        return head;
    };

    for (std::size_t index = 0; index < syntax.proofs.size(); ++index) {
        const ProofDeclaration& proof = syntax.proofs[index];
        blank(projection.runtime, proof.range.span, stream);

        const std::string suffix = std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);

        ProofFunction projected;
        projected.name = options.generated_prefix + "proof_" + suffix;
        projected.proof_index = index;

        const std::string binding_helper = options.generated_prefix + "binding_type_" + suffix;
        Generated replacement;
        replacement += "template<class T> struct " + binding_helper + " { using type = T; };\n";
        // Decomposing a subject needs its type complete, as a member access would
        // (SPEC.md 20.4), but a subject reached through a reference never makes
        // C++ instantiate a class template specialization. Asking for `sizeof` of
        // a subject probe's result in a SFINAE context instantiates it where C++
        // can, and answers false without an error for a type that is genuinely
        // incomplete, which the provider then refuses by name.
        const std::string completion_helper = options.generated_prefix + "completes_" + suffix;
        replacement += "template<class F, class = void> struct ";
        replacement += completion_helper;
        replacement += " { static constexpr bool value = false; };\ntemplate<class R, class... A> struct ";
        replacement += completion_helper;
        replacement += "<R (*)(A...), decltype(void(sizeof(R)))> { static constexpr bool value = true; };\n";
        Generated proof_parameters;
        proof_parameters.copy(stream, proof.parameters);
        replacement +=
            emit(projected.name, proof_parameters, proof.proposition, proof.keyword_location, proof.end_line);

        const auto emit_steps = [&](auto&& self, const std::vector<ProofStatement>& statements,
                                    const Generated& parameters) -> void {
            for (const ProofStatement& statement : statements) {
                const auto expression_probe = [&](const source::ByteSpan& span, const source::SourceLocation& at,
                                                  std::vector<std::string>& names, std::string_view kind) {
                    std::string name =
                        options.generated_prefix + std::string(kind) + suffix + "_" + std::to_string(names.size());
                    replacement += line_directive(at.line, proof.keyword_location.file);
                    replacement += emit_expression(name, parameters, span);
                    replacement += line_directive(proof.end_line, proof.keyword_location.file);
                    names.push_back(std::move(name));
                };
                if (statement.kind == ProofStatementKind::Cases || statement.kind == ProofStatementKind::Decompose) {
                    expression_probe(statement.proposition, statement.location, projected.case_names, "case_");
                    const std::string subject_probe = projected.case_names.back();
                    replacement += "static_assert(";
                    replacement += completion_helper;
                    replacement += "<decltype(&";
                    replacement += subject_probe;
                    replacement += ")>::value || true);\n";
                    for (const ProofArm& arm : statement.arms) {
                        // A label that is a C++ expression is resolved by Clang,
                        // like every other expression a proof mentions. A
                        // reserved label names a state that has no expression,
                        // so there is nothing to resolve.
                        if (!arm.keyword_label)
                            expression_probe(arm.label, arm.location, projected.case_names, "case_");
                        Generated scoped = parameters;
                        for (std::size_t binding = 0; binding < arm.binders.size(); ++binding) {
                            const std::string key = options.generated_prefix + "binding_" + suffix + "_" +
                                                    std::to_string(projection.binding_probes.size());
                            projection.binding_probes.push_back({key, subject_probe, arm.spelling, binding,
                                                                 statement.kind == ProofStatementKind::Decompose,
                                                                 arm.location});
                            if (!scoped.empty())
                                scoped += ", ";
                            const auto known = options.binding_types.find(key);
                            const std::string type = known == options.binding_types.end() ? "int" : known->second;
                            // Reference parameters ask Clang to resolve expressions without
                            // requiring a copy, move, default constructor or runtime object.
                            scoped += "typename ";
                            scoped += binding_helper;
                            scoped += "<";
                            scoped += type;
                            scoped += ">::type &";
                            scoped += arm.binders[binding];
                        }
                        self(self, arm.statements, scoped);
                    }
                    continue;
                }
                // `induction x { ... }` (GRAMMAR.md 5.8). The subject is resolved
                // by Clang like a case subject, so an undeclared name is Clang's
                // error at the statement. An arm's binder, the predecessor in
                // `successor(pred)`, is declared with the subject's own type:
                // `decltype` of a parameter is its declared type, so nothing is
                // converted on the way. Which labels and binders are admitted is
                // the principle's business, decided in elaboration.
                if (statement.kind == ProofStatementKind::Induction) {
                    expression_probe(statement.proposition, statement.location, projected.case_names, "case_");
                    for (const ProofArm& arm : statement.arms) {
                        Generated scoped = parameters;
                        for (const std::string& binder : arm.binders) {
                            if (!scoped.empty())
                                scoped += ", ";
                            scoped += "typename ";
                            scoped += binding_helper;
                            scoped += "<decltype(";
                            scoped += at_written_position(stream, statement.proposition);
                            scoped += ")>::type ";
                            scoped += binder;
                        }
                        self(self, arm.statements, scoped);
                    }
                    continue;
                }
                for (const ProofArgument& argument : statement.arguments)
                    expression_probe(argument.span, argument.location, projected.argument_names, "argument_");
                if (statement.kind != ProofStatementKind::Assume)
                    continue;
                std::string name = options.generated_prefix + "assumption_" + suffix + "_" +
                                   std::to_string(projected.assumption_names.size());
                if (detail::contains_formal_syntax(stream, statement.proposition)) {
                    replacement +=
                        emit(name, parameters, statement.proposition, statement.proposition_location, proof.end_line);
                } else {
                    replacement += line_directive(statement.proposition_location.line, proof.keyword_location.file);
                    replacement += emit_expression(name, parameters, statement.proposition);
                    replacement += line_directive(proof.end_line, proof.keyword_location.file);
                }
                projected.assumption_names.push_back(std::move(name));
            }
        };
        emit_steps(emit_steps, proof.statements, proof_parameters);

        edits.push_back(
            generated_edit(proof.range.span, in_formal_scope(formal.proofs[index], replacement, proof.range.span)));
        projection.proof_functions.push_back(std::move(projected));
    }
}

void Projector::project_contracts() {
    // A contract is not C++, so it leaves both texts. What Clang is given
    // instead is an ordinary function per clause, emitted after the body so
    // that everything the contract can name is already declared. The
    // postcondition takes one parameter more than the function does: `result`,
    // of the declared return type.
    for (std::size_t index = 0; index < syntax.verified_functions.size(); ++index) {
        const VerifiedFunction& verified = syntax.verified_functions[index];
        blank(projection.runtime, verified.keyword, stream);
        blank(projection.runtime, verified.clause_region, stream);
        edits.push_back(Edit{verified.keyword, std::string(verified.keyword.length, ' ')});
        edits.push_back(Edit{verified.clause_region,
                             projection.runtime.substr(verified.clause_region.offset, verified.clause_region.length)});

        const Clause* postcondition = verified.postcondition();

        // Every probe for this function is declared under the function's own
        // template header, so a clause naming a template parameter resolves.
        template_header = verified.template_header.length == 0 ? std::string()
                                                               : std::string(stream.spelling(verified.template_header));
        template_parameters = verified.template_header.length != 0 && !verified.explicit_specialization;
        implicit_object = verified.member && !verified.static_member;

        const std::string suffix = std::to_string(index) + (options.unit_key.empty() ? "" : "_" + options.unit_key);
        std::string_view parameters = stream.spelling(verified.parameters);
        const std::size_t first = parameters.find_first_not_of(" \t\r\n");
        const std::size_t last = parameters.find_last_not_of(" \t\r\n");
        if (first != std::string_view::npos && parameters.substr(first, last - first + 1) == "void") {
            parameters = {};
        }
        const bool has_parameters = parameters.find_first_not_of(" \t\r\n") != std::string_view::npos;
        // Every probe declares the parameters without their default arguments,
        // which a call relying on one evaluates where it is made (SPEC.md
        // R.16); the function's own declaration keeps them.
        Generated parameter_list;
        if (!parameters.empty()) {
            ProbeParameters probe_parameters = without_default_arguments(stream, verified.parameters);
            if (probe_parameters.ambiguous.has_value()) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Error;
                diagnostic.category = diagnostics::Category::UnsupportedSemantics;
                diagnostic.location = *probe_parameters.ambiguous;
                diagnostic.message = "a default argument of verified function '" + verified.function_name +
                                     "' is not delimited: a comma after a '<' it leaves open may separate the "
                                     "parameters or the arguments of a template-id, which only lookup decides";
                diagnostic.notes.push_back(
                    {"parenthesize the default argument, so the parameter it belongs to ends where it does",
                     *probe_parameters.ambiguous});
                projection.diagnostics.push_back(std::move(diagnostic));
            }
            parameter_list = std::move(probe_parameters.text);
        }

        Generated result_parameter;
        if (has_parameters) {
            result_parameter += parameter_list;
            result_parameter += ", ";
        }
        const bool void_result =
            options.void_functions.contains(index) || spelled_tokens(stream, verified.return_type) == "void";
        if (void_result) {
            result_parameter = parameter_list;
        } else {
            result_parameter.copy(stream, verified.return_type);
            result_parameter += " result";
        }

        ContractFunctions projected;
        projected.function_index = index;
        projected.postcondition_name = options.generated_prefix + "ensures_" + suffix;

        // Absence of an explicit ensures is legal only when elaboration resolves
        // a refined result. Membership supplies the actual postcondition there.
        Generated replacement;
        if (postcondition != nullptr) {
            // Entry values are specified and not implemented. Left to Clang, the
            // form would call whatever `old` is visible and state the
            // post-state value in place of the entry value (SPEC.md 11.4).
            if (const auto snapshot = detail::entry_value_form(stream, postcondition->expression)) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Error;
                diagnostic.category = diagnostics::Category::UnsupportedSemantics;
                diagnostic.location = *snapshot;
                diagnostic.message = "'old(...)' in a postcondition denotes the entry value of its operand, and entry "
                                     "values are not supported yet";
                diagnostic.notes.push_back({"within a postcondition the form is never a call to a C++ entity named "
                                            "'old'; a function so named "
                                            "is called there by a qualified name, such as '::old(...)'",
                                            *snapshot});
                projection.diagnostics.push_back(std::move(diagnostic));
            }
            replacement = emit(projected.postcondition_name, result_parameter, postcondition->expression,
                               postcondition->location, verified.body_end_line);
        } else {
            replacement += "\n" + line_directive(verified.function_location.line, verified.function_location.file) +
                           declaration_prefix() + "bool " + projected.postcondition_name + "(";
            replacement += result_parameter;
            replacement += ")" + probe_qualifier() + " { return true; }\n";
        }
        for (const Clause* precondition : verified.preconditions()) {
            std::string name = options.generated_prefix + "expects_" + suffix;
            if (!projected.precondition_names.empty()) {
                name += "_" + std::to_string(projected.precondition_names.size());
            }
            replacement +=
                emit(name, parameter_list, precondition->expression, precondition->location, verified.body_end_line);
            projected.precondition_names.push_back(std::move(name));
        }
        // Each component of a `decreases` measure is a function of the
        // parameters returning it, at the type the expression already has
        // (SPEC.md TERMINATION-004). A function template's probes would need
        // forcing at each specialization; its measure is left unprojected, and
        // elaboration refuses it.
        if (const Clause* measure = verified.measure(); measure != nullptr && !templated()) {
            if (detail::contains_formal_syntax(stream, measure->expression)) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Error;
                diagnostic.category = diagnostics::Category::UnsupportedSemantics;
                diagnostic.location = measure->location;
                diagnostic.message = "formal syntax in a function measure is not supported yet";
                projection.diagnostics.push_back(std::move(diagnostic));
            }
            for (const MeasureComponent& component : measure_components(stream, *measure)) {
                std::string name = options.generated_prefix + "decreases_" + suffix + "_" +
                                   std::to_string(projected.measure_names.size());
                replacement += "\n";
                replacement += line_directive(component.location.line, component.location.file);
                replacement += declaration_prefix() + "auto " + name + "(";
                replacement += parameter_list;
                replacement += ")" + probe_qualifier() + " { return (";
                replacement += at_written_position(stream, component.expression);
                replacement += "); }\n";
                projected.measure_names.push_back(std::move(name));
            }
        }

        replacement += resume_at(stream, verified.body_end);
        edits.push_back(generated_edit(source::ByteSpan{verified.body_end, 0}, std::move(replacement)));

        // A templated function's probes are templates, and nothing has used
        // them: the specializations that would carry this specialization's
        // contract would never exist. The body names each probe at its own
        // template arguments so that instantiating the function instantiates
        // its contract with it, at the very arguments Clang substituted.
        //
        // The probes are defined after the body, so a declaration of each is
        // emitted before the function for the body to name. The reference
        // itself takes the probe's address into an unused variable: it calls
        // nothing, and the runtime text never sees it (SPEC.md TEMPLATE-001).
        if (!template_header.empty() && verified.body_open != 0) {
            if (const auto names = template_parameter_names(stream, verified.template_header); names.has_value()) {
                // An explicit specialization, `template <>`, declares no
                // parameters. Its arguments are already fixed, so its probes
                // are ordinary functions: there is no primary to specialize,
                // and nothing has to be forced into existence because the
                // declaration itself is the instantiation (SPEC.md
                // TEMPLATE-001).
                const bool specialization = !templated();
                const std::string probe_header = specialization ? std::string{} : template_header + " ";
                std::string declared = "\n";
                declared += line_directive(verified.function_location.line, verified.function_location.file);
                declared += probe_header;
                declared += "bool ";
                declared += projected.postcondition_name;
                declared += "(";
                declared += result_parameter.text;
                declared += ");";
                for (const std::string& precondition : projected.precondition_names) {
                    declared += " ";
                    declared += probe_header;
                    declared += "bool ";
                    declared += precondition;
                    declared += "(";
                    declared += parameter_list.text;
                    declared += ");";
                }
                const std::size_t before =
                    verified.template_header.length != 0 ? verified.template_header.offset : verified.keyword.offset;
                declared += resume_at(stream, before);
                edits.push_back(Edit{source::ByteSpan{before, 0}, std::move(declared)});

                if (!specialization) {
                    std::string forced = "\n";
                    forced += line_directive(verified.function_location.line, verified.function_location.file);
                    forced += "[[maybe_unused]] auto " + options.generated_prefix + "force_" + suffix + " = &" +
                              projected.postcondition_name + "<" + *names + ">;";
                    for (std::size_t position = 0; position < projected.precondition_names.size(); ++position) {
                        forced += " [[maybe_unused]] auto " + options.generated_prefix + "force_" + suffix + "_" +
                                  std::to_string(position) + " = &" + projected.precondition_names[position] + "<" +
                                  *names + ">;";
                    }
                    forced += resume_at(stream, verified.body_open);
                    edits.push_back(Edit{source::ByteSpan{verified.body_open, 0}, std::move(forced)});
                }
            }
        }
        projection.contract_functions.push_back(std::move(projected));
    }
    template_header.clear();
    template_parameters = false;
    implicit_object = false;
}

} // namespace cppl::frontend
