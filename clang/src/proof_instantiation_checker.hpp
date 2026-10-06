#pragma once

// The walk that finds where an instantiation of a C++ template reaches
// proof-only code, and the cursor queries (proof_instantiation_cursors.cpp)
// shared by the walk (proof_instantiation.cpp) and its hazard analysis
// (proof_instantiation_hazards.cpp).

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"

#include <clang-c/CXSourceLocation.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::clangbridge::detail::instantiation {

// Types nest and records reach one another. A chain deeper than this is
// refused rather than followed (AGENTS.md 23).
inline constexpr unsigned kMaxDepth = 64;

// Defined in proof_instantiation_cursors.cpp.
std::string spelling(CXCursor cursor);

std::string usr(CXCursor cursor);

std::string type_spelling(CXType type);

std::string named(CXType type);

bool is_null(CXCursor cursor);

CXCursorKind kind_of(CXCursor cursor);

source::SourceLocation presumed_location(CXCursor cursor);

std::optional<std::size_t> main_file_offset(CXSourceLocation location);

std::vector<CXCursor> children_of(CXCursor cursor);

bool is_record(CXCursorKind kind);

bool is_template(CXCursorKind kind);

bool is_template_parameter(CXCursorKind kind);

bool is_function(CXCursorKind kind);

bool is_operator_name(std::string_view name);

std::string qualified(CXCursor cursor);

CXCursor template_of(CXCursor pattern);

bool standard(CXCursor declaration);

bool states_constraint(CXCursor cursor);

bool inert_member(CXCursor member);

bool inert_alias(CXCursor declaration);

// What proof-only text may instantiate: `key` names the template it comes
// from, so a unit is told once per template, and `description` says what it is.
struct Hazard {
    std::string key;
    std::string description;
};

// Defined in proof_instantiation_cursors.cpp.
std::optional<Hazard> through(std::optional<Hazard> hazard, const std::string& carrier);

std::optional<Hazard> considered(std::optional<Hazard> hazard, CXCursor function, const std::string& name);

Hazard outside_template(CXCursor pattern, const std::string& what);

class Checker {
  public:
    Checker(CXTranslationUnit unit, const Selection& selection)
        : spans_(selection.proof_only),
          prefix_(selection.specification_prefix),
          defined_probes_(selection.defined_clause_probes),
          translation_unit_(unit) {}

    // Every proof-only construct under `root`.
    void walk(CXCursor root);

    // A specialization reached from its use. Its body stands at the pattern's
    // offsets, so the proof-only text in it is recognized as in the pattern.
    void walk_specialization(CXCursor specialization);

    std::vector<Diagnostic> take_findings();

  private:
    static CXChildVisitResult visit(CXCursor cursor, CXCursor, CXClientData data);

    void visit_subtree(CXCursor cursor);

    [[nodiscard]] bool proof_only(std::size_t offset) const;

    // Whether [begin, end] meets any proof-only span.
    [[nodiscard]] bool overlaps(std::size_t begin, std::size_t end) const;

    [[nodiscard]] bool generated_name(std::string_view name) const;

    // The projector's clause probes, whose parameters restate the verified
    // function's own.
    [[nodiscard]] bool contract_probe(std::string_view name) const;

    // Whether a declaration, or a record enclosing it, is one the projector
    // generated. A formal namespace is not: what an author writes in a Law
    // stands in one.
    [[nodiscard]] bool generated(CXCursor declaration) const;

    // The projector's own scaffolding, which holds nothing the author wrote: a
    // helper template it instantiates only at types read elsewhere, the
    // reference that makes a verified template instantiate its contract, the
    // reference that reaches what an explicit instantiation already
    // instantiated, and the alias a refinement lowers to in both programs.
    [[nodiscard]] bool scaffolding(CXCursorKind kind, std::string_view name, bool inside) const;

    CXChildVisitResult enter(CXCursor cursor);

    // A clause probe restates the verified function's template header, its
    // constraints and its parameters, and declares a `result` of its return
    // type. The function's declaration stands before the probe in both
    // programs and names the same entities, so only the clause, the probe's
    // body, is the author's proof-only text. A parameter restates what the
    // program has completed already when it is a reference, which completes
    // nothing, or when the function is defined, which completes every
    // parameter and its return type. Otherwise the probe, a definition, would
    // complete it first, so it is checked as any other.
    void enter_probe(CXCursor probe);

    void check(CXCursor cursor, CXCursorKind kind);

    // A call's overload resolution considers every function its name finds,
    // and deducing a function template's arguments can instantiate what its
    // signature names, whichever function is then selected. A member call
    // considers the class's members of that name; any other call, and every
    // operator, the namespace-scope functions and friends of that name too,
    // which is wider than what lookup finds.
    void check_call(CXCursor call, CXCursor referenced);

    // A built-in operator with an operand of class or enumeration type was
    // chosen by overload resolution over every operator function in scope.
    void check_operator(CXCursor expression, CXCursorKind kind);

    std::optional<Hazard> namespace_candidates(const std::function<bool(std::string_view)>& matches);

    std::optional<Hazard> member_candidates(CXCursor owner, const std::function<bool(std::string_view)>& matches,
                                            unsigned depth);

    // Every function and function template outside the implementation that a
    // call could find by its name: those of a namespace, and every friend a
    // class declares, which argument-dependent lookup finds. Partial
    // specializations are noted on the way.
    void inventory();

    // A declaration a proof-only reference names.
    std::optional<Hazard> reference_hazard(CXCursor referenced, unsigned depth);

    // A declaration of the implementation, or one the projector generated, is
    // trusted to mean what it means wherever it is instantiated (TRUST.md
    // TCB-SOURCE-010). What it is instantiated at is the program's, and is
    // checked: its signature, its type, and every specialization enclosing it.
    std::optional<Hazard> implementation_hazard(CXCursor declaration, unsigned depth);

    // A member of a specialization of a template outside the implementation.
    // Reading a data member, a member type or an enumerator instantiates the
    // specialization, which is judged as a whole (`declaration_hazard`); any
    // other member is instantiated on its own where it is used.
    std::optional<Hazard> member_hazard(CXCursor member, unsigned depth);

    // A template outside the implementation, named. Naming a class template
    // is also what deducing its arguments starts from, which deduces every
    // guide written for it, so an inert one with a guide is refused where its
    // name is written, though a value of its specializations is not.
    std::optional<Hazard> template_hazard(CXCursor declaration);

    // Whether a deduction guide is written for the class template.
    bool guided(CXCursor declaration);

    // Whether instantiating a class template can change nothing outside the
    // specialization and mean the same wherever it happens: one defined at
    // namespace scope as data members, member types and member enumerations
    // alone, with no expression in it but an enumerator's built-in arithmetic
    // on literals, no base, no friend, no member function, no constraint and
    // no default template argument, and with no partial specialization a later
    // declaration could make a use select. What it holds is then fixed by its
    // arguments, which are checked where it is used. A template never defined
    // is never instantiated.
    bool inert(CXCursor declaration);

    // What proof-only text may instantiate through a value or a declaration of
    // `type`: every type it is built from.
    std::optional<Hazard> type_hazard(CXType type, unsigned depth);

    // The type arguments of a specialization, and of every specialization it
    // is a member of.
    std::optional<Hazard> arguments_hazard(CXType type, unsigned depth);

    // What naming a value of the class, enumeration or template `declaration`
    // may instantiate. A record's verdict is kept, except one reached while a
    // record it depends on was still being decided: a class can reach itself
    // through a pointer member, and the verdict found inside that cycle is
    // only provisional.
    std::optional<Hazard> declaration_hazard(CXCursor declaration, CXType type, unsigned depth);

    std::optional<Hazard> record_hazard(CXCursor declaration, CXType type, unsigned depth);

    // The specializations an implementation's record is nested in.
    std::optional<Hazard> implementation_hazard_scopes(CXCursor declaration, unsigned depth);

    // The data members an instantiation holds, at the types it holds them.
    std::optional<Hazard> fields_hazard(CXType type, const std::string& name, unsigned depth);

    // What using a value of a class may instantiate without any use of it
    // naming more: its bases, its data members, each constructor, conversion,
    // destructor and assignment it may call implicitly, and every template it
    // declares, a member's or a friend's, which overload resolution may
    // deduce. A class not defined in this unit has nothing to instantiate.
    std::optional<Hazard> class_hazard(CXCursor declaration, const std::string& name, unsigned depth);

    void report(CXCursor at, std::optional<Hazard> hazard);

    const std::vector<source::ByteSpan>& spans_;
    const std::string& prefix_;
    const std::vector<std::string>& defined_probes_;
    CXTranslationUnit translation_unit_;

    std::vector<Diagnostic> findings_;
    std::set<std::string> reported_;

    bool inventoried_ = false;
    std::map<std::string, std::vector<CXCursor>> functions_;
    std::set<std::string> partially_specialized_;
    std::map<std::string, bool> inert_;

    std::map<std::string, Hazard> hazards_;
    std::set<std::string> harmless_;
    std::set<std::string> deciding_;
    std::size_t provisional_ = 0;
};

} // namespace cppl::clangbridge::detail::instantiation
