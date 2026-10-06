#pragma once

#include "cppl/clang/ast.hpp"

#include <clang-c/Index.h>
#include <string>
#include <vector>

// Ghost state in a verified body (ghost.cpp, where each is documented).
namespace cppl::clangbridge::detail {

// What a verified body declares as ghost state, and every error in how it
// declares or uses it (SPEC.md GHOST-001, GHOST-002).
//
// Ghost state leaves the program before it runs, so nothing that runs may name
// it: not a returned value, a branch, an index, an argument, an initializer or
// a write. The only places that may are what leaves with it, the initializer of
// another ghost declaration and the specification expressions the projector
// declared under its own prefix (loop clauses, claim arguments, split subjects).
// Every other reference is an error where it stands, whatever path it is on.
class GhostScan {
  public:
    explicit GhostScan(std::string prefix);

    void run(CXCursor body);

    std::vector<Function::GhostError> errors;
    std::vector<Function::GhostCall> calls;

  private:
    void error(std::string message, std::string note, CXCursor at);

    [[nodiscard]] bool is_ghost(CXCursor declaration) const;

    void declare(CXCursor cursor, unsigned depth);

    void admit(CXCursor declaration);

    void collect_calls(CXCursor cursor, const std::string& ghost, unsigned depth);

    void leaks(CXCursor cursor, bool proof_only, unsigned depth);

    std::string prefix_;
    std::vector<CXCursor> ghosts_;
};

bool ghost_marker_of(CXCursor statement, const std::string& prefix);

} // namespace cppl::clangbridge::detail
