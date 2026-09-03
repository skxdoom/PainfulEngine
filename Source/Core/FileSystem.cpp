#include "FileSystem.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace painful {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Absolute, lexically normal, '/'-separated, no trailing slash. Purely
// lexical - never touches the disk, so archive-only paths normalize too.
std::string NormalizePath(const std::string& p) {
    std::error_code ec;
    std::string s = fs::absolute(fs::path(p), ec).lexically_normal().generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// "Textures2" -> ("Textures", 2); "Textures" -> ("Textures", 0).
std::pair<std::string, int> SplitNumberedName(const std::string& stem) {
    size_t end = stem.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(stem[end - 1]))) --end;
    if (end == stem.size() || end == 0) return {stem, 0};
    return {stem.substr(0, end), std::atoi(stem.c_str() + end)};
}

} // namespace

FileSystem& FileSystem::Get() {
    static FileSystem instance;
    return instance;
}

size_t FileSystem::MountData(const std::string& dataDir) {
    const std::string root = NormalizePath(dataDir);
    if (root == dataRoot_) return archives_.size();

    archives_.clear();
    mountPrefixes_.clear();
    files_.clear();
    tree_.clear();
    dataRoot_ = root;
    rootKey_ = Lower(root);

    // Collect and order the archives so numbered ones mount last and win.
    std::vector<fs::path> paks;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dataDir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (Lower(entry.path().extension().string()) != ".pak") continue;
        paks.push_back(entry.path());
    }
    std::sort(paks.begin(), paks.end(), [](const fs::path& a, const fs::path& b) {
        const auto ka = SplitNumberedName(Lower(a.stem().string()));
        const auto kb = SplitNumberedName(Lower(b.stem().string()));
        return ka != kb ? ka < kb : a < b;
    });

    for (const fs::path& pak : paks) {
        auto archive = std::make_unique<PakArchive>();
        if (!archive->Open(pak.string())) {
            LogWarn("pak %s: %s", pak.filename().string().c_str(),
                    archive->error().c_str());
            continue;
        }
        const std::string prefix = SplitNumberedName(pak.stem().string()).first;
        const uint32_t archiveIdx = static_cast<uint32_t>(archives_.size());
        const auto& entries = archive->entries();
        for (uint32_t i = 0; i < entries.size(); ++i) {
            const PakArchive::Entry& e = entries[i];
            if (e.name.empty()) continue;
            std::string virt = prefix + "/" + e.name;
            if (e.isDirectory) virt.pop_back();          // drop the trailing '/'
            AddEntry(archiveIdx, i, virt, e.isDirectory);
        }
        archives_.push_back(std::move(archive));
        mountPrefixes_.push_back(prefix);
    }
    return archives_.size();
}

void FileSystem::AddEntry(uint32_t archive, uint32_t entry,
                          const std::string& virtualPath, bool isDirectory) {
    if (isDirectory) {
        EnsureDirChain(virtualPath);
        return;
    }
    const size_t slash = virtualPath.find_last_of('/');
    const std::string parent = slash == std::string::npos ? "" : virtualPath.substr(0, slash);
    const std::string name = slash == std::string::npos ? virtualPath
                                                        : virtualPath.substr(slash + 1);
    EnsureDirChain(parent);
    tree_[Lower(parent)][Lower(name)] = {name, false};
    files_[Lower(virtualPath)] = {archive, entry};
}

void FileSystem::EnsureDirChain(const std::string& dirPath) {
    tree_.try_emplace("");
    std::string parent;         // original case
    size_t from = 0;
    while (from <= dirPath.size() && !dirPath.empty()) {
        size_t to = dirPath.find('/', from);
        if (to == std::string::npos) to = dirPath.size();
        const std::string name = dirPath.substr(from, to - from);
        if (!name.empty()) {
            tree_[Lower(parent)].try_emplace(Lower(name), DirEntry{name, true});
            parent = parent.empty() ? name : parent + "/" + name;
            tree_.try_emplace(Lower(parent));
        }
        from = to + 1;
    }
}

bool FileSystem::RelToRoot(const std::string& path, std::string& lcRel) const {
    if (archives_.empty()) return false;
    const std::string key = Lower(NormalizePath(path));
    if (key.size() < rootKey_.size() || key.compare(0, rootKey_.size(), rootKey_) != 0)
        return false;
    if (key.size() == rootKey_.size()) {
        lcRel.clear();
        return true;
    }
    if (key[rootKey_.size()] != '/') return false;
    lcRel = key.substr(rootKey_.size() + 1);
    return true;
}

const FileSystem::ExtraMount* FileSystem::FindExtra(const std::string& path,
                                                     uint32_t* entry) const {
    if (extra_.empty()) return nullptr;
    const std::string key = Lower(NormalizePath(path));
    // Later mounts win, as with the numbered archives.
    for (auto it = extra_.rbegin(); it != extra_.rend(); ++it) {
        const std::string& d = it->dirKey;
        if (key.size() <= d.size() + 1 || key.compare(0, d.size(), d) != 0 ||
            key[d.size()] != '/')
            continue;
        const auto e = it->entries.find(key.substr(d.size() + 1));
        if (e == it->entries.end()) continue;
        if (entry) *entry = e->second;
        return &*it;
    }
    return nullptr;
}

bool FileSystem::ReadPakFile(const std::string& path, std::vector<uint8_t>& out) const {
    uint32_t entry = 0;
    if (const ExtraMount* m = FindExtra(path, &entry))
        return m->archive->Read(m->archive->entries()[entry], out);
    std::string lcRel;
    if (!RelToRoot(path, lcRel)) return false;
    const auto it = files_.find(lcRel);
    if (it == files_.end()) return false;
    const PakArchive& a = *archives_[it->second.archive];
    return a.Read(a.entries()[it->second.entry], out);
}

bool FileSystem::Exists(const std::string& path) const {
    if (FindExtra(path, nullptr)) return true;
    std::string lcRel;
    if (RelToRoot(path, lcRel) &&
        (files_.count(lcRel) || tree_.count(lcRel)))
        return true;
    std::error_code ec;
    return fs::exists(path, ec);
}

int FileSystem::MountPack(const std::string& pakPath, const std::string& dir) {
    auto archive = std::make_unique<PakArchive>();
    if (!archive->Open(pakPath)) {
        LogWarn("pack %s: %s", pakPath.c_str(), archive->error().c_str());
        return 0;
    }
    ExtraMount m;
    m.handle = nextExtraHandle_++;
    m.dirKey = Lower(NormalizePath(dir));
    const auto& entries = archive->entries();
    for (uint32_t i = 0; i < entries.size(); ++i)
        if (!entries[i].isDirectory && !entries[i].name.empty())
            m.entries[Lower(entries[i].name)] = i;
    m.archive = std::move(archive);
    extra_.push_back(std::move(m));
    return extra_.back().handle;
}

void FileSystem::UnmountPack(int handle) {
    for (auto it = extra_.begin(); it != extra_.end(); ++it)
        if (it->handle == handle) {
            extra_.erase(it);
            return;
        }
}

bool FileSystem::BeginPak(const std::string& pakPath) {
    return writer_.Begin(pakPath);
}

bool FileSystem::EndPak() {
    return writer_.End();
}

bool FileSystem::WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    if (writer_.open()) {
        const std::string norm = NormalizePath(path);
        const size_t slash = norm.find_last_of('/');
        return writer_.Add(slash == std::string::npos ? norm : norm.substr(slash + 1), data);
    }
    FILE* fp = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&fp, path.c_str(), "wb") != 0 || !fp) return false;
#else
    fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
#endif
    const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), fp) == data.size();
    std::fclose(fp);
    return ok;
}

bool FileSystem::IsDirectory(const std::string& path) const {
    std::string lcRel;
    if (RelToRoot(path, lcRel) && tree_.count(lcRel)) return true;
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::vector<DirEntry> FileSystem::List(const std::string& dir) const {
    std::vector<DirEntry> out;
    std::set<std::string> seen;

    // A registered pack's files come first, the way its entries shadow disk.
    if (!extra_.empty()) {
        const std::string key = Lower(NormalizePath(dir));
        for (const ExtraMount& m : extra_) {
            if (m.dirKey != key) continue;
            for (const auto& kv : m.entries) {
                if (kv.first.find('/') != std::string::npos) continue;   // nested: not a child
                if (!seen.insert(kv.first).second) continue;
                out.push_back({m.archive->entries()[kv.second].name, false});
            }
        }
    }
    std::string lcRel;
    if (RelToRoot(dir, lcRel)) {
        const auto node = tree_.find(lcRel);
        if (node != tree_.end()) {
            for (const auto& kv : node->second) {
                if (!seen.insert(kv.first).second) continue;
                out.push_back(kv.second);
            }
        }
    }
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (seen.count(Lower(name))) continue;
        out.push_back({name, entry.is_directory()});
    }
    std::sort(out.begin(), out.end(),
              [](const DirEntry& a, const DirEntry& b) { return Lower(a.name) < Lower(b.name); });
    return out;
}

std::vector<std::string> FileSystem::ListRecursive(const std::string& dir) const {
    std::vector<std::string> out;
    std::set<std::string> seen;

    std::string lcRel;
    if (RelToRoot(dir, lcRel)) {
        const std::string prefix = lcRel.empty() ? "" : lcRel + "/";
        for (const auto& kv : files_) {
            if (kv.first.size() <= prefix.size() ||
                kv.first.compare(0, prefix.size(), prefix) != 0)
                continue;
            // Rebuild the original-case path; the key only differs in case,
            // so the prefix length carries over.
            const PakArchive& a = *archives_[kv.second.archive];
            const std::string orig = mountPrefixes_[kv.second.archive] + "/" +
                                     a.entries()[kv.second.entry].name;
            out.push_back(orig.substr(prefix.size()));
            seen.insert(kv.first.substr(prefix.size()));
        }
    }
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            const std::string rel =
                entry.path().lexically_relative(dir).generic_string();
            if (seen.count(Lower(rel))) continue;
            out.push_back(rel);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace painful
