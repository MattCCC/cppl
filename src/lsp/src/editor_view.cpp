#include "cppl/lsp/editor_view.hpp"

#include "cppl/clang/editor.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/lsp/completion.hpp"
#include "cppl/lsp/hover.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/projected_file.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/symbols.hpp"
#include "cppl/lsp/uri.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

bool is_name_byte(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '_' || byte >= 0x80;
}

} // namespace

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string normal_path(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    return std::filesystem::path(path).lexically_normal().string();
}

std::string resolve_driver(const std::string& configured) {
    if (configured.empty() || configured.find_first_of("/\\") != std::string::npos) {
        return configured;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once, when the server starts and before any thread does.
    const char* search = std::getenv("PATH");
    if (search == nullptr) {
        return configured;
    }
#ifdef _WIN32
    constexpr char separator = ';';
    const std::vector<std::string> names = {configured, configured + ".exe"};
#else
    constexpr char separator = ':';
    const std::vector<std::string> names = {configured};
#endif
    const std::string_view directories(search);
    std::size_t start = 0;
    while (start <= directories.size()) {
        const std::size_t end = std::min(directories.find(separator, start), directories.size());
        const std::filesystem::path directory(directories.substr(start, end - start));
        for (const std::string& name : names) {
            std::error_code error;
            const std::filesystem::path candidate = directory / name;
            if (!directory.empty() && std::filesystem::is_regular_file(candidate, error)) {
                return candidate.string();
            }
        }
        start = end + 1;
    }
    return configured;
}

bool EditorView::refresh(const OpenBuffer& document, const std::vector<OpenBuffer>& open, const Options& options) {
    named_.reset();
    outline_.reset();
    mappers_.clear();
    disk_.clear();
    uri_ = document.uri;
    main_ = std::make_unique<ProjectedFile>(document.path, *document.text);

    // Every other open buffer is read as the editor holds it, not as it was
    // last saved. One that has not changed keeps the projection made of it.
    std::map<std::string, std::shared_ptr<const ProjectedFile>> buffers;
    std::map<std::string, std::string> uris;
    for (const OpenBuffer& buffer : open) {
        if (buffer.uri == document.uri || buffer.text == nullptr) {
            continue;
        }
        const std::string key = normal_path(buffer.path);
        const auto known = buffers_.find(key);
        if (known != buffers_.end() && known->second->text() == *buffer.text) {
            buffers[key] = known->second;
        } else {
            buffers[key] = std::make_shared<const ProjectedFile>(buffer.path, *buffer.text);
        }
        uris[key] = buffer.uri;
    }
    buffers_ = std::move(buffers);
    buffer_uris_ = std::move(uris);

    // A header saved since it was read is read again.
    for (auto header = headers_.begin(); header != headers_.end();) {
        std::error_code error;
        const auto written = std::filesystem::last_write_time(header->first, error);
        header = error || written != header->second.written ? headers_.erase(header) : std::next(header);
    }

    const bool rebuild = unit_ == nullptr || options.driver != options_.driver ||
                         options.arguments != options_.arguments || unit_->main_path() != document.path;
    options_ = options;
    const clangbridge::FileContent main{document.path, main_->analysis()};
    if (rebuild) {
        unit_.reset();
        auto parsed =
            clangbridge::EditorUnit::parse({options_.driver, options_.arguments, options_.once}, main, unsaved());
        if (!parsed.has_value()) {
            return false;
        }
        unit_ = std::move(*parsed);
        ++parses_;
    } else if (!unit_->reparse(main, unsaved()).has_value()) {
        unit_.reset();
        return false;
    }

    // A header that holds C++L is found only once Clang has read the unit, and
    // is then read as its projection too.
    if (read_included_headers() && !unit_->reparse(main, unsaved()).has_value()) {
        unit_.reset();
        return false;
    }
    return true;
}

std::vector<clangbridge::FileContent> EditorView::unsaved() const {
    std::vector<clangbridge::FileContent> files;
    files.reserve(buffers_.size() + headers_.size());
    for (const auto& [key, buffer] : buffers_) {
        files.push_back(clangbridge::FileContent{buffer->path(), buffer->analysis()});
    }
    for (const auto& [key, header] : headers_) {
        if (header.file != nullptr && !buffers_.contains(key)) {
            files.push_back(clangbridge::FileContent{header.file->path(), header.file->analysis()});
        }
    }
    return files;
}

bool EditorView::read_included_headers() {
    bool added = false;
    for (const std::string& file : unit_->included_files()) {
        const std::string key = normal_path(file);
        if (buffers_.contains(key) || headers_.contains(key)) {
            continue;
        }
        std::error_code error;
        const auto written = std::filesystem::last_write_time(file, error);
        std::optional<std::string> text = error ? std::nullopt : read_file(file);
        if (!text.has_value()) {
            continue;
        }
        auto projected = std::make_shared<const ProjectedFile>(file, std::move(*text));
        Header header{written, projected->projected() ? std::move(projected) : nullptr};
        added = added || header.file != nullptr;
        headers_.emplace(key, std::move(header));
    }
    return added;
}

std::optional<std::size_t> EditorView::request_offset(const Position& position) const {
    const std::string& text = main_->text();
    std::size_t offset = PositionMapper(text).position_to_byte_offset(position);
    // Clang reads a token from where it is asked to, so a request inside a
    // name is made at the name's start.
    const auto name_start = [&text](std::size_t within) {
        while (within > 0 && is_name_byte(text[within - 1])) {
            --within;
        }
        return within;
    };
    if (offset < text.size() && is_name_byte(text[offset])) {
        return name_start(offset);
    }
    if (offset > 0 && offset <= text.size() && is_name_byte(text[offset - 1])) {
        offset = name_start(offset - 1);
        return offset;
    }
    if (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset])) == 0) {
        return offset;
    }
    return std::nullopt;
}

std::optional<Location> EditorView::locate(const clangbridge::Extent& extent) const {
    const std::string key = normal_path(extent.begin.file);
    const ProjectedFile* file = nullptr;
    std::string uri;
    if (extent.begin.in_main_file) {
        file = main_.get();
        uri = uri_;
    } else if (const auto buffer = buffers_.find(key); buffer != buffers_.end()) {
        file = buffer->second.get();
        uri = buffer_uris_.at(key);
    } else if (const auto header = headers_.find(key); header != headers_.end() && header->second.file != nullptr) {
        file = header->second.file.get();
        uri = path_to_uri(key);
    }

    if (file != nullptr) {
        const std::optional<std::size_t> begin = file->to_written(extent.begin.offset);
        const std::optional<std::size_t> end = file->to_written(extent.end.offset);
        if (!begin.has_value() || !end.has_value() || *end < *begin) {
            return std::nullopt;
        }
        auto [known, fresh] = mappers_.try_emplace(file, file->text());
        const PositionMapper& mapper = known->second;
        return Location{uri, Range{mapper.byte_offset_to_position(*begin), mapper.byte_offset_to_position(*end)}};
    }

    // A file Clang read from disk as it is, read once for every place in it.
    if (key.empty()) {
        return std::nullopt;
    }
    auto known = disk_.find(key);
    if (known == disk_.end()) {
        std::optional<std::string> text = read_file(extent.begin.file);
        known = disk_
                    .emplace(key, text.has_value() ? std::make_unique<DiskFile>(path_to_uri(key), std::move(*text))
                                                   : nullptr)
                    .first;
    }
    const DiskFile* disk = known->second.get();
    if (disk == nullptr || extent.end.offset > disk->text.size() || extent.end.offset < extent.begin.offset) {
        return std::nullopt;
    }
    return Location{disk->uri, Range{disk->mapper.byte_offset_to_position(extent.begin.offset),
                                     disk->mapper.byte_offset_to_position(extent.end.offset)}};
}

namespace {

bool same_location(const Location& lhs, const Location& rhs) {
    return lhs.uri == rhs.uri && lhs.range.start.line == rhs.range.start.line &&
           lhs.range.start.character == rhs.range.start.character && lhs.range.end.line == rhs.range.end.line &&
           lhs.range.end.character == rhs.range.end.character;
}

bool before(const Position& lhs, const Position& rhs) {
    return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.character < rhs.character);
}

// A declaration Clang outlined, over the text as written. Its name must be
// one the projection kept where it was written: anything else is a
// declaration the projection generated, whether its name is generated or
// copied from C++L that the outline shows as written instead.
std::optional<DocumentSymbol> outline_entry(const ProjectedFile& file, const PositionMapper& mapper,
                                            const clangbridge::Symbol& symbol) {
    const std::size_t length = symbol.name_extent.end.offset - symbol.name_extent.begin.offset;
    const std::optional<std::size_t> name = file.kept(symbol.name_extent.begin.offset);
    if (!name.has_value() || symbol.name_extent.end.offset < symbol.name_extent.begin.offset ||
        (length > 0 && file.kept(symbol.name_extent.end.offset - 1) != *name + length - 1)) {
        return std::nullopt;
    }
    DocumentSymbol entry;
    entry.name = symbol.name;
    entry.kind = symbol_kind(symbol.kind);
    entry.detail = symbol.detail;
    if (entry.kind == SymbolKind::Function || entry.kind == SymbolKind::Method) {
        entry.detail = specifiers_of(file, *name) + entry.detail;
    }
    entry.selection = Range{mapper.byte_offset_to_position(*name), mapper.byte_offset_to_position(*name + length)};
    const std::optional<std::size_t> begin = file.to_written(symbol.extent.begin.offset);
    const std::optional<std::size_t> end = file.to_written(symbol.extent.end.offset);
    entry.range = begin.has_value() && end.has_value() && *begin <= *end
                      ? Range{mapper.byte_offset_to_position(*begin), mapper.byte_offset_to_position(*end)}
                      : entry.selection;
    // The name is part of what is selected as the declaration, whatever the
    // projection left of the text around it.
    if (before(entry.selection.start, entry.range.start)) {
        entry.range.start = entry.selection.start;
    }
    if (before(entry.range.end, entry.selection.end)) {
        entry.range.end = entry.selection.end;
    }
    for (const clangbridge::Symbol& child : symbol.children) {
        if (std::optional<DocumentSymbol> nested = outline_entry(file, mapper, child)) {
            entry.children.push_back(std::move(*nested));
        }
    }
    return entry;
}

} // namespace

std::vector<DocumentSymbol> EditorView::outline() const {
    if (outline_.has_value()) {
        return *outline_;
    }
    std::vector<DocumentSymbol> outline;
    if (main_ == nullptr) {
        return outline;
    }
    if (unit_ != nullptr) {
        const PositionMapper mapper(main_->text());
        for (const clangbridge::Symbol& symbol : unit_->outline()) {
            if (std::optional<DocumentSymbol> entry = outline_entry(*main_, mapper, symbol)) {
                outline.push_back(std::move(*entry));
            }
        }
    }
    for (DocumentSymbol& symbol : cppl_symbols(*main_)) {
        place_symbol(outline, std::move(symbol));
    }
    outline_ = outline;
    return outline;
}

std::optional<EditorView::Target> EditorView::target_at(const Position& position) const {
    if (unit_ == nullptr || main_ == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::size_t> offset = request_offset(position);
    const std::optional<std::size_t> analysis = offset.has_value() ? main_->to_analysis(*offset) : std::nullopt;
    if (!analysis.has_value()) {
        return std::nullopt;
    }
    const std::vector<clangbridge::Entity> entities = unit_->entities_at(*analysis);
    if (entities.empty()) {
        return std::nullopt;
    }
    Target target;
    target.name = entities.front().name;
    for (const clangbridge::Entity& entity : entities) {
        target.usrs.push_back(entity.usr);
    }
    if (entities.front().declaration.has_value()) {
        target.declaration = locate(*entities.front().declaration);
    }
    return target;
}

EditorView::Target EditorView::renamed_together(Target target) const {
    if (unit_ == nullptr) {
        return target;
    }
    const std::vector<std::string> named = target.usrs;
    for (const std::string& usr : named) {
        for (std::string& together : unit_->renamed_together(usr)) {
            if (std::ranges::find(target.usrs, together) == target.usrs.end()) {
                target.usrs.push_back(std::move(together));
            }
        }
    }
    return target;
}

std::vector<EditorView::Mention> EditorView::mentions(const Target& target) const {
    std::vector<Mention> found;
    const std::vector<Named>& all = named();
    std::set<std::string, std::less<>> usrs(target.usrs.begin(), target.usrs.end());
    // A declaration written where the target's is -- a parameter the
    // projection repeated -- is the target too.
    if (target.declaration.has_value()) {
        for (const Named& other : all) {
            if (other.mention.role == clangbridge::Role::Declaration &&
                same_location(other.mention.location, *target.declaration)) {
                usrs.insert(other.usr);
            }
        }
    }
    std::map<std::tuple<std::string, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>, std::size_t> at;
    for (const Named& occurrence : all) {
        if (!usrs.contains(occurrence.usr)) {
            continue;
        }
        const Range& range = occurrence.mention.location.range;
        const auto [known, fresh] =
            at.try_emplace(std::tuple{occurrence.mention.location.uri, range.start.line, range.start.character,
                                      range.end.line, range.end.character},
                           found.size());
        if (fresh) {
            found.push_back(occurrence.mention);
        } else if (occurrence.mention.role == clangbridge::Role::Declaration) {
            // Written once, reached as a repetition's reference and as the
            // declaration it repeats: it is the declaration.
            found[known->second].role = occurrence.mention.role;
        }
    }
    return found;
}

std::vector<EditorView::Named> EditorView::all_mentions() const {
    return named();
}

const std::vector<EditorView::Named>& EditorView::named() const {
    if (named_.has_value()) {
        return *named_;
    }
    std::vector<Named>& found = named_.emplace();
    if (unit_ == nullptr) {
        return found;
    }
    // Each name once per place it is written.
    std::map<std::tuple<std::string, std::string, std::uint32_t, std::uint32_t>, std::size_t> seen;
    for (const clangbridge::Occurrence& occurrence : unit_->all_occurrences()) {
        std::optional<Location> location = locate(occurrence.name);
        if (!location.has_value()) {
            continue;
        }
        const auto [known, fresh] = seen.try_emplace(
            std::tuple{occurrence.usr, location->uri, location->range.start.line, location->range.start.character},
            found.size());
        if (fresh) {
            found.push_back(Named{occurrence.usr, Mention{std::move(*location), occurrence.role}});
        } else if (occurrence.role == clangbridge::Role::Declaration) {
            found[known->second].mention.role = occurrence.role;
        }
    }
    return found;
}

std::vector<EditorView::Named> EditorView::unwritten(const std::vector<std::string>* usrs) const {
    std::vector<Named> found;
    if (unit_ == nullptr) {
        return found;
    }
    std::set<std::tuple<std::string, std::string, std::uint32_t, std::uint32_t>> seen;
    for (const clangbridge::Occurrence& use : unit_->unwritten_uses(usrs)) {
        std::optional<Location> location = locate(use.name);
        if (location.has_value() &&
            seen.emplace(use.usr, location->uri, location->range.start.line, location->range.start.character).second) {
            found.push_back(Named{use.usr, Mention{std::move(*location), use.role}});
        }
    }
    return found;
}

const ProjectedFile* EditorView::file_for(const std::string& uri) const {
    if (uri == uri_) {
        return main_.get();
    }
    for (const auto& [key, buffer_uri] : buffer_uris_) {
        if (buffer_uri == uri) {
            return buffers_.at(key).get();
        }
    }
    for (const auto& [key, header] : headers_) {
        if (header.file != nullptr && path_to_uri(key) == uri) {
            return header.file.get();
        }
    }
    return nullptr;
}

std::optional<CpplDeclaration> EditorView::cppl_declaration_at(const Location& location) const {
    const ProjectedFile* file = file_for(location.uri);
    if (file == nullptr || !file->projected()) {
        return std::nullopt;
    }
    const std::size_t offset = PositionMapper(file->text()).position_to_byte_offset(location.range.start);
    return describe_cppl(file->syntax(), file->text(), offset);
}

std::optional<std::size_t> EditorView::insertion_point(std::size_t written) const {
    if (written < main_->text().size()) {
        if (const std::optional<std::size_t> at = main_->to_analysis(written)) {
            return at;
        }
    }
    if (written > 0) {
        if (const std::optional<std::size_t> before = main_->to_analysis(written - 1)) {
            return *before + 1;
        }
    }
    return std::nullopt;
}

CompletionList EditorView::complete(const Position& position, bool snippets) const {
    CompletionList list;
    if (main_ == nullptr) {
        return list;
    }
    const std::string& text = main_->text();
    const std::size_t cursor = PositionMapper(text).position_to_byte_offset(position);
    std::size_t start = cursor;
    while (start > 0 && is_name_byte(text[start - 1])) {
        --start;
    }
    const std::string_view prefix(text.data() + start, cursor - start);

    clangbridge::Scope scope = clangbridge::Scope::Other;
    if (unit_ != nullptr) {
        if (const std::optional<std::size_t> analysis = insertion_point(start)) {
            scope = unit_->scope_at(*analysis);
            list = cpp_completions(unit_->complete(*analysis), prefix, snippets);
        }
    }
    // C++L is read from the recognizer's draft of the text, which keeps what
    // is not written whole yet; what the author is writing starts at `start`.
    diagnostics::Engine unreported;
    const frontend::Syntax draft = frontend::recognize(main_->tokens(), unreported, frontend::RecognitionMode::Draft);
    std::vector<CompletionItem> own = cppl_completions(main_->tokens(), draft, start, prefix, scope, snippets);
    list.items.insert(list.items.begin(), std::make_move_iterator(own.begin()), std::make_move_iterator(own.end()));
    return list;
}

std::optional<SignatureHelp> EditorView::signature_help(const Position& position) const {
    if (unit_ == nullptr || main_ == nullptr) {
        return std::nullopt;
    }
    const std::size_t cursor = PositionMapper(main_->text()).position_to_byte_offset(position);
    const std::optional<std::size_t> analysis = insertion_point(cursor);
    if (!analysis.has_value()) {
        return std::nullopt;
    }
    // Clang names a call's candidates only inside its argument list, only
    // those that can take the arguments written so far, the best first, and
    // says which parameter the argument being written stands for.
    std::vector<clangbridge::Signature> signatures = unit_->signatures(*analysis);
    if (signatures.empty()) {
        return std::nullopt;
    }

    SignatureHelp help;
    help.active_parameter = signatures.front().active.value_or(0);
    for (const clangbridge::Signature& signature : signatures) {
        SignatureInformation information;
        information.label = signature.label;
        information.documentation = signature.documentation;
        for (const auto& [start, end] : signature.parameters) {
            // A parameter is a range of the label in UTF-16 code units, as LSP
            // counts characters.
            information.parameters.emplace_back(count_utf16_code_units(signature.label, 0, start),
                                                count_utf16_code_units(signature.label, 0, end));
        }
        help.signatures.push_back(std::move(information));
    }
    return help;
}

std::optional<EditorView::HoverAnswer> EditorView::hover(const Position& position) const {
    if (unit_ == nullptr || main_ == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::size_t> offset = request_offset(position);
    const std::optional<std::size_t> analysis = offset.has_value() ? main_->to_analysis(*offset) : std::nullopt;
    if (!analysis.has_value()) {
        return std::nullopt;
    }
    const std::optional<clangbridge::Description> description = unit_->describe(*analysis);
    if (!description.has_value()) {
        return std::nullopt;
    }
    HoverAnswer answer;
    if (description->named.has_value()) {
        if (const std::optional<Location> named = locate(*description->named)) {
            answer.hover.range = named->range;
        }
    }
    const std::optional<Location> declared =
        description->declared.has_value() ? locate(*description->declared) : std::nullopt;
    if (description->declared.has_value() && !declared.has_value()) {
        // Declared only in generated text: what it means, or nothing.
        const std::optional<std::string> implicit = describe_implicit(*description);
        if (!implicit.has_value()) {
            return std::nullopt;
        }
        answer.hover.contents = *implicit;
        return answer;
    }
    if (declared.has_value()) {
        if (std::optional<CpplDeclaration> written = cppl_declaration_at(*declared)) {
            answer.hover.contents = written->markdown;
            answer.declaration = std::move(*written);
            return answer;
        }
    }
    std::string declared_in;
    if (declared.has_value() && declared->uri != uri_) {
        const std::size_t slash = declared->uri.rfind('/');
        declared_in = declared->uri.substr(slash == std::string::npos ? 0 : slash + 1) + ":" +
                      std::to_string(declared->range.start.line + 1);
    }
    answer.hover.contents = describe_cpp(*description, declared_in);
    return answer;
}

std::vector<Location> EditorView::navigate(clangbridge::Destination destination, const Position& position) const {
    std::vector<Location> locations;
    if (unit_ == nullptr || main_ == nullptr) {
        return locations;
    }
    const std::optional<std::size_t> offset = request_offset(position);
    const std::optional<std::size_t> analysis = offset.has_value() ? main_->to_analysis(*offset) : std::nullopt;
    if (!analysis.has_value()) {
        return locations;
    }
    for (const clangbridge::Extent& extent : unit_->navigate(destination, *analysis)) {
        std::optional<Location> location = locate(extent);
        if (!location.has_value()) {
            continue;
        }
        const bool repeated = std::ranges::any_of(locations, [&](const Location& known) {
            return known.uri == location->uri && known.range.start.line == location->range.start.line &&
                   known.range.start.character == location->range.start.character;
        });
        if (!repeated) {
            locations.push_back(std::move(*location));
        }
    }
    return locations;
}

} // namespace cppl::lsp
