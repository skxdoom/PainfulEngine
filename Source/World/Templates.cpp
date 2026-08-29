#include "Templates.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace painful {

namespace {
std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

bool TemplateCache::Init(const std::string& templatesRoot) {
    FileSystem& vfs = FileSystem::Get();
    if (!vfs.IsDirectory(templatesRoot)) return false;
    for (const std::string& rel : vfs.ListRecursive(templatesRoot)) {
        const fs::path p(rel);
        if (p.extension() == ".scc") continue;
        index_[Lower(p.filename().string())] = templatesRoot + "/" + rel;
    }
    return true;
}

void TemplateCache::SetLevelOverlay(const std::string& levelTemplatesDir) {
    overlay_.clear();
    overlayLoaded_.clear();
    if (levelTemplatesDir.empty()) return;
    FileSystem& vfs = FileSystem::Get();
    if (!vfs.IsDirectory(levelTemplatesDir)) return;
    for (const std::string& rel : vfs.ListRecursive(levelTemplatesDir)) {
        const fs::path p(rel);
        if (p.extension() == ".scc") continue;
        overlay_[Lower(p.filename().string())] = levelTemplatesDir + "/" + rel;
    }
}

const Properties* TemplateCache::Find(const std::string& name) {
    if (name.empty()) return nullptr;
    std::string key = Lower(name);

    // The level's own Templates directory shadows the global set, and is
    // cached separately so it can be dropped wholesale on the next level.
    auto overlayCached = overlayLoaded_.find(key);
    if (overlayCached != overlayLoaded_.end()) return &overlayCached->second;
    auto overlayPath = overlay_.find(key);
    if (overlayPath != overlay_.end()) {
        Properties props;
        if (props.LoadFromFile(overlayPath->second))
            return &(overlayLoaded_[key] = std::move(props));
    }

    auto cached = loaded_.find(key);
    if (cached != loaded_.end()) return &cached->second;

    auto path = index_.find(key);
    if (path == index_.end()) return nullptr;

    Properties props;
    if (!props.LoadFromFile(path->second)) return nullptr;
    return &(loaded_[key] = std::move(props));
}

std::string TemplateCache::ResolveString(const Properties& instance, const std::string& baseObj,
                                         const std::string& key) {
    if (instance.Has(key)) {
        const std::string own = instance.String(key);
        if (!own.empty()) return own;
    }
    return ResolveString(baseObj, key);
}

double TemplateCache::ResolveNumber(const Properties& instance, const std::string& baseObj,
                                    const std::string& key, double fallback) {
    const double chain = ResolveNumber(baseObj, key, fallback);
    return instance.Has(key) ? instance.Number(key, chain) : chain;
}

std::string TemplateCache::ResolveString(const std::string& templateName, const std::string& key) {
    std::string current = templateName;
    // Bounded so a malformed BaseObj cycle cannot hang the loader.
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        const Properties* props = Find(current);
        if (!props) break;
        std::string value = props->String(key);
        if (!value.empty()) return value;
        current = props->String("BaseObj");
    }
    return {};
}

double TemplateCache::ResolveNumber(const std::string& templateName, const std::string& key,
                                    double fallback) {
    std::string current = templateName;
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        const Properties* props = Find(current);
        if (!props) break;
        if (props->Has(key)) return props->Number(key, fallback);
        current = props->String("BaseObj");
    }
    return fallback;
}

bool TemplateCache::ResolveBool(const std::string& templateName, const std::string& key,
                                bool fallback) {
    std::string current = templateName;
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        const Properties* props = Find(current);
        if (!props) break;
        if (props->Has(key)) return props->Bool(key, fallback);
        current = props->String("BaseObj");
    }
    return fallback;
}

int TemplateCache::ReadBodyType(const std::string& templateName) {
    const std::string key = Lower(templateName);
    auto cached = bodyTypes_.find(key);
    if (cached != bodyTypes_.end()) return cached->second;

    // The script sits beside the property file under the same stem:
    // Items/BarrelBig.CItem and Items/BarrelBig.lua.
    const std::string script = Lower(fs::path(templateName).stem().string() + ".lua");
    std::string path;
    auto overlayPath = overlay_.find(script);
    if (overlayPath != overlay_.end()) path = overlayPath->second;
    else {
        auto indexed = index_.find(script);
        if (indexed != index_.end()) path = indexed->second;
    }

    int type = -1;
    if (!path.empty()) {
        std::vector<uint8_t> bytes;
        if (ReadFile(path, bytes))
            type = BodyTypeInScript(
                std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    bodyTypes_[key] = type;
    return type;
}

int TemplateCache::BodyTypeInScript(const std::string& text) {
    const std::string marker = "PO_Create(BodyTypes.";
    const size_t at = text.find(marker);
    if (at == std::string::npos) return -1;

    size_t end = at + marker.size();
    while (end < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
        ++end;

    // Definitions.lua's BodyTypes table, verbatim.
    const std::string name = text.substr(at + marker.size(), end - at - marker.size());
    if (name == "Default") return 0;
    if (name == "Simple" || name == "Sphere") return 1;
    if (name == "Fatter") return 2;
    if (name == "DefaultSweep") return 3;
    if (name == "FromMesh") return 4;
    if (name == "FromMeshNonConvex") return 5;
    if (name == "FromMeshNotCentered") return 7;
    if (name == "SphereSweep") return 9;
    if (name == "FatterSweep") return 10;
    if (name == "FromMeshSweep") return 11;
    if (name == "FromRagdoll") return 15;
    if (name == "Player") return 100;
    return -1;
}

int TemplateCache::PhysicsBodyType(const std::string& templateName) {
    std::string current = templateName;
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        const int type = ReadBodyType(current);
        if (type >= 0) return type;
        const Properties* props = Find(current);
        if (!props) break;
        current = props->String("BaseObj");
    }
    return -1;
}

bool TemplateCache::ResolveHas(const std::string& templateName, const std::string& key) {
    std::string current = templateName;
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        const Properties* props = Find(current);
        if (!props) break;
        if (props->Has(key)) return true;
        current = props->String("BaseObj");
    }
    return false;
}

} // namespace painful
