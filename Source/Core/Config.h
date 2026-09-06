#pragma once
#include <map>
#include <string>

// painful_config.ini: the engine's own settings, beside the executable, for
// everything the original's config.ini has no field for. Plain `key = value`
// lines; `#` starts a comment. Unknown keys are kept, so a newer file survives
// an older build. Docs/Reference/Hud.md, "Widescreen".
namespace painful {

class EngineConfig {
public:
    static const char* FileName() { return "painful_config.ini"; }

    // Reads `dir/painful_config.ini`; missing is not an error. Returns false
    // only when the file exists and cannot be read.
    bool Load(const std::string& dir);
    // Writes every key, with the comment each known key carries. Also called
    // after a load with a missing file, so the user finds it with defaults.
    bool Save() const;

    std::string GetString(const std::string& key, const std::string& fallback) const;
    int GetInt(const std::string& key, int fallback) const;
    bool GetBool(const std::string& key, bool fallback) const;
    void Set(const std::string& key, const std::string& value);

    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::map<std::string, std::string> values_;
};

// The one instance, loaded by the entry point before anything reads it. The
// tools never load it and read the defaults.
EngineConfig& Settings();

}  // namespace painful
