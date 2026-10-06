#pragma once

// Recognition of a source text, as the recognizer tests
// (unit_recognizer_test) run it.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

#include <string>

namespace recognizer_test_detail {

struct Recognized {
    cppl::diagnostics::Engine engine;
    cppl::frontend::Syntax syntax;
};

inline void recognize(const std::string& text, Recognized& out) {
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(text, "main.cpp");
    out.syntax = cppl::frontend::recognize(stream, out.engine);
}

} // namespace recognizer_test_detail
