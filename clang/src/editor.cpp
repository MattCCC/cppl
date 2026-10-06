#include "cppl/clang/editor.hpp"

#include "editor_state.hpp"

#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cppl::clangbridge {

namespace detail::editor {

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

// Whether a walk of the main file keeps `cursor`, met beneath `parent`. At the
// top of the unit, a declaration is kept only when the main file writes it,
// since every header's declarations are the unit's too. Beneath one,
// everything is kept: Clang places an expression a macro began in the macro,
// not in the file, though the file writes the rest of it.
bool in_main_file(CXCursor cursor, CXCursor parent) {
    return clang_getCursorKind(parent) != CXCursor_TranslationUnit ||
           clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) != 0;
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

} // namespace detail::editor

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
    // A file's handle and contents belong to the parse that read them.
    state.contents.clear();
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

} // namespace cppl::clangbridge
