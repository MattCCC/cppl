#!/usr/bin/env bash
# Disables one soundness check at a time in a disposable source copy, and
# reports whether the suite noticed.
#
# A test suite that passes says nothing on its own: it may be checking that the
# kernel accepts what it should, while never checking that it rejects what it
# must. Breaking a check on purpose is how that is measured. If a mutation
# survives, some rule of the proof system is going untested (AGENTS.md 24).
#
# No mutation touches the checkout. Each one is applied to a copy, built, tested
# and thrown away. Build failures and timeouts are errors, not kills: only a
# genuine test failure counts as catching a mutation.
#
# Usage: scripts/test-mutations.sh [--only <name>]... [--jobs <n>] [--list]
#                                   [--reuse <run directory>]
#
# --reuse builds in the copy an earlier run left, brought up to date with the
# checkout, rather than in a new one: the build is incremental, and no second
# copy is written. A file the copy holds that the checkout no longer does is
# refused, since the copy would then not be the checkout.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
root=$(pwd)

# Each mutation is: name | file | before | after | ctest regex
#
# 'before' is an exact source string, matched once. Anything a mutation cannot
# find is reported before any building starts, because an anchor that no longer
# matches means that check is silently untested.
#
# A tab separates the fields, so the source strings may contain anything else.
mutations=$(cat <<'MUTATIONS'
reflexivity-equality	kernel/src/check.cpp	!(*lhs == *rhs)	false && (!(*lhs == *rhs))	^kernel_
forall-binder-match	kernel/src/check.cpp	!(introduction->binder == quantified->binder)	false && (!(introduction->binder == quantified->binder))	^kernel_
implication-premise-match	kernel/src/check.cpp	!(*introduction->premise == *implication->premise)	false && (!(*introduction->premise == *implication->premise))	^kernel_
hypothesis-proposition	kernel/src/check.cpp	!(available == proposition)	false && (!(available == proposition))	^kernel_
forall-result	kernel/src/check.cpp	!(instantiated == proposition)	false && (!(instantiated == proposition))	^kernel_
implication-result	kernel/src/check.cpp	!(*implication->conclusion == proposition)	false && (!(*implication->conclusion == proposition))	^kernel_
transport-result	kernel/src/check.cpp	!(result == proposition)	false && (!(result == proposition))	^kernel_
conditional-motive	kernel/src/check.cpp	!(instantiate(*branch->motive, selected) == proposition)	false && (!(instantiate(*branch->motive, selected) == proposition))	^kernel_
arithmetic-certificate	kernel/src/check.cpp	!refuted	false && (!refuted)	^kernel_arithmetic_test$
hypothesis-scope	kernel/src/check.cpp	if (assumed->index.value >= assumptions.size()) {	if (assumed->index.value >= assumptions.size()) { return {};	^kernel_
substitution-capture	kernel/src/substitution.cpp	return shift(argument, depth, 0);	return argument;	^kernel_
hypothesis-binder-shift	kernel/src/check.cpp	static_cast<std::uint32_t>(locals.size() - assumption.binders)	0u	^kernel_
verdict-goal-identity	compiler/obligations/src/status.cpp	!(acceptance.proposition() == relative_to(premises, obligation.goal))	false && (!(acceptance.proposition() == relative_to(premises, obligation.goal)))	^unit_verdict_test$
conjunction-side-identity	kernel/src/check.cpp	!(side == proposition)	false && (!(side == proposition))	^kernel_
conjunction-shape	kernel/src/check.cpp	const auto* conjunction = std::get_if<And>(&taken->conjunction->node);	const auto* conjunction = std::get_if<And>(&taken->conjunction->node); if (conjunction == nullptr) { return {}; }	^kernel_
disjunction-shape	kernel/src/check.cpp	const auto* disjunction = std::get_if<Or>(&cases->disjunction->node);	const auto* disjunction = std::get_if<Or>(&cases->disjunction->node); if (disjunction == nullptr) { return {}; }	^kernel_
spend-dependency-proven	compiler/automation/src/composition.cpp	dependency.has_value() && !proven_.contains(*dependency)	false && (dependency.has_value() && !proven_.contains(*dependency))	^unit_contracts_test$
erasure-span-blank	compiler/erasure/src/erase.cpp	spans_erased = false; // proof-only text left in the program	(void)spans_erased;	^unit_projection_test$
erasure-lowering-canonical	compiler/erasure/src/erase.cpp	runtime.substr(runtime_offset, lowering.expected.size()) != lowering.expected	false && (runtime.substr(runtime_offset, lowering.expected.size()) != lowering.expected)	^unit_projection_test$
law-formal-namespace-hidden	compiler/frontend/src/projection.cpp	std::string text = "\nnamespace " + name_of(path) + " {";	std::string text = "\ninline namespace " + name_of(path) + " {";	^negative_erasure$
proof-instantiation-refused	clang/src/bridge.cpp	if (!result.has_errors) {	if (false && !result.has_errors) {	^negative_proof_instantiation$
proof-instantiation-ghost	compiler/frontend/src/projection.cpp	if (begin < end) {	if (false && begin < end) {	^(unit_projection_test|negative_proof_instantiation)$
proof-instantiation-defined-probes	compiler/analysis/src/analyze.cpp	syntax.verified_functions[contract.function_index].body_open == 0)	false)	^negative_proof_instantiation$
proof-instantiation-inert	clang/src/proof_instantiation_hazards.cpp	result = !states_constraint(definition) && std::ranges::all_of(children_of(definition), inert_member);	result = !states_constraint(definition) && (std::ranges::all_of(children_of(definition), inert_member) || true);	^negative_proof_instantiation$
proof-instantiation-deduction-guide	clang/src/proof_instantiation_hazards.cpp	return functions_.contains("<deduction guide for " + spelling(declaration) + ">");	return false && functions_.contains("<deduction guide for " + spelling(declaration) + ">");	^negative_proof_instantiation$
proof-instantiation-alias	clang/src/proof_instantiation_cursors.cpp	return !states_constraint(declaration) && std::ranges::all_of(children_of(declaration), inert_alias_member);	return !states_constraint(declaration) && (std::ranges::all_of(children_of(declaration), inert_alias_member) || true);	^negative_proof_instantiation$
proof-instantiation-candidates	clang/src/proof_instantiation.cpp	if (kind_of(function) == CXCursor_FunctionTemplate) {	if (false && kind_of(function) == CXCursor_FunctionTemplate) {	^negative_proof_instantiation$
proof-instantiation-operators	clang/src/proof_instantiation.cpp	report(expression, namespace_candidates(is_operator_name));	(void)expression;	^negative_proof_instantiation$
proof-instantiation-standard-arguments	clang/src/proof_instantiation_hazards.cpp	if (argument.kind == CXType_Invalid) {	if (true || argument.kind == CXType_Invalid) {	^negative_proof_instantiation$
proof-instantiation-specializations	clang/src/proof_instantiation.cpp	checker.walk_specialization(specialization);	(void)specialization;	^negative_proof_instantiation$
proof-instantiation-restated-parameters	clang/src/proof_instantiation.cpp	if (defined || passing == CXType_LValueReference || passing == CXType_RValueReference) {	if (true || defined || passing == CXType_LValueReference || passing == CXType_RValueReference) {	^negative_proof_instantiation$
proof-instantiation-variable-template	clang/src/proof_instantiation_hazards.cpp	if (templated) {	if (false && templated) {	^negative_proof_instantiation$
bridge-refusal-category	compiler/driver/src/pipeline.cpp	return diagnostics::Category::UnsupportedSemantics;	return diagnostics::Category::CppSemantic;	^(negative_diagnostic_categories|negative_proof_instantiation|lsp_server_test)$
partial-contract-warned	compiler/driver/src/pipeline.cpp	warn_partial_contracts(program, elaborated.module, outcome.counters.closure, engine);	(void)&warn_partial_contracts;	^(e2e_partial_correctness|e2e_erasure_directives|lsp_server_test)$
partial-warning-only-partial	compiler/driver/src/pipeline.cpp	if (claim.kind != obligations::ClaimKind::Contract || claim.total) {	if (claim.kind != obligations::ClaimKind::Contract) {	^e2e_partial_correctness$
partial-callees-recorded	compiler/obligations/src/contracts.cpp	if (callee != index && !total[callee]) {	if (false && callee != index && !total[callee]) {	^(e2e_partial_correctness|e2e_cross_tu)$
partial-warning-not-for-refused	compiler/driver/src/pipeline.cpp	!function->contract->measures.empty()) {	false) {	^e2e_partial_correctness$
init-statement-detected	clang/src/statements.cpp	} else if (spelled == ";" && nesting == 1) {	} else if (false) {	^(negative_verified_paths|negative_switch_statements|e2e_if_statements)$
if-init-runs	clang/src/bridge.cpp	prefix.push_back(parts[0]);	(void)0;	^(e2e_if_statements|negative_if_statements)$
if-condition-variable-declared	clang/src/bridge.cpp	prefix.push_back(parts[condition]);	(void)0;	^e2e_if_statements$
if-constexpr-selects	clang/src/bridge.cpp	if (holds) {	if (!holds) {	^(e2e_if_statements|negative_if_statements)$
if-constexpr-only-selected	clang/src/bridge.cpp	if (!header.constant) {	if (true) {	^e2e_if_statements$
if-consteval-refused	clang/src/bridge.cpp	if (head->immediate) {	if (false) {	^negative_if_statements$
switch-init-statement-refused	clang/src/bridge.cpp	return reject("a 'switch' statement with an init-statement is not modeled");	(void)0;	^negative_switch_statements$
switch-nested-label-refused	clang/src/bridge.cpp	if (holds_switch_label(statement)) {	if (false && holds_switch_label(statement)) {	^negative_switch_statements$
switch-unreachable-prefix-refused	clang/src/bridge.cpp	if (entries.empty() && !is_switch_label(statement)) {	if (false && entries.empty() && !is_switch_label(statement)) {	^negative_switch_statements$
switch-case-range-refused	clang/src/bridge.cpp	if (!fallback && label.size() == 3) {	if (false && !fallback && label.size() == 3) {	^negative_switch_statements$
switch-fallthrough-only	clang/src/statements.cpp	return spelled == standard || spelled == qualified;	return true;	^negative_switch_statements$
switch-condition-read-once	clang/src/bridge.cpp	read.node = PlaceRef{version, anonymous_place("switch condition")};	read = *value;	^(e2e_switch_statements|negative_switch_statements)$
switch-condition-effects	clang/src/bridge.cpp	body = unknown(state, changed, std::move(body), header.statement);	(void)changed;	^(e2e_switch_statements|negative_switch_statements)$
switch-case-compares-equal	clang/src/bridge.cpp	matches.node = Binary{BinaryOp::Equal, {std::move(read), std::move(*literal)}};	matches.node = Binary{BinaryOp::NotEqual, {std::move(read), std::move(*literal)}};	^(e2e_switch_statements|negative_switch_statements|e2e_safety_subset)$
switch-default-entered	clang/src/bridge.cpp	chain = std::move(entered[static_cast<std::size_t>(fallback - entries.begin())]);	chain = lower_statements(*header.exit, state, depth + 1);	^(e2e_switch_statements|negative_switch_statements)$
switch-break-innermost	clang/src/bridge.cpp	if (!switch_frames.empty() && switch_frames.back()->loops_outside == frames.size()) {	if (!switch_frames.empty()) {	^(e2e_switch_statements|negative_switch_statements)$
switch-exit-closes-frame	clang/src/bridge.cpp	switch_frames.resize(std::min(switch_frames.size(), frame.switches_outside));	(void)frame.switches_outside;	^e2e_switch_statements$
switch-continue-closes-frames	clang/src/bridge.cpp	leave_switches_inside(frame);	(void)frame;	^e2e_switch_statements$
switch-label-reachable	clang/src/bridge.cpp	const bool labelled = next.labels != nullptr && std::ranges::contains(*next.labels, next.index);	const bool labelled = false;	^(e2e_switch_statements|e2e_safety_subset)$
switch-labels-propagate	clang/src/bridge.cpp	next.labels = from.labels;	(void)from.labels;	^e2e_switch_statements$
comma-statement-left-operand	clang/src/bridge.cpp	return lower_statement(operands[0], then, locals, depth + 1);	return lower_statements(then, locals, depth + 1);	^(e2e_switch_statements|negative_switch_statements|negative_verified_locals)$
comma-nested-refused-by-name	clang/src/bridge.cpp	if (op == CXBinaryOperator_Comma) {	if (false && op == CXBinaryOperator_Comma) {	^(negative_switch_statements|e2e_safety_subset)$
lexer-records-directives	compiler/frontend/src/lexer.cpp	directives_.push_back(Directive{source::ByteSpan{offset_, cursor - offset_}, is_marker});	(void)is_marker;	^(unit_projection_test|e2e_erasure_directives)$
erasure-blank-keeps-directives	compiler/frontend/src/projection.cpp	if (const Directive* directive = directive_at(stream, base + offset); directive != nullptr) {	if (const Directive* directive = directive_at(stream, base + offset); false && directive != nullptr) {	^(unit_projection_test|e2e_erasure_directives)$
erasure-directive-kept	compiler/erasure/src/erase.cpp	directives_kept = false; // a directive erased with the C++L around it	directives_kept = true; // a directive erased with the C++L around it	^unit_projection_test$
analysis-directives-restated	compiler/frontend/src/projection.cpp	if (directive.span.offset >= span.offset && directive.span.end() <= span.end() && !directive.line_marker) {	if (false && directive.span.offset >= span.offset && directive.span.end() <= span.end() && !directive.line_marker) {	^(unit_projection_test|e2e_erasure_directives)$
directive-inside-expression-refused	compiler/frontend/src/projection.cpp	if (!misplaced) {	if (!misplaced || true) {	^(unit_projection_test|negative_erasure)$
lowering-pads-last-line	compiler/frontend/src/projection.cpp	        text.append(written_last - lowered_last, ' ');	        text.append(0, ' ');	^(unit_projection_test|e2e_erasure_positions)$
analysis-resumes-at-column	compiler/frontend/src/projection.cpp	    resumed.append(offset - line_start, ' ');	    resumed.append(0, ' ');	^(unit_projection_test|e2e_erasure_positions)$
erasure-columns-preserved	compiler/erasure/src/erase.cpp	            columns_preserved = false;	            (void)columns_preserved;	^unit_projection_test$
lowering-moves-columns-refused	compiler/frontend/src/projection.cpp	        if (lowering_moves_columns(stream, refinement.range.span, lowering)) {	        if (false && lowering_moves_columns(stream, refinement.range.span, lowering)) {	^(unit_projection_test|negative_erasure)$
validation-in-proof-syntax-refused	compiler/frontend/src/projection.cpp	        if (in_ghost || in_proof_syntax) {	        if (false && (in_ghost || in_proof_syntax)) {	^negative_erasure$
declarator-list-ends-clauses	compiler/frontend/src/recognizer.cpp	nesting == 0 && token.is_punctuator(",")	false && (nesting == 0 && token.is_punctuator(","))	^unit_recognizer_test$
specifier-scope-is-a-name	compiler/frontend/src/recognizer.cpp	if (next >= tokens.size() || names_a_scope(tokens, index)) {	if (next >= tokens.size()) {	^unit_recognizer_test$|^conformance_words_as_cpp$
specifier-word-scope-is-a-name	compiler/frontend/src/recognizer.cpp	tokens[index].is_identifier(word) && !names_a_scope(tokens, index);	tokens[index].is_identifier(word) && true;	^unit_recognizer_test$
ghost-scope-is-a-name	compiler/frontend/src/recognizer.cpp	if (index + 1 >= tokens.size() || tokens[index + 1].kind != TokenKind::Identifier) {	if (index + 1 >= tokens.size() || (tokens[index + 1].kind != TokenKind::Identifier && !tokens[index + 1].is_punctuator("::"))) {	^unit_recognizer_test$|^conformance_words_as_cpp$
split-scope-is-a-name	compiler/frontend/src/recognizer.cpp	if (names_a_scope(tokens, index)) {	if (false && names_a_scope(tokens, index)) {	^unit_recognizer_test$
specifiers-after-type-decide-nothing	compiler/frontend/src/recognizer.cpp	(is_one_of(tokens[cursor], kSpecifiersAfterType) || tokens[cursor].is_identifier("explicit"))) {	(false || tokens[cursor].is_identifier("explicit"))) {	^unit_recognizer_test$|^conformance_words_as_cpp$
explicit-has-no-return-type	compiler/frontend/src/recognizer.cpp	if (tokens[cursor].is_identifier("explicit")) {	if (false && tokens[cursor].is_identifier("explicit")) {	^unit_recognizer_test$
operator-function-is-not-a-type	compiler/frontend/src/recognizer.cpp	tokens[cursor].is_identifier("operator") ||	false ||	^unit_recognizer_test$|^conformance_words_as_cpp$
alternative-operator-is-not-a-type	compiler/frontend/src/recognizer.cpp	is_one_of(tokens[cursor], kAlternativeOperators)) {	(false && is_one_of(tokens[cursor], kAlternativeOperators))) {	^unit_recognizer_test$|^conformance_words_as_cpp$
constructor-after-specifiers-refused	compiler/frontend/src/recognizer.cpp	while (declarator < tokens.size() && (is_one_of(tokens[declarator], kSpecifiersAfterType) ||	while (declarator < tokens.size() && (false ||	^unit_recognizer_test$
loop-body-declaration-left-to-cpp	compiler/frontend/src/recognizer.cpp	if (written.size() == 1 && declaration_follows &&	if (false && written.size() == 1 && declaration_follows &&	^unit_recognizer_test$|^conformance_words_as_cpp$
loop-declaration-needs-a-declarator	compiler/frontend/src/recognizer.cpp	parenthesized_declarator(tokens, written[0].keyword + 1, written[0].close) &&	(true || parenthesized_declarator(tokens, written[0].keyword + 1, written[0].close)) &&	^unit_recognizer_test$
loop-body-statement-is-no-initializer	compiler/frontend/src/recognizer.cpp	!holds_a_statement(tokens, cursor, body_close)) {	(true || !holds_a_statement(tokens, cursor, body_close))) {	^unit_recognizer_test$
where-after-operator-is-cpp	compiler/frontend/src/recognizer.cpp	before.kind == TokenKind::Punctuator && !before.is_punctuator("=") &&	false && before.kind == TokenKind::Punctuator && !before.is_punctuator("=") &&	^unit_recognizer_test$|^conformance_words_as_cpp$
ghost-warning-only-in-verified-body	compiler/frontend/src/recognizer.cpp	if (!in_verified_body(written.keyword)) {	if (false && !in_verified_body(written.keyword)) {	^unit_recognizer_test$|^conformance_words_as_cpp$|^e2e_ghost_state$
unsafe-warning-only-where-meant	compiler/frontend/src/recognizer.cpp	if (in_verified_body(written.keyword) ||	if (true || in_verified_body(written.keyword) ||	^unit_recognizer_test$|^conformance_words_as_cpp$
unsafe-warning-for-a-statement-block	compiler/frontend/src/recognizer.cpp	holds_a_statement(tokens, written.keyword + 1, written.terminator)) {	(false && holds_a_statement(tokens, written.keyword + 1, written.terminator))) {	^unit_recognizer_test$
module-import-may-name-any-word	compiler/frontend/src/recognizer.cpp	return module_import;	return false ? module_import : std::nullopt;	^unit_recognizer_test$|^conformance_words_as_cpp$
module-import-detected	compiler/frontend/src/recognizer.cpp	if (begins && (next.kind == TokenKind::Identifier || next.is_punctuator(":") || next.is_punctuator("<") ||	if (false && begins && (next.kind == TokenKind::Identifier || next.is_punctuator(":") || next.is_punctuator("<") ||	^unit_recognizer_test$|^conformance_words_as_cpp$
module-import-begins-a-declaration	compiler/frontend/src/recognizer.cpp	const bool begins = at == 0 || tokens[at - 1].is_punctuator(";") || tokens[at - 1].is_punctuator("}") ||	const bool begins = true || at == 0 || tokens[at - 1].is_punctuator(";") || tokens[at - 1].is_punctuator("}") ||	^unit_recognizer_test$
cppl-warnings-silenced-by-w	compiler/driver/src/driver.cpp	if (silenced && diagnostic.severity == diagnostics::Severity::Warning) {	if (false && silenced && diagnostic.severity == diagnostics::Severity::Warning) {	^conformance_words_as_cpp$
cppl-errors-never-silenced	compiler/driver/src/driver.cpp	diagnostic.severity == diagnostics::Severity::Warning) {	true) {	^conformance_words_as_cpp$
mem-initializers-hold-no-clause	compiler/frontend/src/recognizer.cpp	if (nesting == 0 && token.is_punctuator(":")) {	if (false && nesting == 0 && token.is_punctuator(":")) {	^unit_recognizer_test$|^conformance_words_as_cpp$
trailing-return-names-its-type	compiler/frontend/src/recognizer.cpp	if (nesting == 0 && !awaiting_return_type && !tokens[cursor - 1].is_punctuator("::") &&	if (nesting == 0 && (true || !awaiting_return_type) && !tokens[cursor - 1].is_punctuator("::") &&	^unit_recognizer_test$|^conformance_words_as_cpp$
qualified-clause-word-is-a-name	compiler/frontend/src/recognizer.cpp	!tokens[cursor - 1].is_punctuator("::") &&	(true || !tokens[cursor - 1].is_punctuator("::")) &&	^unit_recognizer_test$|^conformance_words_as_cpp$
statement-keyword-is-no-declarator	compiler/frontend/src/recognizer.cpp	!is_type_keyword(tokens[cursor - 1]) && !is_one_of(tokens[cursor - 1], kNotDeclaratorNames)) {	!is_type_keyword(tokens[cursor - 1]) && (true || !is_one_of(tokens[cursor - 1], kNotDeclaratorNames))) {	^unit_recognizer_test$|^conformance_words_as_cpp$
call-capability-kind	compiler/obligations/src/contracts.cpp	return candidate.kind == required.kind &&	return true &&	^negative_memory_capabilities$
call-capability-pointer	compiler/obligations/src/contracts.cpp	candidate.place.root.id == passed->parameter;	(true || candidate.place.root.id == passed->parameter);	^negative_memory_capabilities$
call-capability-extent	compiler/obligations/src/contracts.cpp	if (required.extent.empty() && !held_sized && !region.has_value()) {	if (true || (required.extent.empty() && !held_sized && !region.has_value())) {	^negative_memory_capabilities$
memory-assumption-trusted-only	compiler/elaboration/src/elaborate.cpp	    if (!declaration.trusted) {	    if (false && !declaration.trusted) {	^negative_trusted_dependencies$
verified-specifier-span	compiler/frontend/src/recognizer.cpp	verified.keyword = tokens[index].span;	verified.keyword = source::ByteSpan{tokens[specifiers_start(tokens, index)].span.offset, tokens[index].span.end() - tokens[specifiers_start(tokens, index)].span.offset};	^e2e_erasure_equivalence$
unsafe-block-havoc	clang/src/bridge.cpp	const std::vector<std::size_t> reached = unsafe_reach(state);	const std::vector<std::size_t> reached;	^negative_unsafe_boundary$
unsafe-names-escape	clang/src/bridge.cpp	lowering.escaped.insert(named);	(void)named;	^negative_unsafe_boundary$
unsafe-revokes-capabilities	clang/src/bridge.cpp	revoked_by = where;	(void)where;	^negative_unsafe_boundary$
capability-rechecked-on-reuse	clang/src/bridge.cpp	if (!held) {	if (false && !held) {	^negative_unsafe_boundary$|^negative_memory_capabilities$
capability-reuse-callable-position	clang/src/bridge.cpp	granted(signature.position(static_cast<std::size_t>(at - parameters.begin())), required);	granted(static_cast<std::uint32_t>(at - parameters.begin()), required);	^e2e_memory_capabilities$
unsafe-revokes-call-capabilities	compiler/obligations/src/contracts.cpp	scope.unsafe = location;	(void)location;	^negative_unsafe_boundary$
unsafe-control-stays-in-block	clang/src/bridge.cpp	left = leaves_block(block, 0, 0, 0)	left = (false ? leaves_block(block, 0, 0, 0) : std::optional<std::string>{})	^negative_unsafe_boundary$
unsafe-call-outside-block	compiler/elaboration/src/elaborate.cpp	if (unsafe_call != callees.end()) {	if (false && unsafe_call != callees.end()) {	^negative_unsafe_boundary$
unsafe-pure-refused	compiler/elaboration/src/elaborate.cpp	candidate.pure && contains_unsafe_region(	false && contains_unsafe_region(	^negative_unsafe_boundary$
unsafe-contract-refused	compiler/frontend/src/recognizer.cpp	if (std::size_t clause_index = 0; has_specification_clause(tokens, *name, clause_index)) {	if (std::size_t clause_index = 0; false && has_specification_clause(tokens, *name, clause_index)) {	^negative_unsafe_boundary$|^unit_recognizer_test$
unsafe-closure-through-calls	compiler/obligations/src/trust.cpp	regions[index].emplace(where, UnsafeDependency{region.location, false}).second	(false && regions[index].emplace(where, UnsafeDependency{region.location, false}).second)	^e2e_unsafe_boundary$
unsafe-not-assumption-free	compiler/driver/src/trust_report.cpp	!obligations::rests_on_unsafe_code(claim) && !obligations::rests_on_library_models(claim);	!obligations::rests_on_library_models(claim);	^e2e_unsafe_boundary$
library-model-not-assumption-free	compiler/driver/src/trust_report.cpp	!obligations::rests_on_library_models(claim);	true;	^e2e_containers$
xtu-external-dependency-listed	compiler/driver/src/driver.cpp	std::cout << "    rests on the " << imported_from(imported) << ", "	std::cout << ""	^e2e_cross_tu$|^negative_cross_tu$
xtu-import-not-assumption-free	compiler/driver/src/trust_report.cpp	return claim.imported.empty() && !obligations::rests_on_trusted_laws(claim) &&	return !obligations::rests_on_trusted_laws(claim) &&	^e2e_cross_tu$|^negative_cross_tu$|^e2e_cross_feature$|^e2e_integration_ledger$|^e2e_containers$
xtu-provenance-stated	compiler/driver/src/driver.cpp	if (summary.imports.empty() && summary.interfaces_imported == 0) {	if (true) {	^e2e_cross_tu$|^negative_cross_tu$|^e2e_containers$|^e2e_trust_report_json$
xtu-provenance-unused-interfaces	compiler/driver/src/trust_report.cpp	summary.imports.empty() && summary.interfaces_imported == 0 ? "none_imported"	summary.imports.empty() ? "none_imported"	^unit_trust_report_test$|^e2e_trust_report_json$
xtu-provenance-interfaces-counted	compiler/driver/src/driver.cpp	summary.interfaces_imported = imported_interfaces.size();	(void)imported_interfaces;	^e2e_trust_report_json$
trust-json-closure-flag	compiler/driver/src/trust_report.cpp	json.boolean(!obligations::rests_on_trusted_laws(claim));	json.boolean(true);	^unit_trust_report_test$|^e2e_trust_report_json$
trust-json-assumption-free-flag	compiler/driver/src/trust_report.cpp	json.boolean(assumption_free(claim));	json.boolean(true);	^unit_trust_report_test$|^e2e_trust_report_json$
trust-json-escaped	compiler/driver/src/trust_report.cpp	if (character == '"' || character == '\\') {	if (false) {	^unit_trust_report_test$
trust-json-displayed	compiler/driver/src/trust_report.cpp	for (const char character : artifact::displayed(value)) {	for (const char character : std::string(value)) {	^unit_trust_report_test$
trust-json-withdrawn-on-failure	compiler/driver/src/driver.cpp	            withdraw_trust_report(options.emit_trust_report);	            (void)options.emit_trust_report;	^e2e_trust_report_json$
trust-json-withdraws-only-reports	compiler/driver/src/trust_report.cpp	if (first != kTrustReportPrefix) {	if (false) {	^unit_trust_report_test$|^e2e_trust_report_json$
trust-translation-stated-text	compiler/driver/src/driver.cpp	std::cout << "Trusted translation:         not verified: C++ semantics as Clang resolves them, the bridge, "	std::cout << ""	^e2e_trust_report_json$
trust-translation-stated-json	compiler/driver/src/trust_report.cpp	json.key("verified");	json.key("verified"); json.boolean(true); json.key("stated");	^unit_trust_report_test$|^e2e_trust_report_json$
trust-json-needs-a-compile	compiler/driver/src/driver.cpp	if (!options.emit_trust_report.empty() && (options.passthrough || options.inputs.empty())) {	if (false) {	^e2e_trust_report_json$
xtu-unsafe-imported	compiler/obligations/src/contracts.cpp	recorded->entry.unsafe, recorded->entry.depends,	std::vector<artifact::UnsafeBlock>{}, recorded->entry.depends,	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
library-model-closure-through-calls	compiler/obligations/src/trust.cpp	changed = models[index].emplace(model, LibraryDependency{model, false}).second || changed;	changed = (models[index].contains(model) && false) || changed;	^e2e_containers$
container-stale-view	clang/src/bridge.cpp	if (root.version == entry.borrows->version) {	if (true) {	^negative_containers$|^negative_integration_ledger$|^negative_cross_feature$|^negative_sequence_attacks$
container-element-generation	clang/src/bridge.cpp	return !entry.formed_at.has_value() ||	return true ||	^negative_containers$|^negative_sequence_attacks$
sequence-mutator-new-generation	clang/src/bridge.cpp	effects.push_back(new_generation(state[*root], where));	(void)where;	^negative_containers$|^negative_sequence_attacks$
call-two-writable-views	clang/src/bridge.cpp	for (const std::size_t other : written_roots) {	for (const std::size_t other : std::vector<std::size_t>{}) {	^negative_sequence_boundaries$
call-span-parameter-written-twice	clang/src/bridge.cpp	if (writes && std::ranges::any_of(written_span_parameters, [&](CXCursor written_parameter) {	if (false && writes && std::ranges::any_of(written_span_parameters, [&](CXCursor written_parameter) {	^negative_sequence_boundaries$
unsafe-callee-writes-const-references	clang/src/bridge.cpp	            if (position.writable || unsafe_callee) {	            if (position.writable) {	^negative_unsafe_callees$
unsafe-callee-through-calls	clang/src/bridge.cpp	node.effects = std::ranges::any_of(node.callees, [&](const std::string& called) {	node.effects = false && std::ranges::any_of(node.callees, [&](const std::string& called) {	^negative_unsafe_callees$
unsafe-callee-from-interface	clang/src/bridge.cpp	if (current_key.empty() || imported_.contains(current_key)) {	if (current_key.empty()) {	^negative_unsafe_callees$
unsafe-callee-temporary-post-state	clang/src/bridge.cpp	                                                           std::move(declared)});	                                                           std::move(declared)}), call->effects.pop_back();	^negative_unsafe_callees$
call-statement-silent-temporaries	clang/src/bridge.cpp	if (clang_getCursorKind(inner) == CXCursor_CallExpr && temporaries_destroy_silently(statement)) {	if (clang_getCursorKind(inner) == CXCursor_CallExpr && (temporaries_destroy_silently(statement) || true)) {	^negative_sequence_generations$
compile-time-constant-expression	clang/src/bridge.cpp	if (kind == CXCursor_UnaryExpr || kind == CXCursor_RequiresExpr) {	if (false && (kind == CXCursor_UnaryExpr || kind == CXCursor_RequiresExpr)) {	^e2e_safety_subset$
static-assertion-decided-by-clang	clang/src/bridge.cpp	        if (clang_getCursorKind(declaration) == CXCursor_StaticAssert) {	        if (false && clang_getCursorKind(declaration) == CXCursor_StaticAssert) {	^e2e_safety_subset$
unsafe-callee-const-view-refined-elements	clang/src/bridge.cpp	if (const auto& held = *state[root].sequence; !held.element.refinements.empty()) {	if (const auto& held = *state[root].sequence; false && !held.element.refinements.empty()) {	^negative_unsafe_callees$
global-constant-read	clang/src/bridge.cpp	            clang_Cursor_hasVarDeclGlobalStorage(referenced) != 0) {	            clang_Cursor_hasVarDeclGlobalStorage(referenced) != 0 && false) {	^negative_global_constants$
global-constant-const-only	clang/src/bridge.cpp	            if (clang_isConstQualifiedType(declared) != 0 && clang_isVolatileQualifiedType(declared) == 0) {	            if (clang_isVolatileQualifiedType(declared) == 0) {	^negative_global_constants$
unscoped-enumeration-converts	clang/src/bridge.cpp	            if (kind == CXCursor_UnexposedExpr && integral(converted) && nested.kind == CXType_Enum &&	            if (false && kind == CXCursor_UnexposedExpr && integral(converted) && nested.kind == CXType_Enum &&	^negative_global_constants$
unscoped-enumeration-modeled	clang/src/bridge.cpp	const bool opaque_enumeration = clang_Cursor_isNull(definition) != 0;	const bool opaque_enumeration = clang_EnumDecl_isScoped(declaration) == 0 || clang_Cursor_isNull(definition) != 0;	^negative_global_constants$|^negative_proof_cases$
boolean-conversion-direction	clang/src/bridge.cpp	    if (type.kind == TypeKind::Bool) {	    if (type.kind != TypeKind::Bool) {	^negative_boolean_conversions$
integer-to-boolean-nonzero	clang/src/bridge.cpp	nonzero.node = Binary{BinaryOp::NotEqual, {std::move(operand), std::move(zero)}};	nonzero.node = Binary{BinaryOp::Equal, {std::move(operand), std::move(zero)}};	^negative_boolean_conversions$
boolean-conversion-implicit	clang/src/bridge.cpp	if (kind == CXCursor_UnexposedExpr && boolean_pair(original, converted)) {	if (false && kind == CXCursor_UnexposedExpr && boolean_pair(original, converted)) {	^negative_boolean_conversions$
boolean-conversion-written	clang/src/bridge.cpp	            if (destination.kind == TypeKind::Bool && source.kind == TypeKind::Bool) {	            if (false && destination.kind == TypeKind::Bool && source.kind == TypeKind::Bool) {	^negative_boolean_conversions$
requires-expression-constant	clang/src/bridge.cpp	if (kind == CXCursor_UnaryExpr || kind == CXCursor_RequiresExpr) {	if (kind == CXCursor_UnaryExpr || false) {	^e2e_safety_subset$
label-names-statement	clang/src/bridge.cpp	        if (kind == CXCursor_LabelStmt) {	        if (false && kind == CXCursor_LabelStmt) {	^e2e_safety_subset$
reference-binds-temporary	clang/src/bridge.cpp	const bool binds_temporary = reference && is_prvalue(initializer);	const bool binds_temporary = false && reference && is_prvalue(initializer);	^e2e_safety_subset$
linkage-specification-is-namespace-scope	compiler/frontend/src/recognizer.cpp	if (brace >= 2 && tokens[brace - 1].kind == TokenKind::StringLiteral && tokens[brace - 2].is_identifier("extern")) {	if (false && brace >= 2 && tokens[brace - 1].kind == TokenKind::StringLiteral && tokens[brace - 2].is_identifier("extern")) {	^e2e_safety_subset$
call-statement-temporaries-mutator-only	clang/src/bridge.cpp	if (clang_getCursorKind(inner) == CXCursor_CallExpr && sequence_call(inner).has_value()) {	if (clang_getCursorKind(inner) == CXCursor_CallExpr) {	^negative_sequence_generations$
call-writable-span-havocs-pointees	clang/src/bridge.cpp	return convert_type(canonical).representation.kind == source::RepresentationKind::Span &&	return false && convert_type(canonical).representation.kind == source::RepresentationKind::Span &&	^negative_sequence_boundaries$
unsafe-reaches-viewed-container	clang/src/bridge.cpp	reach(*held->views);	(void)held;	^negative_sequence_attacks$
unsafe-reaches-whole-object	clang/src/bridge.cpp	reach(other);	(void)other;	^negative_sequence_attacks$
unsafe-refined-container-refused	clang/src/bridge.cpp	held.has_value() && !held->element.refinements.empty()) {	held.has_value() && false && !held->element.refinements.empty()) {	^negative_sequence_attacks$|^negative_sequence_boundaries$
hidden-refinement-spelling-refused	clang/src/bridge.cpp	if (written.kind == CXType_Unexposed && unnamed &&	if (false && written.kind == CXType_Unexposed && unnamed &&	^negative_refinement_types$
template-argument-default-refinement	clang/src/bridge.cpp	if (auto by_default = defaulted(named)) {	if (auto by_default = std::optional<RefinedTemplateArgument>{}) {	^negative_refinement_types$
template-argument-decltype-refinement	clang/src/bridge.cpp	if (kind == CXCursor_DeclRefExpr && user_template.has_value()) {	if (false && kind == CXCursor_DeclRefExpr && user_template.has_value()) {	^negative_refinement_types$
template-specializations-indexed	clang/src/bridge.cpp	collector.specializations.push_back(specialization);	(void)specialization;	^negative_template_identity$
template-index-resolves-specialization	clang/src/bridge.cpp	const CXCursor referenced = clang_getCursorReferenced(reference->cursor);	const CXCursor referenced = reference->referencedEntity->cursor;	^negative_template_identity$
default-argument-evaluated-at-call	clang/src/bridge.cpp	if (is_default_argument(argument)) {	if (false && is_default_argument(argument)) {	^e2e_default_arguments$|^negative_default_arguments$
default-argument-refusal-named	clang/src/bridge.cpp	refused->reason = "the default argument of " + owner +	refused->reason = std::string() +	^negative_default_arguments$
default-argument-reference-refused	clang/src/bridge.cpp	if (is_default_argument(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)))) {	if (false && is_default_argument(clang_Cursor_getArgument(cursor, static_cast<unsigned>(index)))) {	^negative_default_arguments$
default-argument-ghost-effects	clang/src/bridge.cpp	if (std::optional<std::string> found = ghost_effect(*initializer, depth + 1)) {	if (std::optional<std::string> found = std::nullopt) {	^negative_default_arguments$
default-argument-ghost-calls	clang/src/bridge.cpp	collect_calls(*initializer, ghost, depth + 1);	(void)initializer;	^negative_default_arguments$
probe-parameters-without-defaults	compiler/frontend/src/projection.cpp	} else if (depth == 0 && !in_default && token.is_punctuator("=")) {	} else if (false && depth == 0 && !in_default && token.is_punctuator("=")) {	^unit_default_arguments_test$|^e2e_default_arguments$
probe-default-not-delimited	compiler/frontend/src/projection.cpp	if (angles != 0 && !parameters.ambiguous.has_value()) {	if (false && angles != 0 && !parameters.ambiguous.has_value()) {	^unit_default_arguments_test$|^negative_default_arguments$
contract-probe-parameter-count	compiler/elaboration/src/elaborate.cpp	bool agrees = probe->parameters.size() ==	bool agrees = true || probe->parameters.size() ==	^unit_default_arguments_test$
contract-probe-parameter-types	compiler/elaboration/src/elaborate.cpp	const clangbridge::Type& stated = probe->parameters[index].type;	const clangbridge::Type& stated = function.parameters[index].type;	^unit_default_arguments_test$
range-for-initialization-refused	clang/src/bridge.cpp	if (range_for_initializes(statement)) {	if (false && range_for_initializes(statement)) {	^negative_range_for$
range-for-invariant-before-variable	clang/src/bridge.cpp	named_declarations(initializer).contains(clang_hashCursor(header.range->variable))) {	false) {	^negative_range_for$
range-for-range-is-a-name	clang/src/bridge.cpp	if (clang_getCursorKind(named) != CXCursor_DeclRefExpr) {	if (false) {	^negative_range_for$|^negative_sequence_boundaries$|^negative_verified_loops$
range-for-unmodeled-range-refused	clang/src/bridge.cpp	return reject("the range of the range-based for at " + where + " is '" + range.range + "' of type '" +	(void)std::string("the range of the range-based for at " + where + " is '" + range.range + "' of type '" +	^negative_range_for$
range-for-aggregate-elements-refused	clang/src/bridge.cpp	held.kind != CXType_Invalid && elements.kind != TypeKind::Int && elements.kind != TypeKind::Bool) {	false) {	^negative_range_for$
range-for-span-parameter-reference	clang/src/bridge.cpp	if (range.reference && range.sequence && !range.region.root.has_value()) {	if (false) {	^negative_range_for$
range-for-storage-kept	clang/src/bridge.cpp	locals[root].version == frame.head[root].version) {	true) {	^negative_range_for$|^negative_sequence_attacks$
range-for-element-alias-carried	clang/src/bridge.cpp	        if (!range.writable) {	        if (true) {	^e2e_range_for$
range-for-generated-measure	clang/src/bridge.cpp	measures.push_back(range_measure(*header.range, frame.head));	(void)frame;	^e2e_range_for$
range-for-condition-bounds-position	clang/src/bridge.cpp	below.op = BinaryOp::Less;	below.op = BinaryOp::LessEqual;	^e2e_range_for$
operator-parameters-after-operator	compiler/frontend/src/recognizer.cpp	return at + 2;	return name + 1;	^unit_recognizer_test$|^negative_verified_methods$
operator-conversion-function-refused	compiler/frontend/src/recognizer.cpp	if (operator_function && conversion_function(tokens, *name)) {	if (false && conversion_function(tokens, *name)) {	^unit_recognizer_test$
call-element-beside-reallocatable-container	clang/src/bridge.cpp	(container.storage != owner && !may_alias(holder, state[owner]))) {	true || (container.storage != owner && !may_alias(holder, state[owner]))) {	^negative_sequence_boundaries$
deref-symbolic-index-overlaps	clang/src/bridge.cpp	if (other.has_symbolic_step() || target.has_symbolic_step()) {	if (false && (other.has_symbolic_step() || target.has_symbolic_step())) {	^negative_memory_capabilities$
record-user-destructor-unmodeled	clang/src/bridge.cpp	                if (has_user_provided_destructor(definition)) {	                if (false && has_user_provided_destructor(definition)) {	^negative_verified_methods$
format-keeps-directives	compiler/formatter/src/format.cpp	return spans_overlap(edit.span, source::ByteSpan{begin, end - begin});	return false && spans_overlap(edit.span, source::ByteSpan{begin, end - begin});	^formatter_test$
lsp-meaning-option-unverified	src/lsp/src/compile_commands.cpp	if (argument.starts_with("-f") && !passed_on(argument) && !ignorable_flag(argument)) {	if (false && argument.starts_with("-f") && !passed_on(argument) && !ignorable_flag(argument)) {	^lsp_compile_commands_test$
template-argument-refinement-use-site	clang/src/bridge.cpp	collector.refused_arguments.emplace_back(refined_template_argument_refusal(*refined), cursor);	(void)refined;	^negative_refinement_types$
template-argument-refinement-verified-body	clang/src/bridge.cpp	found.refused_arguments.emplace_back(refined_template_argument_refusal(*refined), cursor);	(void)refined;	^negative_refinement_types$
template-argument-refinement-through-alias	clang/src/bridge.cpp	if (auto hidden = refined_template_argument(named, selection, depth + 1)) {	if (auto hidden = std::optional<RefinedTemplateArgument>{}) {	^negative_refinement_types$
unsafe-block-new-generation	clang/src/bridge.cpp	new_generation(state[index], "the unsafe block at " + at);	(void)state[index];	^negative_containers$|^negative_sequence_attacks$
container-span-capability	clang/src/bridge.cpp	if (!region.parameter.has_value() || granted(*region.parameter, required)) {	if (true) {	^negative_containers$
container-call-disjointness	clang/src/bridge.cpp	if (other == root || may_alias(state[other], state[root])) {	if (false && (other == root || may_alias(state[other], state[root]))) {	^negative_containers$
span-copy-hands-storage	clang/src/bridge.cpp	return handed_storage(call->arguments.front(), state);	return std::nullopt;	^negative_sequence_boundaries$
container-refined-writable-view	clang/src/bridge.cpp	if (!state[root].sequence->element.refinements.empty()) {	if (false && !state[root].sequence->element.refinements.empty()) {	^negative_containers$
container-mutable-call-aliases	clang/src/bridge.cpp	!may_alias(state[target], state[other])) {	true) {	^negative_containers$|^negative_cross_feature$
container-copy-refinement	clang/src/bridge.cpp	if (auto gap = refinement_gap(root, declaring[*origin])) {	if (auto gap = refinement_gap(root, declaring[*origin]); false) {	^negative_containers$
container-refined-mutable-reference	clang/src/bridge.cpp	if (handed.sequence.has_value() && !handed.sequence->element.refinements.empty()) {	if (false) {	^negative_containers$
container-element-beside-view	clang/src/bridge.cpp	if (root == owner || may_alias(state[root], state[owner])) {	if (false) {	^negative_containers$
container-refined-result	clang/src/bridge.cpp	if (const bool refined_result = !element->refinements.empty(); refined_result) {	if (const bool refined_result = false; refined_result) {	^negative_containers$
container-refined-std-array	clang/src/bridge.cpp	if (!stated || !stated->empty()) {	if (false) {	^negative_containers$
container-refined-span-local	clang/src/bridge.cpp	!written || !written->refinements.empty()) {	false) {	^negative_containers$
capability-const-writable	clang/src/bridge.cpp	if (capability.kind == Capability::Kind::Writable && clang_isConstQualifiedType(element) != 0)	if (false && capability.kind == Capability::Kind::Writable && clang_isConstQualifiedType(element) != 0)	^negative_containers$
container-pop-precondition	compiler/obligations/src/library.cpp	summary.preconditions.push_back(	(void)(	^negative_containers$
validation-fact-conditional	compiler/obligations/src/contracts.cpp	kernel::Proposition::implication(kernel::predicate(result, true), std::move(holds)),	std::move(holds),	^negative_runtime_validation$
validation-fact-polarity	compiler/obligations/src/contracts.cpp	kernel::Proposition::implication(kernel::predicate(result, true), std::move(holds)),	kernel::Proposition::implication(kernel::predicate(result, false), std::move(holds)),	^negative_runtime_validation$|^e2e_runtime_validation$
validation-site-seeded	compiler/obligations/src/trust.cpp	for (const ValidationSite& found : contract.validations) {	for (const ValidationSite& found : std::vector<ValidationSite>{}) {	^e2e_runtime_validation$|^unit_trust_closure_test$
validation-undefined-predicate	compiler/obligations/src/contracts.cpp	if (stated->unvalidatable.has_value()) {	if (false && stated->unvalidatable.has_value()) {	^negative_runtime_validation$
validation-loop-clause	compiler/frontend/src/recognizer.cpp	if (in_loop_clause(keyword.span.offset)) {	if (false && in_loop_clause(keyword.span.offset)) {	^negative_runtime_validation$
trust-proof-uses-reported	compiler/obligations/src/trust.cpp	proofs_used(written_uses(obligation), closure.claims.back().uses);	(void)written_uses(obligation);	^e2e_trust_report_json$|^unit_trust_closure_test$
trust-contract-uses-reported	compiler/obligations/src/trust.cpp	std::ranges::move(called, std::back_inserter(closure.claims.back().uses));	(void)called;	^e2e_trust_report_json$|^unit_trust_closure_test$
proof-uses-recorded	compiler/obligations/src/generate.cpp	written.uses = std::move(uses);	(void)uses;	^e2e_trust_report_json$
runtime-check-closure-through-calls	compiler/obligations/src/trust.cpp	changed = sites[index].emplace(where, std::move(reached)).second || changed;	changed = (false && sites[index].emplace(where, std::move(reached)).second) || changed;	^e2e_runtime_validation$|^unit_trust_closure_test$
runtime-check-imported-closure	compiler/obligations/src/trust.cpp	recorded.runtime};	std::vector<artifact::RuntimeCheck>{}};	^e2e_runtime_validation$
runtime-check-exported	compiler/obligations/src/interface.cpp	entry.runtime.push_back(artifact::RuntimeCheck{	(void)(artifact::RuntimeCheck{	^e2e_runtime_validation$
runtime-check-carried-on	compiler/obligations/src/interface.cpp	entry.runtime.insert(entry.runtime.end(), imported.runtime.begin(), imported.runtime.end());	(void)imported.runtime;	^e2e_runtime_validation$
runtime-check-interface-identity	compiler/artifact/src/interface.cpp	{"runtime", unique(entry.runtime, runtime_identity)},	{"runtime", unique(decltype(entry.runtime){}, runtime_identity)},	^unit_interface_test$|^e2e_runtime_validation$
container-default-allocator	clang/src/bridge.cpp	if (!is_standard_template(held, "allocator") || !inert_allocator(call.arguments.back(), 0)) {	if (held.kind != CXType_Invalid || true) {	^e2e_containers$
conjoined-capability-detected	compiler/elaboration/src/elaborate.cpp	return !function.capabilities.empty() && function.returned_value.has_value();	return function.capabilities.empty() && false;	^negative_containers$
conjoined-capability-postcondition	compiler/elaboration/src/elaborate.cpp	if (function != nullptr && !capabilities_read_apart && conjoins_capabilities(*function)) {	if (false && !capabilities_read_apart) {	^negative_containers$
conjoined-capability-trusted-law	compiler/elaboration/src/elaborate.cpp	report_conjoined_capabilities(engine, declaration.keyword_location, "law '" + declaration.name + "'");	elaborate_memory_assumption(request, specification, declaration, *function, next_expression_id, result, engine);	^negative_containers$
ghost-runtime-use	clang/src/bridge.cpp	            if (is_ghost(referenced)) {	            if (false && is_ghost(referenced)) {	^negative_ghost_state$
ghost-initializer-effect	clang/src/bridge.cpp	if (const std::optional<std::string> effect = ghost_effect(initializer, 0)) {	if (const std::optional<std::string> effect = (false ? ghost_effect(initializer, 0) : std::optional<std::string>{})) {	^negative_ghost_state$
ghost-initializer-pure	compiler/elaboration/src/elaborate.cpp	            if (!pure_symbols.contains(call.callee_usr)) {	            if (false && !pure_symbols.contains(call.callee_usr)) {	^negative_ghost_state$
ghost-scalar-type	clang/src/bridge.cpp	        if (!ghost_scalar(type)) {	        if (false && !ghost_scalar(type)) {	^negative_ghost_state$
ghost-erased-whole	compiler/erasure/src/erase.cpp	        spans.push_back(ghost.erased);	        spans.push_back(ghost.keyword);	^e2e_ghost_state$
generated-prefix-reserved	compiler/driver/src/pipeline.cpp	token.text.starts_with(projection_options.generated_prefix)	false && token.text.starts_with(projection_options.generated_prefix)	^negative_ghost_state$
recursive-call-descent-owed	compiler/obligations/src/contracts.cpp	            if (std::ranges::find(recursion_, found->second) != recursion_.end()) {	            if (false && std::ranges::find(recursion_, found->second) != recursion_.end()) {	^negative_termination$
recursion-needs-measure	compiler/obligations/src/contracts.cpp	            if (function.contract->measures.empty()) {	            if (false && function.contract->measures.empty()) {	^negative_termination$
recursion-group-established-whole	compiler/automation/src/composition.cpp	} else if (std::ranges::all_of(group, proven_whole)) {	} else if (proven_whole(condition->second.contract)) {	^negative_termination$
totality-unmeasured-loop	compiler/obligations/src/contracts.cpp	total[index] = contract.unmeasured_loops.empty() && contract.unsafe_regions.empty();	total[index] = true;	^negative_termination$
totality-through-callees	compiler/obligations/src/contracts.cpp	            if (total[index] &&	            if (false && total[index] &&	^negative_termination$
lexicographic-first-stays	compiler/obligations/src/contracts.cpp	compare(kernel::PrimOp::Equal, index)	compare(kernel::PrimOp::GreaterEqual, index)	^negative_refused_declarations$|^negative_termination$
do-loop-exit-decided	clang/src/bridge.cpp	        if (!frame.condition_last) {	        if (true) {	^negative_termination$
xtu-statement-compared	compiler/obligations/src/contracts.cpp	if (!(*plan.interface_statement == recorded->entry.statement)) {	if (false && !(*plan.interface_statement == recorded->entry.statement)) {	^negative_cross_tu$|^unit_cross_unit_contracts_test$
xtu-imported-established	compiler/automation/src/composition.cpp	    if (function.imported.has_value()) {	    if (function.imported.has_value() && false) {	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
xtu-imported-totality	compiler/obligations/src/contracts.cpp	total[index] = contract.total;	total[index] = true;	^negative_cross_tu$|^unit_cross_unit_contracts_test$
xtu-partial-record-refused-with-measure	compiler/obligations/src/contracts.cpp	if (!total && function.contract.has_value() && !function.contract->measures.empty()) {	if (false && !total && function.contract.has_value() && !function.contract->measures.empty()) {	^unit_cross_unit_contracts_test$
xtu-recursion-refused	compiler/obligations/src/contracts.cpp	if (through_record && cyclic) {	if (through_record && cyclic && component.empty()) {	^unit_cross_unit_contracts_test$
xtu-recursion-through-records	compiler/obligations/src/contracts.cpp	const bool through_record =	const bool through_record = false &&	^unit_cross_unit_contracts_test$
xtu-recursion-self-dependency	compiler/obligations/src/contracts.cpp	std::ranges::find(graph[component.front()], component.front()) !=	std::ranges::find(graph[component.front()], graph.size()) !=	^unit_cross_unit_contracts_test$
xtu-recursion-record-edges	compiler/obligations/src/contracts.cpp	graph[node].push_back(target);	graph[node].reserve(target);	^unit_cross_unit_contracts_test$
xtu-cycle-record-withdrawn	compiler/obligations/src/contracts.cpp	withdrawn.insert(usr);	(void)usr;	^unit_cross_unit_contracts_test$
xtu-compiler-version-compared	compiler/driver/src/interface_io.cpp	if (recorded.compiler != current.compiler) {	if (false) {	^negative_cross_tu$
xtu-models-known	compiler/driver/src/interface_io.cpp	unusable = unknown_model(*recorded);	unusable = std::nullopt; (void)&unknown_model;	^negative_cross_tu$
xtu-models-known-by-name	compiler/obligations/src/interface.cpp	known.identity == model.identity && known.name == model.name	known.identity == model.identity	^negative_cross_tu$
xtu-result-identity-dependencies	compiler/artifact/src/interface.cpp	{"depends", unique(entry.depends, dependency_line)}	{"depends", std::vector<std::string>{}}	^unit_interface_test$
xtu-internal-linkage-not-imported	compiler/elaboration/src/elaborate.cpp	converted.defined_elsewhere = candidate.contract != nullptr && function->external_linkage;	converted.defined_elsewhere = candidate.contract != nullptr;	^negative_cross_tu$
xtu-internal-linkage-not-exported	compiler/obligations/src/contracts.cpp	    if (function.external_linkage) {	    if (true) {	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
xtu-restatements-agree	compiler/obligations/src/contracts.cpp	if (!restatements_agree(function, pure_definitions, program, engine)) {	if (false && !restatements_agree(function, pure_definitions, program, engine)) {	^negative_cross_tu$|^unit_cross_unit_contracts_test$
xtu-trusted-through-import	compiler/obligations/src/trust.cpp	return !claim.premises.empty() || std::ranges::any_of(claim.imported	return !claim.premises.empty() || std::ranges::any_of(std::vector<ImportedDependency>{}	^e2e_cross_tu$|^e2e_integration_ledger$
xtu-unsafe-through-import	compiler/obligations/src/trust.cpp	return !claim.unsafe.empty() || std::ranges::any_of(claim.imported	return !claim.unsafe.empty() || std::ranges::any_of(std::vector<ImportedDependency>{}	^e2e_cross_tu$|^e2e_integration_ledger$|^e2e_cross_feature$
xtu-imported-closure-through-calls	compiler/obligations/src/trust.cpp	changed = through[index].emplace(imported, false).second || changed;	(void)imported;	^e2e_cross_tu$
xtu-configuration-compared	compiler/driver/src/interface_io.cpp	std::optional<std::string> unusable = configuration_difference(recorded->configuration, configuration);	std::optional<std::string> unusable = false ? configuration_difference(recorded->configuration, configuration) : std::nullopt;	^negative_cross_tu$
xtu-stale-sources	compiler/driver/src/interface_io.cpp	if (std::optional<std::string> stale = stale_source(*recorded, digests)) {	if (std::optional<std::string> stale = (false ? stale_source(*recorded, digests) : std::nullopt)) {	^negative_cross_tu$
xtu-dependencies-imported	compiler/driver/src/interface_io.cpp	return found == accepted.end() || !(found->second.identity == d.entry);	return false && (found == accepted.end() || !(found->second.identity == d.entry));	^negative_cross_tu$
xtu-conflicting-records	compiler/driver/src/interface_io.cpp	if (added || existing->second.identity == imported.identity) {	if (true) {	^negative_cross_tu$
xtu-withdrawn-on-failure	compiler/driver/src/interface_io.cpp	    std::filesystem::remove(path, error);	    (void)path;	^negative_cross_tu$
xtu-withdraw-only-interfaces	compiler/driver/src/interface_io.cpp	if (first != std::string(artifact::kMagic) + " ") {	if (false && first != std::string(artifact::kMagic) + " ") {	^negative_cross_tu$
xtu-status-proven-only	compiler/artifact/src/interface.cpp	if (status->fields[1] != "proven") {	if (false) {	^unit_interface_test$|^negative_cross_tu$
xtu-checksum-verified	compiler/artifact/src/interface.cpp	if (!(source::hash_bytes(text.substr(0, last_start)) == *recorded_checksum)) {	if (false) {	^unit_interface_test$|^negative_cross_tu$|^fuzz_interface_replay$
xtu-canonical-order	compiler/artifact/src/interface.cpp	if (!previous.empty() && !(previous < line.text)) {	if (false && !previous.empty() && !(previous < line.text)) {	^unit_interface_test$
xtu-pure-identity-transitive	compiler/obligations/src/interface.cpp	for (std::size_t position = 0; position < order_.size(); ++position) {	for (std::size_t position = 0, reached = order_.size(); position < reached; ++position) {	^negative_cross_tu$
xtu-measure-as-request	compiler/obligations/src/interface.cpp	if (measures == Measures::Requested) {	if (false) {	^negative_cross_tu$|^unit_cross_unit_contracts_test$
xtu-termination-request-identified	compiler/obligations/src/interface.cpp	hasher.update_u8(contract.measures.empty() ? 0 : 1);	hasher.update_u8(0);	^negative_cross_tu$|^unit_cross_unit_contracts_test$
xtu-entry-identity-by-meaning	compiler/artifact/src/interface.cpp	hasher.update_field(entry.symbol);	hasher.update_field(entry.symbol + entry.name);	^negative_cross_tu$|^unit_interface_test$
xtu-semantics-compared	compiler/driver/src/interface_io.cpp	if (recorded.semantics != current.semantics) {	if (false) {	^negative_cross_tu$
xtu-verifier-digest-compared	compiler/driver/src/interface_io.cpp	if (!(recorded.verifier == current.verifier)) {	if (false) {	^negative_cross_tu$
semantics-digest-covers-kernel	cmake/VerifierSemanticsSources.cmake	    kernel	    kernel_left_out	^architecture_verifier_semantics$
semantics-sources-classified	cmake/CheckVerifierSemantics.cmake	if(problems)	if(FALSE)	^architecture_verifier_semantics$
file-length-limit	cmake/ci/CheckFileLength.cmake	elseif(lines GREATER CPPL_FILELENGTH_LIMIT)	elseif(lines GREATER 1001)	^ci_filelength
file-length-oversized-growth	cmake/ci/CheckFileLength.cmake	if(lines GREATER recorded)	if(FALSE)	^ci_filelength
version-reports-verifier-digest	compiler/driver/src/version.cpp	verifier.bytes = kVerifierSemanticsDigest;	verifier.bytes = {};	^integration_release_metadata$
version-reports-semantics	compiler/driver/src/version.cpp	line("Verification semantics:", obligations::kVerificationSemanticsVersion);	line("Verification semantics:", "cppl-verification-0");	^integration_release_metadata$
xtu-report-escapes-recorded-text	compiler/artifact/src/interface.cpp	if (byte >= 0x20U && byte <= 0x7EU && byte != '%') {	if (true) {	^unit_interface_test$|^negative_cross_tu$
xtu-format-version	compiler/artifact/include/cppl/artifact/interface.hpp	inline constexpr std::uint32_t kFormatVersion = 3;	inline constexpr std::uint32_t kFormatVersion = 2;	^unit_interface_test$|^negative_cross_tu$
xtu-refusal-escapes-source	compiler/driver/src/interface_io.cpp	const std::string shown = artifact::displayed(file.path);	const std::string shown = file.path;	^negative_cross_tu$
xtu-refusal-escapes-unit	compiler/driver/src/interface_io.cpp	"rebuild '" + artifact::displayed(recorded->unit) +	"rebuild '" + recorded->unit +	^negative_cross_tu$
xtu-refusal-escapes-conflict	compiler/driver/src/interface_io.cpp	artifact::displayed(entry.name)	entry.name	^negative_cross_tu$
xtu-refusal-escapes-dependency	compiler/driver/src/interface_io.cpp	artifact::displayed(broken->symbol)	broken->symbol	^negative_cross_tu$
lsp-interface-language-mode	compiler/driver/src/buffer_compile.cpp	selected_standard(request.clang_arguments)	std::string{}	^lsp_interfaces_test$
language-mode-joined	compiler/driver/src/options.cpp	standard = std::move(*joined);	(void)joined;	^negative_cross_tu$|^lsp_interfaces_test$
language-mode-double-dash	compiler/driver/src/options.cpp	{std::string_view("-std="), std::string_view("--std=")}	{std::string_view("-std=")}	^negative_cross_tu$|^lsp_interfaces_test$
language-mode-separate	compiler/driver/src/options.cpp	if (argument == "--std") {	if (false) {	^negative_cross_tu$|^lsp_interfaces_test$
lsp-imported-contract-recorded	compiler/driver/src/pipeline.cpp	                record.imported.push_back(ImportedRecord{imported.name, imported.origin});	                (void)imported;	^lsp_interfaces_test$
lsp-lens-names-imported-contract	src/lsp/src/verification.cpp	part(rests.imported, "the imported contract of ", "the imported contracts of ");	(void)rests.imported;	^lsp_interfaces_test$
lsp-lens-names-trusted-laws	src/lsp/src/verification.cpp	part(rests.premises, "trusted ", "trusted ");	(void)rests.premises;	^lsp_verification_test$
lsp-lens-names-models	src/lsp/src/verification.cpp	part(models, "", "");	(void)models;	^lsp_verification_test$
lsp-lens-names-unsafe	src/lsp/src/verification.cpp	part(rests.unsafe, "the unsafe block at ", "the unsafe blocks at ");	(void)rests.unsafe;	^lsp_verification_test$
lsp-lens-names-validations	src/lsp/src/verification.cpp	part(rests.validations, "the runtime validation of ", "the runtime validations of ");	(void)rests.validations;	^lsp_verification_test$
lsp-premises-through-uses	compiler/driver/src/pipeline.cpp	for (const obligations::TrustedPremise& premise : claim.premises) {	for (const obligations::TrustedPremise& premise : std::vector<obligations::TrustedPremise>{}) {	^lsp_verification_test$
lsp-models-recorded	compiler/driver/src/pipeline.cpp	add(record.models, std::string(source::describe_model(library.model)));	(void)library;	^lsp_verification_test$
lsp-unsafe-recorded	compiler/driver/src/pipeline.cpp	add(record.unsafe, at(block.location.file, block.location.line));	(void)block;	^lsp_verification_test$
lsp-validations-recorded	compiler/driver/src/pipeline.cpp	add(record.validations, site.refinement + " at " + at(site.location.file, site.location.line));	(void)site;	^lsp_verification_test$
lsp-hover-states-translation	src/lsp/src/verification.cpp	markdown += "\nPROVEN relative to the translation from C++ to the core, which is trusted and not verified.\n";	(void)markdown;	^lsp_verification_test$
xtu-models-written	compiler/artifact/src/interface.cpp	canonical_lines(entry.models, model_line)	canonical_lines(std::vector<Model>{}, model_line)	^unit_interface_test$|^e2e_containers$
xtu-models-exported	compiler/obligations/src/interface.cpp	entry.models.push_back(library_model(dependency.model));	(void)dependency;	^e2e_containers$|^unit_cross_unit_contracts_test$
xtu-models-carried	compiler/obligations/src/interface.cpp	entry.models.insert(entry.models.end(), imported.models.begin(), imported.models.end());	(void)imported.models;	^e2e_containers$|^unit_cross_unit_contracts_test$
xtu-models-imported	compiler/obligations/src/contracts.cpp	recorded->entry.models, recorded->entry.unsafe	std::vector<artifact::Model>{}, recorded->entry.unsafe	^e2e_containers$|^unit_cross_unit_contracts_test$
xtu-models-reported	compiler/obligations/src/trust.cpp	return !claim.library.empty() || std::ranges::any_of(claim.imported	return !claim.library.empty() || std::ranges::any_of(std::vector<ImportedDependency>{}	^e2e_containers$|^unit_cross_unit_contracts_test$
xtu-laws-exported	compiler/obligations/src/interface.cpp	for (const TrustedPremise& premise : claim.premises) {	for (const TrustedPremise& premise : std::vector<TrustedPremise>{}) {	^e2e_cross_tu$
xtu-unsafe-exported	compiler/obligations/src/interface.cpp	for (const UnsafeDependency& dependency : claim.unsafe) {	for (const UnsafeDependency& dependency : std::vector<UnsafeDependency>{}) {	^e2e_cross_tu$
xtu-laws-carried	compiler/obligations/src/interface.cpp	entry.premises.insert(entry.premises.end(), imported.premises.begin(), imported.premises.end());	(void)imported.premises;	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
xtu-unsafe-carried	compiler/obligations/src/interface.cpp	entry.unsafe.insert(entry.unsafe.end(), imported.unsafe.begin(), imported.unsafe.end());	(void)imported.unsafe;	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
xtu-dependency-exported	compiler/obligations/src/interface.cpp	entry.depends.push_back(artifact::Dependency{imported.symbol, imported.entry});	(void)imported.entry;	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
xtu-dependencies-carried	compiler/obligations/src/interface.cpp	entry.depends.insert(entry.depends.end(), imported.depends.begin(), imported.depends.end());	(void)imported.depends;	^unit_cross_unit_contracts_test$
xtu-laws-imported	compiler/obligations/src/contracts.cpp	recorded->identity,     recorded->entry.premises,	recorded->identity,     std::vector<artifact::Premise>{},	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
member-call-writes-object	clang/src/bridge.cpp	const bool writes = (callee_receiver.has_value() && callee_receiver->writes()) ||	const bool writes = false ||	^negative_verified_methods$
const-receiver-mutable-member	clang/src/bridge.cpp	if (constant && !leaf.mutable_member) {	if (constant && (true || !leaf.mutable_member)) {	^negative_verified_methods$
virtual-member-refused	clang/src/bridge.cpp	if (clang_CXXMethod_isVirtual(cursor) != 0) {	if (false && clang_CXXMethod_isVirtual(cursor) != 0) {	^negative_verified_methods$
virtual-call-refused	clang/src/bridge.cpp	if (clang_CXXMethod_isVirtual(referenced) != 0) {	if (false && clang_CXXMethod_isVirtual(referenced) != 0) {	^negative_verified_methods$
member-refinement-kept	clang/src/bridge.cpp	converted.refinements = std::move(*declared);	(void)declared;	^negative_verified_methods$
container-element-refinement-kept	clang/src/bridge.cpp	auto refinements = refinements_of(declared, element, *known);	auto refinements = decltype(refinements_of(declared, element, *known)){};	^negative_containers$|^negative_integration_ledger$
reference-aggregate-witness	clang/src/bridge.cpp	if (parameter.type.kind == TypeKind::Value && source::aliases_storage(parameter.passing) &&	if (false && parameter.type.kind == TypeKind::Value && source::aliases_storage(parameter.passing) &&	^negative_verified_storage$
unsafe-member-write-rooted	clang/src/bridge.cpp	return access.has_value() && !access->dereferenced && clang_equalCursors(access->declaration, declaration) != 0;	return access.has_value() && access->path.empty() && !access->dereferenced && clang_equalCursors(access->declaration, declaration) != 0;	^negative_unsafe_boundary$
alias-write-charged	clang/src/bridge.cpp	            require(locals[index].type);	            (void)index;	^negative_verified_methods$
alias-write-validity-from-prior	clang/src/bridge.cpp	const bool valid = valid_versions.contains(locals[index].version);	const bool valid = true;	^negative_verified_methods$
return-charges-unestablished	clang/src/bridge.cpp	if (!valid_versions.contains(locals[local].version) && carries_refinement(locals[local].type)) {	if (false) {	^negative_verified_methods$|^negative_cross_feature$
call-effect-refinement-charged	compiler/obligations/src/contracts.cpp	const auto required = membership(program_, effect.declared, arguments[effect.argument]);	const auto required = membership(program_, effect.declared.erased(), arguments[effect.argument]);	^negative_verified_methods$|^negative_cross_feature$
unsafe-reach-counted-valid	clang/src/bridge.cpp	new_generation(state[index], "the unsafe block at " + at);	new_generation(state[index], "the unsafe block at " + at); valid_versions.insert(state[index].version);	^negative_verified_methods$|^negative_cross_feature$
loop-head-counted-valid	clang/src/bridge.cpp	"the loop at " + describe_location(header.statement) + ", which may change it");	"the loop at " + describe_location(header.statement) + ", which may change it"); valid_versions.insert(frame.head[index].version);	^negative_cross_feature$
read-only-position-kept-apart	clang/src/bridge.cpp	if (!position.writable && !reached_by_a_write(position.storage)) {	if (!position.writable && (true || !reached_by_a_write(position.storage))) {	^negative_verified_methods$|^e2e_verified_methods$
call-effect-common-alias-model	clang/src/bridge.cpp	[&](std::size_t written) { return may_alias(state[written], state[other]); })) {	[&](std::size_t written) { return false && may_alias(state[written], state[other]); })) {	^negative_verified_methods$
pointer-receiver-capability	clang/src/bridge.cpp	if (!granted(position, kind)) {	if (false && !granted(position, kind)) {	^negative_verified_methods$
rvalue-receiver-is-the-object	clang/src/bridge.cpp	return strip_parens(clang_Cursor_getArgument(expression, 0));	return (void)clang_Cursor_getArgument(expression, 0), expression;	^e2e_verified_methods$
volatile-member-function-refused	clang/src/bridge.cpp	if (volatile_member_function(cursor)) {	if (false && volatile_member_function(cursor)) {	^negative_verified_methods$
signed-overflow-owed	compiler/obligations/src/definedness.cpp	return type.is_integer() && type.integer_type().is_signed;	return false && type.is_integer();	^negative_signed_arithmetic$|^negative_integration_ledger$|^negative_cross_feature$
zero-divisor-owed	compiler/obligations/src/definedness.cpp	site(Definedness::ZeroDivisor);	(void)0;	^negative_signed_arithmetic$
quotient-overflow-owed	compiler/obligations/src/definedness.cpp	site(Definedness::QuotientOverflow);	(void)0;	^negative_signed_arithmetic$
signed-conversion-owed	compiler/obligations/src/definedness.cpp	if (!to.is_integer() || !from.is_integer() || !to.integer_type().is_signed) {	if (true) {	^negative_signed_arithmetic$
definedness-then-arm	compiler/obligations/src/definedness.cpp	guards.emplace_back(&choice->operands[0], true);	guards.emplace_back(&choice->operands[0], false);	^e2e_signed_arithmetic$|^negative_signed_arithmetic$
definedness-else-arm	compiler/obligations/src/definedness.cpp	guards.back().second = false;	guards.back().second = true;	^e2e_signed_arithmetic$|^negative_signed_arithmetic$
specification-definedness	compiler/obligations/src/definedness.cpp	return kernel::Proposition::conjunction(std::move(**defined), std::move(stated));	return stated;	^negative_signed_arithmetic$
definedness-unsequenced-call	compiler/obligations/src/contracts.cpp	if (!sequenced_before(site, *post.call)) {	if (false && !sequenced_before(site, *post.call)) {	^negative_signed_arithmetic$
pure-definedness-refused	compiler/obligations/src/generate.cpp	if (const auto site = detail::first_definedness_site(*function.returned_value)) {	if (const auto site = (false ? detail::first_definedness_site(*function.returned_value) : std::nullopt)) {	^negative_signed_arithmetic$
law-argument-definedness	compiler/obligations/src/generate.cpp	if (const auto sites = detail::definedness_sites(argument); !sites.empty()) {	if (const auto sites = detail::definedness_sites(argument); false && !sites.empty()) {	^negative_signed_arithmetic$
product-range-corners	kernel/src/linear.cpp	!multiply(a, b, product) || product < lowest(primitive.type) || product > highest(primitive.type)	!multiply(a, b, product)	^kernel_definedness_test$|^unit_definedness_arithmetic_test$|^negative_signed_arithmetic$
widening-conversion-range	kernel/src/linear.cpp	ranges_.emplace(variable, std::pair{lowest(from), highest(from)});	ranges_.emplace(variable, std::pair{Wide{0}, Wide{0}});	^kernel_definedness_test$|^unit_definedness_arithmetic_test$|^negative_signed_arithmetic$
conversion-identity-widening-only	kernel/src/linear.cpp	if (lowest(from) >= lowest(target) && highest(from) <= highest(target)) {	if (true) {	^kernel_definedness_test$|^unit_definedness_arithmetic_test$
truncating-remainder-magnitude	kernel/src/linear.cpp	const Wide largest = (divisor < 0 ? -divisor : divisor) - 1;	const Wide largest = (divisor < 0 ? -divisor : divisor) - 2;	^kernel_definedness_test$|^unit_definedness_arithmetic_test$
remainder-sign-of-dividend	kernel/src/linear.cpp	return either(negated(*dividend), 0, remainder, 0);	return either(negated(*dividend), 0, remainder, 1);	^kernel_definedness_test$|^unit_definedness_arithmetic_test$
representability-fails-outside	kernel/src/linear.cpp	return holds ? bound(**exact, primitive->type) : outside(**exact, primitive->type);	return holds ? bound(**exact, primitive->type) : (false ? outside(**exact, primitive->type) : bound(**exact, primitive->type));	^kernel_definedness_test$|^unit_definedness_arithmetic_test$
definedness-not-self-supposed	compiler/obligations/src/contracts.cpp	emit(before, Origin::DefinedBehavior, function_.qualified_name, site.operation->provenance.range,	before.events.emplace_back(*condition); emit(before, Origin::DefinedBehavior, function_.qualified_name, site.operation->provenance.range,	^negative_signed_arithmetic$
bit-field-read-refused	clang/src/bridge.cpp	if (clang_getFieldDeclBitWidth(field) >= 0) {	if (false && clang_getFieldDeclBitWidth(field) >= 0) {	^negative_signed_arithmetic$
pointer-call-havocs-aliases	clang/src/bridge.cpp	for (const std::size_t reached : invalidate_pointee_aliases(state, handed, invalidated)) {	for (const std::size_t reached : (false ? invalidate_pointee_aliases(state, handed, invalidated) : std::vector<std::size_t>{})) {	^negative_verified_storage$
pointer-call-havoc-without-reference-writes	clang/src/bridge.cpp	            havoc_pointees({});	            (void)0;	^negative_verified_storage$
pointer-call-havoc-beside-reference-writes	clang/src/bridge.cpp	        havoc_pointees(targets);	        (void)targets;	^negative_verified_storage$
representability-outside-below	kernel/src/linear.cpp	finish(expression, Wide{1} - lowest(type))	finish(expression, Wide{2} - lowest(type))	^kernel_definedness_test$
representability-outside-above	kernel/src/linear.cpp	finish(negated(expression), highest(type) + 1)	finish(negated(expression), highest(type) + 2)	^kernel_definedness_test$
conversion-wrap-modulus	kernel/src/linear.cpp	reduced.terms.emplace(wrap, -modulus(target));	reduced.terms.emplace(wrap, -2 * modulus(target));	^kernel_definedness_test$
division-identity-divisor	kernel/src/linear.cpp	identity.terms.emplace(*quotient, divisor);	identity.terms.emplace(*quotient, divisor + 1);	^kernel_definedness_test$
unknown-divisor-remainder-below	kernel/src/linear.cpp	either(*divisor, 0, *excess, 1)	either(*divisor, 0, *excess, 2)	^kernel_definedness_test$
unknown-divisor-remainder-above	kernel/src/linear.cpp	either(*divisor, 0, *shortfall, 1)	either(*divisor, 0, *shortfall, 2)	^kernel_definedness_test$
unsigned-remainder-within-dividend	kernel/src/linear.cpp	return constrain(*within, 0);	return constrain(*within, 1);	^kernel_definedness_test$
remainder-sign-of-nonnegative-dividend	kernel/src/linear.cpp	either(*dividend, 1, negated(remainder), 0)	either(*dividend, 1, negated(remainder), 1)	^kernel_definedness_test$
refined-binder-membership	compiler/obligations/src/generate.cpp	body = suppose_membership(program, quantified->binders, std::move(*body), location);	(void)0;	^negative_quantified_propositions$|^unit_quantified_propositions_test$
refined-parameter-membership	compiler/obligations/src/generate.cpp	auto ranged = suppose_membership(program, types, std::move(body), {}, &unstated);	auto ranged = ((void)program, std::expected<kernel::Proposition, Failure>(std::move(body)));	^negative_quantified_propositions$|^unit_quantified_propositions_test$
forall-binder-refinement-kept	clang/src/bridge.cpp	binder_type.refinements = std::move(*refined);	(void)refined;	^negative_quantified_propositions$|^e2e_refined_quantifiers$
equality-operand-refinement-kept	clang/src/bridge.cpp	equality.operand_type.refinements = std::move(*refined);	(void)refined;	^negative_quantified_propositions$
formal-equality-refinement-refused	compiler/obligations/src/generate.cpp	if (carries_refinement(equality->operand_type)) {	if (false && carries_refinement(equality->operand_type)) {	^negative_quantified_propositions$|^unit_quantified_propositions_test$
old-entry-value-refused	compiler/frontend/src/projection.cpp	if (const auto snapshot = detail::entry_value_form(stream, postcondition->expression)) {	if (const auto snapshot = (false ? detail::entry_value_form(stream, postcondition->expression) : std::nullopt)) {	^negative_erasure$
analysis-target-compared	compiler/driver/src/pipeline.cpp	if (analyzed->unit.target != target->effective) {	if (false) {	^negative_analysis_target$
analysis-target-named	compiler/driver/src/target.cpp	given.push_back("--target=" + target.triple);	(void)target.triple;	^negative_analysis_target$
analysis-configuration-read	compiler/driver/src/target.cpp	given.push_back("--config=" + file);	(void)file;	^negative_analysis_target$
analysis-default-configuration-unread	compiler/driver/src/target.cpp	given.emplace_back("--no-default-config");	(void)given;	^negative_analysis_target$
xtu-files-read-recorded	compiler/driver/src/driver.cpp	files.insert(files.end(), read->begin(), read->end());	(void)read;	^negative_cross_tu$
xtu-files-read-own-rule	compiler/driver/src/interface_io.cpp	if (argument == "-MF" || argument == "-MT" || argument == "-MQ" || argument == "-MJ") {	if (false) {	^negative_cross_tu$
dependency-rule-target	compiler/driver/src/dependencies.cpp	if (!text.starts_with(target) || text.size() == target.size() || text[target.size()] != ':') {	if (false) {	^unit_dependencies_test$
dependency-one-rule	compiler/driver/src/dependencies.cpp	if (!blank(text[rest])) {	if (false && !blank(text[rest])) {	^unit_dependencies_test$
dependency-escaped-space	compiler/driver/src/dependencies.cpp	name.push_back(' ');	(void)0;	^unit_dependencies_test$
dependency-escaped-hash	compiler/driver/src/dependencies.cpp	name.push_back('#');	(void)0;	^unit_dependencies_test$
dependency-escaped-dollar	compiler/driver/src/dependencies.cpp	name.push_back('$');	(void)0;	^unit_dependencies_test$
driver-language-reset-for-any-input	compiler/driver/src/driver.cpp	std::ranges::any_of(options.positional, [index](std::size_t later) { return later > index; });	std::ranges::any_of(options.inputs, [index](const Input& later) { return later.argument_index > index; });	^integration_driver_inputs$
driver-every-input-out-of-preprocessing	compiler/driver/src/driver.cpp	if (index < is_input.size()) {	if (index < is_input.size() && std::ranges::any_of(options.inputs, [index](const Input& input) { return input.argument_index == index; })) {	^integration_driver_inputs$
driver-every-cpp-extension	compiler/driver/src/options.cpp	{".cpp", ".CPP", ".cc", ".CC", ".cp", ".cxx", ".CXX", ".c++", ".C++", ".C", ".cppl"}	{".cpp", ".cc", ".cxx", ".c++", ".C", ".cppl"}	^integration_driver_inputs$
driver-preprocessing-options-unread	compiler/driver/src/driver.cpp	if (!replacements.empty() && !preprocessing) {	if (false && !replacements.empty() && !preprocessing) {	^integration_driver_inputs$
driver-preprocessing-options-kept-for-sources	compiler/driver/src/driver.cpp	return !replacements.contains(index) && !unpreprocessed_input(options.arguments[index]);	return false && !replacements.contains(index) && !unpreprocessed_input(options.arguments[index]);	^integration_driver_inputs$
driver-unpreprocessed-inputs	compiler/driver/src/driver.cpp	return path.find(".so.") != std::string_view::npos ||	return false && path.find(".so.") != std::string_view::npos &&	^integration_driver_inputs$
driver-search-options-unread-without-link	compiler/driver/src/driver.cpp	if (separate(kSeparate) || (compile_only && separate(kSearchSeparate))) {	if (separate(kSeparate) || (false && compile_only && separate(kSearchSeparate))) {	^integration_driver_inputs$
driver-dependency-file-named	compiler/driver/src/driver.cpp	if (!named_file) {	if (false) {	^integration_driver_inputs$
driver-dependency-target-named	compiler/driver/src/driver.cpp	if (!named_target) {	if (false) {	^integration_driver_inputs$
driver-runtime-named-after-source	compiler/driver/src/pipeline.cpp	request.scratch / (std::filesystem::path(request.stem).stem().string() + ".ii");	request.scratch / (request.stem + ".runtime.ii");	^integration_driver_inputs$
driver-separate-value-options	compiler/driver/src/options.cpp	if (std::ranges::find(kValueOptions, argument) != kValueOptions.end()) {	if (false && std::ranges::find(kValueOptions, argument) != kValueOptions.end()) {	^negative_analysis_options$|^unit_driver_options_test$
driver-config-value-option	compiler/driver/src/options.cpp	"--config",	"--config-unlisted",	^negative_analysis_options$|^unit_driver_options_test$
driver-multiple-value-options	compiler/driver/src/options.cpp	return multiple->values;	return 1;	^negative_analysis_options$|^unit_driver_options_test$
driver-joined-and-separate-options	compiler/driver/src/options.cpp	if (std::ranges::any_of(kJoinedAndSeparatePrefixes,	if (false && std::ranges::any_of(kJoinedAndSeparatePrefixes,	^negative_analysis_options$|^unit_driver_options_test$
driver-option-values-recorded	compiler/driver/src/options.cpp	options.option_value.push_back(values_left != 0);	options.option_value.push_back(false);	^negative_analysis_options$|^unit_driver_options_test$
driver-option-kept-with-values	compiler/driver/src/driver.cpp	const std::size_t span = option_span(options, index);	const std::size_t span = 1;	^negative_analysis_options$
driver-runtime-option-kept-with-values	compiler/driver/src/driver.cpp	if (!option_value(options, index) &&	if (true &&	^negative_analysis_options$
analysis-configuration-found-once	compiler/driver/src/target.cpp	if (!names_configuration) {	if (true || !names_configuration) {	^negative_analysis_options$
analysis-response-file-refused	compiler/driver/src/driver.cpp	if (response.has_value()) {	if (false) {	^negative_analysis_options$
analysis-override-environment-refused	compiler/driver/src/pipeline.cpp	if (const std::optional<std::string> variable = argument_editing_environment()) {	if (const std::optional<std::string> variable = (false ? argument_editing_environment() : std::nullopt)) {	^negative_analysis_options$
lsp-machine-options-kept	src/lsp/src/compile_commands.cpp	one_of(argument, kSwitches) || machine_option(argument) ||	one_of(argument, kSwitches) || (false && machine_option(argument)) ||	^lsp_compile_commands_test$|^lsp_interfaces_test$
lsp-architecture-kept	src/lsp/src/compile_commands.cpp	({"-target", "-arch", "-mthread-model", "--std"});	({"-target", "-mthread-model", "--std"});	^lsp_compile_commands_test$
lsp-unpassed-frontend-options	src/lsp/src/compile_commands.cpp	if (argument == "-Xclang" || argument.starts_with("-Xarch_")) {	if (false && (argument == "-Xclang" || argument.starts_with("-Xarch_"))) {	^lsp_compile_commands_test$|^lsp_interfaces_test$
lsp-unpassed-response-file	src/lsp/src/compile_commands.cpp	if (argument.starts_with("@")) {	if (false && argument.starts_with("@")) {	^lsp_compile_commands_test$
lsp-unpassed-configuration	src/lsp/src/compile_commands.cpp	if (argument == "--config" || argument.starts_with("--config=") || argument.starts_with("--config-user-dir=") ||	if (false &&	^lsp_compile_commands_test$
lsp-unpassed-driver	src/lsp/src/compile_commands.cpp	if (named != driver && absolute(named, directory) != normal(driver).string()) {	if (false && named != driver && absolute(named, directory) != normal(driver).string()) {	^lsp_compile_commands_test$
lsp-unverifiable-not-verified	compiler/driver/src/buffer_compile.cpp	const bool stop_after_elaboration = request.stop_after_elaboration || !request.unverifiable.empty();	const bool stop_after_elaboration = request.stop_after_elaboration;	^lsp_interfaces_test$
lsp-unverifiable-reported	compiler/driver/src/buffer_compile.cpp	if (!request.unverifiable.empty() && result.has_cppl) {	if (false) {	^lsp_interfaces_test$
lsp-unverifiable-requested	src/lsp/src/server.cpp	compile_commands_.unpassed_option_for(doc.path(), clang_.empty() ? std::string{CPPL_DEFAULT_CLANG} : clang_)	std::optional<std::string>{}	^lsp_interfaces_test$
aggregate-leaf-equations	compiler/obligations/src/aggregates.cpp	supposed.push_back(kernel::Proposition::equality(*type, std::move(projected), kernel::shift(*value, 1)));	(void)value;	^e2e_struct_values$|^negative_struct_values$
aggregate-leaf-order	compiler/obligations/src/aggregates.cpp	kernel::Term projected = kernel::Term::project(domain, static_cast<std::uint32_t>(index), subject);	kernel::Term projected = kernel::Term::project(domain, static_cast<std::uint32_t>(signature.size() - 1 - index), subject);	^negative_struct_values$
struct-group-effect	clang/src/aggregate_effects.cpp	effects.push_back(CallEffect{group.argument, *version, group.type});	(void)effects;	^e2e_struct_values$|^negative_struct_values$
struct-group-rebound-member	clang/src/aggregate_effects.cpp	std::optional<Expr> member = member_at(std::move(post), inside(group, state[leaf]));	std::optional<Expr> member = member_at(std::move(post), [&] { std::vector<PlaceStep> swapped = inside(group, state[leaf]); swapped.back().index ^= 1u; return swapped; }());	^negative_struct_values$
struct-group-unsafe-writes	clang/src/aggregate_effects.cpp	const bool written = group.writable || call.unsafe_callee;	const bool written = group.writable;	^negative_struct_values$
struct-group-writes-reach-aliases	clang/src/aggregate_effects.cpp	written_storage.insert(written_storage.end(), group.leaves.begin(), group.leaves.end());	(void)group.leaves;	^negative_struct_values$
struct-group-leaf-coverage	clang/src/aggregate_effects.cpp	if (std::ranges::none_of(group.leaves, [&](std::size_t leaf) { return state[leaf].path == path; })) {	if (false && std::ranges::none_of(group.leaves, [&](std::size_t leaf) { return state[leaf].path == path; })) {	^negative_struct_values$
struct-copy-user-code	clang/src/aggregate_values.cpp	if ((copy || move) && clang_CXXMethod_isDefaulted(member) == 0) {	if (false && (copy || move) && clang_CXXMethod_isDefaulted(member) == 0) {	^negative_struct_values$
struct-copy-member-user-code	clang/src/aggregate_values.cpp	if (std::optional<std::string> inner = user_provided_copy(clang_getCursorType(field), copying, depth + 1)) {	if (std::optional<std::string> inner = (false ? user_provided_copy(clang_getCursorType(field), copying, depth + 1) : std::nullopt)) {	^negative_struct_values$
struct-copy-constructor-kind	clang/src/aggregate_values.cpp	if ((!copy && !move) || clang_Cursor_getNumArguments(construction) != 1) {	if (false && ((!copy && !move) || clang_Cursor_getNumArguments(construction) != 1)) {	^negative_struct_values$
unsafe-callee-call-sequenced	clang/src/bridge.cpp	if (!sequenced_call && writes_unsafely(signature.unsafe_effects, referenced)) {	if (false && !sequenced_call && writes_unsafely(signature.unsafe_effects, referenced)) {	^negative_unsafe_callees$
unsafe-callee-default-argument-sequenced	clang/src/bridge.cpp	{}, std::nullopt, signature.clause, signature.refinements, signature.unsafe_effects};	{}, std::nullopt, signature.clause, signature.refinements, nullptr};	^negative_unsafe_callees$
MUTATIONS
)

# These carry embedded newlines, so they are held separately rather than being
# squeezed onto one line above.
multiline_names=(forall-recursive-evidence implication-recursive-evidence
                 implication-premise-evidence transport-equality-evidence
                 transport-recursive-evidence conditional-false-arm
                 arithmetic-fact-evidence callee-body-linkage
                 call-precondition-gate conjunction-introduction-right
                 disjunction-right-case conjunction-elimination-evidence
                 receiver-caller-storage post-state-after-returned-call)

multiline_file() {
    case "$1" in
        callee-body-linkage|call-precondition-gate) echo "compiler/automation/src/composition.cpp" ;;
        receiver-caller-storage) echo "clang/src/bridge.cpp" ;;
        post-state-after-returned-call) echo "compiler/obligations/src/contracts.cpp" ;;
        *) echo "kernel/src/check.cpp" ;;
    esac
}

multiline_tests() {
    case "$1" in
        callee-body-linkage) echo '^unit_contracts_test$' ;;
        receiver-caller-storage) echo '^negative_verified_methods$' ;;
        post-state-after-returned-call) echo '^negative_verified_storage$' ;;
        call-precondition-gate) echo '^unit_contracts_test$|^negative_verified_calls$|^negative_verified_paths$' ;;
        *) echo '^kernel_' ;;
    esac
}

# A mutation whose removal leaves externally observable behavior
# indistinguishable: the same refusal, the same termination, no crash, and no
# proof that was not there before.
#
# This is a claim about the program, justified by naming the enforcement that
# still holds the invariant. It is never a way to record that this harness
# cannot observe a difference -- a search that stops terminating, or that
# crashes, has observably different behavior and is killed, not equivalent.
#
# Each entry names the tests that state the invariant directly, so removing
# every enforcement of it is still caught. There is no entry today
# (docs/MUTATION_TESTING.md 5).
equivalent_justification() {
    case "$1" in
        *) return 1 ;;
    esac
}

multiline_before() {
    case "$1" in
        forall-recursive-evidence)
            printf '%s' '*elimination->evidence,
                                        limits, depth + 1);
            !evidence)' ;;
        implication-recursive-evidence)
            printf '%s' '*application->evidence,
                                        limits, depth + 1);
            !evidence)' ;;
        implication-premise-evidence)
            printf '%s' '*application->premise,
                                       limits, depth + 1);
            !premise)' ;;
        transport-equality-evidence)
            printf '%s' '*transport->equality, limits, depth + 1);
            !checked)' ;;
        transport-recursive-evidence)
            printf '%s' '*transport->evidence, limits, depth + 1);
            !checked)' ;;
        arithmetic-fact-evidence)
            printf '%s' '*fact.evidence, limits, depth + 1);
                !checked)' ;;
        conditional-false-arm)
            printf '%s' 'return check_under(context, locals, assumptions, false_goal, *branch->false_case, limits, depth + 1);' ;;
        callee-body-linkage)
            printf '%s' 'if (!checked) {
            return std::unexpected("callee contract linkage failed:' ;;
        call-precondition-gate)
            printf '%s' 'if (index < stage.prefix && !std::ranges::all_of(' ;;
        conjunction-introduction-right)
            printf '%s' 'return check_under(context, locals, assumptions, *conjunction->right, *introduction->right, limits, depth + 1);' ;;
        disjunction-right-case)
            printf '%s' 'return check_under(context, locals, assumptions, from_right, *cases->right_case, limits, depth + 1);' ;;
        conjunction-elimination-evidence)
            printf '%s' '*taken->conjunction, *taken->evidence, limits, depth + 1);
            !evidence)' ;;
        receiver-caller-storage)
            printf '%s' '.type = leaf.type,
                                       .external = true,' ;;
        post-state-after-returned-call)
            printf '%s' '            if (auto evaluated = evaluate(result, scope); !evaluated)
                return evaluated;
            std::vector<kernel::Term> arguments;
            for (std::size_t index = 1; index < completed->operands.size(); ++index) {
                if (core_type(completed->operands[index].type) != std::optional{plan_.parameters[index - 1]})
                    return fail("post-state parameter type mismatch", location);
                auto value = lower(completed->operands[index], scope);
                if (!value)
                    return std::unexpected(value.error());
                arguments.push_back(*value);
            }' ;;
    esac
}

multiline_after() {
    case "$1" in
        conditional-false-arm)
            printf '%s' '(void)check_under(context, locals, assumptions, false_goal, *branch->false_case, limits, depth + 1); return {};' ;;
        call-precondition-gate)
            printf '%s' 'if (false && index < stage.prefix && !std::ranges::all_of(' ;;
        callee-body-linkage)
            printf '%s' 'if (false && !checked) {
            return std::unexpected("callee contract linkage failed:' ;;
        forall-recursive-evidence)
            printf '%s' '*elimination->evidence,
                                        limits, depth + 1);
            false && !evidence)' ;;
        implication-recursive-evidence)
            printf '%s' '*application->evidence,
                                        limits, depth + 1);
            false && !evidence)' ;;
        implication-premise-evidence)
            printf '%s' '*application->premise,
                                       limits, depth + 1);
            false && !premise)' ;;
        transport-equality-evidence)
            printf '%s' '*transport->equality, limits, depth + 1);
            false && !checked)' ;;
        transport-recursive-evidence)
            printf '%s' '*transport->evidence, limits, depth + 1);
            false && !checked)' ;;
        arithmetic-fact-evidence)
            printf '%s' '*fact.evidence, limits, depth + 1);
                false && !checked)' ;;
        conjunction-introduction-right)
            printf '%s' '(void)check_under(context, locals, assumptions, *conjunction->right, *introduction->right, limits, depth + 1); return {};' ;;
        disjunction-right-case)
            printf '%s' '(void)check_under(context, locals, assumptions, from_right, *cases->right_case, limits, depth + 1); return {};' ;;
        conjunction-elimination-evidence)
            printf '%s' '*taken->conjunction, *taken->evidence, limits, depth + 1);
            false && !evidence)' ;;
        receiver-caller-storage)
            printf '%s' '.type = leaf.type,
                                       .external = false,' ;;
        post-state-after-returned-call)
            printf '%s' '            std::vector<kernel::Term> arguments;
            for (std::size_t index = 1; index < completed->operands.size(); ++index) {
                if (core_type(completed->operands[index].type) != std::optional{plan_.parameters[index - 1]})
                    return fail("post-state parameter type mismatch", location);
                auto value = lower(completed->operands[index], scope);
                if (!value)
                    return std::unexpected(value.error());
                arguments.push_back(*value);
            }
            if (auto evaluated = evaluate(result, scope); !evaluated)
                return evaluated;' ;;
    esac
}

only=()
jobs=4
# A hard backstop only. The in-process transition budget is what should stop a
# search that cannot make progress, and it reports an ordinary failure when it
# does. This exists so a path nothing budgets still ends the run on CI instead
# of hanging it, and so such a path is visible as 'killed (nontermination)'
# rather than passing silently.
ctest_timeout=120
list_only=0
reuse=""
while [ $# -gt 0 ]; do
    case "$1" in
        --only) only+=("$2"); shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --list) list_only=1; shift ;;
        --reuse) reuse="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

all_names() {
    printf '%s\n' "$mutations" | cut -f1
    printf '%s\n' "${multiline_names[@]}"
}

if [ "$list_only" -eq 1 ]; then
    all_names
    exit 0
fi

# Validated here rather than inside the command substitution that collects the
# names, where a failure would exit only the subshell and be lost.
known=$(all_names)
for name in ${only+"${only[@]}"}; do
    if ! grep -qx -- "$name" <<< "$known"; then
        echo "no such mutation: $name" >&2
        exit 2
    fi
done

selected() {
    if [ ${#only[@]} -eq 0 ]; then
        printf '%s\n' "$known"
        return
    fi
    printf '%s\n' "${only[@]}"
}

field() {
    printf '%s\n' "$mutations" | awk -F'\t' -v n="$1" -v f="$2" '$1 == n { print $f }'
}

spec_file() {
    if grep -q . <<< "$(field "$1" 2)"; then field "$1" 2; else multiline_file "$1"; fi
}

spec_tests() {
    if grep -q . <<< "$(field "$1" 5)"; then field "$1" 5; else multiline_tests "$1"; fi
}

spec_before() {
    if grep -q . <<< "$(field "$1" 3)"; then field "$1" 3; else multiline_before "$1"; fi
}

spec_after() {
    if grep -q . <<< "$(field "$1" 4)"; then field "$1" 4; else multiline_after "$1"; fi
}

# Substitutes the single occurrence of "$2" with "$3" in the file "$1", writing
# the result to stdout. Both strings are taken literally: \Q..\E keeps every
# character of an anchor (which is C++, full of regex metacharacters) from being
# read as a pattern. -0777 reads the whole file, so an anchor may span lines.
substitute() {
    BEFORE="$2" AFTER="$3" perl -0777 -pe '
        BEGIN { $b = $ENV{BEFORE}; $a = $ENV{AFTER} }
        $n = ($_ =~ s/\Q$b\E/$a/);
        END { exit($n == 1 ? 0 : 1) }
    ' "$1"
}

# Counts occurrences of "$2" in the file "$1" as a literal string.
occurrences() {
    BEFORE="$2" perl -0777 -ne '
        BEGIN { $b = $ENV{BEFORE} }
        my $n = () = /\Q$b\E/g;
        print "$n\n";
    ' "$1"
}

names=$(selected)

# An anchor is an exact source string, so ordinary reformatting of the code it
# names silently stops that check from being tested. Saying so here costs a file
# read; finding out inside the loop costs a build and a test run.
stale=""
while IFS= read -r name; do
    [ -n "$name" ] || continue
    file=$(spec_file "$name")
    if [ ! -f "$file" ]; then
        stale="$stale $name(no $file)"
        continue
    fi
    count=$(occurrences "$file" "$(spec_before "$name")")
    if [ "$count" != "1" ]; then
        stale="$stale $name($count matches)"
    fi
done <<< "$names"

if [ -n "$stale" ]; then
    echo "Mutation anchors no longer match the source, so these checks are untested:$stale" >&2
    exit 1
fi

artifacts="$root/build/mutations"
mkdir -p "$artifacts"
if [ -n "$reuse" ]; then
    run=$(cd "$reuse" && pwd)
    if [ ! -d "$run/source" ] || [ ! -d "$run/build" ]; then
        echo "$reuse is not a mutation run directory" >&2
        exit 2
    fi
else
    run=$(mktemp -d "$artifacts/run-XXXXXX")
fi
source_copy="$run/source"
build="$run/build"

echo "Mutation artifacts: $run"

# The copy leaves the checkout untouched. find prunes what builds and tools
# write, matching each name at any depth, and tar carries everything else with
# its mode, times and links. Both are on every POSIX host; rsync, which this
# once used, is not, and the Linux CI image does not install it. The pax format
# keeps each time to the nanosecond, so a copied file's time equals the
# checkout's and a reused build rebuilds only what changed.
mkdir -p "$source_copy"
copied() {
    find . \( -name .git -o -name build -o -name 'build-*' -o -name tmp \
        -o -name .code-review-graph -o -name .claude -o -name .codex \) -prune \
        -o ! -name . "$@"
}
# tar restores the checkout's times, which can be older than an object the
# reused build made from the copy's earlier content, such as a mutation an
# interrupted run left applied. Every file whose content changes is touched
# after the copy, so the build cannot keep that object. So is every file whose
# time differs from the checkout's although its content does not: a run
# interrupted after restoring a mutated file leaves the original content with
# the restore's time, and the object built from the mutation in between is
# older than that time but newer than the checkout's.
changed="$run/changed-files"
: > "$changed"
if [ -n "$reuse" ]; then
    (cd "$root" && copied -type f -print0) | while IFS= read -r -d '' file; do
        if ! cmp -s "$root/$file" "$source_copy/$file" ||
            [ "$root/$file" -nt "$source_copy/$file" ] || [ "$source_copy/$file" -nt "$root/$file" ]; then
            printf '%s\0' "$file" >> "$changed"
        fi
    done
fi
(cd "$root" && copied -print0 | tar --null --no-recursion -T - --format=pax -cf -) |
    (cd "$source_copy" && tar -xf -)
if [ -s "$changed" ]; then
    (cd "$source_copy" && xargs -0 touch < "$changed")
fi
if [ -n "$reuse" ]; then
    stale_files=$(comm -13 <(cd "$root" && copied -print | LC_ALL=C sort) \
                           <(cd "$source_copy" && copied -print | LC_ALL=C sort))
    if [ -n "$stale_files" ]; then
        echo "The reused copy holds files the checkout does not:" >&2
        echo "$stale_files" >&2
        exit 2
    fi
fi

# The copy is not a Git checkout. Under the checkout's build directory Git
# would otherwise find the checkout's repository above it, and a commit made
# there during the run would change the source identity the copy's build and
# tests read (integration_release_metadata).
export GIT_CEILING_DIRECTORIES="$run"

# A green control run establishes that a later test failure was introduced by
# the mutation rather than being there all along. An LLVM_ROOT the caller
# names is the LLVM it builds against, as the ci-* presets read it.
#
# The copy is compiled as RelWithDebInfo is, at -O2 with NDEBUG, without the
# debug information: no test reads it, and writing it is over a third of the
# time to compile a large translation unit and most of the time to link each
# of the dozens of executables a one-file change relinks. Every entry rebuilds,
# so that is most of what a run spends.
cmake -S "$source_copy" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      "-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -DNDEBUG" "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -DNDEBUG" \
      -DCPPL_WARNINGS_AS_ERRORS=ON ${LLVM_ROOT:+"-DLibClang_ROOT=$LLVM_ROOT"} > "$run/configure.log" 2>&1 ||
    { echo "Control configure failed; see $run/configure.log" >&2; exit 1; }
cmake --build "$build" -j "$jobs" > "$run/build.log" 2>&1 ||
    { echo "Control build failed; see $run/build.log" >&2; exit 1; }

if command -v sha256sum > /dev/null 2>&1; then
    hasher=(sha256sum)
else
    hasher=(shasum -a 256)
fi

# Each file of a list, one path a line, that exists, by the digest of its
# content: a size and a time could stay the same across a change.
described() {
    local path
    while IFS= read -r path; do
        if [ -e "$path" ]; then
            "${hasher[@]}" "$path" || return 1
        fi
    done
}

# Every file under a directory, by the digest of its content.
described_tree() {
    (cd "$1" && find -L . -type f -print0 | LC_ALL=C sort -z | xargs -0 "${hasher[@]}")
}

# The programs the test scripts run through PATH, each by where it is found and
# the digest of its content, and a tool not found by its absence.
test_tools() {
    local tool path
    for tool in bash sh sed awk grep diff cmp find sort xargs mktemp cat head tail tr cut wc env perl \
        timeout tee od printf ls cp mv mkdir touch dirname basename realpath readlink nm objdump; do
        if path=$(command -v "$tool" 2>/dev/null) && [ -f "$path" ]; then
            printf '%s ' "$tool"
            "${hasher[@]}" "$path" || return 1
        else
            printf '%s absent\n' "$tool"
        fi
    done
}

# Everything the control run's outcome depends on, written out to be digested:
# every file of the copy; every file the build wrote under bin/ and lib/, the
# binaries themselves, so none of them can be stale; the build's
# configuration; the programs it found outside the tree, the headers its Clang
# reads, the libraries the compiler loads; the tools running the experiment and
# the environment the tests see. Fails when any of it cannot be read. It is
# called where its status is tested, which turns errexit off inside it, so each
# step returns its own failure.
control_inputs() {
    local clang
    (cd "$source_copy" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 "${hasher[@]}") || return 1
    (cd "$build" && find bin lib -type f -print0 | LC_ALL=C sort -z | xargs -0 "${hasher[@]}") || return 1
    cat "$build/CMakeCache.txt" || return 1
    sed -n 's/^[A-Za-z_][^:]*:FILEPATH=\(\/.*\)$/\1/p' "$build/CMakeCache.txt" | LC_ALL=C sort -u | described ||
        return 1
    clang=$(sed -n 's/^CPPL_DEFAULT_CLANG:FILEPATH=//p' "$build/CMakeCache.txt")
    [ -x "$clang" ] || return 1
    "$clang" -E -x c++ -v - < /dev/null 2>&1 > /dev/null |
        sed -n '/^#include <\.\.\.> search starts here:$/,/^End of search list\.$/s/^ \(\/.*\)$/\1/p' |
        while IFS= read -r directory; do
            described_tree "$directory" || exit 1
        done || return 1
    if command -v ldd > /dev/null 2>&1; then
        ldd "$build/bin/cppl" | awk '{ for (i = 1; i <= NF; ++i) if ($i ~ /^\//) print $i }'
    else
        otool -L "$build/bin/cppl" | awk 'NR > 1 && $1 ~ /^\// { print $1 }'
    fi | LC_ALL=C sort -u | described || return 1
    command -v cmake ctest ninja || return 1
    command -v cmake ctest ninja | described || return 1
    cmake --version || return 1
    test_tools || return 1
    env | grep -E '^(PATH|LD_LIBRARY_PATH|DYLD_[A-Z_]*|LANG|LC_[A-Z]*|TZ|TMPDIR|HOME|CC|CXX|LLVM_ROOT|CPPL_[A-Z_]*)=' |
        LC_ALL=C sort
}

# The control run is the same experiment whenever its inputs are. A reused copy
# whose inputs are byte for byte those of a green control run, recorded by
# their digest, does not repeat it. Any difference at all, or any input that
# cannot be read, runs it again; it is recorded only once it is green.
passed="$run/control.passed"
fingerprint=""
if inputs=$(control_inputs); then
    fingerprint=$(printf '%s\n' "$inputs" | "${hasher[@]}" | cut -d' ' -f1)
fi
unset inputs
if [ -n "$fingerprint" ] && [ -f "$passed" ] && [ "$(cat "$passed")" = "$fingerprint" ]; then
    echo "Control run not repeated: its inputs are those of the green run recorded in $passed"
else
    rm -f "$passed"
    ctest --test-dir "$build" --output-on-failure -j "$jobs" > "$run/baseline.log" 2>&1 ||
        { echo "Control baseline failed; see $run/baseline.log" >&2; exit 1; }
    if [ -n "$fingerprint" ]; then
        printf '%s\n' "$fingerprint" > "$passed"
    fi
fi

caught=0
total=0
survivors=""
equivalents=""
while IFS= read -r name <&3; do
    [ -n "$name" ] || continue
    total=$((total + 1))
    file=$(spec_file "$name")
    target="$source_copy/$file"
    log_dir="$run/$name"
    mkdir -p "$log_dir"
    cp "$target" "$log_dir/original"

    substitute "$target" "$(spec_before "$name")" "$(spec_after "$name")" > "$log_dir/mutated"
    diff -u "$log_dir/original" "$log_dir/mutated" > "$log_dir/mutation.diff" || true
    cp "$log_dir/mutated" "$target"

    # stdin is redirected away from these because the loop reads the remaining
    # mutation names from it, and a child that consumes it ends the run early.
    outcome="unknown"
    if ! cmake --build "$build" -j "$jobs" > "$log_dir/build.log" 2>&1 < /dev/null; then
        outcome="build-error"
    else
        status=0
        ctest --test-dir "$build" --output-on-failure --timeout "$ctest_timeout" \
              -R "$(spec_tests "$name")" > "$log_dir/tests.log" 2>&1 < /dev/null || status=$?
        justification=""
        # CTest uses 8 for ordinary test failures. An empty selection tests
        # nothing, so it is an error in the experiment rather than a result.
        if grep -qE '\*\*\*(Timeout|Exception)' "$log_dir/tests.log"; then
            # Turning a terminating program into one that does not terminate,
            # or into one that crashes, is a change in required behavior. It
            # kills the mutation. The in-process budget should reach this first
            # and report an ordinary failure; arriving here instead means some
            # path is still unbudgeted, which is worth seeing rather than
            # hiding.
            outcome="killed (nontermination)"
        elif [ "$status" -eq 0 ] && ! grep -q "No tests were found" "$log_dir/tests.log"; then
            # Passing under mutation is only acceptable where the mutated
            # program's required behavior is genuinely indistinguishable, and
            # the justification has to name what still enforces the invariant.
            if justification=$(equivalent_justification "$name"); then
                outcome="equivalent"
            else
                outcome="survived"
            fi
        elif [ "$status" -eq 8 ] && grep -q '\*\*\*Failed' "$log_dir/tests.log" &&
             ! grep -q '\*\*\*Not Run' "$log_dir/tests.log"; then
            outcome="caught"
        else
            outcome="test-error"
        fi
    fi

    cp "$log_dir/original" "$target"
    case "$outcome" in
        caught|killed*)
            caught=$((caught + 1))
            echo "$name: $outcome" ;;
        equivalent)
            equivalents="$equivalents $name"
            echo "$name: equivalent -- $justification" ;;
        *)
            survivors="$survivors $name($outcome)"
            echo "$name: $outcome" ;;
    esac
done 3<<< "$names"

# Leaves no runnable mutated compiler behind.
cmake --build "$build" -j "$jobs" > "$run/restored-build.log" 2>&1 ||
    { echo "Restoring the unmutated build failed; see $run/restored-build.log" >&2; exit 1; }

echo "$caught/$total mutations caught"
if [ -n "$equivalents" ]; then
    echo "equivalent (justified at equivalent_justification):$equivalents"
fi
if [ -n "$survivors" ]; then
    # Every mutation ends in exactly one of: caught, killed (nontermination), or
    # equivalent. A survivor is none of those, and means some rule of the proof
    # system is going untested.
    echo "not caught:$survivors" >&2
    exit 1
fi
