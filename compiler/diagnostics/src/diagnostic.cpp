#include "cppl/diagnostics/diagnostic.hpp"

#include "cppl/source/location.hpp"

#include <string>
#include <utility>

namespace cppl::diagnostics {

std::string describe(Category category) {
    switch (category) {
        case Category::CpplSyntax:
            return "cppl-syntax";
        case Category::CppSemantic:
            return "cpp-semantic";
        case Category::Elaboration:
            return "elaboration";
        case Category::UnsupportedSemantics:
            return "unsupported-semantics";
        case Category::ProofFailure:
            return "proof-failure";
        case Category::KernelRejection:
            return "kernel-rejection";
        case Category::VerificationInterface:
            return "verification-interface";
        case Category::Policy:
            return "policy";
        case Category::Style:
            return "style";
        case Category::Internal:
            return "internal";
    }
    return "unknown";
}

std::string describe(Severity severity) {
    switch (severity) {
        case Severity::Note:
            return "note";
        case Severity::Warning:
            return "warning";
        case Severity::Error:
            return "error";
    }
    return "unknown";
}

void Engine::report(Diagnostic diagnostic) {
    if (diagnostic.severity == Severity::Error) {
        ++errors_;
    }
    diagnostics_.push_back(std::move(diagnostic));
}

std::string render(const Diagnostic& diagnostic) {
    std::string text;
    if (diagnostic.location.is_valid()) {
        text += source::describe(diagnostic.location);
        text += ": ";
    }
    text += describe(diagnostic.severity);
    text += " [";
    text += describe(diagnostic.category);
    text += "]: ";
    text += diagnostic.message;

    for (const Note& note : diagnostic.notes) {
        text += "\n  ";
        if (note.location.is_valid()) {
            text += source::describe(note.location);
            text += ": ";
        }
        text += "note: ";
        text += note.message;
    }
    return text;
}

} // namespace cppl::diagnostics
