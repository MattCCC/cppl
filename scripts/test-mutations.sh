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
declarator-list-ends-clauses	compiler/frontend/src/recognizer.cpp	nesting == 0 && token.is_punctuator(",")	false && (nesting == 0 && token.is_punctuator(","))	^unit_recognizer_test$
call-capability-kind	compiler/obligations/src/contracts.cpp	return candidate.kind == required.kind &&	return true &&	^negative_memory_capabilities$
call-capability-pointer	compiler/obligations/src/contracts.cpp	candidate.place.root.id == passed->parameter;	(true || candidate.place.root.id == passed->parameter);	^negative_memory_capabilities$
call-capability-extent	compiler/obligations/src/contracts.cpp	if (required.extent.empty() && holding->extent.empty()) {	if (true || (required.extent.empty() && holding->extent.empty())) {	^negative_memory_capabilities$
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
unsafe-not-assumption-free	compiler/driver/src/driver.cpp	return claim.premises.empty() && claim.unsafe.empty() && claim.imported.empty() && claim.library.empty();	return claim.premises.empty() && claim.imported.empty() && claim.library.empty();	^e2e_unsafe_boundary$
library-model-not-assumption-free	compiler/driver/src/driver.cpp	return claim.premises.empty() && claim.unsafe.empty() && claim.imported.empty() && claim.library.empty();	return claim.premises.empty() && claim.unsafe.empty() && claim.imported.empty();	^e2e_containers$
library-model-closure-through-calls	compiler/obligations/src/trust.cpp	changed = models[index].emplace(model, LibraryDependency{model, false}).second || changed;	changed = (models[index].contains(model) && false) || changed;	^e2e_containers$
container-stale-view	clang/src/bridge.cpp	if (root.version == entry.borrows->version) {	if (true) {	^negative_containers$
container-element-generation	clang/src/bridge.cpp	return !entry.formed_at.has_value() ||	return true ||	^negative_containers$
container-span-capability	clang/src/bridge.cpp	if (region->parameter.has_value() && !granted(*region->parameter, required)) {	if (false && region->parameter.has_value() && !granted(*region->parameter, required)) {	^negative_containers$
container-call-disjointness	clang/src/bridge.cpp	if (other == root || may_alias(state[other], state[root])) {	if (false && (other == root || may_alias(state[other], state[root]))) {	^negative_containers$
container-refined-writable-view	clang/src/bridge.cpp	if (!state[root].sequence->element.refinements.empty()) {	if (false && !state[root].sequence->element.refinements.empty()) {	^negative_containers$
container-mutable-call-aliases	clang/src/bridge.cpp	!may_alias(state[target], state[other])) {	true) {	^negative_containers$
container-copy-refinement	clang/src/bridge.cpp	if (auto gap = refinement_gap(root, declaring[*origin])) {	if (auto gap = refinement_gap(root, declaring[*origin]); false) {	^negative_containers$
container-pop-precondition	compiler/obligations/src/library.cpp	summary.preconditions.push_back(	(void)(	^negative_containers$
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
xtu-statement-compared	compiler/obligations/src/contracts.cpp	if (!(*plan.statement == recorded->entry.statement)) {	if (false && !(*plan.statement == recorded->entry.statement)) {	^negative_cross_tu$|^unit_cross_unit_contracts_test$
xtu-imported-established	compiler/automation/src/composition.cpp	    if (function.imported.has_value()) {	    if (function.imported.has_value() && false) {	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
xtu-imported-totality	compiler/obligations/src/contracts.cpp	total[index] = contract.total;	total[index] = true;	^negative_cross_tu$|^unit_cross_unit_contracts_test$
xtu-partial-record-refused-with-measure	compiler/obligations/src/contracts.cpp	if (!total && function.contract.has_value() && !function.contract->measures.empty()) {	if (false && !total && function.contract.has_value() && !function.contract->measures.empty()) {	^unit_cross_unit_contracts_test$
xtu-recursion-refused	compiler/obligations/src/contracts.cpp	return local != position_of.end() && reaches(local->second, position);	return local != position_of.end() && (false && reaches(local->second, position));	^unit_cross_unit_contracts_test$
xtu-internal-linkage-not-imported	compiler/elaboration/src/elaborate.cpp	converted.defined_elsewhere = candidate.contract != nullptr && function->external_linkage;	converted.defined_elsewhere = candidate.contract != nullptr;	^negative_cross_tu$
xtu-internal-linkage-not-exported	compiler/obligations/src/contracts.cpp	    if (function.external_linkage) {	    if (true) {	^e2e_cross_tu$|^unit_cross_unit_contracts_test$
xtu-restatements-agree	compiler/obligations/src/contracts.cpp	if (!restatements_agree(function, pure_definitions, program, engine)) {	if (false && !restatements_agree(function, pure_definitions, program, engine)) {	^negative_cross_tu$|^unit_cross_unit_contracts_test$
xtu-trusted-through-import	compiler/obligations/src/trust.cpp	return !claim.premises.empty() || std::ranges::any_of(claim.imported	return !claim.premises.empty() || std::ranges::any_of(std::vector<ImportedDependency>{}	^e2e_cross_tu$
xtu-unsafe-through-import	compiler/obligations/src/trust.cpp	return !claim.unsafe.empty() || std::ranges::any_of(claim.imported	return !claim.unsafe.empty() || std::ranges::any_of(std::vector<ImportedDependency>{}	^e2e_cross_tu$
xtu-import-not-assumption-free	compiler/driver/src/driver.cpp	return claim.premises.empty() && claim.unsafe.empty() && claim.imported.empty() && claim.library.empty();	return claim.premises.empty() && claim.unsafe.empty() && claim.library.empty();	^e2e_cross_tu$|^negative_cross_tu$
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
member-call-writes-object	clang/src/bridge.cpp	const bool writes = (callee_receiver.has_value() && callee_receiver->writes()) ||	const bool writes = false ||	^negative_verified_methods$
const-receiver-mutable-member	clang/src/bridge.cpp	if (constant && !leaf.mutable_member) {	if (constant && (true || !leaf.mutable_member)) {	^negative_verified_methods$
virtual-member-refused	clang/src/bridge.cpp	if (clang_CXXMethod_isVirtual(cursor) != 0) {	if (false && clang_CXXMethod_isVirtual(cursor) != 0) {	^negative_verified_methods$
virtual-call-refused	clang/src/bridge.cpp	if (clang_CXXMethod_isVirtual(referenced) != 0) {	if (false && clang_CXXMethod_isVirtual(referenced) != 0) {	^negative_verified_methods$
member-refinement-kept	clang/src/bridge.cpp	converted.refinements = std::move(*refinements);	(void)refinements;	^negative_verified_methods$
reference-aggregate-witness	clang/src/bridge.cpp	if (parameter.type.kind == TypeKind::Value && source::aliases_storage(parameter.passing) &&	if (false && parameter.type.kind == TypeKind::Value && source::aliases_storage(parameter.passing) &&	^negative_verified_storage$
unsafe-member-write-rooted	clang/src/bridge.cpp	return access.has_value() && !access->dereferenced && clang_equalCursors(access->declaration, declaration) != 0;	return access.has_value() && access->path.empty() && !access->dereferenced && clang_equalCursors(access->declaration, declaration) != 0;	^negative_unsafe_boundary$
signed-overflow-owed	compiler/obligations/src/definedness.cpp	return type.is_integer() && type.integer_type().is_signed;	return false && type.is_integer();	^negative_signed_arithmetic$
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
                 receiver-caller-storage)

multiline_file() {
    case "$1" in
        callee-body-linkage|call-precondition-gate) echo "compiler/automation/src/composition.cpp" ;;
        receiver-caller-storage) echo "clang/src/bridge.cpp" ;;
        *) echo "kernel/src/check.cpp" ;;
    esac
}

multiline_tests() {
    case "$1" in
        callee-body-linkage) echo '^unit_contracts_test$' ;;
        receiver-caller-storage) echo '^negative_verified_methods$' ;;
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
# every enforcement of it is still caught.
equivalent_justification() {
    case "$1" in
        # `Composition::spend` is charged against the dependency whose evidence
        # is read on the next line, and refuses an unproven one there. The gate
        # refuses the same stage earlier. With both present, removing the gate
        # changes when the search stops, never whether it stops or what it
        # concludes: it refuses, terminates, and builds no proof either way.
        #
        # Stated directly in tests/unit/contracts_test.cpp by
        # an_unproven_dependency_is_refused_before_its_evidence_is_read, so
        # removing spend's check -- the enforcement that remains -- fails that
        # test rather than going unnoticed.
        call-precondition-gate)
            echo "Composition::spend refuses the same unproven dependency before reading its evidence;" \
                 "that enforcement is itself mutated as 'spend-dependency-proven', which is caught" ;;
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
while [ $# -gt 0 ]; do
    case "$1" in
        --only) only+=("$2"); shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --list) list_only=1; shift ;;
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
    if ! printf '%s\n' "$known" | grep -qx -- "$name"; then
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
    if field "$1" 2 | grep -q .; then field "$1" 2; else multiline_file "$1"; fi
}

spec_tests() {
    if field "$1" 5 | grep -q .; then field "$1" 5; else multiline_tests "$1"; fi
}

spec_before() {
    if field "$1" 3 | grep -q .; then field "$1" 3; else multiline_before "$1"; fi
}

spec_after() {
    if field "$1" 4 | grep -q .; then field "$1" 4; else multiline_after "$1"; fi
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
run=$(mktemp -d "$artifacts/run-XXXXXX")
source_copy="$run/source"
build="$run/build"

echo "Mutation artifacts: $run"

# rsync keeps the copy cheap and leaves the checkout untouched.
rsync -a --exclude .git --exclude 'build' --exclude 'build-*' --exclude tmp \
      --exclude .code-review-graph --exclude .claude --exclude .codex \
      "$root/" "$source_copy/"

# A green control run establishes that a later test failure was introduced by
# the mutation rather than being there all along.
cmake -S "$source_copy" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCPPL_WARNINGS_AS_ERRORS=ON > "$run/configure.log" 2>&1 ||
    { echo "Control configure failed; see $run/configure.log" >&2; exit 1; }
cmake --build "$build" -j "$jobs" > "$run/build.log" 2>&1 ||
    { echo "Control build failed; see $run/build.log" >&2; exit 1; }
ctest --test-dir "$build" --output-on-failure -j "$jobs" > "$run/baseline.log" 2>&1 ||
    { echo "Control baseline failed; see $run/baseline.log" >&2; exit 1; }

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
