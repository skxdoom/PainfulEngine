#include "Level.h"
#include "../Core/Log.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace painful {

bool Level::LoadSettings(const std::string& levelDir) {
    name_ = fs::path(levelDir).filename().string();

    // The settings file is named after the level folder.
    fs::path settings = fs::path(levelDir) / (name_ + ".CLevel");
    Properties p;
    if (!p.LoadFromFile(settings.string())) {
        error_ = "cannot read " + settings.string();
        return false;
    }

    info_.mapFile = p.String("Map");
    info_.waypointsFile = p.String("WayPointsMap");
    // The engine scales the static world mesh by o.Scale at load
    // (CLevel.lua: WORLD.LoadMap(..., self.Scale, ...)). The CLevel class
    // default is 0.3, NOT 1 - levels that omit the key rely on it.
    info_.scale = static_cast<float>(p.Number("Scale", 0.3));
    info_.overbright = p.Bool("Overbright", false);
    info_.fogDensity = static_cast<float>(p.Number("Fog.Density"));
    info_.fogStart = static_cast<float>(p.Number("Fog.Start"));
    info_.fogMode = static_cast<int>(p.Number("Fog.Mode"));
    info_.meshFriction = static_cast<float>(p.Number("Physics.DefaultMeshFriction", 0.7));
    p.Vector3("Ambient", info_.ambient);
    p.Vector3("Fog.Color", info_.fogColor);
    p.Vector3("Pos", info_.startPos);
    p.Vector3("Ang", info_.angles);
    info_.skyMap = p.String("SkyDome.LowQuality.Map");
    info_.skyTexture = p.String("SkyDome.LowQuality.Tex");
    info_.skyAngle = static_cast<float>(p.Number("SkyDome.LowQuality.Angle", 0.0));
    // Sky dome layers. Absent keys simply leave defaults, so interior levels
    // that declare no sky end up with no valid layers.
    info_.skyDomeMap = p.String("SkyDome.Map");
    for (int i = 0; i < 4; ++i) {
        const std::string base = "SkyDome.Layer" + std::to_string(i + 1) + ".";
        SkyLayer& layer = info_.skyLayers[i];
        layer.mask = p.String(base + "Tex_Mask");
        layer.lightmap = p.String(base + "Tex_LMap");
        SkyTexture* pair[2] = {&layer.tex1, &layer.tex2};
        for (int t = 0; t < 2; ++t) {
            const std::string key = base + "Tex" + std::to_string(t + 1);
            pair[t]->name = p.String(key);
            pair[t]->panU = static_cast<float>(p.Number(key + "PanUSpeed", 0.0));
            pair[t]->panV = static_cast<float>(p.Number(key + "PanVSpeed", 0.0));
            pair[t]->rotSpeed = static_cast<float>(p.Number(key + "RotSpeed", 0.0));
            pair[t]->tileU = static_cast<float>(p.Number(key + "TileU", 1.0));
            pair[t]->tileV = static_cast<float>(p.Number(key + "TileV", 1.0));
        }
    }
    return true;
}

void Level::LoadEntities(const std::string& levelDir) {
    // Each subfolder is an entity type and each file in it is one instance.
    std::error_code ec;
    for (const auto& dir : fs::directory_iterator(levelDir, ec)) {
        if (!dir.is_directory()) continue;
        std::string type = dir.path().filename().string();
        if (type == "MapEntities") continue;   // not instance data

        for (const auto& file : fs::directory_iterator(dir.path(), ec)) {
            if (!file.is_regular_file()) continue;
            if (file.path().extension() == ".scc") continue;   // source-control leftovers

            Entity e;
            e.type = type;
            e.name = file.path().stem().string();
            if (!e.props.LoadFromFile(file.path().string())) continue;
            e.baseObj = e.props.String("BaseObj");
            e.props.Vector3("Pos", e.pos);
            entities_.push_back(std::move(e));
        }
    }
}

bool Level::Load(const std::string& levelDir, const std::string& dataRoot) {
    if (!LoadSettings(levelDir)) return false;
    LoadEntities(levelDir);

    if (!info_.mapFile.empty()) {
        fs::path mapPath = fs::path(dataRoot) / "Maps" / info_.mapFile;
        if (fs::exists(mapPath)) {
            mapLoaded_ = MapMesh::Load(mapPath.string(), map_);
            if (!mapLoaded_) LogWarn("map %s failed: %s", info_.mapFile.c_str(), map_.error.c_str());
        } else {
            LogWarn("map not found: %s", mapPath.string().c_str());
        }
    }
    return true;
}

size_t Level::CountOf(const std::string& type) const {
    size_t n = 0;
    for (const Entity& e : entities_) if (e.type == type) ++n;
    return n;
}

} // namespace painful
