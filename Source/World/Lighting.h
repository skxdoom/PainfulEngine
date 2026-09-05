#pragma once
#include "Level.h"
#include "Templates.h"
#include <string>
#include <vector>

namespace painful {

// How PainEngine lights MODELS. The world mesh has baked lightmaps; entities
// have none, and are lit at runtime instead:
//
//   ambient          from the level's o.Ambient, overwritten by whichever
//                    CEnvironment box the entity stands in
//   one directional  from o.DirLight, likewise overwritten, and CROSS-FADED
//                    over DirLight.FadeTime as the entity crosses a boundary
//   up to four       the nearest CLights by attenuated intensity, evaluated at
//   dynamic lights   the ENTITY ORIGIN - one value for the whole model
//
// This is Entity::ComputeVSLights (Engine.dll 0x1d1dc0), which fills three
// vertex-shader constants per light - colour, direction, half-vector - at c12
// through c23, with ambient at c10 and the bone palette from c60 ("def ntu,
// 3C"). skin.shader's `specular true` is what adds the specular term after the
// texture modulate.
//
// The half-vector is computed ONCE PER ENTITY, from the entity origin, not per
// vertex. That is why the original's specular reads as a coarse, low-frequency
// sheen rather than a tight highlight, and it is reproduced here rather than
// improved on.
constexpr int kMaxEntityLights = 4;      // Entity::MaxLights

// One light as the vertex shader sees it. Layout mirrors the engine's three
// consecutive constants.
struct EntityLightSlot {
    // rgb: light colour x intensity x attenuation, 0..1. w: the attenuation on
    // its own, which is what the engine puts there.
    float color[4] = {0, 0, 0, 0};
    // xyz: direction from the entity TO the light (already negated for a
    // directional). w: 1 when the slot holds a light.
    float dir[4] = {1, 0, 0, 0};
    // xyz: Blinn half-vector, normalize((camera - entity) + dir). w: how much
    // DIFFUSE this light contributes - 0 for an IsFakeSpecular light, which is
    // placed only to put a highlight on models and would wash out the level if
    // it lit them too (Cathedral's aa_fake1 has Range 5000).
    float half[4] = {1, 0, 0, 0};
};

// Everything a model needs for one frame.
struct EntityLightState {
    float ambient[3] = {0, 0, 0};              // 0..1
    // The directional is not carried here - it goes through the slots, the
    // way Entity::ResetLights AddLight()s it. EntityLightFade keeps its
    // faded colour and direction between frames.
    EntityLightSlot slots[kMaxEntityLights];
};

// The per-entity fade state. An entity keeps one of these because
// Entity::GetEnvironmentDirLight lerps colour, intensity and direction toward
// the environment it is entering rather than snapping.
struct EntityLightFade {
    bool primed = false;
    float ambient[3] = {0, 0, 0};
    float dirColor[3] = {0, 0, 0};
    float dirDir[3] = {0, 1, 0};
};

class EntityLighting {
public:
    // Reads the level's CLight and CEnvironment entities. Values resolve
    // through the BaseObj chain, because a placed light usually declares only
    // its position and colour - Point_White.CLight is what says it is a point
    // light with Range 10.
    void Build(const Level& level, TemplateCache& templates);
    void Clear();
    // The world ambient the scripts set through WORLD.AmbientColor (0-255),
    // which is what Entity::GetEnvironmentAmbient falls back to.
    void SetLevelAmbient(const float rgb255[3]) {
        for (int i = 0; i < 3; ++i) levelAmbient_[i] = rgb255[i] / 255.f;
    }

    // Lights the entity at pos, seen from camPos. fade carries the environment
    // cross-fade between calls; dt is the frame time in seconds. Pass a fade
    // block per entity, or a throwaway one to snap.
    void Evaluate(const float pos[3], const float camPos[3], float dt,
                  EntityLightFade& fade, EntityLightState& out) const;

    size_t lightCount() const { return lights_.size(); }
    size_t environmentCount() const { return environments_.size(); }

private:
    // Light::GetType - 1 directional, 2 point, 3 spot.
    enum Type { kDirectional = 1, kPoint = 2, kSpot = 3 };

    struct Light {
        int type = kPoint;
        float pos[3] = {0, 0, 0};
        float dir[3] = {0, -1, 0};
        float color[3] = {1, 1, 1};      // 0..1
        float intensity = 1.f;
        // Light::GetAttIntensity: full brightness within startFalloff, zero
        // past range, linear between. The engine stores range first.
        float range = 10.f;
        float startFalloff = 0.f;
        float coneCos = 0.f, coneOuterCos = 0.f;
        bool fakeSpecular = false;
    };

    // A CEnvironment: an axis-aligned box that overwrites the lighting of
    // entities inside it. Cathedral places 50.
    struct Environment {
        float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        bool ambientOverwrite = false, dirOverwrite = false;
        // Which fields the file actually declared - see the Overwrite note in
        // Build. Anything unstated keeps the level's own value.
        bool hasAmbient = false, hasDirColor = false, hasDirDir = false;
        float ambient[3] = {0, 0, 0};
        float dirColor[3] = {0, 0, 0};
        float dirDir[3] = {0, -1, 0};
        float dirIntensity = 1.f;
        float fadeTime = 0.f;
        float volume = 0.f;              // smallest box wins
    };

    // Light::GetAttIntensity - the value AddLight sorts the four slots by.
    float AttIntensity(const Light& l, const float pos[3]) const;
    const Environment* Innermost(const float pos[3]) const;

    std::vector<Light> lights_;
    std::vector<Environment> environments_;
    float levelAmbient_[3] = {0, 0, 0};
    float levelDirColor_[3] = {0, 0, 0};
    float levelDirIntensity_ = 1.f;
    float levelDirDir_[3] = {0, -1, 0};
};

} // namespace painful
