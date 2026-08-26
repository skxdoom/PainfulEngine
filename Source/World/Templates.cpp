#include "Templates.h"
#include <algorithm>
#include <filesystem>

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
    std::error_code ec;
    if (!fs::exists(templatesRoot, ec)) return false;
    for (const auto& entry : fs::recursive_directory_iterator(templatesRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".scc") continue;
        index_[Lower(entry.path().filename().string())] = entry.path().string();
    }
    return true;
}

void TemplateCache::SetLevelOverlay(const std::string& levelTemplatesDir) {
    overlay_.clear();
    overlayLoaded_.clear();
    if (levelTemplatesDir.empty()) return;
    std::error_code ec;
    if (!fs::exists(levelTemplatesDir, ec)) return;
    for (const auto& entry : fs::recursive_directory_iterator(levelTemplatesDir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".scc") continue;
        overlay_[Lower(entry.path().filename().string())] = entry.path().string();
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
