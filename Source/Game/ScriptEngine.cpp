#include "ScriptEngine.h"

#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include "../Render/EntityRenderer.h"
#include "../Render/TextureCache.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace painful {

namespace {

bool StartsWithCI(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char a = s[i], b = prefix[i];
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (std::tolower(static_cast<unsigned char>(a)) !=
            std::tolower(static_cast<unsigned char>(b)))
            return false;
    }
    return true;
}

// Entity handles are plain integers pushed as Lua numbers - scripts store
// them (self._Entity), pass them back as the first native argument, and use
// them as EntityToObject keys, all of which numbers satisfy. Zero is never
// handed out, so nil/absent arguments read as "no entity".
int HandleArg(lua_State* L, int idx) {
    return lua_isnumber(L, idx) ? int(lua_tonumber(L, idx)) : 0;
}

} // namespace

ScriptEngine* ScriptEngine::From(lua_State* L) {
    return static_cast<ScriptEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
}

ScriptEngine::Entity* ScriptEngine::Find(int handle) {
    auto it = entities_.find(handle);
    return it == entities_.end() ? nullptr : &it->second;
}

void ScriptEngine::AttachRenderer(EntityRenderer* entities, TextureCache* textures,
                                  const std::string& dataRoot) {
    renderer_ = entities;
    textures_ = textures;
    dataRoot_ = dataRoot;
}

void ScriptEngine::AttachPhysics(PhysicsWorld* physics, const std::string& dataRoot) {
    physics_ = physics;
    dataRoot_ = dataRoot;
}

void ScriptEngine::SyncFromPhysics(bool activeOnly) {
    if (!physics_) return;
    physics_->CollectScriptPoses(poseScratch_, activeOnly);
    for (const ScriptBodyPose& pose : poseScratch_) {
        auto it = bodyToEntity_.find(pose.slot);
        if (it == bodyToEntity_.end()) continue;
        Entity* e = Find(it->second);
        if (!e) continue;
        for (int c = 0; c < 3; ++c) e->pos[c] = pose.pos[c];
        for (int c = 0; c < 4; ++c) e->rotWXYZ[c] = pose.quatWXYZ[c];
        SyncPose(*e);
    }
}

// Splits an engine-style "../Data/Items/<pack>" back into the pack name the
// physics loader joins with its items root.
bool ScriptEngine::SplitPackSource(const std::string& source, std::string& packName) const {
    const std::string resolved = host_->ResolvePath(source);
    const std::string prefix = dataRoot_ + "/Items/";
    if (!StartsWithCI(resolved, prefix)) return false;
    packName = resolved.substr(prefix.size());
    return true;
}

void ScriptEngine::CreateRendererInstance(Entity& e) {
    if (!renderer_ || !textures_ || e.rendererInstance >= 0) return;
    if (e.type == kModel) {
        e.rendererInstance = renderer_->CreateScriptModel(
            e.source, e.scale, *textures_, dataRoot_ + "/Models");
    } else if (e.type == kMesh && !e.worldObject) {
        // The pack path arrives engine-style: "../Data/Items/<pack>".
        // GetPack joins itemsRoot + "/" + pack, so split the resolved path
        // back apart (which also keeps its cache keyed consistently).
        const std::string resolved = host_->ResolvePath(e.source);
        const std::string itemsRoot = dataRoot_ + "/Items";
        std::string pack = resolved;
        std::string root = ".";
        if (StartsWithCI(resolved, itemsRoot + "/")) {
            pack = resolved.substr(itemsRoot.size() + 1);
            root = itemsRoot;
        }
        e.rendererInstance =
            renderer_->CreateScriptPack(pack, e.mesh, e.scale, *textures_, root);
    }
    if (e.rendererInstance >= 0) SyncPose(e);
}

void ScriptEngine::SyncPose(Entity& e) {
    if (!renderer_ || e.rendererInstance < 0) return;
    renderer_->SetScriptPose(e.rendererInstance, e.pos, e.rotWXYZ);
    renderer_->SetScriptVisible(e.rendererInstance, e.visible && e.inWorld);
}

void ScriptEngine::FlushToRenderer() {
    for (auto& kv : entities_) CreateRendererInstance(kv.second);
}

// ---------------------------------------------------------------- ENTITY

// ENTITY.Create(etype, source, nameTag, scale [, translateToZero]) -> handle
// The Model path passes a .pkmdl name with the scripts' own *0.1 already
// applied; the Mesh path passes "../Data/Items/<pack>" plus the object name.
int ScriptEngine::L_Create(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity e;
    e.type = int(luaL_checknumber(L, 1));
    e.source = luaL_optstring(L, 2, "");
    e.mesh = luaL_optstring(L, 3, "");
    e.scale = float(luaL_optnumber(L, 4, 1.0));
    if (e.type == kModel) {
        // Argument 3 is the "Name:Tag" identity for models, not a mesh name.
        e.name = e.mesh;
        e.mesh.clear();
    }
    const int handle = self->nextHandle_++;
    auto it = self->entities_.emplace(handle, e).first;
    ++self->created_;
    self->CreateRendererInstance(it->second);
    lua_pushnumber(L, handle);
    return 1;
}

int ScriptEngine::L_Release(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    auto it = self->entities_.find(handle);
    if (it == self->entities_.end()) return 0;   // nil and doubles are routine
    if (self->renderer_ && it->second.rendererInstance >= 0)
        self->renderer_->ReleaseScript(it->second.rendererInstance);
    if (self->physics_ && it->second.physicsBody >= 0) {
        self->physics_->RemoveScriptBody(it->second.physicsBody);
        self->bodyToEntity_.erase(it->second.physicsBody);
    }
    self->entities_.erase(it);
    ++self->released_;
    return 0;
}

int ScriptEngine::L_SetPosition(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        e->pos[0] = float(luaL_optnumber(L, 2, 0));
        e->pos[1] = float(luaL_optnumber(L, 3, 0));
        e->pos[2] = float(luaL_optnumber(L, 4, 0));
        self->SyncPose(*e);
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyPose(e->physicsBody, e->pos, e->rotWXYZ);
    }
    return 0;
}

int ScriptEngine::L_GetPosition(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushnumber(L, e ? e->pos[0] : 0);
    lua_pushnumber(L, e ? e->pos[1] : 0);
    lua_pushnumber(L, e ? e->pos[2] : 0);
    return 3;
}

int ScriptEngine::L_SetRotationQ(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        for (int i = 0; i < 4; ++i)
            e->rotWXYZ[i] = float(luaL_optnumber(L, 2 + i, i == 0 ? 1 : 0));
        self->SyncPose(*e);
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyPose(e->physicsBody, e->pos, e->rotWXYZ);
    }
    return 0;
}

int ScriptEngine::L_GetRotationQ(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushnumber(L, e ? e->rotWXYZ[0] : 1);
    lua_pushnumber(L, e ? e->rotWXYZ[1] : 0);
    lua_pushnumber(L, e ? e->rotWXYZ[2] : 0);
    lua_pushnumber(L, e ? e->rotWXYZ[3] : 0);
    return 4;
}

// SetOrientation/GetOrientation: yaw about Y, radians - BindPoint in
// Utils.lua rotates offsets with cos/sin of the negated value.
int ScriptEngine::L_SetOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        const float a = float(luaL_optnumber(L, 2, 0)) * 0.5f;
        e->rotWXYZ[0] = std::cos(a);
        e->rotWXYZ[1] = 0;
        e->rotWXYZ[2] = std::sin(a);
        e->rotWXYZ[3] = 0;
        self->SyncPose(*e);
    }
    return 0;
}

int ScriptEngine::L_GetOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    // Extract the yaw component of the stored quaternion.
    const float yaw =
        e ? 2.f * std::atan2(e->rotWXYZ[2], e->rotWXYZ[0]) : 0.f;
    lua_pushnumber(L, yaw);
    return 1;
}

int ScriptEngine::L_EnableDraw(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        e->visible = lua_toboolean(L, 2) != 0;
        self->SyncPose(*e);
    }
    return 0;
}

// No physics yet, so entities are at rest.
int ScriptEngine::L_GetVelocity(lua_State* L) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);   // scripts read a fourth value: the speed
    return 4;
}

// ENTITY.PO_Create(e, bodytype, scale, collisionGroup): CObject:PO_Create
// calls this right after SetPosition/SetRotationQ, then sets mass, friction
// and the rest through the PO_Set* family - so the body starts bare and the
// scripts dress it, exactly as the original divides the work. A scale of -1
// (the scripts' "not given") means the entity's own scale, which already
// carries the model *0.1 rule.
int ScriptEngine::L_PO_Create(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !self->physics_ || e->physicsBody >= 0) return 0;

    const int bodyType = int(luaL_optnumber(L, 2, 0));
    float scale = float(luaL_optnumber(L, 3, -1.0));
    if (scale <= 0.f) scale = e->scale;

    std::string model, pack;
    if (e->type == kModel) {
        model = e->source;
    } else if (e->type == kMesh && !e->worldObject) {
        if (!self->SplitPackSource(e->source, pack)) return 0;
    } else {
        return 0;
    }

    const int slot = self->physics_->CreateScriptBody(
        bodyType, model, pack, e->mesh, scale, e->pos, e->rotWXYZ, self->dataRoot_);
    if (slot >= 0) {
        e->physicsBody = slot;
        self->bodyToEntity_[slot] = HandleArg(L, 1);
    }
    return 0;
}

int ScriptEngine::L_PO_Exist(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && self->physics_ && e->physicsBody >= 0 &&
                           self->physics_->ScriptBodyExists(e->physicsBody));
    return 1;
}

// CActor divides and multiplies by this; 0.8 is the scripts' own fallback
// for actors without a physics body.
int ScriptEngine::L_PO_GetMaxSphereRay(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    const float r = (e && self->physics_ && e->physicsBody >= 0)
                        ? self->physics_->ScriptBodyRadius(e->physicsBody)
                        : 0.f;
    lua_pushnumber(L, r > 0.f ? r : 0.8);
    return 1;
}

int ScriptEngine::L_PO_SetMass(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyMass(e->physicsBody,
                                              float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PO_SetFriction(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyFriction(e->physicsBody,
                                                  float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PO_SetRestitution(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyRestitution(e->physicsBody,
                                                     float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PO_SetLinearDamping(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyLinearDamping(e->physicsBody,
                                                       float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PO_SetAngularDamping(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyAngularDamping(e->physicsBody,
                                                        float(luaL_optnumber(L, 2, 0)));
    return 0;
}

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

int ScriptEngine::L_MESH_SetDefaultDetailMaps(lua_State* L) {
    ScriptEngine* self = From(L);
    self->world_.detailTex = luaL_optstring(L, 1, "");
    self->world_.detailTileU = float(luaL_optnumber(L, 2, 8.2));
    self->world_.detailTileV = float(luaL_optnumber(L, 3, 7.1));
    return 0;
}

// ---------------------------------------------------------------- binding

void ScriptEngine::Bind(LuaHost& host) {
    host_ = &host;
    const struct {
        const char* module;
        const char* name;
        int (*fn)(lua_State*);
    } natives[] = {
        {"ENTITY", "Create", L_Create},
        {"ENTITY", "Release", L_Release},
        {"ENTITY", "SetPosition", L_SetPosition},
        {"ENTITY", "GetPosition", L_GetPosition},
        {"ENTITY", "SetRotationQ", L_SetRotationQ},
        {"ENTITY", "GetRotationQ", L_GetRotationQ},
        {"ENTITY", "SetOrientation", L_SetOrientation},
        {"ENTITY", "GetOrientation", L_GetOrientation},
        {"ENTITY", "EnableDraw", L_EnableDraw},
        {"ENTITY", "GetVelocity", L_GetVelocity},
        {"ENTITY", "PO_Create", L_PO_Create},
        {"ENTITY", "PO_Exist", L_PO_Exist},
        {"ENTITY", "PO_GetMaxSphereRay", L_PO_GetMaxSphereRay},
        {"ENTITY", "PO_SetMass", L_PO_SetMass},
        {"ENTITY", "PO_SetFriction", L_PO_SetFriction},
        {"ENTITY", "PO_SetRestitution", L_PO_SetRestitution},
        {"ENTITY", "PO_SetLinearDamping", L_PO_SetLinearDamping},
        {"ENTITY", "PO_SetAngularDamping", L_PO_SetAngularDamping},
        {"WORLD", "Init", L_WORLD_Init},
        {"WORLD", "AddEntity", L_WORLD_AddEntity},
        {"WORLD", "FindEntityByName", L_WORLD_FindEntityByName},
        {"WORLD", "LoadMap", L_WORLD_LoadMap},
        {"WORLD", "SetupFog", L_WORLD_SetupFog},
        {"WORLD", "SetFarClipDist", L_WORLD_SetFarClipDist},
        {"WORLD", "AmbientColor", L_WORLD_AmbientColor},
        {"MESH", "SetDefaultDetailMaps", L_MESH_SetDefaultDetailMaps},
    };
    for (const auto& n : natives) host.RegisterNative(n.module, n.name, n.fn, this);
}

} // namespace painful
