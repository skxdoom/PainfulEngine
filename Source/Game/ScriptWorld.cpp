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
// Every "phys" object of the map becomes a rigid body and an entity, as
// World::LoadMeshPakFile + PhysicsWorld::AddMesh make them. The level's mass
// factor comes from the Lua global the ENGINE calls, Level_GetActiveMeshesData
// (CLevel.lua:970), substring-matched on the lowercased name; 1 means "use
// ActiveMeshesMassScale", which WORLD.Init brings a moment later.
float ScriptEngine::ActiveMeshMassScale(const std::string& objectName) {
    lua_State* L = host_ ? host_->state() : nullptr;
    float massScale = 1.f;
    if (L) {
        const int top = lua_gettop(L);
        lua_pushstring(L, "Level_GetActiveMeshesData");
        lua_gettable(L, LUA_GLOBALSINDEX);
        if (lua_isfunction(L, -1)) {
            lua_pushstring(L, objectName.c_str());
            if (lua_pcall(L, 1, 1, 0) == 0 && lua_isnumber(L, -1))
                massScale = float(lua_tonumber(L, -1));
        }
        lua_settop(L, top);
    }
    return massScale;
}

void ScriptEngine::CreateActiveMeshes() {
    if (!physics_ || !mapLoaded_) return;
    destructibles_.clear();
    size_t made = 0, pinned = 0, held = 0;
    std::vector<std::pair<size_t, int>> pieces;   // (object, handle) of every "physdest"
    for (size_t i = 0; i < map_.objects.size(); ++i) {
        const MapObject& o = map_.objects[i];
        if (!o.isActiveMesh() || o.vertexCount() == 0) continue;
        const float massScale = ActiveMeshMassScale(o.name);
        float origin[3];
        const int slot = physics_->CreateActiveMeshBody(
            o, world_.scale, massScale, o.isPinned(), o.nameHas("concave"),
            o.activeGroup(), origin);
        if (slot < 0) continue;
        Entity e;
        e.type = kMesh;
        e.name = o.name;
        e.worldObject = true;
        e.inWorld = true;
        e.activeMesh = int(i);
        e.physicsBody = slot;
        e.collisionGroup = 3;   // AddMesh creates every one in group 3
        for (int c = 0; c < 3; ++c) e.pos[c] = e.activeOrigin[c] = origin[c];
        // A piece waits for its twin's release: out of the simulation and
        // unseen (AddMesh's physdest branch ends in World::RemoveEntity).
        const bool piece = o.isDestructiblePiece();
        if (piece) e.visible = false;
        const int handle = nextHandle_++;
        entities_.emplace(handle, e);
        bodyToEntity_[slot] = handle;
        ++created_;
        ++made;
        if (o.isPinned()) ++pinned;
        CreateRendererInstance(entities_[handle]);
        if (piece) {
            physics_->SetScriptBodyEnabled(slot, false);
            if (renderer_ && entities_[handle].rendererInstance >= 0)
                renderer_->SetScriptVisible(entities_[handle].rendererInstance, false);
            pieces.emplace_back(i, handle);
            ++held;
        }
    }
    // The intact twins, paired with their pieces by name (FUN_101BA530's
    // prefix). Each piece goes to the LONGEST matching prefix: Enclave's
    // grob2 would otherwise take grob22's pieces. ASSUMED - the original's
    // matcher is not located yet. Docs/Reference/Physics.md, "Destructibles".
    std::vector<std::string> prefixes;
    for (size_t i = 0; i < map_.objects.size(); ++i) {
        const MapObject& o = map_.objects[i];
        if (!o.isStaticTwin() || o.vertexCount() == 0) continue;
        Destructible d;
        d.object = i;
        d.group = o.activeGroup();
        float origin[3];
        d.twinBody = physics_->CreateStaticTwinBody(o, world_.scale, d.group, origin);
        if (d.twinBody < 0) continue;
        destructibles_.push_back(std::move(d));
        prefixes.push_back(o.piecePrefix());
    }
    size_t orphans = 0;
    for (const auto& p : pieces) {
        const std::string& name = map_.objects[p.first].name;
        size_t best = SIZE_MAX, bestLen = 0;
        for (size_t k = 0; k < prefixes.size(); ++k) {
            const std::string& pre = prefixes[k];
            if (pre.empty() || pre.size() <= bestLen) continue;
            if (name.compare(0, pre.size(), pre) == 0) { best = k; bestLen = pre.size(); }
        }
        if (best == SIZE_MAX) ++orphans;
        else destructibles_[best].pieces.push_back(p.second);
    }
    if (made)
        LogInfo("active meshes: %zu bodies, %zu pinned, %zu pieces held for %zu destructibles"
                " (%zu unpaired)",
                made, pinned, held, destructibles_.size(), orphans);
}

void ScriptEngine::ReleaseDestructible(size_t index, const float* blast) {
    if (index >= destructibles_.size()) return;
    Destructible& d = destructibles_[index];
    if (d.released) return;
    d.released = true;
    LogInfo("destructible: %s -> %zu pieces%s", map_.objects[d.object].name.c_str(),
            d.pieces.size(), blast ? " (blast)" : "");
    float at[3] = {0, 0, 0};
    if (physics_ && d.twinBody >= 0) {
        physics_->GetScriptBodyPosition(d.twinBody, at);
        physics_->RemoveScriptBody(d.twinBody);
    }
    if (worldObjectVisible_) worldObjectVisible_(d.object, false);
    for (int handle : d.pieces) {
        auto it = entities_.find(handle);
        if (it == entities_.end()) continue;
        Entity& e = it->second;
        e.visible = true;
        if (renderer_ && e.rendererInstance >= 0)
            renderer_->SetScriptVisible(e.rendererInstance, true);
        if (physics_ && e.physicsBody >= 0) {
            physics_->SetScriptBodyEnabled(e.physicsBody, true);
            const float still[3] = {0, 0, 0};
            physics_->SetScriptBodyVelocity(e.physicsBody, still);
        }
    }
    // Lev:OnExplodeMesh(actgrp, x, y, z) - Cemetery plays the collapse and
    // shakes the camera off it.
    const double args[4] = {double(d.group), blast ? blast[0] : at[0],
                            blast ? blast[1] : at[1], blast ? blast[2] : at[2]};
    host_->PostMsg("EXPLODEMESH", args, 4);
}

void ScriptEngine::ReleaseTwins(const std::vector<int>& twinSlots, const float* blast) {
    for (int slot : twinSlots)
        for (size_t i = 0; i < destructibles_.size(); ++i)
            if (destructibles_[i].twinBody == slot && !destructibles_[i].released)
                ReleaseDestructible(i, blast);
}

int ScriptEngine::L_PHYSICS_ActiveMeshGroupActivate(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->physics_) return 0;
    std::vector<int> twins;
    self->physics_->ActivateActiveMeshGroup(int(luaL_optnumber(L, 1, -1)), twins);
    self->ReleaseTwins(twins, nullptr);
    return 0;
}

int ScriptEngine::L_PHYSICS_ActiveMeshGroupEnable(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->physics_)
        self->physics_->EnableActiveMeshGroup(int(luaL_optnumber(L, 1, -1)),
                                              lua_toboolean(L, 2) != 0);
    return 0;
}

// The static twins of a group, on or off (FUN_101B25D0): the intact
// "statdest" bodies leave or rejoin the simulation. Drawing is the scripts'
// own WORLD.EnableDrawMeshGroup call beside it, so only the body moves here.
int ScriptEngine::L_PHYSICS_ActiveMeshGroupStaticMeshEnable(lua_State* L) {
    ScriptEngine* self = From(L);
    const int group = int(luaL_optnumber(L, 1, -1));
    const bool on = lua_toboolean(L, 2) != 0;
    if (!self->physics_) return 0;
    for (const Destructible& d : self->destructibles_)
        if (d.group == group && !d.released && d.twinBody >= 0)
            self->physics_->SetScriptBodyEnabled(d.twinBody, on);
    return 0;
}

// Collision reporting and time-to-live per group (FUN_101B9E60). The
// collision callbacks arrive through ENTITY.EnableCollisionsToAll instead;
// the autodelete timers are not ported.
int ScriptEngine::L_PHYSICS_ActiveMeshGroupSetActivationParams(lua_State*) { return 0; }

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
    // A map was already up: this is a level switch, and the scripts have just
    // released their own entities in Game:Clear. Drop what is ours.
    if (self->mapLoaded_) self->ResetLevelState();
    self->mapLoaded_ = false;
    // A level is going up (or the empty one): the app rebuilds its renderer
    // on the next TakeLevelChange, and a LoadWorld after this marks itself.
    ++self->levelChangeSerial_;
    self->loadedFromSave_ = false;
    if (self->physics_ && self->world_.loadRequested) {
        const std::string path = self->host_->ResolvePath(self->world_.mapPath);
        // Load reports success as "no error recorded", so the reused mesh
        // must start clean or a previous failure poisons this one.
        self->map_ = MapMesh();
        if (MapMesh::Load(path, self->map_)) {
            self->mapLoaded_ = true;
            self->physics_->LoadWorldMesh(self->map_, self->world_.scale,
                                          self->dataRoot_);
            self->CreateActiveMeshes();
            // Water is not in that mesh - every shipped water object is also
            // named `noclip` - so it is registered separately here.
            self->BuildWaterSurfaces();
        } else {
            LogWarn("WORLD.LoadMap: %s failed: %s", path.c_str(),
                    self->map_.error.c_str());
        }
    }
    return 0;
}

void ScriptEngine::ResetLevelState() {
    // The active meshes are entities the ENGINE made from the map's `phys`
    // objects (CreateActiveMeshes); GObjects:Clear never sees them.
    std::vector<int> engineOwned;
    for (const auto& kv : entities_)
        if (kv.second.worldObject) engineOwned.push_back(kv.first);
    for (int handle : engineOwned) ReleaseEntity(handle);
    water_.clear();
    lastExploded_.clear();
    contactVelocity_.clear();
    excludedSlots_.clear();
    if (playerHandle_ && Find(playerHandle_) == nullptr) playerHandle_ = 0;
    LogInfo("level switch: %zu engine entities dropped, %zu script entities still live",
            engineOwned.size(), entities_.size());
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

// WORLD.BloomFXParams(threshold, multiplier, overlayColor, dimScale) - the
// CLevel.BloomFX block (World+0x6cc..0x6d8). DimScale is what the sprite
// packers (FUN_101e4080, Billboard::Draw) multiply RGB by when bloom is on.
int ScriptEngine::L_WORLD_BloomFXParams(lua_State* L) {
    WorldState& w = From(L)->world_;
    w.bloomThreshold = float(luaL_optnumber(L, 1, 0.25));
    w.bloomMultiplier = float(luaL_optnumber(L, 2, 1.0));
    w.bloomOverlay = uint32_t(int64_t(luaL_optnumber(L, 3, 0x808080)));
    w.bloomDimScale = float(luaL_optnumber(L, 4, 0.8));
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
