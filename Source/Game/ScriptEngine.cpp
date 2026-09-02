// ScriptEngine: construction, the entity registry and the subsystem attachments.
//
// The class itself is declared in ScriptEngine.h. Its natives are implemented
// one family per file - ScriptEntity, ScriptMonster, ScriptSound, ScriptPlayer,
// ScriptInput, ScriptTrace, ScriptAnim, ScriptWorld, ScriptHud, ScriptMenu,
// ScriptDeath, ScriptLimbs - and ScriptBind maps every one of them to the
// module and name the shipped Lua calls it by.
#include "ScriptEngineInternal.h"

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

}  // namespace

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
    animations_.SetRoot(dataRoot + "/Models");
    skeletons_.SetRoot(dataRoot + "/Models");
}

void ScriptEngine::AttachPhysics(PhysicsWorld* physics, const std::string& dataRoot) {
    physics_ = physics;
    dataRoot_ = dataRoot;
    animations_.SetRoot(dataRoot + "/Models");
    skeletons_.SetRoot(dataRoot + "/Models");
}

void ScriptEngine::AttachParticles(ParticleRenderer* particles, EmitterLibrary* library) {
    particles_ = particles;
    emitterLib_ = library;
}

void ScriptEngine::AttachBillboards(BillboardRenderer* billboards) {
    billboards_ = billboards;
}

void ScriptEngine::AttachPlayer(PlayerPawn* pawn) {
    pawn_ = pawn;
}

void ScriptEngine::AttachInput(Input* input) {
    input_ = input;
}

// THE PLAYER ENTITY SITS AT THE FEET, not at the eyes.
//
// Three shipped scripts agree, and none of them would work otherwise:
//   CPlayer:IsOnGround traces from GetPosition()+0.5 down to -0.6 looking for
//     the floor - a head-anchored origin never reaches it;
//   BindSoundToEntity parks a player's sound at offset (0, 2, 0) with the
//     comment "-- head", and kEyeAboveFloor is exactly 2;
//   PainHead:Tick flies the returning blade to GetPosition()+1.62, which is
//     chest height off the floor and a good half-metre OVER the head off the
//     eyes - which is where the blades were going.
//
// ENTITY.PO_GetPawnHeadPos is what the weapons ask when they want the eye, and
// every one of them does.
void ScriptEngine::SyncPlayerFromPawn() {
    if (!pawn_ || !playerHandle_) return;
    if (Entity* e = Find(playerHandle_)) {
        float floor[3];
        pawn_->FloorPos(floor);
        for (int i = 0; i < 3; ++i) e->pos[i] = floor[i];
    }
}

void ScriptEngine::UpdateAttachments(Entity& e) {
    if (particles_ && !e.emitterSlots.empty()) {
        float rot9[9];
        EngineQuatToRot9(e.rotWXYZ, rot9);
        for (int slot : e.emitterSlots) {
            if (slot < 0) continue;
            particles_->SetScriptEmitterOwner(slot, e.pos, rot9, e.scale,
                                              e.visible && e.inWorld);
        }
    }
    if (billboards_ && e.spriteSlot >= 0)
        billboards_->SetScriptSpritePos(e.spriteSlot, e.pos);
}

void ScriptEngine::SyncFromPhysics(bool activeOnly) {
    if (!physics_) return;
    physics_->CollectScriptPoses(poseScratch_, activeOnly);
    for (const ScriptBodyPose& pose : poseScratch_) {
        auto it = bodyToEntity_.find(pose.slot);
        if (it == bodyToEntity_.end()) continue;
        Entity* e = Find(it->second);
        if (!e) continue;
        // A projectile's body is kinematic and carries the velocity
        // SetVelocity gave it, so the physics step moves it too - and reading
        // that back ADDED a second advance on top of the one TickProjectiles
        // had already made. A stake configured for 70 m/s flew at 140, and
        // every distance-dependent thing in the scripts came out at half the
        // range: Stake:Tick's arc starts on a timer, so it began its dive 28m
        // out instead of 14.
        if (e->isProjectile) continue;
        for (int c = 0; c < 3; ++c) e->pos[c] = pose.pos[c];
        // A monster's body cannot rotate (translation-only DOFs) and its yaw
        // is what SetOrientation wrote; the entity keeps the scripts' value.
        if (!e->isMonster)
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
    // A rebuilt instance starts with every mesh shown, so the hidden set has to
    // be replayed - otherwise swapping a weapon's model brings back the blades
    // its alt fire had hidden.
    if (e.rendererInstance >= 0 && renderer_) {
        for (const auto& kv : e.hiddenMeshes)
            renderer_->SetScriptMeshVisibility(e.rendererInstance, kv.first, kv.second);
    }
    if (e.rendererInstance >= 0) SyncPose(e);
}

void ScriptEngine::SyncPose(Entity& e) {
    if (renderer_ && e.rendererInstance >= 0) {
        renderer_->SetScriptPose(e.rendererInstance, e.pos, e.rotWXYZ);
        renderer_->SetScriptVisible(e.rendererInstance, e.visible && e.inWorld);
    }
    // A billboard is not a model instance, and EnableDraw never reached one:
    // its sprite kept drawing at full alpha until the entity was released and
    // then vanished between frames. Routed through the fade instead.
    if (billboards_ && e.spriteSlot >= 0) {
        billboards_->SetScriptSpriteVisible(e.spriteSlot, e.visible && e.inWorld);
    }
    UpdateAttachments(e);
}

void ScriptEngine::FlushToRenderer() {
    for (auto& kv : entities_) CreateRendererInstance(kv.second);
}


}  // namespace painful
