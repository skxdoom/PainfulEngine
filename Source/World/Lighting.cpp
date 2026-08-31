#include "Lighting.h"
#include <algorithm>
#include <cmath>

namespace painful {
namespace {

void Normalize(float v[3]) {
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 1e-6f) { v[0] /= n; v[1] /= n; v[2] /= n; }
}

float Dist(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Colours are authored 0..255 in Color:New(...). Returns false when nothing in
// the chain declares the key, which matters for CEnvironment: "Dark001" sets
// DirLight.Overwrite and DirLight.Intensity but no colour, and means "the
// level's light at half strength", not "black".
bool ReadColor(TemplateCache& templates, const Properties& props, const std::string& base,
               const std::string& key, float out[3]) {
    float raw[3] = {out[0] * 255.f, out[1] * 255.f, out[2] * 255.f};
    bool found = true;
    if (!props.Vector3(key, raw)) {
        found = false;
        // Not on the instance, so walk the template chain by hand: Vector3 has
        // no resolving form, and the colour usually lives on Point_White.CLight.
        std::string name = base;
        for (int hop = 0; hop < 8 && !name.empty(); ++hop) {
            const Properties* t = templates.Find(name);
            if (!t) break;
            if (t->Vector3(key, raw)) { found = true; break; }
            const std::string next = t->String("BaseObj", "");
            if (next == name) break;
            name = next;
        }
    }
    if (found) for (int i = 0; i < 3; ++i) out[i] = raw[i] / 255.f;
    return found;
}

bool ReadVector(TemplateCache& templates, const Properties& props, const std::string& base,
                const std::string& key, float out[3]) {
    if (props.Vector3(key, out)) return true;
    std::string name = base;
    for (int hop = 0; hop < 8 && !name.empty(); ++hop) {
        const Properties* t = templates.Find(name);
        if (!t) break;
        if (t->Vector3(key, out)) return true;
        const std::string next = t->String("BaseObj", "");
        if (next == name) break;
        name = next;
    }
    return false;
}

} // namespace

void EntityLighting::Clear() {
    lights_.clear();
    environments_.clear();
}

void EntityLighting::Build(const Level& level, TemplateCache& templates) {
    Clear();
    const LevelInfo& info = level.info();
    for (int i = 0; i < 3; ++i) {
        levelAmbient_[i] = info.ambient[i] / 255.f;
        levelDirColor_[i] = info.dirLightColor[i] / 255.f;
        levelDirDir_[i] = -info.dirLightDir[i];    // direction TO the light
    }
    levelDirIntensity_ = info.dirLightIntensity;
    Normalize(levelDirDir_);

    for (const Entity& e : level.entities()) {
        if (e.type == "CLight") {
            Light l;
            l.type = static_cast<int>(
                templates.ResolveNumber(e.props, e.baseObj, "Type", kPoint));
            l.intensity = static_cast<float>(
                templates.ResolveNumber(e.props, e.baseObj, "Intensity", 1.0));
            l.range = static_cast<float>(
                templates.ResolveNumber(e.props, e.baseObj, "Range", 10.0));
            l.startFalloff = static_cast<float>(
                templates.ResolveNumber(e.props, e.baseObj, "StartFalloff", 0.0));
            // A cone that was never authored must not black out a spot light,
            // so an absent angle means "no cone".
            const double cone = templates.ResolveNumber(e.props, e.baseObj, "ConeAngle", 0.0);
            const double outer = templates.ResolveNumber(e.props, e.baseObj, "ConeOuterAngle", cone);
            l.coneCos = cone > 0.0 ? float(std::cos(cone * 3.14159265 / 360.0)) : -1.f;
            l.coneOuterCos = outer > 0.0 ? float(std::cos(outer * 3.14159265 / 360.0)) : -1.f;
            l.fakeSpecular = templates.ResolveNumber(e.props, e.baseObj, "IsFakeSpecular", 0.0) != 0.0 ||
                             e.props.Bool("IsFakeSpecular", false);
            l.color[0] = l.color[1] = l.color[2] = 1.f;
            ReadColor(templates, e.props, e.baseObj, "Color", l.color);
            for (int i = 0; i < 3; ++i) l.pos[i] = e.pos[i];
            e.props.Vector3("Pos", l.pos);
            ReadVector(templates, e.props, e.baseObj, "Direction", l.dir);
            Normalize(l.dir);
            // An IsFakeSpecular light is not an entity light at all. Engine.dll
            // has WorldMesh::AddSpecularLight beside Entity::AddLight, the
            // scripts call MESH.ResetSpecularLights, and Entity::AddLight
            // (0x1d1b70) rejects a light carrying the fake-specular flag
            // (0x01000000) unless the entity names it explicitly. It shows:
            // Cathedral's aa_fake1 has Range 5000 and would otherwise occupy a
            // slot everywhere in the level, crowding out a real light.
            if (l.fakeSpecular) continue;
            if (l.intensity > 0.f && l.range > 0.f) lights_.push_back(l);
            continue;
        }
        if (e.type != "CEnvironment") continue;

        Environment env;
        float centre[3] = {e.pos[0], e.pos[1], e.pos[2]};
        e.props.Vector3("Pos", centre);
        const float w = float(templates.ResolveNumber(e.props, e.baseObj, "Size.Width", 0.0));
        const float h = float(templates.ResolveNumber(e.props, e.baseObj, "Size.Height", 0.0));
        const float d = float(templates.ResolveNumber(e.props, e.baseObj, "Size.Depth", 0.0));
        if (w <= 0.f || h <= 0.f || d <= 0.f) continue;
        const float half[3] = {w * 0.5f, h * 0.5f, d * 0.5f};
        for (int i = 0; i < 3; ++i) {
            env.lo[i] = centre[i] - half[i];
            env.hi[i] = centre[i] + half[i];
        }
        env.volume = w * h * d;
        env.ambientOverwrite = e.props.Bool("Ambient.Overwrite", false);
        env.dirOverwrite = e.props.Bool("DirLight.Overwrite", false);
        // Overwrite means "my values win", not "everything I did not mention is
        // black": Cathedral's "Dark001" sets DirLight.Overwrite and
        // DirLight.Intensity 0.5 and no colour, meaning the level's light at
        // half strength. So each field is applied only where it was authored.
        env.hasAmbient = ReadColor(templates, e.props, e.baseObj, "Ambient.Color", env.ambient);
        env.hasDirColor = ReadColor(templates, e.props, e.baseObj, "DirLight.Color", env.dirColor);
        env.hasDirDir = ReadVector(templates, e.props, e.baseObj, "DirLight.Dir", env.dirDir);
        Normalize(env.dirDir);
        env.dirIntensity = float(
            templates.ResolveNumber(e.props, e.baseObj, "DirLight.Intensity", 1.0));
        env.fadeTime = float(
            templates.ResolveNumber(e.props, e.baseObj, "DirLight.FadeTime", 0.0));
        environments_.push_back(env);
    }
}

float EntityLighting::AttIntensity(const Light& l, const float pos[3]) const {
    if (l.type == kDirectional) return l.intensity;

    const float d = Dist(l.pos, pos);
    if (d > l.range) return 0.f;
    float att = 1.f;
    if (d > l.startFalloff && l.range > l.startFalloff)
        att = (d - l.range) / (l.startFalloff - l.range);

    if (l.type == kSpot && l.coneCos > -1.f) {
        // Axis term: how far off the cone centre the entity sits.
        float toEntity[3] = {pos[0] - l.pos[0], pos[1] - l.pos[1], pos[2] - l.pos[2]};
        Normalize(toEntity);
        const float c = toEntity[0] * l.dir[0] + toEntity[1] * l.dir[1] + toEntity[2] * l.dir[2];
        if (c < l.coneOuterCos) return 0.f;
        if (c < l.coneCos && l.coneCos > l.coneOuterCos)
            att *= (c - l.coneOuterCos) / (l.coneCos - l.coneOuterCos);
    }
    return att * l.intensity;
}

const EntityLighting::Environment* EntityLighting::Innermost(const float pos[3]) const {
    const Environment* best = nullptr;
    for (const Environment& e : environments_) {
        bool inside = true;
        for (int i = 0; i < 3 && inside; ++i)
            inside = pos[i] >= e.lo[i] && pos[i] <= e.hi[i];
        // Boxes nest - a dark alcove sits inside a dark hall - and the tightest
        // one is the one the entity is actually standing in.
        if (inside && (!best || e.volume < best->volume)) best = &e;
    }
    return best;
}

void EntityLighting::Evaluate(const float pos[3], const float camPos[3], float dt,
                              EntityLightFade& fade, EntityLightState& out) const {
    // --- ambient and the one directional, per environment ---
    float ambient[3], dirColor[3], dirDir[3];
    for (int i = 0; i < 3; ++i) {
        ambient[i] = levelAmbient_[i];
        dirColor[i] = levelDirColor_[i];   // intensity applied below
        dirDir[i] = levelDirDir_[i];
    }
    float intensity = levelDirIntensity_;
    float fadeTime = 0.f;
    if (const Environment* env = Innermost(pos)) {
        fadeTime = env->fadeTime;
        if (env->ambientOverwrite && env->hasAmbient)
            for (int i = 0; i < 3; ++i) ambient[i] = env->ambient[i];
        if (env->dirOverwrite) {
            // Intensity always applies; colour and direction only where stated.
            if (env->hasDirColor)
                for (int i = 0; i < 3; ++i) dirColor[i] = env->dirColor[i];
            intensity = env->dirIntensity;
            if (env->hasDirDir) {
                for (int i = 0; i < 3; ++i) dirDir[i] = -env->dirDir[i];
                Normalize(dirDir);
            }
        }
    }
    // The intensity multiplies whichever colour won.
    for (int i = 0; i < 3; ++i) dirColor[i] *= intensity;

    // Entity::GetEnvironmentDirLight lerps toward the new environment instead
    // of snapping, so walking through a doorway is a fade, not a step.

    float k = 1.f;
    if (fade.primed && fadeTime > 1e-3f) k = std::min(1.f, dt / fadeTime);
    if (!fade.primed) {
        for (int i = 0; i < 3; ++i) {
            fade.ambient[i] = ambient[i];
            fade.dirColor[i] = dirColor[i];
            fade.dirDir[i] = dirDir[i];
        }
        fade.primed = true;
    } else {
        for (int i = 0; i < 3; ++i) {
            fade.ambient[i] += (ambient[i] - fade.ambient[i]) * k;
            fade.dirColor[i] += (dirColor[i] - fade.dirColor[i]) * k;
            fade.dirDir[i] += (dirDir[i] - fade.dirDir[i]) * k;
        }
        Normalize(fade.dirDir);
    }
    for (int i = 0; i < 3; ++i) out.ambient[i] = fade.ambient[i];

    // --- the four slots ---
    // Entity::AddLight keeps a list of four sorted by attenuated intensity,
    // descending, so a model near two torches takes the two brightest.
    //
    // The environment's directional is ONE OF THEM, not a term beside them:
    // Entity::ResetLights (0x1d2c70) does AddLight(this,
    // GetEnvironmentDirLight(this)), so it competes for a slot like any other
    // light and, being a light, casts specular too. Its score is its intensity
    // flat (Light::GetAttIntensity returns that for type 1), which usually wins
    // it slot 0.
    int best[kMaxEntityLights] = {-1, -1, -1, -1};
    float score[kMaxEntityLights] = {0, 0, 0, 0};
    int count = 0;
    // -1 is the environment directional; 0.. index lights_.
    auto consider = [&](int index, float s) {
        if (s <= 0.f) return;
        int at = 0;
        while (at < count && score[at] >= s) ++at;
        if (at >= kMaxEntityLights) return;
        for (int j = std::min(count, kMaxEntityLights - 1); j > at; --j) {
            best[j] = best[j - 1];
            score[j] = score[j - 1];
        }
        best[at] = index;
        score[at] = s;
        if (count < kMaxEntityLights) ++count;
    };
    const float dirScore = std::max({fade.dirColor[0], fade.dirColor[1], fade.dirColor[2]});
    consider(-1, dirScore);
    for (size_t i = 0; i < lights_.size(); ++i) consider(int(i), AttIntensity(lights_[i], pos));

    for (int s = 0; s < kMaxEntityLights; ++s) {
        EntityLightSlot& slot = out.slots[s];
        if (best[s] < 0 && s >= count) { slot = EntityLightSlot(); continue; }

        float dir[3], color[3];
        float att = score[s];
        bool fakeSpecular = false;
        if (best[s] < 0) {
            // The environment directional, already faded.
            for (int i = 0; i < 3; ++i) {
                dir[i] = fade.dirDir[i];
                color[i] = fade.dirColor[i];
            }
            att = 1.f;
        } else {
            const Light& l = lights_[best[s]];
            fakeSpecular = l.fakeSpecular;
            if (l.type == kDirectional)
                for (int i = 0; i < 3; ++i) dir[i] = -l.dir[i];
            else
                for (int i = 0; i < 3; ++i) dir[i] = l.pos[i] - pos[i];
            for (int i = 0; i < 3; ++i) color[i] = l.color[i] * att;
        }
        Normalize(dir);

        for (int i = 0; i < 3; ++i) slot.color[i] = color[i];
        slot.color[3] = att;
        for (int i = 0; i < 3; ++i) slot.dir[i] = dir[i];
        slot.dir[3] = 1.f;

        // ComputeVSLights: H = normalize((camera - entity) + lightDir), once
        // for the whole model.
        float h[3] = {(camPos[0] - pos[0]) + dir[0],
                      (camPos[1] - pos[1]) + dir[1],
                      (camPos[2] - pos[2]) + dir[2]};
        Normalize(h);
        for (int i = 0; i < 3; ++i) slot.half[i] = h[i];
        slot.half[3] = fakeSpecular ? 0.f : 1.f;
    }
}

} // namespace painful
