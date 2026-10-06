#pragma once

// An editor unit's state: the translation unit Clang parsed and what the
// editor services read from it, shared by the files that implement them.

#include "cppl/clang/editor.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <clang-c/CXErrorCode.h>
#include <clang-c/CXFile.h>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::clangbridge {

namespace detail::editor {

// Defined in editor.cpp.
std::string take(CXString value);

bool is_null(CXCursor cursor);

bool same(CXCursor lhs, CXCursor rhs);

bool in_main_file(CXCursor cursor, CXCursor parent);

std::vector<CXUnsavedFile> unsaved_view(const std::vector<FileContent>& files);

// Parsing keeps a preamble of the headers the main file starts by including,
// so a reparse after an edit below them skips them. Warnings are never read.
inline constexpr unsigned kReadOnce = CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_KeepGoing;
inline constexpr unsigned kParseOptions =
    kReadOnce | CXTranslationUnit_PrecompiledPreamble | CXTranslationUnit_CreatePreambleOnFirstParse |
    CXTranslationUnit_CacheCompletionResults | CXTranslationUnit_IncludeBriefCommentsInCodeCompletion;

} // namespace detail::editor

using detail::editor::is_null;
using detail::editor::kParseOptions;
using detail::editor::kReadOnce;
using detail::editor::take;
using detail::editor::unsaved_view;

struct EditorUnit::State {
    Options options;
    FileContent main;
    std::vector<FileContent> unsaved;
    CXIndex index = nullptr;
    CXTranslationUnit unit = nullptr;
    // What each file the unit read holds, found once per parse: Clang finds a
    // file's contents by searching every file it loaded.
    mutable std::map<CXFile, std::string_view> contents;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

    ~State() {
        if (unit != nullptr) {
            clang_disposeTranslationUnit(unit);
        }
        if (index != nullptr) {
            clang_disposeIndex(index);
        }
    }

    // The main file first, so a request naming it finds the text it was given.
    [[nodiscard]] std::vector<FileContent> files() const {
        std::vector<FileContent> all;
        all.reserve(unsaved.size() + 1);
        all.push_back(main);
        for (const FileContent& file : unsaved) {
            if (file.path != main.path) {
                all.push_back(file);
            }
        }
        return all;
    }

    [[nodiscard]] std::expected<void, std::string> parse() {
        std::vector<std::string> words;
        words.reserve(options.arguments.size() + 4);
        words.push_back(options.driver);
        words.insert(words.end(), options.arguments.begin(), options.arguments.end());
        words.emplace_back("-w");
        // A C++L document is often named `.cppl`, which says nothing to Clang;
        // what it reads is C++ either way.
        words.emplace_back("-x");
        words.emplace_back("c++");
        std::vector<const char*> argv;
        argv.reserve(words.size());
        for (const std::string& word : words) {
            argv.push_back(word.c_str());
        }

        const std::vector<FileContent> all = files();
        std::vector<CXUnsavedFile> view = unsaved_view(all);
        CXTranslationUnit parsed = nullptr;
        const CXErrorCode error = clang_parseTranslationUnit2FullArgv(
            index, main.path.c_str(), argv.data(), static_cast<int>(argv.size()), view.data(),
            static_cast<unsigned>(view.size()), options.once ? kReadOnce : kParseOptions, &parsed);
        if (error != CXError_Success || parsed == nullptr) {
            return std::unexpected("Clang could not parse '" + main.path + "' for editor services");
        }
        unit = parsed;
        contents.clear();
        return {};
    }

    [[nodiscard]] std::string_view contents_of(CXFile file) const {
        const auto known = contents.find(file);
        if (known != contents.end()) {
            return known->second;
        }
        std::size_t size = 0;
        const char* text = clang_getFileContents(unit, file, &size);
        return contents.emplace(file, text == nullptr ? std::string_view() : std::string_view(text, size))
            .first->second;
    }

    [[nodiscard]] CXFile main_file() const {
        return clang_getFile(unit, main.path.c_str());
    }

    [[nodiscard]] FilePosition place(CXSourceLocation location) const {
        FilePosition result;
        CXFile file = nullptr;
        unsigned line = 0;
        unsigned column = 0;
        unsigned offset = 0;
        clang_getFileLocation(location, &file, &line, &column, &offset);
        if (file != nullptr) {
            result.file = take(clang_getFileName(file));
            CXFile main_entry = main_file();
            result.in_main_file = main_entry != nullptr && clang_File_isEqual(file, main_entry) != 0;
        }
        result.line = line;
        result.column = column;
        result.offset = offset;

        CXString presumed_file{};
        unsigned presumed_line = 0;
        unsigned presumed_column = 0;
        clang_getPresumedLocation(location, &presumed_file, &presumed_line, &presumed_column);
        result.presumed.file = take(presumed_file);
        result.presumed.line = presumed_line;
        result.presumed.column = presumed_column;

        result.in_system_header = clang_Location_isInSystemHeader(location) != 0;
        return result;
    }

    // The extent of a declaration's name, where the declaration spells it.
    [[nodiscard]] std::optional<Extent> name_extent(CXCursor cursor) const {
        if (clang_getCursorKind(cursor) == CXCursor_InclusionDirective) {
            return std::nullopt;
        }
        const CXSourceRange range = clang_Cursor_getSpellingNameRange(cursor, 0, 0);
        if (clang_Range_isNull(range) == 0) {
            Extent extent{place(clang_getRangeStart(range)), place(clang_getRangeEnd(range))};
            if (!extent.begin.file.empty() && extent.end.offset > extent.begin.offset) {
                return extent;
            }
        }
        // A macro's definition, and anything else Clang names no range for,
        // is its location and the length of its name.
        const FilePosition begin = place(clang_getCursorLocation(cursor));
        if (begin.file.empty()) {
            return std::nullopt;
        }
        const std::string name = take(clang_getCursorSpelling(cursor));
        FilePosition end = begin;
        end.offset += name.size();
        end.column += static_cast<std::uint32_t>(name.size());
        return Extent{begin, end};
    }

    // Where a declaration's name starts, as `name_extent` finds it.
    [[nodiscard]] static std::optional<CXSourceLocation> name_start(CXCursor cursor) {
        if (clang_getCursorKind(cursor) == CXCursor_InclusionDirective) {
            return std::nullopt;
        }
        const CXSourceRange range = clang_Cursor_getSpellingNameRange(cursor, 0, 0);
        if (clang_Range_isNull(range) == 0) {
            CXFile file = nullptr;
            unsigned begin = 0;
            unsigned end = 0;
            clang_getFileLocation(clang_getRangeStart(range), &file, nullptr, nullptr, &begin);
            clang_getFileLocation(clang_getRangeEnd(range), nullptr, nullptr, nullptr, &end);
            if (file != nullptr && end > begin) {
                return clang_getRangeStart(range);
            }
        }
        return clang_getCursorLocation(cursor);
    }

    [[nodiscard]] CXCursor cursor_at(std::size_t offset) const {
        CXFile file = main_file();
        if (file == nullptr) {
            return clang_getNullCursor();
        }
        return clang_getCursor(unit, clang_getLocationForOffset(unit, file, static_cast<unsigned>(offset)));
    }

    // The token written at `offset`, when one is: its kind and spelling.
    [[nodiscard]] std::optional<std::pair<CXTokenKind, std::string>> token_at(std::size_t offset) const {
        CXFile file = main_file();
        if (file == nullptr) {
            return std::nullopt;
        }
        CXToken* token = clang_getToken(unit, clang_getLocationForOffset(unit, file, static_cast<unsigned>(offset)));
        if (token == nullptr) {
            return std::nullopt;
        }
        std::pair<CXTokenKind, std::string> result{clang_getTokenKind(*token),
                                                   take(clang_getTokenSpelling(unit, *token))};
        clang_disposeTokens(unit, token, 1);
        return result;
    }

    // What the name written at `offset` denotes: every declaration an
    // unresolved template name could mean, or the one it does. An operator is
    // a name only where it calls an overloaded one.
    [[nodiscard]] std::vector<CXCursor> named_at(std::size_t offset) const {
        std::vector<CXCursor> targets;
        const auto token = token_at(offset);
        const CXCursor cursor = cursor_at(offset);
        if (!token.has_value() || is_null(cursor) || clang_getCursorKind(cursor) == CXCursor_InclusionDirective) {
            return targets;
        }
        if (clang_getCursorKind(cursor) == CXCursor_OverloadedDeclRef) {
            const unsigned count = clang_getNumOverloadedDecls(cursor);
            for (unsigned candidate = 0; candidate < count; ++candidate) {
                targets.push_back(clang_getOverloadedDecl(cursor, candidate));
            }
            return targets;
        }
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (is_null(referenced)) {
            return targets;
        }
        const bool operator_call =
            token->first == CXToken_Punctuation && take(clang_getCursorSpelling(referenced)).starts_with("operator");
        if (token->first == CXToken_Identifier || operator_call) {
            targets.push_back(referenced);
        }
        return targets;
    }

    // Whether the text Clang read spells `name` at `start`, and if so the
    // extent it spells it over.
    [[nodiscard]] std::optional<Extent> spelled(CXSourceLocation start, std::string_view name) const {
        CXFile file = nullptr;
        unsigned offset = 0;
        clang_getFileLocation(start, &file, nullptr, nullptr, &offset);
        if (file == nullptr || name.empty()) {
            return std::nullopt;
        }
        const std::string_view text = contents_of(file);
        if (offset + name.size() > text.size() || text.substr(offset, name.size()) != name) {
            return std::nullopt;
        }
        FilePosition begin = place(start);
        FilePosition end = begin;
        end.offset += name.size();
        end.column += static_cast<std::uint32_t>(name.size());
        return Extent{begin, end};
    }

    struct Walk {
        const State* state = nullptr;
        // Occurrences of these; or, when `everything`, every occurrence of
        // every name.
        const std::vector<std::string>* usrs = nullptr;
        bool everything = false;
        // When set, each use of one of those names that is not written where
        // it is used -- a macro's body spells it -- is noted here too.
        std::vector<Occurrence>* unwritten = nullptr;
        // The operands an assignment or an increment writes, met before them.
        std::vector<CXCursor> written;
        std::vector<Occurrence> found;
    };

    static CXCursor first_child(CXCursor cursor) {
        CXCursor first = clang_getNullCursor();
        clang_visitChildren(
            cursor,
            [](CXCursor child, CXCursor, CXClientData data) {
                *static_cast<CXCursor*>(data) = child;
                return CXChildVisit_Break;
            },
            &first);
        return first;
    }

    static bool writes(CXCursor cursor, CXCursorKind kind) {
        if (kind == CXCursor_BinaryOperator || kind == CXCursor_CompoundAssignOperator) {
            const CXBinaryOperatorKind operation = clang_getCursorBinaryOperatorKind(cursor);
            return operation >= CXBinaryOperator_Assign && operation <= CXBinaryOperator_OrAssign;
        }
        if (kind == CXCursor_UnaryOperator) {
            const CXUnaryOperatorKind operation = clang_getCursorUnaryOperatorKind(cursor);
            return operation == CXUnaryOperator_PostInc || operation == CXUnaryOperator_PostDec ||
                   operation == CXUnaryOperator_PreInc || operation == CXUnaryOperator_PreDec;
        }
        return false;
    }

    static bool is_reference(CXCursorKind kind) {
        switch (kind) {
            case CXCursor_DeclRefExpr:
            case CXCursor_MemberRefExpr:
            case CXCursor_TypeRef:
            case CXCursor_TemplateRef:
            case CXCursor_NamespaceRef:
            case CXCursor_MemberRef:
            case CXCursor_VariableRef:
            case CXCursor_LabelRef:
            case CXCursor_OverloadedDeclRef:
            case CXCursor_MacroExpansion:
                return true;
            default:
                return false;
        }
    }

    void note_declaration(CXCursor cursor, Walk& walk) const {
        const std::string name = take(clang_getCursorSpelling(cursor));
        std::string usr = take(clang_getCursorUSR(cursor));
        if (walk.everything ? usr.empty() || name.empty() : std::ranges::find(*walk.usrs, usr) == walk.usrs->end()) {
            return;
        }
        const std::optional<CXSourceLocation> start = name_start(cursor);
        if (!start.has_value()) {
            return;
        }
        if (std::optional<Extent> written = spelled(*start, name)) {
            walk.found.push_back(Occurrence{*written, std::move(usr), Role::Declaration});
        }
    }

    void note_reference(CXCursor cursor, CXCursorKind kind, Walk& walk) const {
        std::vector<CXCursor> targets;
        if (kind == CXCursor_OverloadedDeclRef) {
            const unsigned count = clang_getNumOverloadedDecls(cursor);
            for (unsigned candidate = 0; candidate < count; ++candidate) {
                targets.push_back(clang_getOverloadedDecl(cursor, candidate));
            }
        } else {
            targets.push_back(clang_getCursorReferenced(cursor));
        }
        for (const CXCursor target : targets) {
            if (is_null(target)) {
                continue;
            }
            std::string usr = take(clang_getCursorUSR(target));
            if (usr.empty() || (!walk.everything && std::ranges::find(*walk.usrs, usr) == walk.usrs->end())) {
                continue;
            }
            // An expression's extent starts at its qualifier or its object; its
            // name is where the name range says.
            const CXSourceRange range = kind == CXCursor_DeclRefExpr || kind == CXCursor_MemberRefExpr
                                            ? clang_getCursorReferenceNameRange(cursor, CXNameRange_WantSinglePiece, 0)
                                            : clang_getCursorExtent(cursor);
            const std::string name = take(clang_getCursorSpelling(target));
            if (std::optional<Extent> written = spelled(clang_getRangeStart(range), name)) {
                // Cursor identity includes the declaration a visit came
                // through, which differs between the walk and the visit that
                // found the operand, so the operand is matched by where it is.
                const bool write = std::ranges::any_of(walk.written, [&](CXCursor operand) {
                    return clang_getCursorKind(operand) == kind &&
                           clang_equalLocations(clang_getCursorLocation(operand), clang_getCursorLocation(cursor)) != 0;
                });
                walk.found.push_back(Occurrence{*written, std::move(usr), write ? Role::Write : Role::Read});
            } else if (walk.unwritten != nullptr) {
                // Clang places a use a macro's body spells where the macro is
                // expanded, which does not spell the name.
                const FilePosition expanded = place(clang_getRangeStart(range));
                if (!expanded.file.empty()) {
                    walk.unwritten->push_back(Occurrence{Extent{expanded, expanded}, std::move(usr), Role::Read});
                }
            }
        }
    }

    static CXChildVisitResult visit(CXCursor cursor, CXCursor, CXClientData data) {
        auto& walk = *static_cast<Walk*>(data);
        if (clang_Location_isInSystemHeader(clang_getCursorLocation(cursor)) != 0) {
            return CXChildVisit_Continue;
        }
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (writes(cursor, kind)) {
            walk.written.push_back(first_child(cursor));
        }
        if (clang_isDeclaration(kind) != 0 || kind == CXCursor_MacroDefinition) {
            walk.state->note_declaration(cursor, walk);
        } else if (is_reference(kind)) {
            walk.state->note_reference(cursor, kind, walk);
        }
        return CXChildVisit_Recurse;
    }

    // Each constructor's and destructor's identity, with its class's.
    using Members = std::vector<std::pair<std::string, std::string>>;

    static CXChildVisitResult member_visit(CXCursor cursor, CXCursor, CXClientData data) {
        if (clang_Location_isInSystemHeader(clang_getCursorLocation(cursor)) != 0) {
            return CXChildVisit_Continue;
        }
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_Constructor || kind == CXCursor_Destructor) {
            static_cast<Members*>(data)->emplace_back(take(clang_getCursorUSR(cursor)),
                                                      take(clang_getCursorUSR(clang_getCursorSemanticParent(cursor))));
        }
        return CXChildVisit_Recurse;
    }

    [[nodiscard]] std::vector<std::string> renamed_together(const std::string& usr) const {
        Members members;
        if (unit != nullptr) {
            clang_visitChildren(clang_getTranslationUnitCursor(unit), member_visit, &members);
        }
        std::string owner = usr;
        for (const auto& [member, parent] : members) {
            if (member == usr) {
                owner = parent;
            }
        }
        std::vector<std::string> together = {owner};
        for (const auto& [member, parent] : members) {
            if (parent == owner && std::ranges::find(together, member) == together.end()) {
                together.push_back(member);
            }
        }
        if (std::ranges::find(together, usr) == together.end()) {
            together.push_back(usr);
        }
        return together;
    }

    struct OutlineWalk {
        const State* state = nullptr;
        std::vector<Symbol>* into = nullptr;
    };

    static CXChildVisitResult outline_visit(CXCursor cursor, CXCursor, CXClientData data);

    // The main file's tokens Clang lexed in `range`, released with the object.
    class Tokens {
      public:
        Tokens(CXTranslationUnit unit, CXSourceRange range) : unit_(unit) {
            clang_tokenize(unit_, range, &tokens_, &count_);
        }
        ~Tokens() {
            if (tokens_ != nullptr) {
                clang_disposeTokens(unit_, tokens_, count_);
            }
        }
        Tokens(const Tokens&) = delete;
        Tokens& operator=(const Tokens&) = delete;
        Tokens(Tokens&&) = delete;
        Tokens& operator=(Tokens&&) = delete;

        [[nodiscard]] unsigned size() const noexcept {
            return count_;
        }
        [[nodiscard]] CXToken* data() const noexcept {
            return tokens_;
        }
        [[nodiscard]] CXToken operator[](unsigned position) const {
            return std::span(tokens_, count_)[position];
        }
        // A punctuator's spelling, and nothing for any other token.
        [[nodiscard]] std::string punctuator(unsigned position) const {
            return clang_getTokenKind((*this)[position]) == CXToken_Punctuation
                       ? take(clang_getTokenSpelling(unit_, (*this)[position]))
                       : std::string();
        }

      private:
        CXTranslationUnit unit_;
        CXToken* tokens_ = nullptr;
        unsigned count_ = 0;
    };

    // The `{` through the `}` of what `cursor` declares, where it has a body:
    // the last `}` of its extent and the `{` it closes.
    [[nodiscard]] std::optional<Extent> declared_body(CXCursor cursor) const {
        const Tokens tokens(unit, clang_getCursorExtent(cursor));
        if (tokens.size() == 0 || tokens.punctuator(tokens.size() - 1) != "}") {
            return std::nullopt;
        }
        const unsigned close = tokens.size() - 1;
        unsigned depth = 0;
        for (unsigned position = close + 1; position-- > 0;) {
            const std::string spelling = tokens.punctuator(position);
            if (spelling == "}") {
                ++depth;
            } else if (spelling == "{" && --depth == 0) {
                return Extent{place(clang_getTokenLocation(unit, tokens[position])),
                              place(clang_getRangeEnd(clang_getTokenExtent(unit, tokens[close])))};
            }
        }
        return std::nullopt;
    }

    // What a block or a braced initializer spans, when Clang's extent for it
    // is its braces as written.
    [[nodiscard]] std::optional<Extent> braced(CXCursor cursor) const {
        const CXSourceRange range = clang_getCursorExtent(cursor);
        Extent extent{place(clang_getRangeStart(range)), place(clang_getRangeEnd(range))};
        const std::string& text = main.text;
        if (!extent.begin.in_main_file || !extent.end.in_main_file || extent.end.offset <= extent.begin.offset ||
            extent.end.offset > text.size() || text[extent.begin.offset] != '{' || text[extent.end.offset - 1] != '}') {
            return std::nullopt;
        }
        return extent;
    }

    // Where `cursor` starts in the main file, when that is where it is written
    // rather than where a macro that wrote it is used.
    [[nodiscard]] std::optional<FilePosition> written_start(CXCursor cursor) const {
        const CXSourceLocation start = clang_getRangeStart(clang_getCursorExtent(cursor));
        CXFile expanded_in = nullptr;
        CXFile spelled_in = nullptr;
        unsigned expanded = 0;
        unsigned spelled = 0;
        clang_getExpansionLocation(start, &expanded_in, nullptr, nullptr, &expanded);
        clang_getSpellingLocation(start, &spelled_in, nullptr, nullptr, &spelled);
        if (expanded_in == nullptr || spelled_in == nullptr || clang_File_isEqual(expanded_in, spelled_in) == 0 ||
            expanded != spelled) {
            return std::nullopt;
        }
        FilePosition at = place(start);
        if (!at.in_main_file) {
            return std::nullopt;
        }
        return at;
    }

    // The names of what `callee` declares its parameters as, in order.
    static std::vector<std::string> parameter_names(CXCursor callee) {
        std::vector<std::string> names;
        clang_visitChildren(
            callee,
            [](CXCursor child, CXCursor, CXClientData data) {
                if (clang_getCursorKind(child) == CXCursor_ParmDecl) {
                    static_cast<std::vector<std::string>*>(data)->push_back(take(clang_getCursorSpelling(child)));
                }
                return CXChildVisit_Continue;
            },
            &names);
        return names;
    }

    void parameter_hints(CXCursor call, std::vector<Hint>& into) const {
        const CXCursor callee = clang_getCursorReferenced(call);
        const int arguments = clang_Cursor_getNumArguments(call);
        if (is_null(callee) || arguments <= 0 || take(clang_getCursorSpelling(callee)).starts_with("operator")) {
            return;
        }
        // A call a macro's body writes is annotated nowhere: its arguments
        // stand, at best, where the macro is used.
        const std::optional<FilePosition> call_start = written_start(call);
        if (!call_start.has_value()) {
            return;
        }
        const std::vector<std::string> names = parameter_names(callee);
        const auto count = std::min(names.size(), static_cast<std::size_t>(arguments));
        for (std::size_t position = 0; position < count; ++position) {
            // A library's reserved spelling, `__x`, is shown as the name it is.
            std::string_view name = names[position];
            while (name.starts_with('_')) {
                name.remove_prefix(1);
            }
            const CXCursor argument = clang_Cursor_getArgument(call, static_cast<unsigned>(position));
            // Where the argument is written, or where the macro that writes
            // it is used.
            const FilePosition at = place(clang_getRangeStart(clang_getCursorExtent(argument)));
            // A default argument is written nowhere in the call, and an
            // implicit conversion or construction writes no call around it.
            if (name.empty() || !at.in_main_file || call_start->offset == at.offset) {
                continue;
            }
            const FilePosition end = place(clang_getRangeEnd(clang_getCursorExtent(argument)));
            if (end.in_main_file && end.offset == at.offset + name.size() &&
                std::string_view(main.text).substr(at.offset, name.size()) == name) {
                continue; // the argument spells its parameter's name already
            }
            into.push_back(Hint{Hint::Kind::Parameter, at, std::string(name) + ":"});
        }
    }

    void type_hint(CXCursor variable, std::vector<Hint>& into) const {
        CXType declared = clang_getCursorType(variable);
        bool is_auto = false;
        for (bool looking = true; looking;) {
            switch (declared.kind) {
                case CXType_Auto:
                    is_auto = true;
                    looking = false;
                    break;
                case CXType_Pointer:
                case CXType_LValueReference:
                case CXType_RValueReference:
                    declared = clang_getPointeeType(declared);
                    break;
                case CXType_Elaborated:
                    declared = clang_Type_getNamedType(declared);
                    break;
                default:
                    looking = false;
                    break;
            }
        }
        const std::string type = take(clang_getTypeSpelling(clang_getCursorType(variable)));
        // Not yet deduced (in a template), a lambda's unnameable type, or a
        // type too long to read inline.
        constexpr std::size_t kLongest = 32;
        if (!is_auto || type.empty() || type.find("auto") != std::string::npos ||
            type.find("lambda") != std::string::npos || type.size() > kLongest || !written_start(variable)) {
            return;
        }
        const std::optional<Extent> name = name_extent(variable);
        if (name.has_value() && name->end.in_main_file) {
            into.push_back(Hint{Hint::Kind::Type, name->end, ": " + type});
        }
    }

    // Each branch of each conditional directive in the main file, paired as
    // the preprocessor pairs them: a branch runs from its directive's `#` to
    // the `#` of the directive after it.
    void conditional_branches(std::vector<Fold>& into) const {
        CXFile file = main_file();
        if (file == nullptr) {
            return;
        }
        const Tokens tokens(
            unit, clang_getRange(clang_getLocationForOffset(unit, file, 0),
                                 clang_getLocationForOffset(unit, file, static_cast<unsigned>(main.text.size()))));
        const auto line_of = [&](unsigned position) {
            unsigned line = 0;
            clang_getFileLocation(clang_getTokenLocation(unit, tokens[position]), nullptr, &line, nullptr, nullptr);
            return line;
        };
        std::vector<FilePosition> open;
        unsigned previous_line = 0;
        for (unsigned position = 0; position + 1 < tokens.size(); ++position) {
            const unsigned line = line_of(position);
            const bool starts_line = position == 0 || line != previous_line;
            previous_line = line;
            if (!starts_line || tokens.punctuator(position) != "#" || line_of(position + 1) != line) {
                continue;
            }
            const std::string directive = take(clang_getTokenSpelling(unit, tokens[position + 1]));
            const FilePosition at = place(clang_getTokenLocation(unit, tokens[position]));
            const bool opens = directive == "if" || directive == "ifdef" || directive == "ifndef";
            const bool continues =
                directive == "elif" || directive == "elifdef" || directive == "elifndef" || directive == "else";
            if ((continues || directive == "endif") && !open.empty()) {
                into.push_back(Fold{Fold::Kind::Conditional, Extent{open.back(), at}});
                open.pop_back();
            }
            if (opens || continues) {
                open.push_back(at);
            }
        }
    }

    // Occurrences of `usrs`, or of every name when it is null.
    [[nodiscard]] std::vector<Occurrence> walk(const std::vector<std::string>* usrs,
                                               std::vector<Occurrence>* unwritten = nullptr) const {
        Walk walk;
        walk.state = this;
        walk.usrs = usrs;
        walk.everything = usrs == nullptr;
        walk.unwritten = unwritten;
        if (unit != nullptr) {
            clang_visitChildren(clang_getTranslationUnitCursor(unit), visit, &walk);
        }
        // A name reached twice, as a template's and as its instantiation's,
        // is written once.
        std::vector<Occurrence> unique;
        for (Occurrence& occurrence : walk.found) {
            const bool repeated = std::ranges::any_of(unique, [&](const Occurrence& known) {
                return known.name.begin.file == occurrence.name.begin.file &&
                       known.name.begin.offset == occurrence.name.begin.offset;
            });
            if (!repeated) {
                unique.push_back(std::move(occurrence));
            }
        }
        return unique;
    }
};

namespace detail::editor {

// Defined in editor_navigation.cpp.
CXType pointee_of(CXType type);

} // namespace detail::editor

} // namespace cppl::clangbridge
