// SPDX-License-Identifier: GPL-3.0-only
// Whole-file replacement mounts (sco/vfs.h): the `files\<game path>` content kind of a data pack.
// A replacement is a Transform that removes the whole base and inserts the file's bytes, read at
// the path's first open, so it goes through the mount table's merge, limits and inert rules.
#include "sco/vfs.h"
#include <fstream>
#include <memory>
#include <system_error>

namespace sco::vfs {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxDepth = 16;   // folders below files/ that are entered

std::string Utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

// Empty when gamePath may be mounted; otherwise why not.
std::string CheckGamePath(std::string_view gamePath) {
    if (gamePath.empty()) return "empty game path";
    if (gamePath.size() > kMaxPathLength) return "game path over " + std::to_string(kMaxPathLength) + " characters";
    if (gamePath.front() == '/' || gamePath.front() == '\\') return "absolute game path";
    if (gamePath.find(':') != std::string_view::npos) return "game path with a drive or stream (':')";
    size_t start = 0;
    while (start <= gamePath.size()) {
        size_t end = gamePath.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = gamePath.size();
        const std::string_view seg = gamePath.substr(start, end - start);
        if (seg == "." || seg == "..") return "game path with a '.' or '..' segment";
        start = end + 1;
    }
    return {};
}

}  // namespace

Result ReplaceMount(std::string_view gamePath, fs::path sourceFile, std::string source, int priority, Mount& out,
                    uint64_t maxBytes) {
    if (std::string why = CheckGamePath(gamePath); !why.empty()) return { std::move(why) + ": " + std::string(gamePath) };
    if (sourceFile.empty()) return { "no replacement file for " + std::string(gamePath) };
    Mount m;
    m.path = std::string(gamePath);
    m.priority = priority;
    m.source = std::move(source);
    m.producer = Transform([file = std::move(sourceFile), maxBytes](BaseIo&, uint64_t baseSize, std::vector<Splice>& splices) -> Result {
        std::error_code ec;
        const fs::file_status st = fs::symlink_status(file, ec);
        if (ec || !fs::exists(st)) return { "replacement file missing: " + Utf8(file.filename()) };
        if (!fs::is_regular_file(st)) return { "replacement is not a regular file: " + Utf8(file.filename()) };
        const uintmax_t size = fs::file_size(file, ec);
        if (ec) return { "replacement file size unreadable: " + Utf8(file.filename()) };
        if (size > maxBytes)
            return { "replacement is " + std::to_string(size) + " bytes, over the limit of " + std::to_string(maxBytes) };
        auto bytes = std::make_shared<Bytes>(static_cast<size_t>(size));
        std::ifstream in(file, std::ios::binary);
        if (!in) return { "replacement file can't be opened: " + Utf8(file.filename()) };
        if (size && !in.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size)))
            return { "replacement file read short: " + Utf8(file.filename()) };
        if (in.peek() != std::ifstream::traits_type::eof())
            return { "replacement file grew while it was read: " + Utf8(file.filename()) };
        if (!size && !baseSize) return {};   // both empty: nothing to change
        splices.push_back({ 0, baseSize, std::move(bytes), nullptr });
        return {};
    });
    out = std::move(m);
    return {};
}

Result FileMounts(const fs::path& packDir, std::string_view packId, int priority, std::vector<Mount>& out,
                  std::vector<std::string>& refused, uint64_t maxBytes) {
    const fs::path base = packDir / "files";
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(base, ec);
    if (ec || st.type() != fs::file_type::directory) return {};
    std::vector<Mount> mine;
    std::vector<std::string> left;
    size_t files = 0;
    // Symlinked folders are not followed (directory_options::none), symlinked files are skipped.
    for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        if (it.depth() >= kMaxDepth) it.disable_recursion_pending();
        std::error_code e2;
        if (it->symlink_status(e2).type() != fs::file_type::regular || e2) continue;
        if (++files > kMaxReplaceFiles) return { "files/ holds more than " + std::to_string(kMaxReplaceFiles) + " files" };
        const fs::path rel = fs::relative(it->path(), base, e2);
        if (e2) return { "cannot read files/: " + e2.message() };
        const std::string game = Utf8(rel);
        const uintmax_t size = it->file_size(e2);
        if (e2) { left.push_back(game + ": size unreadable"); continue; }
        if (size > maxBytes) {
            left.push_back(game + ": " + std::to_string(size) + " bytes, over the limit of " + std::to_string(maxBytes));
            continue;
        }
        Mount m;
        if (Result r = ReplaceMount(game, it->path(), std::string(packId), priority, m, maxBytes); !r) {
            left.push_back(r.error);
            continue;
        }
        mine.push_back(std::move(m));
    }
    if (ec) return { "cannot read files/: " + ec.message() };
    for (Mount& m : mine) out.push_back(std::move(m));
    for (std::string& s : left) refused.push_back(std::move(s));
    return {};
}

}  // namespace sco::vfs
