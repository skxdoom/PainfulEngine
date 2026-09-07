// ScriptEngine: decals - ENTITY.SpawnDecal and its variants, R3D.KeepDecals,
// and ENTITY.EnableCollisionsToRagdoll, the limb contact that plays the fall
// sound and spawns the blood the decals come from. Docs/Reference/Decals.md.

#include "ScriptEngineInternal.h"
#include "../Core/Vectors.h"
#include "../Core/Matrix.h"
#include <string>

namespace painful {

// The DECAL family. Declared here rather than in ScriptEngine.h so that adding
// a native touches only this file: the header carries one friend line for the
// struct, not a declaration per function.
struct DecalNatives : ScriptNativesBase {
    static int L_ENTITY_SpawnDecal(lua_State* L);
    static int L_ENTITY_SpawnOrientedDecal(lua_State* L);
    static int L_ENTITY_SpawnStaticDecal(lua_State* L);
    static int L_ENTITY_UpdateDecal(lua_State* L);
    static int L_ENTITY_ReloadDecalSystem(lua_State* L);
    static int L_R3D_KeepDecals(lua_State* L);
    static int L_ENTITY_EnableCollisionsToRagdoll(lua_State* L);
};

namespace {

// Raw .mpk vertices to world: the object's own transform, then the level
// o.Scale - the space BuildStaticWorld and the renderer put them in.
Mat4 MapObjectToWorld(const MapObject& o, float scale) {
    Mat4 s;
    s[0] = s[5] = s[10] = scale;
    return Mat4::Mul(o.transform, s);
}

// A world object the entity path draws: re-based to activeOrigin, rotated,
// placed at the entity (EntityRenderer::CreateWorldObject + SetScriptPose).
Mat4 WorldObjectToWorld(const MapObject& o, float scale, const ScriptEngine::Entity& e) {
    Mat4 m = MapObjectToWorld(o, scale);
    Mat4 back;
    for (int c = 0; c < 3; ++c) back[12 + c] = -e.activeOrigin[c];
    float rot9[9];
    EngineQuatToRot9(e.rot, rot9);
    Mat4 r;
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c) r[i * 4 + c] = rot9[i * 3 + c];
    for (int c = 0; c < 3; ++c) r[12 + c] = e.pos[c];
    return Mat4::Mul(Mat4::Mul(m, back), r);
}

void ReadVec(lua_State* L, int first, Vec3& out) {
    for (int c = 0; c < 3; ++c) out[c] = float(luaL_optnumber(L, first + c, 0));
}

}  // namespace

// ENTITY.SpawnDecal(e, name, x,y,z, nx,ny,nz [, scale]) -> decal entity
//   0x10135A00: CreateEntity(Decal, name, "", scale) + Decal::Spawn(pos, n).
// ENTITY.SpawnOrientedDecal(e, name, pos, n, up [, scale at 12])
//   0x10135BE0: the box's own axes, right = n x up.
// ENTITY.SpawnStaticDecal(e, texture, pos, n, up, right)
//   0x1013D420: the "static" definition with a texture set by hand.
// The engine refuses handle 0; here 0 is the world, which is what every
// trace and contact reports for level geometry.
int ScriptEngine::SpawnDecalEntity(lua_State* L, bool oriented, const char* staticTexture) {
    const int target = HandleArg(L, 1);
    Entity* te = target != 0 ? Find(target) : nullptr;
    if (target != 0 && te == nullptr) {
        lua_pushnumber(L, 0);
        return 1;
    }
    const std::string name = staticTexture ? "static" : luaL_optstring(L, 2, "");
    Vec3 pos, n;
    ReadVec(L, 3, pos);
    ReadVec(L, 6, n);
    float scale = 1.f;
    if (staticTexture == nullptr)
        scale = float(luaL_optnumber(L, oriented ? 12 : 9, 1.0));

    Entity e;
    e.type = kDecal;
    e.name = name;
    e.scale = scale;
    e.inWorld = true;
    for (int c = 0; c < 3; ++c) e.pos[c] = pos[c];
    e.decalSlot = decals_.Create(decalLib_.Get(name), scale);
    if (oriented || staticTexture) {
        Vec3 up, right;
        ReadVec(L, 9, up);
        if (staticTexture) {
            ReadVec(L, 12, right);
        } else {
            right[0] = n[1] * up[2] - n[2] * up[1];
            right[1] = n[2] * up[0] - n[0] * up[2];
            right[2] = n[0] * up[1] - n[1] * up[0];
            const float len = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
            if (len > 1e-12f) for (int c = 0; c < 3; ++c) right[c] /= len;
        }
        decals_.SetBasisOriented(e.decalSlot, pos, n, up, right);
    } else {
        decals_.SetBasis(e.decalSlot, pos, n);
    }
    if (staticTexture) decals_.SetTextureOverride(e.decalSlot, staticTexture);

    const int handle = nextHandle_++;
    Entity& stored = entities_.emplace(handle, e).first->second;
    ++created_;
    // Entity::RegisterChild(target, decal, true, -1): the decal goes when the
    // thing it is on goes - a destructible's twin takes its holes with it.
    if (te != nullptr) {
        te->children.push_back(handle);
        stored.parent = target;
        stored.dieWithParent = true;
    }
    BuildDecalGeometry(stored, target, pos, n);
    // PAINFUL_DECAL_TRACE: what each spawn cut, for the headless probe.
    static const bool kTrace = DebugFlag("PAINFUL_DECAL_TRACE");
    if (kTrace) {
        const DecalInstance& d = decals_.decals()[size_t(stored.decalSlot)];
        LogInfo("decal %s at (%.2f %.2f %.2f) n=(%.2f %.2f %.2f) target=%d scale=%.2f "
                "objects=%d verts=%zu", name.c_str(), pos[0], pos[1], pos[2], n[0], n[1], n[2],
                target, scale, d.objects, d.verts.size());
    }
    lua_pushnumber(L, handle);
    return 1;
}

void ScriptEngine::BuildDecalGeometry(Entity& decal, int target, const Vec3& pos,
                                      const Vec3& normal) {
    const int slot = decal.decalSlot;
    if (slot < 0 || !mapLoaded_) return;
    auto worldObject = [&](const Entity* we) {
        if (we == nullptr || !we->worldObject) return false;
        if (we->activeMesh >= 0 && size_t(we->activeMesh) < map_.objects.size()) {
            const MapObject& o = map_.objects[size_t(we->activeMesh)];
            decals_.Append(slot, o, WorldObjectToWorld(o, world_.scale, *we));
            return true;
        }
        // A water surface: a world-object entity that names its object and
        // was never re-based - the splash lands on the mesh as authored.
        for (const MapObject& o : map_.objects) {
            if (o.name != we->name) continue;
            decals_.Append(slot, o, MapObjectToWorld(o, world_.scale));
            return true;
        }
        return false;
    };

    // Decal::Spawn clips to the mesh of the entity it was given - a Mesh
    // entity only. A model or a pack mesh gets nothing, as in the original.
    if (target != 0) {
        worldObject(Find(target));
        return;
    }

    // The world: the object under the spawn point, found by a short trace
    // along the normal - the contact and the line trace both leave the point
    // on that surface.
    if (physics_) {
        Vec3 from, to;
        for (int c = 0; c < 3; ++c) {
            from[c] = pos[c] + normal[c] * 0.25f;
            to[c] = pos[c] - normal[c] * 0.25f;
        }
        PhysicsWorld::RayHit hit;
        if (physics_->RayCast(from, to, hit, true)) {
            if (hit.bodySlot >= 0) {
                if (worldObject(Find(EntityForBody(hit.bodySlot)))) return;
            } else if (hit.worldObject >= 0 && size_t(hit.worldObject) < map_.objects.size()) {
                const MapObject& o = map_.objects[size_t(hit.worldObject)];
                decals_.Append(slot, o, MapObjectToWorld(o, world_.scale));
                return;
            }
        }
    }
    // Nothing under the point (water, a noclip surface, no physics): every
    // collidable object the box overlaps. Append rejects the rest on bounds.
    for (const MapObject& o : map_.objects) {
        if (!o.isCollidable() || o.isActiveMesh()) continue;
        decals_.Append(slot, o, MapObjectToWorld(o, world_.scale));
    }
}

int DecalNatives::L_ENTITY_SpawnDecal(lua_State* L) {
    return From(L)->SpawnDecalEntity(L, false, nullptr);
}

int DecalNatives::L_ENTITY_SpawnOrientedDecal(lua_State* L) {
    return From(L)->SpawnDecalEntity(L, true, nullptr);
}

int DecalNatives::L_ENTITY_SpawnStaticDecal(lua_State* L) {
    return From(L)->SpawnDecalEntity(L, false, luaL_optstring(L, 2, ""));
}

// ENTITY.UpdateDecal(e, decal, x,y,z, nx,ny,nz) - Decal::Spawn again on an
// existing decal (0x10135E60): the same box, re-cut where it now is.
int DecalNatives::L_ENTITY_UpdateDecal(lua_State* L) {
    ScriptEngine* self = From(L);
    const int target = HandleArg(L, 1);
    Entity* d = self->Find(HandleArg(L, 2));
    if (d == nullptr || d->decalSlot < 0) return 0;
    if (target != 0 && self->Find(target) == nullptr) return 0;
    Vec3 pos, n;
    ReadVec(L, 3, pos);
    ReadVec(L, 6, n);
    for (int c = 0; c < 3; ++c) d->pos[c] = pos[c];
    self->decals_.ClearGeometry(d->decalSlot);
    self->decals_.SetBasis(d->decalSlot, pos, n);
    self->BuildDecalGeometry(*d, target, pos, n);
    return 0;
}

// ENTITY.ReloadDecalSystem (0x1011DFE0): every definition re-read from its
// .ini on next use. Game:Init calls it once at boot.
int DecalNatives::L_ENTITY_ReloadDecalSystem(lua_State* L) {
    From(L)->decalLib_.Reload();
    return 0;
}

// R3D.KeepDecals(on) (0x10123B20): the pkkeepdecals cheat - mortal decals
// stop ageing while it is set.
int DecalNatives::L_R3D_KeepDecals(lua_State* L) {
    From(L)->decals_.SetKeep(lua_toboolean(L, 1) != 0);
    return 0;
}

// ENTITY.EnableCollisionsToRagdoll(e, joint, minTime = 0.4, minStren = 1.0)
// (0x10130500): Ragdoll::Joint_SetCollisionCallbacks on one joint, and
// nothing without a ragdoll. CActor lists the joints per monster in
// RagdollCollisions.Bones; TickCollisions reports them.
int DecalNatives::L_ENTITY_EnableCollisionsToRagdoll(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    static const bool kTrace = DebugFlag("PAINFUL_CONTACT_TRACE");
    if (kTrace)
        LogInfo("EnableCollisionsToRagdoll(%d joint %d): %s", HandleArg(L, 1),
                int(luaL_optnumber(L, 2, -1)),
                e == nullptr ? "no entity" : (e->ragdollSlot < 0 ? "NO RAGDOLL" : "armed"));
    if (e == nullptr || e->ragdollSlot < 0) return 0;
    const int joint = int(luaL_optnumber(L, 2, -1));
    if (joint < 0) return 0;
    Entity::RagdollCallback cb;
    cb.minTime = float(luaL_optnumber(L, 3, 0.4));
    cb.minStrength = float(luaL_optnumber(L, 4, 1.0));
    e->ragdollCallbacks[joint] = cb;
    return 0;
}

void BindDecal(ScriptEngine& engine, LuaHost& host) {
    const ScriptNative natives[] = {
        {"ENTITY", "SpawnDecal", DecalNatives::L_ENTITY_SpawnDecal},
        {"ENTITY", "SpawnOrientedDecal", DecalNatives::L_ENTITY_SpawnOrientedDecal},
        {"ENTITY", "SpawnStaticDecal", DecalNatives::L_ENTITY_SpawnStaticDecal},
        {"ENTITY", "UpdateDecal", DecalNatives::L_ENTITY_UpdateDecal},
        {"ENTITY", "ReloadDecalSystem", DecalNatives::L_ENTITY_ReloadDecalSystem},
        {"R3D", "KeepDecals", DecalNatives::L_R3D_KeepDecals},
        {"ENTITY", "EnableCollisionsToRagdoll", DecalNatives::L_ENTITY_EnableCollisionsToRagdoll},
    };
    RegisterFamily(engine, host, natives);
}

}  // namespace painful
