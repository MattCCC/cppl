#include "cppl/clang/editor.hpp"

#include "cppl/source/location.hpp"

#include <algorithm>
#include <cctype>
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
#include <ranges>
#include <span>
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
        CXFile file = clang_getFile(unit, extent->begin.file.c_str());
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

namespace {

std::string kind_in_words(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_FunctionDecl:
            return "function";
        case CXCursor_FunctionTemplate:
            return "function template";
        case CXCursor_CXXMethod:
            return "method";
        case CXCursor_Constructor:
            return "constructor";
        case CXCursor_Destructor:
            return "destructor";
        case CXCursor_ConversionFunction:
            return "conversion";
        case CXCursor_VarDecl:
            return "variable";
        case CXCursor_ParmDecl:
            return "parameter";
        case CXCursor_FieldDecl:
            return "field";
        case CXCursor_StructDecl:
            return "struct";
        case CXCursor_ClassDecl:
            return "class";
        case CXCursor_UnionDecl:
            return "union";
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
            return "class template";
        case CXCursor_EnumDecl:
            return "enum";
        case CXCursor_EnumConstantDecl:
            return "enumerator";
        case CXCursor_Namespace:
            return "namespace";
        case CXCursor_NamespaceAlias:
            return "namespace alias";
        case CXCursor_TypedefDecl:
            return "typedef";
        case CXCursor_TypeAliasDecl:
            return "type alias";
        case CXCursor_TypeAliasTemplateDecl:
            return "alias template";
        case CXCursor_TemplateTypeParameter:
        case CXCursor_NonTypeTemplateParameter:
        case CXCursor_TemplateTemplateParameter:
            return "template parameter";
        case CXCursor_MacroDefinition:
            return "macro";
        case CXCursor_ConceptDecl:
            return "concept";
        case CXCursor_LabelStmt:
            return "label";
        default:
            return "declaration";
    }
}

// A comment as its author meant it read: without `//`, `///`, `/*`, `*/` or
// the `*` that starts each line of a block.
std::string comment_text(const std::string& raw) {
    std::string out;
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t end = std::min(raw.find('\n', start), raw.size());
        std::string_view line(raw.data() + start, end - start);
        const auto trim = [](std::string_view text) {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
                text.remove_prefix(1);
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
                text.remove_suffix(1);
            }
            return text;
        };
        line = trim(line);
        for (const std::string_view marker : {"///<", "//!<", "///", "//!", "//", "/**<", "/**", "/*!", "/*"}) {
            if (line.starts_with(marker)) {
                line.remove_prefix(marker.size());
                break;
            }
        }
        if (line.ends_with("*/")) {
            line.remove_suffix(2);
        }
        if (line.starts_with("*")) {
            line.remove_prefix(1);
        }
        line = trim(line);
        if (!line.empty() || (!out.empty() && !out.ends_with("\n\n"))) {
            out += line;
            out += '\n';
        }
        start = end + 1;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::string qualified(CXCursor cursor) {
    std::vector<std::string> parts{take(clang_getCursorSpelling(cursor))};
    CXCursor parent = clang_getCursorSemanticParent(cursor);
    for (int depth = 0; depth < 64 && !is_null(parent); ++depth) {
        const CXCursorKind kind = clang_getCursorKind(parent);
        if (kind != CXCursor_Namespace && kind != CXCursor_ClassDecl && kind != CXCursor_StructDecl &&
            kind != CXCursor_UnionDecl && kind != CXCursor_EnumDecl && kind != CXCursor_ClassTemplate &&
            kind != CXCursor_ClassTemplatePartialSpecialization) {
            break;
        }
        // An enumerator of an unscoped enumeration is named without it.
        if (kind == CXCursor_EnumDecl && clang_EnumDecl_isScoped(parent) == 0) {
            parent = clang_getCursorSemanticParent(parent);
            continue;
        }
        const std::string name = take(clang_getCursorSpelling(parent));
        parts.push_back(name.empty() ? "(anonymous)" : name);
        parent = clang_getCursorSemanticParent(parent);
    }
    std::string out;
    for (const std::string& part : std::views::reverse(parts)) {
        out += (out.empty() ? "" : "::") + part;
    }
    return out;
}

std::string pretty(CXCursor cursor) {
    CXPrintingPolicy policy = clang_getCursorPrintingPolicy(cursor);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_TerseOutput, 1);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_PolishForDeclaration, 1);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_IncludeTagDefinition, 0);
    std::string text = take(clang_getCursorPrettyPrinted(cursor, policy));
    clang_PrintingPolicy_dispose(policy);
    // An initializer can be a whole lambda; what a hover needs is its start.
    constexpr std::size_t kLongest = 600;
    if (text.size() > kLongest) {
        text.resize(kLongest);
        text += " ...";
    }
    return text;
}

// The value a constant has, where Clang can evaluate it. A variable that is not
// const is left out: its initializer is not the value it has everywhere.
std::string constant_value(CXCursor cursor) {
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_EnumConstantDecl) {
        return std::to_string(clang_getEnumConstantDeclValue(cursor));
    }
    if (kind != CXCursor_VarDecl || clang_isConstQualifiedType(clang_getCursorType(cursor)) == 0) {
        return {};
    }
    CXEvalResult result = clang_Cursor_Evaluate(cursor);
    if (result == nullptr) {
        return {};
    }
    std::string value;
    switch (clang_EvalResult_getKind(result)) {
        case CXEval_Int:
            value = clang_EvalResult_isUnsignedInt(result) != 0
                        ? std::to_string(clang_EvalResult_getAsUnsigned(result))
                        : std::to_string(clang_EvalResult_getAsLongLong(result));
            break;
        case CXEval_Float:
            value = std::to_string(clang_EvalResult_getAsDouble(result));
            break;
        case CXEval_StrLiteral:
            if (const char* text = clang_EvalResult_getAsStr(result)) {
                value = std::string("\"") + text + "\"";
            }
            break;
        default:
            break;
    }
    clang_EvalResult_dispose(result);
    return value;
}

} // namespace

std::optional<Description> EditorUnit::describe(std::size_t offset) const {
    const State& state = *state_;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return std::nullopt;
    }
    CXFile file = state.main_file();
    if (file == nullptr) {
        return std::nullopt;
    }
    CXToken* token =
        clang_getToken(state.unit, clang_getLocationForOffset(state.unit, file, static_cast<unsigned>(offset)));
    if (token == nullptr) {
        return std::nullopt;
    }
    const CXTokenKind token_kind = clang_getTokenKind(*token);
    const std::string spelling = take(clang_getTokenSpelling(state.unit, *token));
    const CXSourceRange token_extent = clang_getTokenExtent(state.unit, *token);
    clang_disposeTokens(state.unit, token, 1);

    Description description;
    description.named =
        Extent{state.place(clang_getRangeStart(token_extent)), state.place(clang_getRangeEnd(token_extent))};

    CXCursor target = clang_getNullCursor();
    if (token_kind == CXToken_Keyword && (spelling == "auto" || spelling == "decltype")) {
        const CXType deduced = clang_getCursorType(state.cursor_at(offset));
        if (deduced.kind == CXType_Invalid) {
            return std::nullopt;
        }
        description.type = take(clang_getTypeSpelling(deduced));
        target = clang_getTypeDeclaration(pointee_of(deduced));
        if (is_null(target)) {
            description.kind = "type";
            description.name = description.type;
            description.qualified_name = description.type;
            return description;
        }
    } else {
        const std::vector<CXCursor> targets = state.named_at(offset);
        if (targets.empty()) {
            return std::nullopt;
        }
        target = targets.front();
    }

    const CXCursorKind kind = clang_getCursorKind(target);
    description.kind = kind_in_words(kind);
    description.name = take(clang_getCursorSpelling(target));
    description.qualified_name =
        kind == CXCursor_ParmDecl || kind == CXCursor_MacroDefinition ||
                (kind == CXCursor_VarDecl && clang_getCursorSemanticParent(target).kind != CXCursor_TranslationUnit &&
                 clang_getCursorSemanticParent(target).kind != CXCursor_Namespace)
            ? description.name
            : qualified(target);

    const CXCursor definition = clang_getCursorDefinition(target);
    const CXCursor declared = is_null(definition) ? target : definition;
    description.declared = state.name_extent(declared);

    if (kind == CXCursor_MacroDefinition) {
        // A macro is its definition as written.
        const CXSourceRange extent = clang_getCursorExtent(target);
        CXFile macro_file = nullptr;
        unsigned begin = 0;
        unsigned end = 0;
        clang_getFileLocation(clang_getRangeStart(extent), &macro_file, nullptr, nullptr, &begin);
        clang_getFileLocation(clang_getRangeEnd(extent), nullptr, nullptr, nullptr, &end);
        std::size_t size = 0;
        const char* contents = macro_file != nullptr ? clang_getFileContents(state.unit, macro_file, &size) : nullptr;
        if (contents != nullptr && begin < end && end <= size) {
            description.declaration = "#define " + std::string(contents + begin, end - begin);
        }
    } else {
        description.declaration = pretty(declared);
    }

    if (description.type.empty()) {
        if (kind == CXCursor_VarDecl || kind == CXCursor_ParmDecl || kind == CXCursor_FieldDecl) {
            description.type = take(clang_getTypeSpelling(clang_getCursorType(target)));
        } else if (kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl) {
            description.type = take(clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(target)));
        }
    }
    description.value = constant_value(declared);

    if (kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl || kind == CXCursor_UnionDecl ||
        kind == CXCursor_EnumDecl || kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl) {
        const CXType type = clang_getCursorType(declared);
        const long long size = clang_Type_getSizeOf(type);
        const long long alignment = clang_Type_getAlignOf(type);
        if (size >= 0 && alignment >= 0) {
            description.size = size;
            description.alignment = alignment;
        }
    }

    for (const CXCursor candidate : {target, definition, clang_getCanonicalCursor(target)}) {
        if (!is_null(candidate) && description.documentation.empty()) {
            description.documentation = comment_text(take(clang_Cursor_getRawCommentText(candidate)));
        }
    }
    return description;
}

namespace {

Completion::Kind completion_kind(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_FunctionDecl:
        case CXCursor_FunctionTemplate:
            return Completion::Kind::Function;
        case CXCursor_CXXMethod:
        case CXCursor_Destructor:
        case CXCursor_ConversionFunction:
            return Completion::Kind::Method;
        case CXCursor_Constructor:
            return Completion::Kind::Constructor;
        case CXCursor_FieldDecl:
            return Completion::Kind::Field;
        case CXCursor_VarDecl:
            return Completion::Kind::Variable;
        case CXCursor_ParmDecl:
            return Completion::Kind::Parameter;
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
            return Completion::Kind::Class;
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
            return Completion::Kind::Struct;
        case CXCursor_EnumDecl:
            return Completion::Kind::Enum;
        case CXCursor_EnumConstantDecl:
            return Completion::Kind::Enumerator;
        case CXCursor_Namespace:
        case CXCursor_NamespaceAlias:
            return Completion::Kind::Namespace;
        case CXCursor_TypedefDecl:
        case CXCursor_TypeAliasDecl:
        case CXCursor_TypeAliasTemplateDecl:
            return Completion::Kind::TypeAlias;
        case CXCursor_TemplateTypeParameter:
        case CXCursor_NonTypeTemplateParameter:
        case CXCursor_TemplateTemplateParameter:
            return Completion::Kind::TemplateParameter;
        case CXCursor_MacroDefinition:
            return Completion::Kind::Macro;
        case CXCursor_ConceptDecl:
            return Completion::Kind::Concept;
        case CXCursor_NotImplemented:
            return Completion::Kind::Keyword;
        default:
            return Completion::Kind::Other;
    }
}

// Text a snippet shows as written: `$`, `}` and `\` would otherwise start or
// end a tab stop.
std::string snippet_escaped(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        if (character == '$' || character == '}' || character == '\\') {
            out += '\\';
        }
        out += character;
    }
    return out;
}

void render(CXCompletionString string, Completion& completion, int& placeholders) {
    const unsigned count = clang_getNumCompletionChunks(string);
    for (unsigned index = 0; index < count; ++index) {
        const CXCompletionChunkKind kind = clang_getCompletionChunkKind(string, index);
        if (kind == CXCompletionChunk_Optional) {
            // Defaulted parameters are left out, as a call usually leaves them.
            continue;
        }
        const std::string text = take(clang_getCompletionChunkText(string, index));
        switch (kind) {
            case CXCompletionChunk_TypedText:
                completion.typed += text;
                completion.label += text;
                completion.snippet += snippet_escaped(text);
                break;
            case CXCompletionChunk_ResultType:
                completion.result = text;
                break;
            case CXCompletionChunk_Placeholder:
            case CXCompletionChunk_CurrentParameter:
                completion.label += text;
                completion.snippet += "${" + std::to_string(++placeholders) + ":" + snippet_escaped(text) + "}";
                break;
            case CXCompletionChunk_Informative:
                completion.label += text;
                break;
            case CXCompletionChunk_VerticalSpace:
                completion.label += ' ';
                completion.snippet += '\n';
                break;
            default:
                completion.label += text;
                completion.snippet += snippet_escaped(text);
                break;
        }
    }
}

// The 1-based line and column of a byte offset in `text`.
std::pair<unsigned, unsigned> line_and_column(const std::string& text, std::size_t offset) {
    unsigned line = 1;
    std::size_t line_start = 0;
    for (std::size_t index = 0; index < offset && index < text.size(); ++index) {
        if (text[index] == '\n') {
            ++line;
            line_start = index + 1;
        }
    }
    return {line, static_cast<unsigned>(offset - line_start + 1)};
}

} // namespace

std::vector<Completion> EditorUnit::complete(std::size_t offset) const {
    const State& state = *state_;
    std::vector<Completion> completions;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return completions;
    }
    const auto [line, column] = line_and_column(state.main.text, offset);
    const std::vector<FileContent> all = state.files();
    std::vector<CXUnsavedFile> view = unsaved_view(all);
    CXCodeCompleteResults* results = clang_codeCompleteAt(
        state.unit, state.main.path.c_str(), line, column, view.data(), static_cast<unsigned>(view.size()),
        clang_defaultCodeCompleteOptions() | CXCodeComplete_IncludeBriefComments);
    if (results == nullptr) {
        return completions;
    }
    completions.reserve(results->NumResults);
    for (unsigned index = 0; index < results->NumResults; ++index) {
        const CXCompletionResult& result = results->Results[index];
        if (result.CursorKind == CXCursor_OverloadCandidate) {
            continue;
        }
        const CXAvailabilityKind availability = clang_getCompletionAvailability(result.CompletionString);
        if (availability == CXAvailability_NotAvailable || availability == CXAvailability_NotAccessible) {
            continue;
        }
        Completion completion;
        completion.kind = completion_kind(result.CursorKind);
        int placeholders = 0;
        render(result.CompletionString, completion, placeholders);
        if (completion.typed.empty()) {
            continue;
        }
        completion.documentation = take(clang_getCompletionBriefComment(result.CompletionString));
        completion.priority = clang_getCompletionPriority(result.CompletionString);
        completion.deprecated = availability == CXAvailability_Deprecated;
        completions.push_back(std::move(completion));
    }
    clang_disposeCodeCompleteResults(results);
    return completions;
}

std::vector<Signature> EditorUnit::signatures(std::size_t offset) const {
    const State& state = *state_;
    std::vector<Signature> found;
    if (state.unit == nullptr || offset > state.main.text.size()) {
        return found;
    }
    const auto [line, column] = line_and_column(state.main.text, offset);
    const std::vector<FileContent> all = state.files();
    std::vector<CXUnsavedFile> view = unsaved_view(all);
    CXCodeCompleteResults* results =
        clang_codeCompleteAt(state.unit, state.main.path.c_str(), line, column, view.data(),
                             static_cast<unsigned>(view.size()), CXCodeComplete_IncludeBriefComments);
    if (results == nullptr) {
        return found;
    }
    for (unsigned index = 0; index < results->NumResults; ++index) {
        const CXCompletionResult& result = results->Results[index];
        if (result.CursorKind != CXCursor_OverloadCandidate) {
            continue;
        }
        Signature signature;
        std::string result_type;
        const unsigned count = clang_getNumCompletionChunks(result.CompletionString);
        for (unsigned chunk = 0; chunk < count; ++chunk) {
            const CXCompletionChunkKind kind = clang_getCompletionChunkKind(result.CompletionString, chunk);
            if (kind == CXCompletionChunk_Optional) {
                continue;
            }
            const std::string text = take(clang_getCompletionChunkText(result.CompletionString, chunk));
            if (kind == CXCompletionChunk_ResultType) {
                result_type = text;
                continue;
            }
            if (kind == CXCompletionChunk_Placeholder || kind == CXCompletionChunk_CurrentParameter) {
                if (kind == CXCompletionChunk_CurrentParameter) {
                    signature.active = static_cast<std::uint32_t>(signature.parameters.size());
                }
                const auto start = static_cast<std::uint32_t>(signature.label.size());
                signature.label += text;
                signature.parameters.emplace_back(start, static_cast<std::uint32_t>(signature.label.size()));
                continue;
            }
            signature.label += text;
        }
        if (!result_type.empty()) {
            // The result type leads, as a declaration spells it; every
            // parameter range moves with it.
            const auto shift = static_cast<std::uint32_t>(result_type.size() + 1);
            signature.label = result_type + " " + signature.label;
            for (auto& [start, end] : signature.parameters) {
                start += shift;
                end += shift;
            }
        }
        signature.documentation = take(clang_getCompletionBriefComment(result.CompletionString));
        found.push_back(std::move(signature));
    }
    clang_disposeCodeCompleteResults(results);
    return found;
}

Scope EditorUnit::scope_at(std::size_t offset) const {
    const State& state = *state_;
    if (state.unit == nullptr) {
        return Scope::Other;
    }
    CXCursor cursor = state.cursor_at(offset);
    // Between declarations no entity is there, and what encloses the position
    // is the translation unit.
    if (is_null(cursor)) {
        return Scope::Namespace;
    }
    for (; !is_null(cursor); cursor = clang_getCursorSemanticParent(cursor)) {
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_TranslationUnit || kind == CXCursor_Namespace || kind == CXCursor_LinkageSpec) {
            return Scope::Namespace;
        }
        if (kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl ||
            kind == CXCursor_ClassTemplate) {
            return Scope::Class;
        }
        if (clang_isStatement(kind) != 0 || clang_isExpression(kind) != 0 || kind == CXCursor_FunctionDecl ||
            kind == CXCursor_CXXMethod || kind == CXCursor_Constructor || kind == CXCursor_Destructor ||
            kind == CXCursor_FunctionTemplate) {
            return Scope::Function;
        }
    }
    return Scope::Other;
}

namespace {

std::optional<Symbol::Kind> symbol_kind(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_Namespace:
            return Symbol::Kind::Namespace;
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
            return Symbol::Kind::Class;
        case CXCursor_StructDecl:
            return Symbol::Kind::Struct;
        case CXCursor_UnionDecl:
            return Symbol::Kind::Union;
        case CXCursor_EnumDecl:
            return Symbol::Kind::Enum;
        case CXCursor_EnumConstantDecl:
            return Symbol::Kind::Enumerator;
        case CXCursor_FunctionDecl:
        case CXCursor_FunctionTemplate:
            return Symbol::Kind::Function;
        case CXCursor_CXXMethod:
        case CXCursor_Destructor:
        case CXCursor_ConversionFunction:
            return Symbol::Kind::Method;
        case CXCursor_Constructor:
            return Symbol::Kind::Constructor;
        case CXCursor_FieldDecl:
            return Symbol::Kind::Field;
        case CXCursor_VarDecl:
            return Symbol::Kind::Variable;
        case CXCursor_TypedefDecl:
        case CXCursor_TypeAliasDecl:
        case CXCursor_TypeAliasTemplateDecl:
            return Symbol::Kind::TypeAlias;
        case CXCursor_MacroDefinition:
            return Symbol::Kind::Macro;
        case CXCursor_ConceptDecl:
            return Symbol::Kind::Concept;
        default:
            return std::nullopt;
    }
}

bool nests(Symbol::Kind kind) {
    return kind == Symbol::Kind::Namespace || kind == Symbol::Kind::Class || kind == Symbol::Kind::Struct ||
           kind == Symbol::Kind::Union || kind == Symbol::Kind::Enum;
}

} // namespace

CXChildVisitResult EditorUnit::State::outline_visit(CXCursor cursor, CXCursor, CXClientData data) {
    const OutlineWalk& walk = *static_cast<OutlineWalk*>(data);
    const State& state = *walk.state;
    // Every header's declarations are the unit's too; only the main file's are
    // its outline.
    if (clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) == 0) {
        return CXChildVisit_Continue;
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_LinkageSpec) {
        return CXChildVisit_Recurse; // `extern "C" { ... }` declares into its enclosing scope
    }
    const std::optional<Symbol::Kind> symbol_kind_of = symbol_kind(kind);
    if (!symbol_kind_of.has_value() || (kind == CXCursor_MacroDefinition && clang_Cursor_isMacroBuiltin(cursor) != 0)) {
        return CXChildVisit_Continue;
    }
    const CXSourceRange range = clang_getCursorExtent(cursor);
    const Extent extent{state.place(clang_getRangeStart(range)), state.place(clang_getRangeEnd(range))};
    // Clang spells an unnamed namespace or type as where it is written; it has
    // no name, so it is selected at its first byte.
    const bool anonymous = clang_Cursor_isAnonymous(cursor) != 0;
    const std::optional<Extent> name = anonymous ? Extent{extent.begin, extent.begin} : state.name_extent(cursor);
    if (!name.has_value() || !name->begin.in_main_file) {
        return CXChildVisit_Continue;
    }
    Symbol symbol;
    symbol.kind = *symbol_kind_of;
    symbol.name = anonymous ? std::string() : take(clang_getCursorSpelling(cursor));
    if (symbol.name.empty()) {
        symbol.name = "(anonymous)";
    }
    // A member defined outside its class is named with its class.
    const CXCursor semantic = clang_getCursorSemanticParent(cursor);
    const CXCursor lexical = clang_getCursorLexicalParent(cursor);
    if (!is_null(semantic) && !same(semantic, lexical) && symbol.kind != Symbol::Kind::Namespace &&
        clang_getCursorKind(semantic) != CXCursor_TranslationUnit) {
        const std::string owner = take(clang_getCursorSpelling(semantic));
        if (!owner.empty()) {
            symbol.name = owner + "::" + symbol.name;
        }
    }
    if (kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl) {
        symbol.detail = take(clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(cursor)));
    } else if (kind != CXCursor_MacroDefinition && !nests(symbol.kind)) {
        symbol.detail = take(clang_getTypeSpelling(clang_getCursorType(cursor)));
    }
    symbol.name_extent = *name;
    symbol.extent = extent;
    if (nests(symbol.kind)) {
        OutlineWalk inner{&state, &symbol.children};
        clang_visitChildren(cursor, outline_visit, &inner);
    }
    walk.into->push_back(std::move(symbol));
    return CXChildVisit_Continue;
}

std::vector<Symbol> EditorUnit::outline() const {
    std::vector<Symbol> symbols;
    if (state_->unit != nullptr) {
        State::OutlineWalk walk{state_.get(), &symbols};
        clang_visitChildren(clang_getTranslationUnitCursor(state_->unit), State::outline_visit, &walk);
    }
    return symbols;
}

std::vector<Fold> EditorUnit::folds() const {
    std::vector<Fold> found;
    if (state_->unit == nullptr) {
        return found;
    }
    struct Walk {
        const State* state;
        std::vector<Fold>* found;
    } walk{state_.get(), &found};
    clang_visitChildren(
        clang_getTranslationUnitCursor(state_->unit),
        [](CXCursor cursor, CXCursor, CXClientData data) {
            const Walk& walk = *static_cast<Walk*>(data);
            if (clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) == 0) {
                return CXChildVisit_Continue;
            }
            std::optional<Extent> body;
            switch (clang_getCursorKind(cursor)) {
                case CXCursor_InclusionDirective: {
                    const CXSourceRange range = clang_getCursorExtent(cursor);
                    walk.found->push_back(
                        Fold{Fold::Kind::Include, Extent{walk.state->place(clang_getRangeStart(range)),
                                                         walk.state->place(clang_getRangeEnd(range))}});
                    return CXChildVisit_Continue;
                }
                case CXCursor_CompoundStmt:
                case CXCursor_InitListExpr:
                    body = walk.state->braced(cursor);
                    break;
                case CXCursor_Namespace:
                case CXCursor_LinkageSpec:
                case CXCursor_ClassDecl:
                case CXCursor_StructDecl:
                case CXCursor_UnionDecl:
                case CXCursor_EnumDecl:
                case CXCursor_ClassTemplate:
                case CXCursor_ClassTemplatePartialSpecialization:
                    body = walk.state->declared_body(cursor);
                    break;
                default:
                    break;
            }
            if (body.has_value() && body->begin.in_main_file && body->end.in_main_file) {
                walk.found->push_back(Fold{Fold::Kind::Braces, *body});
            }
            return CXChildVisit_Recurse;
        },
        &walk);
    state_->conditional_branches(found);
    return found;
}

std::vector<Extent> EditorUnit::enclosing(std::size_t offset) const {
    std::vector<Extent> found;
    if (state_->unit == nullptr) {
        return found;
    }
    struct Walk {
        const State* state;
        std::size_t offset;
        std::vector<Extent>* found;
    } walk{state_.get(), offset, &found};
    clang_visitChildren(
        clang_getTranslationUnitCursor(state_->unit),
        [](CXCursor cursor, CXCursor, CXClientData data) {
            const Walk& walk = *static_cast<Walk*>(data);
            if (clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) == 0) {
                return CXChildVisit_Continue;
            }
            const CXSourceRange range = clang_getCursorExtent(cursor);
            const Extent extent{walk.state->place(clang_getRangeStart(range)),
                                walk.state->place(clang_getRangeEnd(range))};
            if (!extent.begin.in_main_file || !extent.end.in_main_file || extent.begin.offset > walk.offset ||
                walk.offset > extent.end.offset) {
                return CXChildVisit_Continue;
            }
            walk.found->push_back(extent);
            return CXChildVisit_Recurse;
        },
        &walk);
    std::ranges::reverse(found);
    return found;
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
