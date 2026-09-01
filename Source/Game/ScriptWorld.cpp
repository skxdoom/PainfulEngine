// ScriptEngine: the WORLD natives - map loading, fog, ambient and the sky.

#include "ScriptEngineInternal.h"

namespace painful {

// ---------------------------------------------------------------- WORLD

// WORLD.AddEntity(handle, hidden) - enters the entity into the drawn world;
// CActor passes `not self.Visible` as the second argument.
int ScriptEngine::L_WORLD_AddEntity(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        e->inWorld = true;
        if (lua_isboolean(L, 2)) e->visible = !lua_toboolean(L, 2);
        self->SyncPose(*e);
    }
    return 0;
}

// WORLD.FindEntityByName(name) - resolves a world-mesh object (MapEntities
// bind EMesh scripts to named .mpk objects). Handed out as a pseudo-entity;
// the mesh-level natives that act on it are still stubs.
int ScriptEngine::L_WORLD_FindEntityByName(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, "");
    for (const auto& kv : self->entities_) {
        if (kv.second.worldObject && kv.second.name == name) {
            lua_pushnumber(L, kv.first);
            return 1;
        }
    }
    Entity e;
    e.type = kMesh;
    e.name = name;
    e.worldObject = true;
    e.inWorld = true;
    const int handle = self->nextHandle_++;
    self->entities_.emplace(handle, e);
    lua_pushnumber(L, handle);
    return 1;
}

// WORLD.LoadMap(mapPath, levelName, scale, overbright, rtCubeMap,
// shadowMapSize, shadowMapCount) - recorded; the game loop owns the actual
// renderer upload.
int ScriptEngine::L_WORLD_LoadMap(lua_State* L) {
    ScriptEngine* self = From(L);
    self->world_.mapPath = luaL_optstring(L, 1, "");
    self->world_.levelName = luaL_optstring(L, 2, "");
    self->world_.scale = float(luaL_optnumber(L, 3, 1.0));
    self->world_.overbright = lua_toboolean(L, 4) != 0;
    // The empty "NoName" level passes "../Data/Maps/" with no file - a level
    // without a world, not an error.
    self->world_.loadRequested =
        !self->world_.mapPath.empty() && self->world_.mapPath.back() != '/';

    // With physics attached the static world is built HERE, synchronously:
    // the entity bodies follow through PO_Create later in this same level
    // load, and they need something to rest on.
    self->mapLoaded_ = false;
    if (self->physics_ && self->world_.loadRequested) {
        const std::string path = self->host_->ResolvePath(self->world_.mapPath);
        // Load reports success as "no error recorded", so the reused mesh
        // must start clean or a previous failure poisons this one.
        self->map_ = MapMesh();
        if (MapMesh::Load(path, self->map_)) {
            self->mapLoaded_ = true;
            self->physics_->LoadWorldMesh(self->map_, self->world_.scale,
                                          self->dataRoot_);
        } else {
            LogWarn("WORLD.LoadMap: %s failed: %s", path.c_str(),
                    self->map_.error.c_str());
        }
    }
    return 0;
}

// WORLD.Init(activeMeshesMassScale, defaultMeshFriction,
// defaultMeshRestitution, deactivatorDelay, deactivatorMaxPosDiff) - CLevel
// calls it right after LoadMap. The deactivator pair maps onto Jolt's own
// sleep thresholds, which are close enough to leave alone for now.
int ScriptEngine::L_WORLD_Init(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->physics_)
        self->physics_->SetWorldSurface(float(luaL_optnumber(L, 1, 1.0)),
                                        float(luaL_optnumber(L, 2, 0.5)),
                                        float(luaL_optnumber(L, 3, 0.5)));
    return 0;
}

// WORLD.SetupFog(mode [, start, end, density, packedColor]). CLevel scales
// start/end by the user's clip-plane setting before the call, so the values
// arrive ready to use.
int ScriptEngine::L_WORLD_SetupFog(lua_State* L) {
    ScriptEngine* self = From(L);
    WorldState& w = self->world_;
    w.fogMode = int(luaL_optnumber(L, 1, 0));
    w.fogStart = float(luaL_optnumber(L, 2, 0));
    w.fogEnd = float(luaL_optnumber(L, 3, 90));
    w.fogDensity = float(luaL_optnumber(L, 4, 0));
    const uint32_t c = uint32_t(int64_t(luaL_optnumber(L, 5, 0)));
    w.fogColor[0] = float((c >> 16) & 0xFF);
    w.fogColor[1] = float((c >> 8) & 0xFF);
    w.fogColor[2] = float(c & 0xFF);
    return 0;
}

int ScriptEngine::L_WORLD_SetFarClipDist(lua_State* L) {
    From(L)->world_.farClip = float(luaL_optnumber(L, 1, 1024));
    return 0;
}

// WORLD.AmbientColor(r, g, b, gunAmbientMultiplier), components 0-255.
int ScriptEngine::L_WORLD_AmbientColor(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int i = 0; i < 3; ++i)
        self->world_.ambient[i] = float(luaL_optnumber(L, 1 + i, 128));
    return 0;
}

// WORLD.LoadSky("../Data/Maps/<dome>") -> layer count, read out of the dome
// mesh itself: objects name their layer ("layer01shape",
// "_trans_layer03shape"), and the count is how many carry one. An empty path
// (Cfg.RenderSky < 2) or an unreadable mesh returns 0, which sends
// CLevel:ReloadSky down the low-quality path - the same fallback the
// original uses for DX7-class hardware.
int ScriptEngine::L_WORLD_LoadSky(lua_State* L) {
    ScriptEngine* self = From(L);
    const std::string mapPath = luaL_optstring(L, 1, "");
    self->world_.skyDomeMap.clear();
    self->world_.skyLayerCount = 0;

    if (!mapPath.empty() && mapPath.back() != '/') {
        MapMesh dome;
        if (MapMesh::Load(self->host_->ResolvePath(mapPath), dome)) {
            int count = 0;
            for (const MapObject& o : dome.objects) {
                std::string low = o.name;
                for (char& c : low)
                    c = char(std::tolower(static_cast<unsigned char>(c)));
                if (low.find("layer") != std::string::npos) ++count;
            }
            if (count > 4) count = 4;
            if (count > 0) {
                const size_t slash = mapPath.find_last_of("/\\");
                self->world_.skyDomeMap =
                    slash == std::string::npos ? mapPath : mapPath.substr(slash + 1);
                self->world_.skyLayerCount = count;
            }
        }
    }
    lua_pushnumber(L, self->world_.skyLayerCount);
    return 1;
}

// WORLD.LoadLowQualitySky("../Data/Maps/<dome>", height, angle) -> layer
// count (one: the single-texture dome).
int ScriptEngine::L_WORLD_LoadLowQualitySky(lua_State* L) {
    ScriptEngine* self = From(L);
    const std::string mapPath = luaL_optstring(L, 1, "");
    self->world_.skyMap.clear();
    if (mapPath.empty() || mapPath.back() == '/' ||
        !FileSystem::Get().Exists(self->host_->ResolvePath(mapPath))) {
        lua_pushnumber(L, 0);
        return 1;
    }
    const size_t slash = mapPath.find_last_of("/\\");
    self->world_.skyMap = slash == std::string::npos ? mapPath : mapPath.substr(slash + 1);
    self->world_.skyAngle = float(luaL_optnumber(L, 3, 0));
    lua_pushnumber(L, 1);
    return 1;
}

// WORLD.SetupSkyLayer(i, texMask, texLMap,
//     tex1, rot, panU, panV, tileU, tileV,
//     tex2, rot, panU, panV, tileU, tileV) - argument order straight from
// CLevel:ReloadSky. On the low-quality path (no layered dome) the only
// meaningful argument is tex1: the dome's single texture.
int ScriptEngine::L_WORLD_SetupSkyLayer(lua_State* L) {
    ScriptEngine* self = From(L);
    const int i = int(luaL_optnumber(L, 1, 0));
    if (self->world_.skyLayerCount == 0) {
        self->world_.skyTexture = luaL_optstring(L, 4, "");
        return 0;
    }
    if (i < 0 || i >= 4) return 0;
    SkyLayer& layer = self->world_.skyLayers[i];
    layer.mask = luaL_optstring(L, 2, "");
    layer.lightmap = luaL_optstring(L, 3, "");
    SkyTexture* tex[2] = {&layer.tex1, &layer.tex2};
    for (int t = 0; t < 2; ++t) {
        const int base = 4 + t * 6;
        tex[t]->name = luaL_optstring(L, base, "");
        tex[t]->rotSpeed = float(luaL_optnumber(L, base + 1, 0));
        tex[t]->panU = float(luaL_optnumber(L, base + 2, 0));
        tex[t]->panV = float(luaL_optnumber(L, base + 3, 0));
        tex[t]->tileU = float(luaL_optnumber(L, base + 4, 1));
        tex[t]->tileV = float(luaL_optnumber(L, base + 5, 1));
    }
    return 0;
}

int ScriptEngine::L_MESH_SetDefaultDetailMaps(lua_State* L) {
    ScriptEngine* self = From(L);
    self->world_.detailTex = luaL_optstring(L, 1, "");
    self->world_.detailTileU = float(luaL_optnumber(L, 2, 8.2));
    self->world_.detailTileV = float(luaL_optnumber(L, 3, 7.1));
    return 0;
}



}  // namespace painful
