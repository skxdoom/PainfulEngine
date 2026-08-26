#pragma once
#include <map>
#include <string>
#include <vector>

namespace painful {

// PainEngine stores level and entity data as small Lua property files:
//
//     o.Scale = 1.2
//     o.Map = "1x01_Chaos.mpk"
//     o.Pos = Vector:New(-315.106, -2.36039, -2.90619)
//     o.Fog.Color = Color:New(172, 168, 149, 0)
//     o.IsFakeSpecular = true
//
// Only assignments are read here. Anything else in the file (functions, control
// flow) is skipped, so the same parser handles both plain data files and the
// level scripts that mix data with behaviour.
struct Value {
    enum class Kind { Number, String, Bool, Ctor };
    Kind kind = Kind::Number;
    double number = 0;
    bool boolean = false;
    std::string text;              // string contents, or ctor name ("Vector")
    std::vector<double> args;      // ctor arguments

    float Arg(size_t i, float fallback = 0.f) const {
        return i < args.size() ? static_cast<float>(args[i]) : fallback;
    }
};

class Properties {
public:
    // Parses every "o.<path> = <value>" assignment it can understand.
    bool LoadFromFile(const std::string& path);
    void LoadFromText(const std::string& text);

    bool Has(const std::string& key) const { return values_.count(key) != 0; }
    const Value* Find(const std::string& key) const;

    double     Number(const std::string& key, double fallback = 0) const;
    std::string String(const std::string& key, const std::string& fallback = "") const;
    bool       Bool(const std::string& key, bool fallback = false) const;
    // Reads a Vector:New(x,y,z) style value into three floats.
    bool       Vector3(const std::string& key, float out[3]) const;

    const std::map<std::string, Value>& all() const { return values_; }

private:
    std::map<std::string, Value> values_;
};

// Reads a placed object's orientation into a row-vector 3x3, from whichever of
// o.Rot / o.Ang the instance happens to carry. Shared by everything that
// places something in the world - models and particle emitters alike.
void ReadRotation(const Properties& props, float out[9]);

} // namespace painful
