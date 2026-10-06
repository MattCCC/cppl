#pragma once

// The unit text, fixtures and mapping checks the projection tests
// (unit_projection_test) are written with.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>

namespace projection_test_detail {

inline const std::string kUnit = "# 1 \"main.cpp\"\n"
                                 "pure int identity(int x) {\n"
                                 "    return x;\n"
                                 "}\n"
                                 "law identity_returns_input(int x)\n"
                                 "    proves (identity(x) == x);\n"
                                 "proof identity_returns_input_holds(int x)\n"
                                 "    proves (identity_returns_input(x))\n"
                                 "{\n"
                                 "    refl;\n"
                                 "}\n"
                                 "law identity_of_zero()\n"
                                 "    proves (identity(0) == 0);\n"
                                 "proof identity_of_zero_holds()\n"
                                 "    proves (identity_of_zero())\n"
                                 "{\n"
                                 "    exact identity_returns_input_holds(0);\n"
                                 "}\n"
                                 "law identity_under_a_premise(int x)\n"
                                 "    expects (identity(x) == 0)\n"
                                 "    proves (identity(x) == x);\n"
                                 "proof identity_under_a_premise_holds(int x)\n"
                                 "    proves (identity_under_a_premise(x))\n"
                                 "{\n"
                                 "    assume h : identity(x) == 0;\n"
                                 "    refl;\n"
                                 "}\n"
                                 "int main() { return identity(0); }\n";

// The source map editors read the analysis text through: every run it says was
// kept or copied spells, where it says it now is, exactly what was written, and
// what was kept is in order and never overlaps what was generated.
inline bool maps_exactly(const std::string& text, const std::string& name) {
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, name);
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    std::size_t kept_to = 0;
    std::size_t written_to = 0;
    for (const auto& segment : projection.segments) {
        if (segment.analysis < kept_to || segment.original < written_to || segment.length == 0 ||
            projection.analysis.compare(segment.analysis, segment.length, text, segment.original, segment.length) !=
                0) {
            return false;
        }
        kept_to = segment.analysis + segment.length;
        written_to = segment.original + segment.length;
    }
    for (const auto& copy : projection.copies) {
        if (copy.original.length == 0 || projection.analysis.compare(copy.analysis, copy.original.length, text,
                                                                     copy.original.offset, copy.original.length) != 0) {
            return false;
        }
        for (const auto& segment : projection.segments) {
            if (copy.analysis < segment.analysis + segment.length &&
                segment.analysis < copy.analysis + copy.original.length) {
                return false;
            }
        }
    }
    return true;
}

inline std::string read_fixture(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace projection_test_detail
