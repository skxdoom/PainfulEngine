// ScriptEngine: WORLD.SaveGame / WORLD.LoadGame - the engine's half of a save.
//
// The original's PCFSystem::SaveGame (Engine.dll 0x100518a0) writes "C^", a
// version, then glass, audio, physics, pathfinding, entities, portal and zone
// state, and LoadGame (0x10051700) reads them back and runs
// SaveGame:AfterLoadEntities() between the entities and the portals. That
// file is Havok state and cannot be read here; this is our own file in the
// same place, with the same contract: every entity comes back at the handle
// the scripts saved in EntityToObject. Docs/Reference/LuaHost.md, "Saving".

#include "ScriptEngineInternal.h"
#include "../Script/LuaHost.h"
#include "../World/PhysicsWorld.h"

#include <cstring>

namespace painful {

namespace {

constexpr char kMagic[4] = {'P', 'K', 'S', 'V'};
constexpr uint32_t kVersion = 1;

// One class reads and writes, so a field is listed once. `ok` goes false on a
// short read and stays false.
class Archive {
public:
    Archive(std::vector<uint8_t>& buf, bool writing) : buf_(buf), writing_(writing) {}
    bool writing() const { return writing_; }
    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }

    void Raw(void* p, size_t n) {
        if (writing_) {
            const uint8_t* b = static_cast<const uint8_t*>(p);
            buf_.insert(buf_.end(), b, b + n);
        } else if (pos_ + n <= buf_.size()) {
            std::memcpy(p, buf_.data() + pos_, n);
            pos_ += n;
        } else {
            ok_ = false;
            std::memset(p, 0, n);
        }
    }
    void F(int& v) { Raw(&v, 4); }
    void F(uint32_t& v) { Raw(&v, 4); }
    void F(float& v) { Raw(&v, 4); }
    void F(bool& v) { uint8_t b = v ? 1 : 0; Raw(&b, 1); v = b != 0; }
    template <size_t N> void F(float (&v)[N]) { Raw(v, sizeof v); }
    void F(std::string& s) {
        uint32_t n = uint32_t(s.size());
        F(n);
        if (writing_) {
            buf_.insert(buf_.end(), s.begin(), s.end());
        } else if (pos_ + n <= buf_.size()) {
            s.assign(reinterpret_cast<const char*>(buf_.data() + pos_), n);
            pos_ += n;
        } else {
            ok_ = false;
            s.clear();
        }
    }
    void F(std::vector<int>& v) {
        uint32_t n = uint32_t(v.size());
        F(n);
        if (!writing_) v.assign(std::min<size_t>(n, 1u << 20), 0);
        for (int& x : v) F(x);
    }
    void F(std::vector<Mat4>& v) {
        uint32_t n = uint32_t(v.size());
        F(n);
        if (!writing_) v.assign(std::min<size_t>(n, 4096), Mat4());
        for (Mat4& m : v) Raw(m.m, sizeof m.m);
    }
    void F(std::map<std::string, bool>& m) {
        uint32_t n = uint32_t(m.size());
        F(n);
        if (writing_) {
            for (auto& kv : m) {
                std::string k = kv.first;
                bool b = kv.second;
                F(k);
                F(b);
            }
        } else {
            m.clear();
            for (uint32_t i = 0; i < n && ok_; ++i) {
                std::string k;
                bool b = false;
                F(k);
                F(b);
                m[k] = b;
            }
        }
    }
    void F(std::vector<JointOverride>& v) {
        uint32_t n = uint32_t(v.size());
        F(n);
        if (!writing_) v.assign(std::min<size_t>(n, 1024), JointOverride());
        for (JointOverride& j : v) { F(j.bone); F(j.euler); }
    }

private:
    std::vector<uint8_t>& buf_;
    bool writing_;
    bool ok_ = true;
    size_t pos_ = 0;
};

// The derived slots (renderer, body, emitters, sprite, voice, ragdoll, the
// pose caches, the anim pointers) are NOT here: RebuildEntity makes them.
void ArchiveEntity(Archive& ar, ScriptEngine::Entity& e, bool& hadBody, bool& hadRagdoll,
                   float (&bodyVel)[3]) {
    ar.F(e.type); ar.F(e.source); ar.F(e.mesh); ar.F(e.name);
    ar.F(e.scale); ar.F(e.pos); ar.F(e.rotWXYZ);
    ar.F(e.visible); ar.F(e.inWorld); ar.F(e.worldObject);
    ar.F(e.activeMesh); ar.F(e.activeOrigin);

    uint32_t n = uint32_t(e.emitterRecs.size());
    ar.F(n);
    if (!ar.writing()) e.emitterRecs.assign(std::min<size_t>(n, 256), {});
    for (auto& r : e.emitterRecs) {
        ar.F(r.file); ar.F(r.scale); ar.F(r.offset); ar.F(r.rotDeg);
        ar.F(r.setup); ar.F(r.evolveSet); ar.F(r.evolve); ar.F(r.stopped);
    }
    ar.F(e.hasCorona); ar.F(e.coronaArgs); ar.F(e.coronaTex); ar.F(e.coronaColor);
    ar.F(e.coronaBlend); ar.F(e.coronaSpriteOnly);

    ar.F(e.isRegion); ar.F(e.playerInside); ar.F(e.regionMin); ar.F(e.regionMax);
    ar.F(e.velocity); ar.F(e.children); ar.F(e.dieWithParent);
    ar.F(e.soundName); ar.F(e.soundDist1); ar.F(e.soundDist2); ar.F(e.soundInterval);
    ar.F(e.soundStartIn); ar.F(e.soundPlaying);
    ar.F(e.collisionsOn); ar.F(e.collisionMinTime); ar.F(e.collisionMinStrength);
    ar.F(e.collisionCooldown);
    ar.F(e.hiddenMeshes);
    ar.F(e.parent); ar.F(e.parentOffset); ar.F(e.parentJoint); ar.F(e.parentBound);
    ar.F(e.parentRotBound); ar.F(e.parentRotWXYZ);
    ar.F(e.collisionGroup); ar.F(e.movedByExplosions); ar.F(e.isProjectile); ar.F(e.isGrenade);
    ar.F(e.bodyFriction); ar.F(e.bodyRestitution);
    ar.F(e.bodyType); ar.F(e.bodyArgScale); ar.F(e.bodyMass); ar.F(e.bodyFreedomMode);
    ar.F(e.bodyFreedomSoft); ar.F(e.bodyLinDamp); ar.F(e.bodyAngDamp); ar.F(e.bodyPinned);
    ar.F(e.bodyNonColliding); ar.F(e.bodyGravity);
    ar.F(e.gravityOn); ar.F(e.poEnabled); ar.F(e.angVel);
    ar.F(e.viewAttached); ar.F(e.viewOffset); ar.F(e.viewAngles);
    ar.F(e.timeToDie);

    n = uint32_t(e.animSlots.size());
    ar.F(n);
    if (!ar.writing()) e.animSlots.assign(std::min<size_t>(n, 1024), {});
    for (auto& s : e.animSlots) { ar.F(s.name); ar.F(s.curveMask); ar.F(s.curveBone); }
    ar.F(e.animIndex); ar.F(e.animTime); ar.F(e.animScale); ar.F(e.animLoop);
    ar.F(e.jointRot);
    ar.F(e.action); ar.F(e.jumpedLastAction);
    ar.F(e.isMonster); ar.F(e.moveWish); ar.F(e.monsterMoveConst); ar.F(e.monsterMoveFlag);
    ar.F(e.monsterFlying);
    ar.F(e.sightRange); ar.F(e.sightRange360); ar.F(e.sightHalfYaw); ar.F(e.sightHalfPitch);
    ar.F(e.inSolver); ar.F(e.ragdollInSolver); ar.F(e.disabledJoints);
    ar.F(e.ragdollMovedByExplosions);
    ar.F(e.deathSpin); ar.F(e.deathImpulse); ar.F(e.deathImpulseAt); ar.F(e.hasDeathImpulse);
    ar.F(e.ragdollPose);

    ar.F(hadBody); ar.F(hadRagdoll); ar.F(bodyVel);
}

} // namespace

bool ScriptEngine::SaveWorld(const std::string& enginePath) {
    std::vector<uint8_t> buf;
    Archive ar(buf, true);
    buf.insert(buf.end(), kMagic, kMagic + 4);
    uint32_t version = kVersion;
    ar.F(version);

    ar.F(nextHandle_);
    ar.F(playerHandle_);
    ar.F(pawnEnabled_);
    float head[3] = {0, 0, 0};
    if (pawn_) for (int c = 0; c < 3; ++c) head[c] = pawn_->headPos()[c];
    ar.F(head);
    ar.F(camPos_); ar.F(camYaw_); ar.F(camPitch_);
    ar.F(timeMultiplier_); ar.F(playerSpotDone_); ar.F(mouseLocked_);

    // Handles in order, so a save diffs cleanly and loads deterministically.
    std::vector<int> handles;
    handles.reserve(entities_.size());
    for (const auto& kv : entities_) handles.push_back(kv.first);
    std::sort(handles.begin(), handles.end());
    uint32_t count = uint32_t(handles.size());
    ar.F(count);
    for (int h : handles) {
        Entity& e = entities_[h];
        bool hadBody = e.physicsBody >= 0, hadRagdoll = e.ragdollSlot >= 0;
        float bodyVel[3] = {0, 0, 0};
        if (physics_ && hadBody) physics_->GetScriptBodyVelocity(e.physicsBody, bodyVel);
        ar.F(h);
        ArchiveEntity(ar, e, hadBody, hadRagdoll, bodyVel);
    }

    const std::string path = host_ ? host_->ResolvePath(enginePath) : enginePath;
    if (!FileSystem::Get().WriteFile(path, buf)) {
        LogWarn("WORLD.SaveGame: cannot write %s", path.c_str());
        return false;
    }
    LogInfo("WORLD.SaveGame: %u entities, %zu bytes -> %s", count, buf.size(), path.c_str());
    return true;
}

void ScriptEngine::ReleaseAllEntities() {
    std::vector<int> handles;
    for (const auto& kv : entities_) handles.push_back(kv.first);
    for (int h : handles) ReleaseEntity(h);
    bodyToEntity_.clear();
    excludedSlots_.clear();
    limbShadowed_.clear();
    limbHandles_.clear();
    limbHandleIndex_.clear();
    lastExploded_.clear();
    contactVelocity_.clear();
    playerHandle_ = 0;
}

void ScriptEngine::RebuildEntity(int handle, Entity& src) {
    const bool hadBody = src.physicsBody >= 0;       // carried in the slot field by the loader
    const bool hadRagdoll = src.ragdollSlot >= 0;
    float bodyVel[3];
    for (int c = 0; c < 3; ++c) bodyVel[c] = src.velocity[c];
    src.physicsBody = -1;
    src.ragdollSlot = -1;
    src.rendererInstance = -1;
    src.spriteSlot = -1;
    src.soundVoice = 0;
    src.emitterSlots.clear();
    src.parentJointIndex = -2;
    src.pose = Entity::Pose();
    src.blendFrom = nullptr;
    src.blendFromTracks.clear();
    src.blendLeft = src.blendTotal = 0.f;
    for (auto& s : src.animSlots) { s.anim = nullptr; s.length = 0.f; s.curveBoneIndex = -2; }
    std::vector<Mat4> ragdollPose;
    ragdollPose.swap(src.ragdollPose);

    Entity& e = entities_.emplace(handle, std::move(src)).first->second;
    ++created_;

    // Animation: the same cache SetAnim uses.
    for (auto& s : e.animSlots) {
        s.anim = animations_.Get(e.source, s.name);
        s.length = s.anim ? s.anim->duration() : 0.f;
    }
    if (e.animIndex >= int(e.animSlots.size())) e.animIndex = -1;

    CreateRendererInstance(e);

    // Physics. An active mesh is remade from the map object it came from, a
    // script body from PO_Create's own arguments; then the dressing the
    // scripts applied, then the live velocity.
    if (physics_ && hadBody) {
        int slot = -1;
        if (e.worldObject && e.activeMesh >= 0 && size_t(e.activeMesh) < map_.objects.size()) {
            const MapObject& o = map_.objects[size_t(e.activeMesh)];
            float origin[3];
            slot = physics_->CreateActiveMeshBody(o, world_.scale, ActiveMeshMassScale(o.name),
                                                  o.isPinned(), o.nameHas("concave"),
                                                  o.activeGroup(), origin);
        } else if (e.type == kModel || (e.type == kMesh && !e.worldObject)) {
            std::string model, pack;
            bool okSource = true;
            if (e.type == kModel) model = e.source;
            else okSource = SplitPackSource(e.source, pack);
            if (okSource) {
                const float scale = e.bodyArgScale > 0.f ? e.bodyArgScale : e.scale;
                const float sphereRadius =
                    (e.bodyType == 1 || e.bodyType == 9) && e.bodyArgScale > 0.f
                        ? e.bodyArgScale * 1.1f : 0.f;
                slot = physics_->CreateScriptBody(e.bodyType, model, pack, e.mesh, scale, e.pos,
                                                  e.rotWXYZ, dataRoot_, e.collisionGroup,
                                                  sphereRadius);
            }
        }
        if (slot >= 0) {
            e.physicsBody = slot;
            bodyToEntity_[slot] = handle;
            if (e.bodyMass >= 0.f) physics_->SetScriptBodyMass(slot, e.bodyMass);
            if (e.bodyFreedomMode >= 0)
                physics_->SetScriptBodyFreedomOfRotation(slot, e.bodyFreedomMode, e.bodyFreedomSoft);
            if (e.bodyLinDamp >= 0.f) physics_->SetScriptBodyLinearDamping(slot, e.bodyLinDamp);
            if (e.bodyAngDamp >= 0.f) physics_->SetScriptBodyAngularDamping(slot, e.bodyAngDamp);
            if (e.bodyGravity >= 0) physics_->SetScriptBodyGravityFactor(slot, float(e.bodyGravity));
            if (e.isMonster) {
                float k = 0.f, rootOffset[3] = {0.f, 0.f, 0.f};
                MonsterBodyScale(e, k, rootOffset);
                physics_->MakeScriptBodyCharacter(slot, k, rootOffset);
                physics_->SetCharacterMovement(slot, e.monsterMoveConst, e.monsterMoveFlag);
                physics_->SetCharacterFlying(slot, e.monsterFlying);
                physics_->SetCharacterWish(slot, e.moveWish);
            }
            physics_->SetScriptBodyPose(slot, e.pos, e.rotWXYZ);
            if (e.bodyPinned) physics_->SetScriptBodyPinned(slot, true);
            if (e.bodyNonColliding) physics_->MakeScriptBodyNonColliding(slot);
            if (!e.poEnabled) physics_->SetScriptBodyEnabled(slot, false);
            else physics_->SetScriptBodyVelocity(slot, bodyVel);
        }
    }

    // Effects, exactly as the natives made them.
    for (const Entity::EmitterRec& r : e.emitterRecs) {
        int slot = -1;
        if (particles_ && emitterLib_ && textures_)
            slot = particles_->AddScriptEmitter(r.file, *emitterLib_, *textures_, world_.levelName);
        e.emitterSlots.push_back(slot);
        if (slot < 0) continue;
        if (r.setup) particles_->SetupScriptEmitter(slot, r.scale, r.offset, r.rotDeg);
        if (r.evolveSet) particles_->SetScriptEmitterEvolve(slot, r.evolve);
        if (r.stopped) particles_->StopScriptEmitter(slot);
    }
    if (e.hasCorona && billboards_ && textures_) {
        e.spriteSlot = billboards_->SetupScriptCorona(-1, e.coronaArgs, e.coronaTex, e.coronaColor,
                                                      e.coronaBlend, e.coronaSpriteOnly,
                                                      *textures_, world_.levelName);
    }
    UpdateAttachments(e);

    // A corpse: the ragdoll seeded from the pose it was saved in.
    if (hadRagdoll && !ragdollPose.empty()) EnableRagdoll(e, true, &ragdollPose);

    if (e.soundPlaying && e.soundStartIn < 0.f) StartBoundSound(e);
}

bool ScriptEngine::LoadWorld(const std::string& enginePath) {
    const std::string path = host_ ? host_->ResolvePath(enginePath) : enginePath;
    std::vector<uint8_t> buf;
    if (!FileSystem::Get().Exists(path) || !ReadFile(path, buf)) {
        LogWarn("WORLD.LoadGame: cannot read %s", path.c_str());
        return false;
    }
    Archive ar(buf, false);
    char magic[4] = {0, 0, 0, 0};
    ar.Raw(magic, 4);
    uint32_t version = 0;
    ar.F(version);
    if (std::memcmp(magic, kMagic, 4) != 0 || version != kVersion) {
        LogWarn("WORLD.LoadGame: %s is not a PainfulEngine save (version %u)", path.c_str(),
                version);
        return false;
    }

    // Everything the level load made goes: LoadMap's active meshes and water
    // took handles the save owns.
    ReleaseAllEntities();

    int nextHandle = 1, playerHandle = 0;
    bool pawnEnabled = false;
    float head[3];
    ar.F(nextHandle); ar.F(playerHandle); ar.F(pawnEnabled); ar.F(head);
    ar.F(camPos_); ar.F(camYaw_); ar.F(camPitch_);
    ar.F(timeMultiplier_); ar.F(playerSpotDone_); ar.F(mouseLocked_);
    camPoseDirty_ = true;

    uint32_t count = 0;
    ar.F(count);
    size_t made = 0;
    for (uint32_t i = 0; i < count && ar.ok(); ++i) {
        int handle = 0;
        ar.F(handle);
        Entity e;
        bool hadBody = false, hadRagdoll = false;
        float bodyVel[3] = {0, 0, 0};
        ArchiveEntity(ar, e, hadBody, hadRagdoll, bodyVel);
        if (!ar.ok()) break;
        // RebuildEntity reads these three through the slot fields.
        e.physicsBody = hadBody ? 0 : -1;
        e.ragdollSlot = hadRagdoll ? 0 : -1;
        for (int c = 0; c < 3; ++c) e.velocity[c] = bodyVel[c];
        RebuildEntity(handle, e);
        ++made;
    }
    if (!ar.ok()) {
        LogWarn("WORLD.LoadGame: %s is truncated at %zu bytes", path.c_str(), ar.pos());
    }
    nextHandle_ = std::max(nextHandle_, nextHandle);
    playerHandle_ = playerHandle;
    pawnEnabled_ = pawnEnabled;
    if (pawn_ && playerHandle_) pawn_->Spawn(head);

    // Water surfaces were rebuilt by LoadMap with fresh handles; point them
    // at the restored entities of the same name.
    for (WaterSurface& w : water_)
        for (const auto& kv : entities_)
            if (kv.second.worldObject && kv.second.activeMesh < 0) {
                const MapObject* o = nullptr;
                for (const MapObject& m : map_.objects)
                    if (m.name == kv.second.name) { o = &m; break; }
                if (o && w.lo[0] == o->bboxMin[0] * (world_.scale > 0.f ? world_.scale : 1.f) &&
                    w.y == o->bboxMax[1] * (world_.scale > 0.f ? world_.scale : 1.f)) {
                    w.entity = kv.first;
                    break;
                }
            }

    loadedFromSave_ = true;
    LogInfo("WORLD.LoadGame: %zu of %u entities restored from %s (player #%d, next handle %d)",
            made, count, path.c_str(), playerHandle_, nextHandle_);

    // The original calls this between LoadEntities and LoadPortalState.
    if (host_) host_->RunString("SaveGame:AfterLoadEntities()");
    return ar.ok();
}

bool ScriptEngine::TakeLevelChange(std::string& levelName, bool& hasMap, bool& fromSave) {
    if (levelChangeSeen_ == levelChangeSerial_) return false;
    levelChangeSeen_ = levelChangeSerial_;
    levelName = world_.levelName;
    hasMap = mapLoaded_;
    fromSave = loadedFromSave_;
    return true;
}

int ScriptEngine::L_WORLD_SaveGame(lua_State* L) {
    From(L)->SaveWorld(luaL_optstring(L, 1, ""));
    return 0;
}

int ScriptEngine::L_WORLD_LoadGame(lua_State* L) {
    From(L)->LoadWorld(luaL_optstring(L, 1, ""));
    return 0;
}

}  // namespace painful
