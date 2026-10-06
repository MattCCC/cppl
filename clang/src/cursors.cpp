#include "cppl/source/location.hpp"
#include "places.hpp"

#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <string>
#include <vector>

// What libclang reports of a cursor, a type and a location, read the one way
// every unit of the Clang bridge reads it (places.hpp).
namespace cppl::clangbridge::detail::bridge {

namespace {

class ScopedString {
  public:
    explicit ScopedString(CXString value) : value_(value) {}
    ~ScopedString() {
        clang_disposeString(value_);
    }

    ScopedString(const ScopedString&) = delete;
    ScopedString& operator=(const ScopedString&) = delete;
    ScopedString(ScopedString&&) = delete;
    ScopedString& operator=(ScopedString&&) = delete;

    [[nodiscard]] std::string str() const {
        const char* text = clang_getCString(value_);
        return text != nullptr ? std::string(text) : std::string();
    }

  private:
    CXString value_;
};

} // namespace

std::string take(CXString value) {
    return ScopedString(value).str();
}

source::SourceLocation presumed_location(CXSourceLocation location) {
    CXString file{};
    unsigned line = 0;
    unsigned column = 0;
    clang_getPresumedLocation(location, &file, &line, &column);

    source::SourceLocation result;
    result.file = take(file);
    result.line = line;
    result.column = column;
    return result;
}

std::vector<CXCursor> children_of(CXCursor cursor) {
    std::vector<CXCursor> children;
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            static_cast<std::vector<CXCursor>*>(data)->push_back(child);
            return CXChildVisit_Continue;
        },
        &children);
    return children;
}

// This asks the type rather than walking the definition's cursor children,
// because an instantiated class template specialization has no children: Clang
// instantiates the members without exposing cursors for them, so a cursor walk
// reports a specialization as having no members at all (SPEC.md TEMPLATE-001).
// A record and an instantiation of a class template are the same kind of
// product here, so both are decomposed by the one route.
std::vector<CXCursor> record_fields(CXType record) {
    std::vector<CXCursor> fields;
    clang_Type_visitFields(
        clang_getCanonicalType(record),
        [](CXCursor field, CXClientData data) {
            static_cast<std::vector<CXCursor>*>(data)->push_back(field);
            return CXVisit_Continue;
        },
        &fields);
    return fields;
}

// A base carries state that `record_fields` does not report, so a record with
// one is not decomposed by its members alone. Asking the type matters for the
// same reason: an instantiation exposes no base-specifier cursor either, so a
// cursor walk would report a derived specialization as having no base and would
// silently model it as its own members (AGENTS.md 8).
bool record_has_base(CXType record) {
    unsigned bases = 0;
    clang_visitCXXBaseClasses(
        clang_getCanonicalType(record),
        [](CXCursor, CXClientData data) {
            ++*static_cast<unsigned*>(data);
            return CXVisit_Break;
        },
        &bases);
    return bases != 0;
}

CXCursor strip_parens(CXCursor cursor) {
    while (clang_getCursorKind(cursor) == CXCursor_UnexposedExpr || clang_getCursorKind(cursor) == CXCursor_ParenExpr) {
        const auto inner = children_of(cursor);
        if (inner.size() != 1)
            break;
        cursor = inner[0];
    }
    return cursor;
}

} // namespace cppl::clangbridge::detail::bridge
