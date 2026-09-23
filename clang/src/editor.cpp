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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace cppl::clangbridge {

namespace {

std::string take(CXString value) {
    const char* text = clang_getCString(value);
    std::string result = text != nullptr ? std::string(text) : std::string();
    clang_disposeString(value);
    return result;
}

bool is_null(CXCursor cursor) {
    return clang_Cursor_isNull(cursor) != 0 || clang_isInvalid(clang_getCursorKind(cursor)) != 0;
}

bool same(CXCursor lhs, CXCursor rhs) {
    return clang_equalCursors(lhs, rhs) != 0;
}

// What a unit reads in place of the disk. The strings stay owned by `files`,
// which outlives every call that is handed the result.
std::vector<CXUnsavedFile> unsaved_view(const std::vector<FileContent>& files) {
    std::vector<CXUnsavedFile> view;
    view.reserve(files.size());
    for (const FileContent& file : files) {
        CXUnsavedFile entry{};
        entry.Filename = file.path.c_str();
        entry.Contents = file.text.data();
        entry.Length = static_cast<unsigned long>(file.text.size());
        view.push_back(entry);
    }
    return view;
}

// Parsing keeps a preamble of the headers the main file starts by including,
// so a reparse after an edit below them skips them. Warnings are never read.
constexpr unsigned kParseOptions =
    CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_KeepGoing |
    CXTranslationUnit_PrecompiledPreamble | CXTranslationUnit_CreatePreambleOnFirstParse |
    CXTranslationUnit_CacheCompletionResults | CXTranslationUnit_IncludeBriefCommentsInCodeCompletion;

} // namespace

struct EditorUnit::State {
    Options options;
    FileContent main;
    std::vector<FileContent> unsaved;
    CXIndex index = nullptr;
    CXTranslationUnit unit = nullptr;

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
            static_cast<unsigned>(view.size()), kParseOptions, &parsed);
        if (error != CXError_Success || parsed == nullptr) {
            return std::unexpected("Clang could not parse '" + main.path + "' for editor services");
        }
        unit = parsed;
        return {};
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
            const CXFile main_entry = main_file();
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

    [[nodiscard]] CXCursor cursor_at(std::size_t offset) const {
        const CXFile file = main_file();
        if (file == nullptr) {
            return clang_getNullCursor();
        }
        return clang_getCursor(unit, clang_getLocationForOffset(unit, file, static_cast<unsigned>(offset)));
    }

    // The token written at `offset`, when one is: its kind and spelling.
    [[nodiscard]] std::optional<std::pair<CXTokenKind, std::string>> token_at(std::size_t offset) const {
        const CXFile file = main_file();
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
        std::size_t size = 0;
        const char* contents = clang_getFileContents(unit, file, &size);
        if (contents == nullptr || offset + name.size() > size ||
            std::string_view(contents + offset, name.size()) != name) {
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
        // Occurrences of these, or else declarations spelled `name`.
        const std::vector<std::string>* usrs = nullptr;
        std::string_view name;
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
        std::string usr;
        if (walk.usrs != nullptr) {
            usr = take(clang_getCursorUSR(cursor));
            if (std::ranges::find(*walk.usrs, usr) == walk.usrs->end()) {
                return;
            }
        } else if (name != walk.name) {
            return;
        } else {
            usr = take(clang_getCursorUSR(cursor));
        }
        const std::optional<Extent> extent = name_extent(cursor);
        if (!extent.has_value()) {
            return;
        }
        const CXFile file = clang_getFile(unit, extent->begin.file.c_str());
        if (file == nullptr) {
            return;
        }
        if (std::optional<Extent> written =
                spelled(clang_getLocationForOffset(unit, file, static_cast<unsigned>(extent->begin.offset)), name)) {
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
            if (std::ranges::find(*walk.usrs, usr) == walk.usrs->end()) {
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
        } else if (walk.usrs != nullptr && is_reference(kind)) {
            walk.state->note_reference(cursor, kind, walk);
        }
        return CXChildVisit_Recurse;
    }

    [[nodiscard]] std::vector<Occurrence> walk(const std::vector<std::string>* usrs, std::string_view name) const {
        Walk walk;
        walk.state = this;
        walk.usrs = usrs;
        walk.name = name;
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

EditorUnit::EditorUnit(std::unique_ptr<State> state) : state_(std::move(state)) {}

EditorUnit::~EditorUnit() = default;

std::expected<std::unique_ptr<EditorUnit>, std::string> EditorUnit::parse(Options options, FileContent main,
                                                                          std::vector<FileContent> unsaved) {
    auto state = std::make_unique<State>();
    state->options = std::move(options);
    state->main = std::move(main);
    state->unsaved = std::move(unsaved);
    state->index = clang_createIndex(/*excludeDeclarationsFromPCH=*/0, /*displayDiagnostics=*/0);
    if (state->index == nullptr) {
        return std::unexpected("could not create a Clang index");
    }
    if (auto parsed = state->parse(); !parsed) {
        return std::unexpected(parsed.error());
    }
    return std::unique_ptr<EditorUnit>(new EditorUnit(std::move(state)));
}

std::expected<void, std::string> EditorUnit::reparse(FileContent main, std::vector<FileContent> unsaved) {
    State& state = *state_;
    state.main = std::move(main);
    state.unsaved = std::move(unsaved);
    const std::vector<FileContent> all = state.files();
    std::vector<CXUnsavedFile> view = unsaved_view(all);
    const int failed = clang_reparseTranslationUnit(state.unit, static_cast<unsigned>(view.size()), view.data(),
                                                    clang_defaultReparseOptions(state.unit));
    if (failed == 0) {
        return {};
    }
    // A unit that failed to reparse is unusable, and only a fresh parse
    // recovers it.
    clang_disposeTranslationUnit(state.unit);
    state.unit = nullptr;
    return state.parse();
}

const std::string& EditorUnit::main_path() const noexcept {
    return state_->main.path;
}

const std::string& EditorUnit::main_text() const noexcept {
    return state_->main.text;
}

namespace {

// Types a declaration's own type leads to: through pointers, references and
// arrays to what they refer to, and through sugar to what it names.
CXType pointee_of(CXType type) {
    for (int depth = 0; depth < 32; ++depth) {
        switch (type.kind) {
            case CXType_Pointer:
            case CXType_LValueReference:
            case CXType_RValueReference:
            case CXType_BlockPointer:
            case CXType_MemberPointer:
                type = clang_getPointeeType(type);
                continue;
            case CXType_ConstantArray:
            case CXType_IncompleteArray:
            case CXType_VariableArray:
            case CXType_DependentSizedArray:
                type = clang_getArrayElementType(type);
                continue;
            case CXType_Elaborated:
                type = clang_Type_getNamedType(type);
                continue;
            case CXType_Auto:
            case CXType_Unexposed: {
                const CXType canonical = clang_getCanonicalType(type);
                if (canonical.kind == type.kind) {
                    return type;
                }
                type = canonical;
                continue;
            }
            default:
                return type;
        }
    }
    return type;
}

struct ImplementationSearch {
    std::string usr;
    bool method = false;
    std::vector<CXCursor> found;
};

bool overrides(CXCursor method, const std::string& usr, int depth = 0) {
    if (depth > 16) {
        return false;
    }
    CXCursor* overridden = nullptr;
    unsigned count = 0;
    clang_getOverriddenCursors(method, &overridden, &count);
    bool result = false;
    for (unsigned index = 0; index < count && !result; ++index) {
        result = take(clang_getCursorUSR(overridden[index])) == usr || overrides(overridden[index], usr, depth + 1);
    }
    clang_disposeOverriddenCursors(overridden);
    return result;
}

// Whether `record` derives from the class `usr` names, directly or through
// another base.
bool derives(CXCursor record, const std::string& usr, int depth = 0);

CXChildVisitResult find_base(CXCursor child, CXCursor, CXClientData data) {
    auto& [usr, depth, found] = *static_cast<std::tuple<const std::string*, int, bool>*>(data);
    if (clang_getCursorKind(child) != CXCursor_CXXBaseSpecifier) {
        return CXChildVisit_Continue;
    }
    const CXCursor base = clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(child)));
    if (is_null(base)) {
        return CXChildVisit_Continue;
    }
    const CXCursor definition = clang_getCursorDefinition(base);
    if (take(clang_getCursorUSR(base)) == *usr || (!is_null(definition) && derives(definition, *usr, depth + 1))) {
        found = true;
        return CXChildVisit_Break;
    }
    return CXChildVisit_Continue;
}

bool derives(CXCursor record, const std::string& usr, int depth) {
    if (depth > 16) {
        return false;
    }
    std::tuple<const std::string*, int, bool> search{&usr, depth, false};
    clang_visitChildren(record, find_base, &search);
    return std::get<2>(search);
}

CXChildVisitResult find_implementations(CXCursor cursor, CXCursor, CXClientData data) {
    auto& search = *static_cast<ImplementationSearch*>(data);
    if (clang_Location_isInSystemHeader(clang_getCursorLocation(cursor)) != 0) {
        return CXChildVisit_Continue;
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (search.method) {
        if (kind == CXCursor_CXXMethod && overrides(cursor, search.usr)) {
            search.found.push_back(cursor);
        }
    } else if ((kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl || kind == CXCursor_ClassTemplate) &&
               clang_isCursorDefinition(cursor) != 0 && derives(cursor, search.usr)) {
        search.found.push_back(cursor);
    }
    return CXChildVisit_Recurse;
}

} // namespace

std::vector<Extent> EditorUnit::navigate(Destination destination, std::size_t offset) const {
    const State& state = *state_;
    std::vector<Extent> results;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return results;
    }

    const auto token = state.token_at(offset);
    if (!token.has_value()) {
        return results;
    }
    const CXCursor cursor = state.cursor_at(offset);
    if (is_null(cursor)) {
        return results;
    }

    // An `#include` leads to the file it includes, wherever on its line the
    // request is made.
    if (clang_getCursorKind(cursor) == CXCursor_InclusionDirective) {
        if (destination != Destination::Definition && destination != Destination::Declaration) {
            return results;
        }
        CXFile included = clang_getIncludedFile(cursor);
        if (included == nullptr) {
            return results;
        }
        FilePosition start;
        start.file = take(clang_getFileName(included));
        start.line = 1;
        start.column = 1;
        results.push_back(Extent{start, start});
        return results;
    }

    const bool deduced = token->first == CXToken_Keyword && (token->second == "auto" || token->second == "decltype");
    std::vector<CXCursor> targets;
    if (deduced) {
        // `auto` names the type it was deduced as.
        const CXCursor declared = clang_getTypeDeclaration(pointee_of(clang_getCursorType(cursor)));
        if (!is_null(declared)) {
            targets.push_back(declared);
        }
        destination = destination == Destination::Implementation ? destination : Destination::Definition;
    } else {
        targets = state.named_at(offset);
    }

    const auto add = [&](CXCursor target) {
        if (is_null(target)) {
            return;
        }
        if (std::optional<Extent> extent = state.name_extent(target)) {
            const bool repeated = std::ranges::any_of(results, [&](const Extent& known) {
                return known.begin.file == extent->begin.file && known.begin.offset == extent->begin.offset;
            });
            if (!repeated) {
                results.push_back(std::move(*extent));
            }
        }
    };

    for (const CXCursor target : targets) {
        switch (destination) {
            case Destination::Definition: {
                const CXCursor definition = clang_getCursorDefinition(target);
                if (is_null(definition)) {
                    add(target);
                    break;
                }
                // Asked at the definition itself, the answer is where it was
                // first declared, where that is somewhere else.
                if (same(definition, cursor)) {
                    const CXCursor canonical = clang_getCanonicalCursor(definition);
                    add(same(canonical, definition) ? definition : canonical);
                    break;
                }
                add(definition);
                break;
            }
            case Destination::Declaration:
                add(clang_getCanonicalCursor(target));
                break;
            case Destination::TypeDefinition: {
                CXType type = clang_getCursorType(target);
                const CXCursorKind kind = clang_getCursorKind(target);
                if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod || kind == CXCursor_FunctionTemplate) {
                    type = clang_getCursorResultType(target);
                }
                const CXCursor declared = clang_getTypeDeclaration(pointee_of(type));
                if (is_null(declared)) {
                    break;
                }
                const CXCursor definition = clang_getCursorDefinition(declared);
                add(is_null(definition) ? declared : definition);
                break;
            }
            case Destination::Implementation: {
                const CXCursorKind kind = clang_getCursorKind(target);
                ImplementationSearch search;
                search.usr = take(clang_getCursorUSR(target));
                if (search.usr.empty()) {
                    break;
                }
                if (kind == CXCursor_CXXMethod) {
                    if (clang_CXXMethod_isVirtual(target) == 0) {
                        break;
                    }
                    search.method = true;
                } else if (kind != CXCursor_ClassDecl && kind != CXCursor_StructDecl &&
                           kind != CXCursor_ClassTemplate) {
                    break;
                }
                clang_visitChildren(clang_getTranslationUnitCursor(state.unit), find_implementations, &search);
                for (const CXCursor found : search.found) {
                    add(found);
                }
                break;
            }
        }
    }
    return results;
}

std::vector<Entity> EditorUnit::entities_at(std::size_t offset) const {
    const State& state = *state_;
    std::vector<Entity> entities;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return entities;
    }
    for (const CXCursor target : state.named_at(offset)) {
        std::string usr = take(clang_getCursorUSR(target));
        if (usr.empty()) {
            continue;
        }
        entities.push_back(Entity{std::move(usr), take(clang_getCursorSpelling(target)),
                                  state.name_extent(clang_getCanonicalCursor(target))});
    }
    return entities;
}

std::vector<Occurrence> EditorUnit::occurrences(const std::vector<std::string>& usrs) const {
    return state_->walk(&usrs, {});
}

std::vector<Occurrence> EditorUnit::declarations_named(std::string_view name) const {
    return state_->walk(nullptr, name);
}

std::vector<std::string> EditorUnit::included_files() const {
    std::vector<std::string> files;
    if (state_->unit == nullptr) {
        return files;
    }
    struct Visit {
        CXTranslationUnit unit;
        std::vector<std::string>* files;
    } visit{state_->unit, &files};
    clang_getInclusions(
        state_->unit,
        [](CXFile included, CXSourceLocation*, unsigned depth, CXClientData data) {
            auto& [unit, found] = *static_cast<Visit*>(data);
            if (depth == 0) {
                return; // the main file itself
            }
            if (clang_Location_isInSystemHeader(clang_getLocation(unit, included, 1, 1)) != 0) {
                return;
            }
            std::string name = take(clang_getFileName(included));
            if (std::ranges::find(*found, name) == found->end()) {
                found->push_back(std::move(name));
            }
        },
        &visit);
    return files;
}

} // namespace cppl::clangbridge
