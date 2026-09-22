#include "cppl/lsp/decomposition_view.hpp"

#include <algorithm>
#include <string>

namespace cppl::lsp {
namespace {

// Whether `offset` falls inside the arm block of `statement`.
//
// `arms_span` covers '{' through '}' inclusive, so an offset on either brace
// counts as inside: completing right after typing '{' is the common case.
bool encloses(const frontend::ProofStatement& statement, std::size_t offset) {
    const source::ByteSpan& span = statement.arms_span;
    return span.length != 0 && offset >= span.offset && offset <= span.end();
}

bool is_decomposition(const frontend::ProofStatement& statement) {
    return statement.kind == frontend::ProofStatementKind::Cases ||
           statement.kind == frontend::ProofStatementKind::Decompose;
}

// The innermost decomposition statement containing `offset`, searching the
// proof-step tree depth-first.
//
// Innermost wins: an arm of an outer `cases` may hold another `cases`, and the
// editor is completing in whichever one the cursor actually sits in.
const frontend::ProofStatement* innermost(const std::vector<frontend::ProofStatement>& statements, std::size_t offset) {
    const frontend::ProofStatement* found = nullptr;
    for (const frontend::ProofStatement& statement : statements) {
        for (const frontend::ProofArm& arm : statement.arms) {
            if (const frontend::ProofStatement* nested = innermost(arm.statements, offset)) {
                return nested;
            }
        }
        if (is_decomposition(statement) && encloses(statement, offset)) {
            found = &statement;
        }
    }
    return found;
}

// The engine's record for the statement written at `location`.
//
// Records are matched by source location because that is what both sides
// already carry unchanged: the record is produced from the same
// `ProofStatement::location` the recognizer assigned.
const elaboration::SubjectStates* record_for(const std::vector<elaboration::SubjectStates>& recorded,
                                             const source::SourceLocation& location) {
    const auto match = std::ranges::find_if(
        recorded, [&](const elaboration::SubjectStates& entry) { return entry.location == location; });
    return match == recorded.end() ? nullptr : &*match;
}

// An arm skeleton for `state`, ready to insert.
//
// The binder list is the provider's own, so the inserted arm already has the
// right number of binders for the payload the state carries.
std::string arm_skeleton(const elaboration::SubjectStates::State& state) {
    std::string text = state.label;
    if (!state.binders.empty()) {
        text += "(";
        for (std::size_t i = 0; i < state.binders.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += state.binders[i];
        }
        text += ")";
    }
    return text + " => {\n    \n}";
}

} // namespace

std::optional<CaseSite> enclosing_case_site(const frontend::Syntax& syntax,
                                            const std::vector<elaboration::SubjectStates>& recorded,
                                            std::size_t offset) {
    const frontend::ProofStatement* statement = nullptr;
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        statement = innermost(proof.statements, offset);
        if (statement != nullptr) {
            break;
        }
    }
    if (statement == nullptr) {
        return std::nullopt;
    }
    return CaseSite{statement, record_for(recorded, statement->location)};
}

std::vector<CompletionItem> missing_case_completions(const CaseSite& site) {
    if (site.statement == nullptr || site.states == nullptr) {
        return {};
    }

    std::vector<CompletionItem> items;
    for (std::size_t i = 0; i < site.states->states.size(); ++i) {
        const elaboration::SubjectStates::State& state = site.states->states[i];
        const bool written = std::ranges::any_of(
            site.statement->arms, [&](const frontend::ProofArm& arm) { return arm.spelling == state.label; });
        if (written) {
            continue;
        }

        CompletionItem item;
        item.label = state.label;
        // A label a representation reserves has no C++ expression to resolve;
        // one spelled as an enumerator does. That is the distinction
        // `decomposition::LabelKind` draws, surfaced for the editor's icon.
        item.kind =
            state.label.find("::") == std::string::npos ? CompletionItemKind::Keyword : CompletionItemKind::EnumMember;
        item.detail = site.states->representation;
        item.documentation =
            state.residual ? "The state completing " + site.states->representation +
                                 "'s partition, derived by negating the others. It is a real semantic state, "
                                 "not a catch-all."
                           : "A state of " + site.states->representation + ", from " + site.states->provider + ".";
        item.insertText = arm_skeleton(state);
        // Two digits keep the provider's own order stable; no representation
        // here lists more than the 64 arms the recognizer admits.
        item.sortText = (i < 10 ? "0" : "") + std::to_string(i);
        items.push_back(std::move(item));
    }
    return items;
}

std::optional<Hover> case_site_hover(const CaseSite& site) {
    if (site.statement == nullptr || site.states == nullptr) {
        return std::nullopt;
    }

    const elaboration::SubjectStates& states = *site.states;
    std::string text = "`" + states.subject + "` : `" + states.representation + "`\n\n";
    text += states.product ? "Product decomposition, from " : "Sum decomposition, from ";
    text += states.provider + ".\n\n";

    for (const elaboration::SubjectStates::State& state : states.states) {
        const bool written = std::ranges::any_of(
            site.statement->arms, [&](const frontend::ProofArm& arm) { return arm.spelling == state.label; });
        text += written ? "- [x] `" : "- [ ] `";
        text += state.label;
        if (!state.binders.empty()) {
            text += "(";
            for (std::size_t i = 0; i < state.binders.size(); ++i) {
                if (i != 0) {
                    text += ", ";
                }
                text += state.binders[i];
            }
            text += ")";
        }
        text += "`";
        if (state.residual) {
            text += " — residual";
        }
        text += "\n";
    }
    return Hover{std::move(text), std::nullopt};
}

} // namespace cppl::lsp
