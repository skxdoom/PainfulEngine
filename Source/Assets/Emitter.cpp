#include "Emitter.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace painful {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (a < b && space(s[a])) ++a;
    while (b > a && space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string Stem(const std::string& name) {
    std::string s = name;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    return Lower(s);
}

bool LoadText(const std::string& path, std::string& out) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

// A parsed .ini: "section.key" -> value, names folded to lower case. The
// original's ConfigFile compares names case-insensitively (the sibling material
// parser uses stricmp throughout) and the shipped files are inconsistent about
// case, so folding keeps "Scale" and "scale" together.
class Ini {
public:
    void Parse(const std::string& text) {
        std::string section;
        size_t i = 0;
        while (i <= text.size()) {
            const size_t end = text.find('\n', i);
            const std::string line =
                Trim(text.substr(i, end == std::string::npos ? std::string::npos : end - i));
            i = (end == std::string::npos) ? text.size() + 1 : end + 1;
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[') {
                const size_t close = line.find(']');
                if (close != std::string::npos) section = Lower(Trim(line.substr(1, close - 1)));
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            values_[section + '.' + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
        }
    }

    // Null when the key is absent, mirroring ConfigFile::GetString - the
    // original leaves the constructor's default in place in that case.
    const std::string* Get(const char* section, const std::string& key) const {
        auto it = values_.find(std::string(section) + '.' + Lower(key));
        return it == values_.end() ? nullptr : &it->second;
    }

    bool Float(const char* section, const std::string& key, float& out) const {
        if (const std::string* v = Get(section, key)) {
            out = static_cast<float>(std::atof(v->c_str()));
            return true;
        }
        return false;
    }

    bool Int(const char* section, const std::string& key, int& out) const {
        if (const std::string* v = Get(section, key)) {
            out = std::atoi(v->c_str());
            return true;
        }
        return false;
    }

    bool Bool(const char* section, const std::string& key, bool& out) const {
        int v = 0;
        if (!Int(section, key, v)) return false;
        out = v != 0;
        return true;
    }

    // Reads "<key>.X/.Y/.Z" into three floats, each independently optional.
    void Vec3(const char* section, const std::string& key, float out[3]) const {
        static const char* kAxis[3] = {".x", ".y", ".z"};
        for (int i = 0; i < 3; ++i) Float(section, key + kAxis[i], out[i]);
    }

private:
    std::map<std::string, std::string> values_;
};

// Older editor builds wrote blend modes as single bits; LoadEmitter folds them
// onto the material system's enum before anything else looks at the value.
int RemapBlendMode(int raw) {
    switch (raw) {
        case 0x200: return kBlendAlpha;
        case 0x100: return kBlendTranslucent;
        case 0x80:  return kBlendAdd;
        case 0x40:  return kBlendFilter;
        default:    return raw;
    }
}

}  // namespace

bool ParseEmitterIni(const std::string& text, EmitterParams& p) {
    Ini ini;
    ini.Parse(text);

    if (const std::string* v = ini.Get("general", "Texture")) p.texture = *v;
    if (const std::string* v = ini.Get("general", "Material")) p.material = *v;
    if (const std::string* v = ini.Get("general", "WarpTex")) p.warpTex = *v;
    // TexAnimFPS goes through atol in the original, so a fractional rate would
    // truncate. Read the same way here.
    int fps = 0;
    if (ini.Int("general", "TexAnimFPS", fps)) p.texAnimFps = static_cast<float>(fps);
    ini.Int("general", "Type", p.type);
    int blend = 0;
    if (ini.Int("general", "BlendMode", blend)) p.blendMode = RemapBlendMode(blend);
    ini.Bool("general", "UseRandomNormal", p.randomNormal);
    ini.Bool("general", "DepthTest", p.depthTest);
    ini.Bool("general", "Evolve", p.evolve);
    ini.Bool("general", "Warp", p.warp);
    ini.Int("general", "MaxParticles", p.maxParticles);
    float rate = 0.f;
    if (ini.Float("general", "SpawnRate", rate))
        p.spawnInterval = rate != 0.f ? 1.f / rate : 0.f;
    float killDist = 0.f;
    ini.Float("general", "KillDist", killDist);
    p.killDistSq = killDist * killDist;

    ini.Vec3("editorposition", "Pos", p.editorPos);
    ini.Vec3("posrange", "Min", p.posMin);
    ini.Vec3("posrange", "Max", p.posMax);

    ini.Vec3("velocity", "Min", p.velMin);
    ini.Vec3("velocity", "Max", p.velMax);
    // Acceleration seeds both ends of the range; AccelMax then raises the top.
    ini.Vec3("velocity", "Acceleration", p.accelMin);
    for (int i = 0; i < 3; ++i) p.accelMax[i] = p.accelMin[i];
    ini.Vec3("velocity", "AccelMax", p.accelMax);

    // A missing [VelocityEnd] leaves the particle at its starting velocity.
    for (int i = 0; i < 3; ++i) {
        p.velEndMin[i] = p.velMin[i];
        p.velEndMax[i] = p.velMax[i];
    }
    ini.Vec3("velocityend", "Min", p.velEndMin);
    ini.Vec3("velocityend", "Max", p.velEndMax);

    ini.Vec3("color", "Min", p.colorMin);
    ini.Vec3("color", "Max", p.colorMax);
    ini.Float("color", "AlphaMin", p.alphaMin);
    ini.Float("color", "AlphaMax", p.alphaMax);
    p.alphaMid = p.alphaMax;
    ini.Float("color", "AlphaMid", p.alphaMid);
    ini.Float("color", "FadeTimeMin", p.fadeTimeMin);
    ini.Float("color", "FadeTimeMax", p.fadeTimeMax);
    ini.Float("color", "VelBlendMin", p.velBlendMin);
    ini.Float("color", "VelBlendMax", p.velBlendMax);
    // LoadEmitter zeroes both before reading, so the constructor's -1..1 spin
    // never survives into a file-loaded emitter.
    p.rotMin = p.rotMax = 0.f;
    ini.Float("color", "RotationMin", p.rotMin);
    ini.Float("color", "RotationMax", p.rotMax);

    ini.Float("sizelife", "StartSizeMin", p.startSizeMin);
    ini.Float("sizelife", "StartSizeMax", p.startSizeMax);
    ini.Float("sizelife", "EndSizeMin", p.endSizeMin);
    ini.Float("sizelife", "EndSizeMax", p.endSizeMax);
    ini.Float("sizelife", "LifeTimeMin", p.lifeMin);
    ini.Float("sizelife", "LifeTimeMax", p.lifeMax);
    ini.Bool("sizelife", "Immortal", p.immortal);

    ini.Float("sparkemitter", "ThicknessMin", p.thicknessMin);
    ini.Float("sparkemitter", "ThicknessMax", p.thicknessMax);
    ini.Float("sparkemitter", "LengthMin", p.lengthMin);
    ini.Float("sparkemitter", "LengthMax", p.lengthMax);

    // A material named particle_warp with no explicit WarpTex picks up "warp".
    if (p.material == "particle_warp" && p.warpTex.empty()) p.warpTex = "warp";
    return true;
}

bool ParseParticleFx(const std::string& text, ParticleFxDef& out) {
    // The .pfx grammar is a Lua table literal, but only four keys ever appear
    // inside an emitter entry and one alongside the list, so a line scan is
    // enough: every "File =" opens a new entry and the keys that follow belong
    // to it. Checked against the key census of all 318 shipped effects.
    ParticleFxDef::Ref* cur = nullptr;
    size_t i = 0;
    while (i <= text.size()) {
        const size_t end = text.find('\n', i);
        std::string line =
            Trim(text.substr(i, end == std::string::npos ? std::string::npos : end - i));
        i = (end == std::string::npos) ? text.size() + 1 : end + 1;
        if (line.empty()) continue;
        if (line.back() == ',') line.pop_back();

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Lower(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));

        auto readTriple = [&value](float dst[3]) {
            const size_t open = value.find('{');
            if (open == std::string::npos) return;
            const char* p = value.c_str() + open + 1;
            char* next = nullptr;
            for (int a = 0; a < 3 && *p; ++a) {
                dst[a] = static_cast<float>(std::strtod(p, &next));
                if (next == p) break;
                p = next;
                while (*p == ' ' || *p == ',') ++p;
            }
        };

        if (key == "file") {
            out.emitters.emplace_back();
            cur = &out.emitters.back();
            const size_t a = value.find('"');
            const size_t b = value.find_last_of('"');
            cur->file = (a != std::string::npos && b > a) ? value.substr(a + 1, b - a - 1) : value;
        } else if (key == "fixedtransform") {
            out.fixedTransform = Lower(value) == "true";
        } else if (cur) {
            if (key == "scale") cur->scale = static_cast<float>(std::atof(value.c_str()));
            else if (key == "position") readTriple(cur->position);
            else if (key == "rotation") readTriple(cur->rotation);
        }
    }
    return !out.emitters.empty();
}

bool EmitterLibrary::Init(const std::string& scriptsRoot) {
    namespace fs = std::filesystem;
    auto index = [&](const char* sub, const char* ext,
                     std::map<std::string, std::string>& dst) {
        const std::string dir = scriptsRoot + "/" + sub;
        for (const DirEntry& entry : FileSystem::Get().List(dir)) {
            if (entry.isDirectory) continue;
            if (Lower(fs::path(entry.name).extension().string()) != ext) continue;
            dst[Stem(entry.name)] = dir + "/" + entry.name;
        }
    };
    index("Emitters", ".ini", emitterIndex_);
    index("Effects", ".pfx", effectIndex_);
    if (emitterIndex_.empty() && effectIndex_.empty()) {
        errors_.push_back("no particle scripts under " + scriptsRoot);
        return false;
    }
    return true;
}

const EmitterParams* EmitterLibrary::Emitter(const std::string& name) {
    const std::string key = Stem(name);
    auto cached = emitters_.find(key);
    if (cached != emitters_.end()) return cached->second.get();

    auto path = emitterIndex_.find(key);
    if (path == emitterIndex_.end()) {
        errors_.push_back("unknown emitter '" + name + "'");
        emitters_[key] = nullptr;
        return nullptr;
    }
    std::string text;
    if (!LoadText(path->second, text)) {
        errors_.push_back("cannot read " + path->second);
        emitters_[key] = nullptr;
        return nullptr;
    }
    auto params = std::make_unique<EmitterParams>();
    params->name = key;
    ParseEmitterIni(text, *params);
    const EmitterParams* result = params.get();
    emitters_[key] = std::move(params);
    return result;
}

const ParticleFxDef* EmitterLibrary::Effect(const std::string& name) {
    const std::string key = Stem(name);
    auto cached = effects_.find(key);
    if (cached != effects_.end()) return cached->second.get();

    auto path = effectIndex_.find(key);
    if (path == effectIndex_.end()) {
        errors_.push_back("unknown effect '" + name + "'");
        effects_[key] = nullptr;
        return nullptr;
    }
    std::string text;
    if (!LoadText(path->second, text)) {
        errors_.push_back("cannot read " + path->second);
        effects_[key] = nullptr;
        return nullptr;
    }
    auto def = std::make_unique<ParticleFxDef>();
    def->name = key;
    if (!ParseParticleFx(text, *def)) errors_.push_back("no emitters in " + path->second);
    const ParticleFxDef* result = def.get();
    effects_[key] = std::move(def);
    return result;
}

} // namespace painful
