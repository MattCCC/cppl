#include "cppl/lsp/editor_view.hpp"

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/projected_file.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/uri.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

bool is_name_byte(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '_' || byte >= 0x80;
}

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

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
        auto parsed = clangbridge::EditorUnit::parse({options_.driver, options_.arguments}, main, unsaved());
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
    const std::size_t offset = PositionMapper(text).position_to_byte_offset(position);
    if (offset < text.size() && is_name_byte(text[offset])) {
        return offset;
    }
    if (offset > 0 && offset <= text.size() && is_name_byte(text[offset - 1])) {
        return offset - 1;
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
        const PositionMapper mapper(file->text());
        return Location{uri, Range{mapper.byte_offset_to_position(*begin), mapper.byte_offset_to_position(*end)}};
    }

    // A file Clang read from disk as it is.
    if (key.empty()) {
        return std::nullopt;
    }
    const std::optional<std::string> text = read_file(extent.begin.file);
    if (!text.has_value() || extent.end.offset > text->size() || extent.end.offset < extent.begin.offset) {
        return std::nullopt;
    }
    const PositionMapper mapper(*text);
    return Location{path_to_uri(key), Range{mapper.byte_offset_to_position(extent.begin.offset),
                                            mapper.byte_offset_to_position(extent.end.offset)}};
}

namespace {

bool same_location(const Location& lhs, const Location& rhs) {
    return lhs.uri == rhs.uri && lhs.range.start.line == rhs.range.start.line &&
           lhs.range.start.character == rhs.range.start.character && lhs.range.end.line == rhs.range.end.line &&
           lhs.range.end.character == rhs.range.end.character;
}

} // namespace

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

std::vector<EditorView::Mention> EditorView::mentions(const Target& target) const {
    std::vector<Mention> found;
    if (unit_ == nullptr) {
        return found;
    }
    std::vector<std::string> usrs = target.usrs;
    if (target.declaration.has_value()) {
        for (const clangbridge::Occurrence& declaration : unit_->declarations_named(target.name)) {
            const std::optional<Location> written = locate(declaration.name);
            if (written.has_value() && same_location(*written, *target.declaration) &&
                std::ranges::find(usrs, declaration.usr) == usrs.end()) {
                usrs.push_back(declaration.usr);
            }
        }
    }
    for (const clangbridge::Occurrence& occurrence : unit_->occurrences(usrs)) {
        std::optional<Location> location = locate(occurrence.name);
        if (!location.has_value()) {
            continue;
        }
        const auto known = std::ranges::find_if(
            found, [&](const Mention& mention) { return same_location(mention.location, *location); });
        if (known == found.end()) {
            found.push_back(Mention{std::move(*location), occurrence.role});
        } else if (occurrence.role == clangbridge::Role::Declaration) {
            // Written once, reached as a repetition's reference and as the
            // declaration it repeats: it is the declaration.
            known->role = occurrence.role;
        }
    }
    return found;
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
