#include "Config.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace painful {

namespace {

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// The keys the engine knows, with their defaults and the comment written
// above each in the file. Anything else in the file is kept as it was.
struct Known {
    const char* key;
    const char* value;
    const char* comment;
};
const Known kKnown[] = {
    {"HudAspect", "2",
     "How the 4:3 interface is laid onto a wider screen.\n"
     "#   0 - as the original: stretched to the window\n"
     "#   1 - the whole interface centred in a 4:3 area\n"
     "#   2 - anchored: the left third of the interface sticks to the left edge, the\n"
     "#       right third to the right edge, the middle stays centred (health left,\n"
     "#       ammo right, crosshair centred)"},
};

}  // namespace

bool EngineConfig::Load(const std::string& dir) {
    path_ = dir.empty() ? std::string(FileName()) : dir + "/" + FileName();
    for (const Known& k : kKnown) values_[k.key] = k.value;

    std::ifstream in(path_);
    if (!in) {
        Save();   // so the file is there to edit, with its defaults
        return true;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = Trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        values_[Trim(t.substr(0, eq))] = Trim(t.substr(eq + 1));
    }
    return true;
}

bool EngineConfig::Save() const {
    std::ofstream out(path_);
    if (!out) return false;
    out << "# PainfulEngine settings. The original's config.ini keeps everything it\n"
           "# always had; only what is new with PainfulEngine lives here.\n";
    std::map<std::string, std::string> rest = values_;
    for (const Known& k : kKnown) {
        out << "\n# " << k.comment << "\n";
        const auto it = rest.find(k.key);
        out << k.key << " = " << (it != rest.end() ? it->second : k.value) << "\n";
        if (it != rest.end()) rest.erase(it);
    }
    if (!rest.empty()) {
        out << "\n# Not known to this build, kept as found.\n";
        for (const auto& kv : rest) out << kv.first << " = " << kv.second << "\n";
    }
    return true;
}

std::string EngineConfig::GetString(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    return it != values_.end() ? it->second : fallback;
}

int EngineConfig::GetInt(const std::string& key, int fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(it->second.c_str(), &end, 10);
    return end && *end == '\0' ? int(v) : fallback;
}

bool EngineConfig::GetBool(const std::string& key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    std::string v = it->second;
    for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

void EngineConfig::Set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

EngineConfig& Settings() {
    static EngineConfig config;
    static bool defaults = false;
    if (!defaults) {
        defaults = true;
        for (const Known& k : kKnown) config.Set(k.key, k.value);
    }
    return config;
}

}  // namespace painful
