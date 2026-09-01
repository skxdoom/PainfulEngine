// ScriptEngine: the SOUND / SND natives and the 3D listener.

#include "ScriptEngineInternal.h"

namespace painful {

// ------------------------------------------------------------------ sound
//
// Three families, and the split is by how the scripts hold them rather than by
// what they sound like:
//
//   SOUND.Play2D / Play3D   fire and forget; the handle comes back but most
//                           callers drop it
//   SOUND2D.*               a handle kept and driven - a bullet-time loop
//   SOUND3D.*               the same, at a world position - a flamethrower,
//                           an elevator
//
// Volume arrives as 0..100 (CObject:GetSndInfo defaults to 100), and the
// hearing distances default to 15 and 40 there, so a script that names only a
// sample still gets sensible falloff.

// The sample name the scripts build is a path under Sounds without the
// extension, and CObject:GetSndInfo joins it as `path.."/"..name`, which
// leaves a leading slash when the definition has no path. Trim it rather than
// failing to find the file.
static std::string SoundName(lua_State* L, int index) {
    const char* raw = lua_isstring(L, index) ? lua_tostring(L, index) : nullptr;
    if (!raw) return std::string();
    std::string name = raw;
    while (!name.empty() && (name.front() == '/' || name.front() == '\\'))
        name.erase(name.begin());
    return name;
}

static float SoundVolume(lua_State* L, int index) {
    // 0..100 from the scripts; anything absent means full.
    const double v = luaL_optnumber(L, index, 100.0);
    return float(v) * 0.01f;
}

int ScriptEngine::L_SOUND_Play2D(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const int v = self->audio_->Play2D(SoundName(L, 1), SoundVolume(L, 2),
                                       lua_toboolean(L, 3) != 0,
                                       lua_toboolean(L, 4) != 0);
    lua_pushnumber(L, v);
    return 1;
}

int ScriptEngine::L_SOUND_Play3D(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const float pos[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                          float(luaL_optnumber(L, 4, 0))};
    const int v = self->audio_->Play3D(SoundName(L, 1), pos,
                                       float(luaL_optnumber(L, 5, 15.0)),
                                       float(luaL_optnumber(L, 6, 40.0)),
                                       lua_toboolean(L, 7) != 0);
    lua_pushnumber(L, v);
    return 1;
}

// SOUND2D.Create(name, loop) / SOUND3D.Create(name) -> a handle the script
// keeps. Created stopped: the scripts call Play when they want it.
int ScriptEngine::L_SND_Create2D(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const int v = self->audio_->Create(SoundName(L, 1), false);
    if (v && lua_toboolean(L, 2)) self->audio_->SetLoopCount(v, -1);
    lua_pushnumber(L, v);
    return 1;
}

int ScriptEngine::L_SND_Create3D(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const int v = self->audio_->Create(SoundName(L, 1), true);
    if (v && lua_toboolean(L, 2)) self->audio_->SetLoopCount(v, -1);
    lua_pushnumber(L, v);
    return 1;
}

int ScriptEngine::L_SND_Play(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Start(int(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_SND_Stop(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Stop(int(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_SND_Pause(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Pause(int(luaL_optnumber(L, 1, 0)), true);
    return 0;
}

int ScriptEngine::L_SND_IsPlaying(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_pushboolean(L, self->audio_ &&
                           self->audio_->IsPlaying(int(luaL_optnumber(L, 1, 0))));
    return 1;
}

int ScriptEngine::L_SND_SetVolume(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_)
        self->audio_->SetVolume(int(luaL_optnumber(L, 1, 0)), SoundVolume(L, 2));
    return 0;
}

// SOUND2D/SOUND3D.SetLoopCount(voice, count) - count is MILES' count, not ours.
//
// The native passes the script's value straight to Miles with a default of 0
// (Engine.dll 0x10125ce0 -> MilesEngine::Sound3D_SetLoopCount), and Miles reads
// 0 as "loop forever", 1 as "play once", n as "play n times". The scripts agree:
// of the 24 call sites passing 0, every one is a sustained loop - _sndRotor,
// _loopSnd, _rain, _sndElectro - and the 12 passing 1 are all "let the current
// pass finish, then stop" on a sound already looping.
//
// AudioEngine counts down instead, so forever is -1 there and 0 would silence
// exactly the sounds the scripts loop most. Translate at the boundary.
int ScriptEngine::L_SND_SetLoopCount(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const int miles = int(luaL_optnumber(L, 2, 0));
    self->audio_->SetLoopCount(int(luaL_optnumber(L, 1, 0)), miles == 0 ? -1 : miles);
    return 0;
}

int ScriptEngine::L_SND_SetPosition(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const float pos[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                          float(luaL_optnumber(L, 4, 0))};
    self->audio_->SetPosition(int(luaL_optnumber(L, 1, 0)), pos);
    return 0;
}

int ScriptEngine::L_SND_SetHearingDistance(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_)
        self->audio_->SetHearingDistance(int(luaL_optnumber(L, 1, 0)),
                                         float(luaL_optnumber(L, 2, 15.0)),
                                         float(luaL_optnumber(L, 3, 40.0)));
    return 0;
}

int ScriptEngine::L_SND_SetSoundSpeed(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_)
        self->audio_->SetSpeed(int(luaL_optnumber(L, 1, 0)),
                               float(luaL_optnumber(L, 2, 1.0)));
    return 0;
}

// Delete stops it; Forget lets it finish and stops caring. Both hand the slot
// back, which is what keeps a level's worth of one-shots from filling the pool.
int ScriptEngine::L_SND_Delete(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Release(int(luaL_optnumber(L, 1, 0)), false);
    return 0;
}

int ScriptEngine::L_SND_Forget(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Release(int(luaL_optnumber(L, 1, 0)), true);
    return 0;
}

// SOUND.SetPlayerPos / SetPlayerOrientation - the listener, pushed every frame
// by CPlayer. Without the orientation everything would still attenuate with
// distance but nothing would come from a side.
int ScriptEngine::L_SOUND_SetPlayerPos(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int c = 0; c < 3; ++c)
        self->listenerPos_[c] = float(luaL_optnumber(L, c + 1, 0));
    self->PushListener();
    return 0;
}

int ScriptEngine::L_SOUND_SetPlayerOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int c = 0; c < 3; ++c)
        self->listenerFwd_[c] = float(luaL_optnumber(L, c + 1, 0));
    self->PushListener();
    return 0;
}

void ScriptEngine::PushListener() {
    if (!audio_) return;
    // Right = forward x up. The scripts only hand over a forward vector, and
    // panning needs a side.
    const float* f = listenerFwd_;
    float right[3] = {f[1] * 0.f - f[2] * 1.f, f[2] * 0.f - f[0] * 0.f,
                      f[0] * 1.f - f[1] * 0.f};
    const float l = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (l > 1e-5f) {
        for (int c = 0; c < 3; ++c) right[c] /= l;
    } else {
        right[0] = 1.f; right[1] = 0.f; right[2] = 0.f;
    }
    audio_->SetListener(listenerPos_, listenerFwd_, right);
}

// WPT.Load(dir, mergeFlag) - the navigation graph.
//
// The scripts pass only a DIRECTORY ("../Data/Maps/"); the engine appends the
// map's own name, which is why WORLD.LoadMap has to have run first. Engine.dll
// (0x10128A90) clears both pathfinders, then LoadContents the .wps and
// LoadFloors a companion file that does not ship - so the floors section
// inside the .wps is all there is, and routing does not need it.
int ScriptEngine::L_WPT_Load(lua_State* L) {
    ScriptEngine* self = From(L);
    self->waypoints_ = WaypointSet{};
    self->paths_.clear();

    // "../Data/Maps/1x01_Chaos.mpk" -> the .wps beside it.
    std::string path = self->world_.mapPath;
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return 0;                         // a level with no map has no graph
    path = path.substr(0, dot) + ".wps";

    const std::string resolved = self->host_ ? self->host_->ResolvePath(path) : path;
    if (!WaypointSet::Load(resolved, self->waypoints_)) {
        // Only 29 of the 85 shipped maps carry a .wps at all, so a missing one
        // is an ordinary answer rather than a fault - those levels' actors
        // walk straight at their target, which is what they did before any of
        // this existed. A malformed one IS worth shouting about.
        if (FileSystem::Get().Exists(resolved))
            LogWarn("waypoints: %s", self->waypoints_.error.c_str());
        else
            LogInfo("waypoints: none for this map, actors will walk straight");
        return 0;
    }
    LogInfo("waypoints: %zu points, %zu links from %s",
            self->waypoints_.nodes.size(), self->waypoints_.links.size(),
            path.c_str());
    return 0;
}

// PATH.Create() -> a handle the scripts keep in CActor._Path.
//
// It has to be non-nil: CActor tests `if not self._Path` before creating
// another, and returning nothing made every actor build a fresh path every
// single tick. Slots are reused, and the handle is index+1 so that 0 is never
// a valid one.
int ScriptEngine::L_PATH_Create(lua_State* L) {
    ScriptEngine* self = From(L);
    for (size_t i = 0; i < self->paths_.size(); ++i) {
        if (self->paths_[i].live) continue;
        self->paths_[i] = Route{};
        self->paths_[i].live = true;
        lua_pushnumber(L, double(i + 1));
        return 1;
    }
    self->paths_.push_back(Route{});
    self->paths_.back().live = true;
    lua_pushnumber(L, double(self->paths_.size()));
    return 1;
}

int ScriptEngine::L_PATH_Release(lua_State* L) {
    ScriptEngine* self = From(L);
    const int h = int(luaL_optnumber(L, 1, 0));
    if (h > 0 && size_t(h) <= self->paths_.size()) self->paths_[size_t(h) - 1] = Route{};
    return 0;
}

// PATH.GetShortest(path, x,y,z, destX,destY,destZ, minDist, maxDist) - route
// through the waypoint graph.
//
// minDist/maxDist come from the actor's template as WPminDist / WPmaxDist and
// bound how far it is willing to reach for a waypoint. An actor standing
// somewhere the level designer never marked gets NO path, which is not a
// failure: CActor reads an empty path as "finished" and walks straight at the
// destination instead.
//
// The first waypoint is dropped when the actor is already close to it, so it
// does not walk backwards to a point it has effectively reached.
int ScriptEngine::L_PATH_GetShortest(lua_State* L) {
    ScriptEngine* self = From(L);
    const int h = int(luaL_optnumber(L, 1, 0));
    if (h <= 0 || size_t(h) > self->paths_.size()) return 0;
    Route& route = self->paths_[size_t(h) - 1];
    route.points.clear();
    route.next = 0;
    if (self->waypoints_.nodes.empty()) return 0;

    const float from[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                           float(luaL_optnumber(L, 4, 0))};
    const float to[3] = {float(luaL_optnumber(L, 5, 0)), float(luaL_optnumber(L, 6, 0)),
                         float(luaL_optnumber(L, 7, 0))};
    const float maxDist = float(luaL_optnumber(L, 9, 0));

    const int a = self->waypoints_.Closest(from, maxDist);
    const int b = self->waypoints_.Closest(to, maxDist);
    if (a < 0 || b < 0) return 0;
    if (!self->waypoints_.FindPath(a, b, self->routeScratch_)) return 0;

    const float minDist = float(luaL_optnumber(L, 8, 0));
    for (size_t i = 0; i < self->routeScratch_.size(); ++i) {
        const WaypointSet::Node& n =
            self->waypoints_.nodes[size_t(self->routeScratch_[i])];
        if (i == 0 && minDist > 0.f) {
            float d = 0.f;
            for (int c = 0; c < 3; ++c) {
                const float e = n.pos[c] - from[c];
                d += e * e;
            }
            if (d < minDist * minDist) continue;
        }
        for (int c = 0; c < 3; ++c) route.points.push_back(n.pos[c]);
    }
    return 0;
}

// PATH.IsFinished(path) -> 1 when no waypoint is left.
//
// Engine.dll (0x1013AA20) starts at "finished" and only clears it when PeekPos
// finds a point, so a path that does not exist is finished - and in CActor
// that is the branch which walks straight at the destination. Everything here
// therefore degrades to the old straight-line behaviour rather than stopping.
int ScriptEngine::L_PATH_IsFinished(lua_State* L) {
    ScriptEngine* self = From(L);
    const int h = int(luaL_optnumber(L, 1, 0));
    bool finished = true;
    if (h > 0 && size_t(h) <= self->paths_.size()) {
        const Route& route = self->paths_[size_t(h) - 1];
        finished = route.next * 3 >= route.points.size();
    }
    lua_pushnumber(L, finished ? 1 : 0);
    return 1;
}

// PATH.GetNextPoint(path) -> x,y,z, and CONSUMES it: CActor calls this and
// then asks IsFinished again to learn whether that was the last one.
int ScriptEngine::L_PATH_GetNextPoint(lua_State* L) {
    ScriptEngine* self = From(L);
    const int h = int(luaL_optnumber(L, 1, 0));
    float p[3] = {0, 0, 0};
    if (h > 0 && size_t(h) <= self->paths_.size()) {
        Route& route = self->paths_[size_t(h) - 1];
        if (route.next * 3 + 2 < route.points.size()) {
            for (int c = 0; c < 3; ++c) p[c] = route.points[route.next * 3 + size_t(c)];
            ++route.next;
        }
    }
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, p[c]);
    return 3;
}

// ENTITY.PO_SetSightParams(e, viewDistance, viewDistance360, viewAngle,
// viewAnglePitch) - 0x10131210, writing the four floats at PhysicsObject
// +0x24..+0x30. The defaults are the engine's own: 20, 2, 180, 180.
//
// The names come from the templates, and they say what the model is:
// `viewDistance360` is how far the actor sees in EVERY direction, and
// `viewDistance` how far it sees inside its cone. Shipped monsters carry
// things like `viewAngle = 170, viewDistance360 = 6`: aware of anything within
// six units, and beyond that only what is in front.
//
// The angles arrive in DEGREES as a full spread (360 means all round) and are
// stored as a half-angle in radians, which is what makes the engine's own
// default of 180 come out as pi/2 - the value PO_Create seeds.
int ScriptEngine::L_PO_SetSightParams(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    e->sightRange = float(luaL_optnumber(L, 2, 20.0));
    e->sightRange360 = float(luaL_optnumber(L, 3, 2.0));
    e->sightHalfYaw = float(luaL_optnumber(L, 4, 180.0)) * float(kPi / 360.0);
    e->sightHalfPitch = float(luaL_optnumber(L, 5, 180.0)) * float(kPi / 360.0);
    return 0;
}

// ENTITY.SeesEntity(a, b) -> can a see b.
//
// Engine.dll 0x101335E0 hands this to PhysicsWorld::CalculatePawnToEntityVisibility
// when the looker has a physics object, and otherwise falls back to a plain
// line trace between the two entity POSITIONS (+0x620) - which is the shape
// reproduced here: the range and cone from PO_SetSightParams, then an
// unobstructed line.
//
// Note what the engine brackets the trace with: it turns the looker's own
// ragdoll off for the duration and back on afterwards, because a monster's own
// body sits on the line and would blind it. The same applies to us - both
// bodies are excluded below.
int ScriptEngine::L_SeesEntity(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* a = self->Find(HandleArg(L, 1));
    Entity* b = self->Find(HandleArg(L, 2));
    lua_pushboolean(L, a && b && self->Sees(*a, *b));
    return 1;
}

bool ScriptEngine::Sees(Entity& a, Entity& b) const {
    if (!physics_) return false;

    float to[3];
    for (int c = 0; c < 3; ++c) to[c] = b.pos[c] - a.pos[c];
    const float dist = std::sqrt(to[0]*to[0] + to[1]*to[1] + to[2]*to[2]);
    if (dist > a.sightRange) return false;
    if (dist < 1e-4f) return true;

    // Inside the all-round radius the cone does not apply; outside it, the
    // target has to be in front. A viewAngle of 360 makes the half-angle pi,
    // which no angle can exceed - so a monster declared to see all round
    // never fails this, without needing a special case.
    if (dist > a.sightRange360 && a.sightHalfYaw < float(kPi)) {
        const float fwd[3] = {0, 0, 1};      // model forward, the axis the
        float facing[3];                     // walk animations travel along
        EngineQuatRotate(a.rotWXYZ, fwd, facing);
        const float fl = std::sqrt(facing[0]*facing[0] + facing[2]*facing[2]);
        const float tl = std::sqrt(to[0]*to[0] + to[2]*to[2]);
        if (fl > 1e-6f && tl > 1e-6f) {
            const float cosYaw = (facing[0]*to[0] + facing[2]*to[2]) / (fl * tl);
            if (cosYaw < std::cos(a.sightHalfYaw)) return false;
        }
    }

    // Line of sight. Both bodies are excluded: the looker's own body is on the
    // line by construction, and the target's would stop the trace one step
    // Against the WORLD only, not against other bodies.
    //
    // Engine.dll's CalculatePawnToEntityVisibility (0x10198D30) takes both
    // pawns' head positions, checks the range at PhysicsObject+0x24 and the
    // pitch cone at +0x30, and then resolves the rest through
    // World::FindZone - the zone graph. Visibility there is a question about
    // level geometry, not about what happens to be standing in the way.
    //
    // Measured, and the difference is the whole behaviour of a crowd: 16 monks
    // spawned four deep in front of the player, tracing against bodies, leaves
    // 9 of 16 ever seeing him - only the front rank, because each rank blinds
    // the one behind it. Against the world alone all 16 see, walk and arrive.
    PhysicsWorld::RayHit hit;
    if (!physics_->RayCast(a.pos, b.pos, hit, true))
        return true;                          // nothing in the way at all
    return hit.distance >= dist - 1e-3f;      // whatever it hit is past the target
}

// ENTITY.PO_SetMonsterMovementConst(e, value, flag) - 0x10130920, defaults
// 0.5 and false. Recorded; what the engine's mover does with them is not
// established, so nothing here reads them yet.
int ScriptEngine::L_PO_SetMonsterMovementConst(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    e->monsterMoveConst = float(luaL_optnumber(L, 2, 0.5));
    e->monsterMoveFlag = lua_toboolean(L, 3) != 0;
    return 0;
}

// ENTITY.PO_IsOnFloor(e) -> onFloor, nx, ny, nz. Four values (0x101341C0
// returns 4), and CAiBrain unpacks all four into the floor normal.
int ScriptEngine::L_PO_IsOnFloor(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && e->onFloor);
    for (int c = 0; c < 3; ++c)
        lua_pushnumber(L, e ? e->floorNormal[c] : (c == 1 ? 1.0 : 0.0));
    return 4;
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

// ENTITY.PO_SetMass. On an actor with a ragdoll this is the RAGDOLL's mass -
// CActor:EnableRagdoll sets it immediately after activating one, with the
// comment "dopiero po aktywacji moge pobrac i zmienic mase ragdolla" (only
// after activation can I get and change the ragdoll's mass). The values are
// whole-body: zombie 150, nun 120, up to 400, against the .hke's per-limb
// masses which total far less. That total is what the weapons' impulses are
// calibrated against.
int ScriptEngine::L_PO_SetMass(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !self->physics_) return 0;
    const float mass = float(luaL_optnumber(L, 2, 0));
    if (e->ragdollSlot >= 0) self->physics_->SetRagdollMass(e->ragdollSlot, mass);
    else if (e->physicsBody >= 0) self->physics_->SetScriptBodyMass(e->physicsBody, mass);
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

// ---------------------------------------------------------- bound 3D sounds
//
// A sound that belongs to a THING rather than to a point: the loop a flying
// PainHead carries, a monster's move loop, a turret spinning. The scripts make
// a Sound entity, hang it off its owner with ENTITY.RegisterChild, describe it
// with SND.Setup3D and start it with SND.Play - BindSoundToEntity (Utils.lua)
// is that sequence, and 23 call sites use it.
//
// SND is entity-addressed where SOUND2D/SOUND3D are voice-addressed, and
// SND.GetSound3DPtr is the bridge: it hands back the voice so a script can then
// drive it with the voice API. Only Setup3D, GetSound3DPtr and
// SetVelocityScaleFactor were recovered from the registration table
// (0x102C28C8); Play, Stop and IsPlaying are called by the scripts on the same
// entity handles, so they are implemented to that contract rather than to a
// recovered address.

// SND.Setup3D(entity, name, dist1=10, dist2=20, interval=-1, ?, dontAutoDelete)
//
// Argument order and every default is from 0x10139620, which reads them
// backwards - GetBool(7), GetFloat(6,10), GetFloat(5,-1), GetFloat(4,20),
// GetFloat(3,10), GetString(2) - and calls Sound::Setup3D.
//
// interval is the gap between repeats: BindSoundToEntity passes 0 for a looping
// sound and -1 for a one-shot, the same polarity Miles uses for loop counts.
int ScriptEngine::L_SND_Setup3D(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    e->soundName = luaL_optstring(L, 2, "");
    e->soundDist1 = float(luaL_optnumber(L, 3, 10.0));
    e->soundDist2 = float(luaL_optnumber(L, 4, 20.0));
    e->soundInterval = float(luaL_optnumber(L, 5, -1.0));
    // The sound's own position is its offset ON its parent - BindSoundToEntity
    // calls ENTITY.SetPosition before RegisterChild - and binding it here is
    // what makes it FOLLOW. RegisterChild alone only records who owns it.
    if (e->parent != 0 && !e->parentBound) {
        for (int c = 0; c < 3; ++c) e->parentOffset[c] = e->pos[c];
        e->parentBound = true;
    }
    return 0;
}

// SND.Play(entity, delay) - delay in seconds, counted down by TickSounds.
int ScriptEngine::L_SND_EntityPlay(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !self->audio_ || e->soundName.empty()) return 0;
    const float delay = float(luaL_optnumber(L, 2, 0.0));
    if (delay > 0.f) { e->soundStartIn = delay; return 0; }
    self->StartBoundSound(*e);
    return 0;
}

int ScriptEngine::L_SND_EntityStop(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    e->soundStartIn = -1.f;
    if (self->audio_ && e->soundVoice) self->audio_->Stop(e->soundVoice);
    return 0;
}

int ScriptEngine::L_SND_EntityIsPlaying(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    const bool on = e && self->audio_ && e->soundVoice &&
                    self->audio_->IsPlaying(e->soundVoice);
    lua_pushboolean(L, on ? 1 : 0);
    return 1;
}

// The voice behind a Sound entity, so a script can hand it to the SOUND3D
// natives - Beast, Ghost and Witch all keep the pointer and set the volume or
// the speed on it directly.
int ScriptEngine::L_SND_GetSound3DPtr(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushnumber(L, e ? e->soundVoice : 0);
    return 1;
}

void ScriptEngine::StartBoundSound(Entity& e) {
    if (!audio_ || e.soundName.empty()) return;
    if (e.soundVoice) audio_->Release(e.soundVoice, false);
    e.soundVoice = audio_->Create(e.soundName, true);
    if (!e.soundVoice) return;
    audio_->SetHearingDistance(e.soundVoice, e.soundDist1, e.soundDist2);
    // interval >= 0 repeats; AudioEngine counts down, so forever is -1 there.
    audio_->SetLoopCount(e.soundVoice, e.soundInterval >= 0.f ? -1 : 1);
    audio_->SetPosition(e.soundVoice, e.pos);
    audio_->Start(e.soundVoice);
}

// Started late, and kept on the thing it hangs off. UpdateAttached has already
// placed the bound entities for this frame, so pos is current.
void ScriptEngine::TickSounds(float dt) {
    if (!audio_) return;
    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (e.soundStartIn >= 0.f) {
            e.soundStartIn -= dt;
            if (e.soundStartIn <= 0.f) {
                e.soundStartIn = -1.f;
                StartBoundSound(e);
            }
        }
        if (e.soundVoice) audio_->SetPosition(e.soundVoice, e.pos);
    }
}

}  // namespace painful
