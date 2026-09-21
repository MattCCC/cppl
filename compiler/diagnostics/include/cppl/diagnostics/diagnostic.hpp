#pragma once

#include "cppl/source/location.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cppl::diagnostics {

enum class Severity : std::uint8_t {
    Note,
    Warning,
    Error,
};

// What kind of failure this is. Categories are structured data: policy,
// tooling and reports consume the category, never the rendered wording
// (ARCHITECTURE.md 38, 40).
enum class Category : std::uint8_t {
    CpplSyntax,           // malformed C++L construct
    CppSemantic,          // Clang rejected the C++
    Elaboration,          // the construct could not be given formal meaning
    UnsupportedSemantics, // well-formed, but outside the modeled fragment
    ProofFailure,         // an obligation was not discharged
    KernelRejection,      // the kernel refused the evidence offered
    Policy,               // the build policy refuses the result
    Style,                // canonical formatting is violated; never blocks a build
    Internal,             // the compiler failed; never a verification result
};

std::string describe(Category category);
std::string describe(Severity severity);

struct Note {
    std::string message;
    source::SourceLocation location;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    Category category = Category::Internal;
    std::string message;
    source::SourceLocation location;
    std::vector<Note> notes;
};

class Engine {
  public:
    void report(Diagnostic diagnostic);

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] bool has_errors() const noexcept {
        return errors_ != 0;
    }
    [[nodiscard]] std::size_t error_count() const noexcept {
        return errors_;
    }

  private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t errors_ = 0;
};

std::string render(const Diagnostic& diagnostic);

} // namespace cppl::diagnostics
