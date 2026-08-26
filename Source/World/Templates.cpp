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

const Properties* TemplateCache::Find(const std::string& name) {
    if (name.empty()) return nullptr;
    std::string key = Lower(name);

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
