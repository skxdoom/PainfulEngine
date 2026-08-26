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

// Values read from <level>.CLevel. Names mirror the original property paths so
// they stay greppable against the shipped data.
struct LevelInfo {
    std::string mapFile;         // o.Map, e.g. "1x01_Chaos.mpk"
    std::string waypointsFile;   // o.WayPointsMap
    float scale = 0.3f;          // o.Scale - multiplies the WORLD MESH, not entities
    float ambient[3] = {0, 0, 0};
    float fogColor[3] = {0, 0, 0};
    float fogDensity = 0.f, fogStart = 0.f;
    int   fogMode = 0;
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
};

// A loaded level: its settings, its placed entities, and the world mesh it names.
class Level {
public:
    // levelDir is Data_Extracted/Levels/<name>; dataRoot is Data_Extracted.
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
