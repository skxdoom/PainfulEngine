#pragma once
#include "../Assets/Mpk.h"
#include "../Assets/Properties.h"
#include <string>
#include <vector>

namespace painful {

// One placed object in a level, e.g. Levels/<level>/CItem/AmmoGrenades_001.CItem.
// Instances are thin: a position plus a BaseObj reference to a template under
// LScripts/Templates, with any per-instance overrides.
struct Entity {
    std::string type;        // "CItem", "CLight", "CSpawnPoint", ...
    std::string name;        // file stem
    std::string baseObj;     // template this instance derives from
    float pos[3] = {0, 0, 0};
    Properties props;        // everything the file declared
};

// One texture within a sky layer, with its own scroll/rotate/tile animation.
struct SkyTexture {
    std::string name;
    float panU = 0.f, panV = 0.f;   // UV units per second
    float rotSpeed = 0.f;           // radians per second, about the UV centre
    float tileU = 1.f, tileV = 1.f;
};

// A sky layer blends two animated textures using a mask, then modulates by a
// lightmap: colour = mix(Tex1, Tex2, Mask) * Tex_LMap. Layer 1 is the opaque
// base (its mask is "_black", selecting Tex1); later layers blend over it,
// typically with Tex1 = "_alpha_zero" so the mask cuts out where Tex2 shows.
struct SkyLayer {
    SkyTexture tex1, tex2;
    std::string mask, lightmap;
    bool valid() const { return !tex1.name.empty() || !tex2.name.empty(); }
};

// o.Water from the level's .CLevel. This is where the numbers the water shader
// needs actually live - not in water.shader, whose tile[0]/pan[0] merely repeat
// the class defaults below, and not in Engine.dll. 21 levels declare it.
// Defaults are CLevel.lua's own.
struct WaterInfo {
    float fresnelBias = 0.f;
    float fresnelExponent = 2.f;
    float deepColor[3] = {150.f, 150.f, 100.f};      // 0..255, as authored
    float shallowColor[3] = {100.f, 100.f, 100.f};
    float bumpHeight = 0.05f;                        // normal-map strength
    float waveAmplitude = 1.f, waveFrequency = 1.f, waveSpeed = 1.f;
    float waterAmount = 1.f, reflectionAmount = 1.f;
    float waterLevel = 0.f;
    // These two are the quality bits that make SetupMaterials reach for
    // water2_refl / water2_refr instead of plain water.
    bool reflectScene = false, refractScene = false;
    float pan[2] = {0.00172f, 0.003f};
    float tile[2] = {17.5f, 10.f};
};

// Values read from <level>.CLevel. Names mirror the original property paths so
// they stay greppable against the shipped data.
struct LevelInfo {
    std::string mapFile;         // o.Map, e.g. "1x01_Chaos.mpk"
    std::string waypointsFile;   // o.WayPointsMap
    float scale = 0.3f;          // o.Scale - multiplies the WORLD MESH, not entities
    bool overbright = false;     // o.Overbright - selects the x2 lightmap material set
    std::string detailTex = "special/detail";   // DetailMap.Tex
    float detailTileU = 8.2f, detailTileV = 7.1f;
    // CLevel.lua's class defaults: a level that states no Ambient gets 50,50,50
    // (Catacombs does), and Entity::GetEnvironmentAmbient (0x101D0CA0) falls
    // back to exactly that world ambient. Docs/Reference/Levels.md, "Lighting defaults".
    float ambient[3] = {50, 50, 50};
    // o.DirLight - the level's own directional light, which every entity gets
    // unless a CEnvironment it stands in overwrites it. The world mesh does not
    // use this: its lighting is baked. Colour is 0..255 as authored.
    float dirLightColor[3] = {150, 150, 100};
    float dirLightDir[3] = {-0.7f, -0.7f, -0.7f};
    float dirLightIntensity = 1.f;
    float fogColor[3] = {0, 0, 0};
    // o.BloomFX - CLevel class defaults. With Cfg.Bloom on (the default) and
    // Multiplier > 0, sprites are drawn at DimScale. Particles.md, "Bloom dims".
    float bloomMultiplier = 1.f, bloomDimScale = 0.8f;
    float fogDensity = 0.f, fogStart = 0.f;
    float fogEnd = 90.f;             // Fog.End, class default 90
    int   fogMode = 0;               // 0=none, 1=exp, 2=exp2, 3=linear (CLevel.lua)
    // o.FarClipDist x (Cfg.ClipPlane+100)/200; ClipPlane defaults to 100, so
    // the factor is 1. The original hard-clips the world here and lets the
    // fog ramp hide the cut.
    float farClip = 1024.f;
    float meshFriction = 0.7f;   // o.Physics.DefaultMeshFriction
    float startPos[3] = {0, 0, 0};
    float angles[3] = {0, 0, 0};    // o.Ang, degrees
    // Sky dome. The full version is four animated layers; we use the engine own
    // LowQuality fallback: a mesh plus a single texture and a yaw offset.
    std::string skyMap;             // o.SkyDome.LowQuality.Map
    std::string skyTexture;         // o.SkyDome.LowQuality.Tex
    float skyAngle = 0.f;           // o.SkyDome.LowQuality.Angle, degrees
    std::string skyDomeMap;         // o.SkyDome.Map - the full layered dome
    SkyLayer skyLayers[4];
    WaterInfo water;
};

// A loaded level: its settings, its placed entities, and the world mesh it names.
class Level {
public:
    // levelDir is <DataRoot>/Levels/<name>; both resolve through the mounted
    // .pak archives or a loose extracted tree alike.
    bool Load(const std::string& levelDir, const std::string& dataRoot);

    const std::string& name() const { return name_; }
    const LevelInfo& info() const { return info_; }
    const std::vector<Entity>& entities() const { return entities_; }
    const MapMesh& map() const { return map_; }
    bool mapLoaded() const { return mapLoaded_; }
    const std::string& error() const { return error_; }

    // Entities of one type, e.g. CountOf("CLight").
    size_t CountOf(const std::string& type) const;

private:
    bool LoadSettings(const std::string& levelDir);
    void LoadEntities(const std::string& levelDir);

    std::string name_, error_;
    LevelInfo info_;
    std::vector<Entity> entities_;
    MapMesh map_;
    bool mapLoaded_ = false;
};

} // namespace painful
