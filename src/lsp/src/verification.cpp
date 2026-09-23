#include "cppl/lsp/verification.hpp"

#include "cppl/driver/buffer_compile.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/lsp/document.hpp"
#include "cppl/lsp/editor_view.hpp"
#include "cppl/lsp/hover.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppl::lsp {

namespace {

bool same_place(const source::SourceLocation& lhs, const source::SourceLocation& rhs) {
    return lhs.line == rhs.line && lhs.column == rhs.column && normal_path(lhs.file) == normal_path(rhs.file);
}

bool within(const source::SourceLocation& location, const CpplDeclaration& declaration) {
    return location.line >= declaration.first_line && location.line <= declaration.last_line &&
           normal_path(location.file) == normal_path(declaration.name_location.file);
}

// The first line of a reason, and not all of it when it runs long.
std::string shortened(const std::string& text) {
    constexpr std::size_t kLongest = 96;
    std::string line = text.substr(0, text.find('\n'));
    if (line.size() > kLongest) {
        line.resize(kLongest);
        line += "...";
    }
    return line;
}

std::string listed(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        out += (out.empty() ? "" : ", ") + name;
    }
    return out;
}

// What a group of verdicts comes to, in one line. A proof of a Law is judged
// through the Law: it is the Law's accepted evidence, or it names a Law whose
// verdict it did not decide.
std::string summary(const Document& document, const CpplDeclaration& declaration,
                    const std::vector<const driver::ObligationRecord*>& records) {
    const CpplDeclaration::Kind kind = declaration.kind;
    if (kind == CpplDeclaration::Kind::Proof && records.size() == 1 &&
        records.front()->origin == obligations::Origin::LawProposition &&
        records.front()->written_proof != declaration.name) {
        const driver::ObligationRecord& law = *records.front();
        std::string title = "names law " + law.subject + ", which is " + obligations::describe(law.status);
        if (!law.written_proof.empty()) {
            title += " by written proof " + law.written_proof;
        } else if (law.status != obligations::Status::Proven && !law.reason.empty()) {
            title += ": " + shortened(law.reason);
        }
        return title;
    }
    if (!document.verified()) {
        return "not verified: the compile stopped before verification";
    }
    if (records.empty()) {
        return "UNRESOLVED: no obligation was produced for it; the diagnostics say why";
    }
    std::vector<std::string> premises;
    for (const driver::ObligationRecord* record : records) {
        for (const std::string& premise : record->premises) {
            if (std::ranges::find(premises, premise) == premises.end()) {
                premises.push_back(premise);
            }
        }
    }
    const std::string relative = premises.empty() ? std::string() : " relative to trusted " + listed(premises);
    if (records.size() == 1) {
        const driver::ObligationRecord& record = *records.front();
        std::string title = obligations::describe(record.status);
        if (record.status == obligations::Status::Proven) {
            title += relative;
        } else if (record.status == obligations::Status::Trusted) {
            title += ": an explicit assumption";
        } else if (!record.reason.empty()) {
            title += ": " + shortened(record.reason);
        }
        if (kind == CpplDeclaration::Kind::Proof && record.origin == obligations::Origin::LawProposition) {
            title += " as the evidence for law " + record.subject;
        }
        return title;
    }
    const auto proven = static_cast<std::size_t>(std::ranges::count_if(
        records, [](const driver::ObligationRecord* record) { return record->status == obligations::Status::Proven; }));
    if (proven == records.size()) {
        return "PROVEN: all " + std::to_string(records.size()) + " obligations" + relative;
    }
    std::string title = std::to_string(proven) + " of " + std::to_string(records.size()) + " obligations PROVEN";
    for (const obligations::Status status :
         {obligations::Status::Trusted, obligations::Status::RuntimeChecked, obligations::Status::Unsafe,
          obligations::Status::Unverified, obligations::Status::Unresolved}) {
        const auto count = std::ranges::count_if(
            records, [status](const driver::ObligationRecord* record) { return record->status == status; });
        if (count != 0) {
            title += ", " + std::to_string(count) + " " + obligations::describe(status);
        }
    }
    return title;
}

CpplDeclaration declared(CpplDeclaration::Kind kind, const std::string& name, const source::SourceLocation& at,
                         std::uint32_t first_line, std::uint32_t last_line) {
    CpplDeclaration declaration;
    declaration.kind = kind;
    declaration.name = name;
    declaration.name_location = at;
    declaration.first_line = first_line;
    declaration.last_line = last_line;
    return declaration;
}

} // namespace

std::vector<const driver::ObligationRecord*> obligations_of(const Document& document,
                                                            const CpplDeclaration& declaration) {
    std::vector<const driver::ObligationRecord*> found;
    for (const driver::ObligationRecord& record : document.obligations()) {
        bool mine = false;
        switch (declaration.kind) {
            case CpplDeclaration::Kind::Law:
            case CpplDeclaration::Kind::TrustedLaw:
                mine = (record.origin == obligations::Origin::LawProposition &&
                        same_place(record.location, declaration.name_location)) ||
                       (record.origin == obligations::Origin::OmittedCase && within(record.location, declaration));
                break;
            case CpplDeclaration::Kind::Proof:
                // A proof of a Law has no obligation of its own: its evidence
                // is the Law's, and the Law's verdict is what became of it.
                mine = (record.origin == obligations::Origin::ProofProposition &&
                        same_place(record.location, declaration.name_location)) ||
                       (record.origin == obligations::Origin::LawProposition &&
                        std::ranges::find(record.naming_proofs, declaration.name) != record.naming_proofs.end()) ||
                       (record.origin == obligations::Origin::OmittedCase && within(record.location, declaration));
                break;
            case CpplDeclaration::Kind::VerifiedFunction:
                mine = record.origin != obligations::Origin::LawProposition &&
                       record.origin != obligations::Origin::ProofProposition &&
                       record.origin != obligations::Origin::OmittedCase && within(record.location, declaration);
                break;
            case CpplDeclaration::Kind::Assumption:
            case CpplDeclaration::Kind::RefinementType:
                break;
        }
        if (mine) {
            found.push_back(&record);
        }
    }
    return found;
}

std::vector<CodeLens> verification_lenses(const Document& document) {
    std::vector<CodeLens> lenses;
    const frontend::Syntax* syntax = document.syntax();
    if (syntax == nullptr || document.verified_version() != document.version()) {
        return lenses;
    }
    const std::string path = normal_path(document.path());
    const PositionMapper mapper(document.text());
    const auto lens = [&](const CpplDeclaration& declaration) {
        if (normal_path(declaration.name_location.file) != path) {
            return; // a header's declarations are the header's to show
        }
        source::SourceLocation end = declaration.name_location;
        end.column += static_cast<std::uint32_t>(declaration.name.size());
        lenses.push_back(CodeLens{Range{mapper.source_location_to_position(declaration.name_location),
                                        mapper.source_location_to_position(end)},
                                  summary(document, declaration, obligations_of(document, declaration))});
    };
    for (const frontend::LawDeclaration& law : syntax->laws) {
        lens(declared(law.trusted ? CpplDeclaration::Kind::TrustedLaw : CpplDeclaration::Kind::Law, law.name,
                      law.name_location, law.keyword_location.line, law.end_line));
    }
    for (const frontend::ProofDeclaration& proof : syntax->proofs) {
        if (!proof.inline_law.has_value()) {
            lens(declared(CpplDeclaration::Kind::Proof, proof.name, proof.name_location, proof.keyword_location.line,
                          proof.end_line));
        }
    }
    for (const frontend::VerifiedFunction& verified : syntax->verified_functions) {
        lens(declared(CpplDeclaration::Kind::VerifiedFunction, verified.function_name, verified.function_location,
                      verified.keyword_location.line, verified.body_end_line));
    }
    std::ranges::sort(lenses, [](const CodeLens& lhs, const CodeLens& rhs) {
        return lhs.range.start.line < rhs.range.start.line ||
               (lhs.range.start.line == rhs.range.start.line && lhs.range.start.character < rhs.range.start.character);
    });
    return lenses;
}

std::string verification_markdown(const Document& document, const CpplDeclaration& declaration) {
    if (document.verified_version() != document.version()) {
        return {};
    }
    if (declaration.kind == CpplDeclaration::Kind::Assumption ||
        declaration.kind == CpplDeclaration::Kind::RefinementType) {
        return {};
    }
    if (!document.verified()) {
        return "\n\n---\n\nNot verified: the compile stopped before verification; the diagnostics say why.";
    }
    const std::vector<const driver::ObligationRecord*> records = obligations_of(document, declaration);
    if (records.empty()) {
        return {};
    }
    std::string markdown = "\n\n---\n\n";
    constexpr std::size_t kShown = 24;
    for (std::size_t index = 0; index < records.size() && index < kShown; ++index) {
        const driver::ObligationRecord& record = *records[index];
        markdown += "**" + obligations::describe(record.status) + "** " + obligations::describe(record.origin);
        if (!record.premises.empty()) {
            markdown += ", relative to trusted " + listed(record.premises);
        }
        markdown += "  \n";
        if (!record.goal.empty()) {
            markdown += "Goal: `" + record.goal + "`  \n";
        }
        if (!record.reason.empty()) {
            markdown += "Why: " + record.reason + "  \n";
        }
        if (record.status == obligations::Status::Proven && !record.strategy.empty()) {
            markdown += "Evidence: " + record.strategy + ", accepted by the kernel  \n";
        }
        markdown += "\n";
    }
    if (records.size() > kShown) {
        markdown += "and " + std::to_string(records.size() - kShown) + " more\n";
    }
    while (!markdown.empty() && (markdown.back() == '\n' || markdown.back() == ' ')) {
        markdown.pop_back();
    }
    return markdown;
}

} // namespace cppl::lsp
