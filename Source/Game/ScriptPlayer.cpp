// ScriptEngine: the player pawn, trigger regions and the camera natives.

#include "ScriptEngineInternal.h"

#include <algorithm>

namespace painful {

// ---------------------------------------------------------------- player

// CreatePlayer(model, bool) -> the player's entity handle. The pawn itself
// is engine-side: the original's player locomotion is native code driven by
// the PlayerMove tweaks, and the scripts wrap the handle in CPlayer for
// health, weapons and pickups. The model ("player_box") is never drawn in
// first person - Game:AddPlayer sets Visible = false immediately.
int ScriptEngine::L_CreatePlayer(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity e;
    e.type = kModel;
    e.source = luaL_optstring(L, 1, "");
    e.name = "Player";
    const int handle = self->nextHandle_++;
    self->entities_.emplace(handle, e);
    ++self->created_;
    self->playerHandle_ = handle;
    // Born with its physics object DISABLED. Game:SwitchPlayerToPhysics is
    // the level start's last step (SaveGame.lua: LoadLevel, OnPlay, Switch,
    // MOUSE.Lock), and it only seats the pawn at the camera and seeds
    // Player.Pos when PO_IsEnabled is false. Created enabled, it returned
    // early, Player.Pos stayed (0,0,0) for the first tick, and any ambush box
    // spanning the origin fired at load (C2L1_Bridge's ninjas).
    self->pawnEnabled_ = false;
    if (self->pawn_) {
        const float at[3] = {0, 0, 0};
        self->pawn_->Spawn(at);
    }
    lua_pushnumber(L, handle);
    return 1;
}

int ScriptEngine::L_PO_SetPawnHeadPos(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    const float p[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                        float(luaL_optnumber(L, 4, 0))};
    for (int i = 0; i < 3; ++i) e->pos[i] = p[i];
    if (self->pawn_ && HandleArg(L, 1) == self->playerHandle_) self->pawn_->SetHeadPos(p);
    return 0;
}

int ScriptEngine::L_PO_GetPawnHeadPos(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    float head[3] = {0, 0, 0};
    if (e) self->EyePoint(*e, HandleArg(L, 1), head);
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, head[c]);
    return 3;
}

// PO_Enable / PO_IsEnabled: on the player this is the walk/fly switch
// (SwitchPlayerToPhysics); on a prop it wakes or sleeps the body.
int ScriptEngine::L_PO_IsEnabled(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    if (handle == self->playerHandle_ && self->playerHandle_) {
        lua_pushboolean(L, self->pawnEnabled_);
        return 1;
    }
    const Entity* e = self->Find(handle);
    lua_pushboolean(L, e && e->poEnabled && self->physics_ && e->physicsBody >= 0 &&
                           self->physics_->ScriptBodyExists(e->physicsBody));
    return 1;
}

// EDITOR.OutputText(line) - where Game:Print goes.
//
// The scripts' own tracing sink. Game:Print stamps the line with the game clock
// and hands it here, and 251 of debugMarek's 449 uses are Game:Print calls - so
// this is what carries the developers' running commentary on actor state, boss
// phases and animation decisions.
//
// It is not in the recovered native list at all, because the EDITOR module was
// registered by the editor build rather than the game. Without it, turning
// developer mode on sets both flags correctly and then produces nothing.
int ScriptEngine::L_EDITOR_OutputText(lua_State* L) {
    const char* text = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
    if (text && *text) LogInfo("script: %s", text);
    return 0;
}

// IsFinalBuild() -> is this a RETAIL build.
//
// The shipped scripts gate their own debug tooling on `not IsFinalBuild()` -
// 24 places across 7 files, from the key that hides the viewmodel to actor
// state readouts. Left unimplemented this returned nothing, nil is falsy, and
// every one of those branches was live in our build BY ACCIDENT: retail
// behaviour is the default the engine should present, and debug is something
// asked for.
//
// So it answers true unless developer mode is on, which F6 sets alongside the
// scripts' other switch, debugMarek.
int ScriptEngine::L_IsFinalBuild(lua_State* L) {
    lua_pushboolean(L, From(L)->devMode_ ? 0 : 1);
    return 1;
}

int ScriptEngine::L_PO_Enable(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    const bool enable = lua_toboolean(L, 2) != 0;
    if (handle == self->playerHandle_ && self->playerHandle_) {
        self->pawnEnabled_ = enable;
        return 0;
    }
    if (Entity* e = self->Find(handle)) {
        const bool wasEnabled = e->poEnabled;
        e->poEnabled = enable;
        // Snapshot the velocity before the body leaves the world. This is the
        // last moment it exists: CItem:DestroyItemFX disables the body and THEN
        // calls ExplodeItem, whose parts are meant to inherit it, so without
        // this the wreckage of a barrel a rocket just hit falls straight down.
        // ON THE TRANSITION ONLY. DestroyItemFX disables the body twice, and the
        // second call reads the already-disabled body as zero - which clobbered
        // the snapshot and left the wreckage with no inherited velocity at all.
        if (wasEnabled && !enable && self->physics_ && e->physicsBody >= 0)
            self->physics_->GetScriptBodyVelocity(e->physicsBody, e->velocity);
        if (self->physics_ && e->physicsBody >= 0) {
            self->physics_->SetScriptBodyEnabled(e->physicsBody, enable);
            // A stake that has struck home is scenery. Deactivating the body
            // only puts it to sleep, and a sleeping body still collides - so
            // the stake stayed solid where it stopped, something to bump into
            // in mid-air. Take it out of collision outright.
            //
            // Projectiles only: this is one-way, and a prop that is disabled
            // and later re-enabled has to come back solid.
            if (!enable && e->isProjectile) {
                e->bodyNonColliding = true;
                self->physics_->MakeScriptBodyNonColliding(e->physicsBody);
            }
        }
    }
    return 0;
}

// ENTITY.PO_GetPawnFloorPos(e) -> the feet; the scripts' _groundx/y/z track
// this every player tick and the proximity helpers measure from it.
int ScriptEngine::L_PO_GetPawnFloorPos(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->pawn_ && HandleArg(L, 1) == self->playerHandle_) {
        float feet[3];
        self->pawn_->FloorPos(feet);
        lua_pushnumber(L, feet[0]);
        lua_pushnumber(L, feet[1]);
        lua_pushnumber(L, feet[2]);
        return 3;
    }
    // A character body: body.y - 1.1 * bodyScale (0x10189390), which is the
    // soles. CActor keeps this as _groundx/y/z and every range test in the AI
    // - attackRange, weaponRange, CheckYLevel - measures from it.
    const Entity* e = self->Find(HandleArg(L, 1));
    float floor[3];
    if (e && self->physics_ && e->physicsBody >= 0 &&
        self->physics_->CharacterFloorPos(e->physicsBody, floor)) {
        for (int c = 0; c < 3; ++c) lua_pushnumber(L, floor[c]);
        return 3;
    }
    return L_GetPosition(L);
}

// ENTITY.GetDimensions(e) -> w,h,d, world-space - Slab plates sink by their
// own height when they open, so this must be real for the ambush barriers
// to hide.
int ScriptEngine::L_GetDimensions(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    float dims[3] = {0, 0, 0};
    if (e && self->renderer_ && e->rendererInstance >= 0)
        self->renderer_->GetScriptDimensions(e->rendererInstance, dims);
    lua_pushnumber(L, dims[0]);
    lua_pushnumber(L, dims[1]);
    lua_pushnumber(L, dims[2]);
    return 3;
}

// PLAYER.GetDistanceFromPoint(e, x, y, z) - the pickup poll: every CItem
// measures the player's distance against its takeDistance each tick, and
// OnTake fires inside that radius.
// PLAYER.GetDistanceFromPoint(e, x,y,z) - TO THE BODY'S AXIS, NOT ITS CENTRE.
//
// PhysicsObject::GetDistanceFromPoint (0x1018CF70) holds a second point at
// PhysicsObject+0x5c, projects the query onto the segment between it and the
// body position, clamps to the ends and measures to that. For the player the
// segment is the body axis, feet to head.
//
// A single point cannot stand in for it. CItem:CheckDistFromPlayers asks about
// self.Pos.Y - 1, so a coin lying on the floor asks about a point 0.9 BELOW
// the floor: against the segment the nearest end is the feet, 0.9 away and
// inside CoinG's takeDistance of 1.6; against a centre at +0.9 it is 1.8 away
// and no coin in the game could ever be picked up.
//
// The engine answers 1e7 for a handle that is not a live player - "infinitely
// far", so every distance test fails rather than passing on a zero.
int ScriptEngine::L_PLAYER_GetDistanceFromPoint(lua_State* L) {
    ScriptEngine* self = From(L);
    const float to[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                         float(luaL_optnumber(L, 4, 0))};
    float a[3], b[3];
    if (self->pawn_ && HandleArg(L, 1) == self->playerHandle_) {
        self->pawn_->FloorPos(a);
        const float* head = self->pawn_->headPos();
        for (int c = 0; c < 3; ++c) b[c] = head[c];
    } else if (const Entity* e = self->Find(HandleArg(L, 1))) {
        for (int c = 0; c < 3; ++c) a[c] = b[c] = e->pos[c];
    } else {
        lua_pushnumber(L, 1e7);
        return 1;
    }

    // Nearest point on the segment a..b, then the distance to it.
    float seg[3], rel[3];
    float len2 = 0.f, dot = 0.f;
    for (int c = 0; c < 3; ++c) {
        seg[c] = b[c] - a[c];
        rel[c] = to[c] - a[c];
        len2 += seg[c] * seg[c];
        dot += rel[c] * seg[c];
    }
    const float t = len2 > 1e-8f ? std::min(1.f, std::max(0.f, dot / len2)) : 0.f;
    float d2 = 0.f;
    for (int c = 0; c < 3; ++c) {
        const float k = to[c] - (a[c] + seg[c] * t);
        d2 += k * k;
    }
    lua_pushnumber(L, std::sqrt(d2));
    return 1;
}

int ScriptEngine::L_IsDrawEnabled(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && e->visible && e->inWorld);
    return 1;
}

// GetPlayerSpeed() -> speed, jumpStrength; SetPlayerSpeed(speed [, jump]) -
// the natives at 0x1011df50/0x1011dea0 read and write the LIVE tweak fields
// (+0xc and +0x14 of the physics engine's tweak block), which is how demon
// mode and powerups retune movement.
int ScriptEngine::L_GetPlayerSpeed(lua_State* L) {
    ScriptEngine* self = From(L);
    const Tweaks* tweaks = self->physics_ ? &self->physics_->tweaks() : nullptr;
    double speed = self->playerSpeedOverride_;
    if (speed < 0)
        speed = tweaks ? tweaks->Number("PlayerMove.PlayerSpeed", 8.0) : 8.0;
    double jump = self->jumpStrengthOverride_;
    if (jump < 0)
        jump = tweaks ? tweaks->Number("PlayerMove.JumpStrength", 1.0) : 1.0;
    lua_pushnumber(L, speed);
    lua_pushnumber(L, jump);
    return 2;
}

int ScriptEngine::L_SetPlayerSpeed(lua_State* L) {
    ScriptEngine* self = From(L);
    self->playerSpeedOverride_ = float(luaL_optnumber(L, 1, -1.0));
    if (lua_isnumber(L, 2))
        self->jumpStrengthOverride_ = float(lua_tonumber(L, 2));
    return 0;
}

// ---------------------------------------------------------------- regions

// REGION.BuildFromPoint(e, points): a trigger volume from an array of
// vectors ({X=..., Y=..., Z=...} tables). Stored as the points' AABB - the
// shipped regions are boxes and box-shaped prisms; a genuine polygon prism
// test can replace this if a level ever needs one.
int ScriptEngine::L_REGION_BuildFromPoint(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !lua_istable(L, 2)) return 0;

    bool any = false;
    for (int i = 1;; ++i) {
        lua_rawgeti(L, 2, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        float p[3];
        const char* axes[3] = {"X", "Y", "Z"};
        bool ok = true;
        for (int a = 0; a < 3; ++a) {
            lua_pushstring(L, axes[a]);
            lua_gettable(L, -2);
            ok = ok && lua_isnumber(L, -1);
            p[a] = float(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (!ok) continue;
        for (int a = 0; a < 3; ++a) {
            if (!any || p[a] < e->regionMin[a]) e->regionMin[a] = p[a];
            if (!any || p[a] > e->regionMax[a]) e->regionMax[a] = p[a];
        }
        any = true;
    }
    e->isRegion = any;
    e->playerInside = false;
    return 0;
}

void ScriptEngine::TickTriggers() {
    if (!host_ || !playerHandle_) return;
    const Entity* player = Find(playerHandle_);
    if (!player) return;

    // The test point follows the scripts' own convention: a metre above the
    // feet, the same PY+1 every IsInside caller uses.
    float at[3];
    if (pawn_) {
        pawn_->FloorPos(at);
    } else {
        for (int i = 0; i < 3; ++i) at[i] = player->pos[i];
    }
    at[1] += 1.f;

    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (!e.isRegion) continue;
        bool inside = true;
        for (int a = 0; a < 3 && inside; ++a)
            inside = at[a] >= e.pos[a] + e.regionMin[a] &&
                     at[a] <= e.pos[a] + e.regionMax[a];
        if (inside == e.playerInside) continue;
        e.playerInside = inside;
        const double args[2] = {double(kv.first), double(playerHandle_)};
        host_->PostMsg(inside ? "REGION_ENTERED" : "REGION_LEFT", args, 2);
    }
}

// The CAM reads, from the pose the game loop feeds each frame. GetAng is in
// degrees (CActor converts with -x * 3.14/180), GetAngRad in radians
// (CPlayer wraps it straight into 0..2pi).
int ScriptEngine::L_CAM_GetPos(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_pushnumber(L, self->camPos_[0]);
    lua_pushnumber(L, self->camPos_[1]);
    lua_pushnumber(L, self->camPos_[2]);
    return 3;
}

int ScriptEngine::L_CAM_GetForwardVector(lua_State* L) {
    ScriptEngine* self = From(L);
    const float cp = std::cos(self->camPitch_);
    lua_pushnumber(L, std::cos(self->camYaw_) * cp);
    lua_pushnumber(L, std::sin(self->camPitch_));
    lua_pushnumber(L, std::sin(self->camYaw_) * cp);
    return 3;
}

// Our camera yaw is measured from +X turning toward +Z. The engine's turn
// angle is not: reading the scripts' own maths back out shows their basis at
// turn a is right = (cos a, 0, -sin a), forward = (-sin a, 0, -cos a) - it
// starts down -Z and runs the opposite way round. Matching that against our
// right = (-sin yaw, 0, cos yaw) gives turn = -(yaw + pi/2), and the
// elevation passes through unchanged (both put sin(pitch) in Y).
//
// This is load-bearing, not cosmetic: CPlayer:SetupAction rebuilds the
// player's whole movement basis from CAM.GetAngRad in pure Lua, so getting
// it wrong walks the player at ninety degrees to where the camera looks.
// The conversion is its own inverse, which is what the handover in Tick2
// needs. Verified against CAM.GetForwardVector, which computes the same
// basis on the C++ side and must agree.

static float EngineTurn(float camYaw) {
    return camYaw + kPi * 0.5f;
}

static float CamYawFromTurn(float turn) {
    return turn - kPi * 0.5f;
}

// The engine's elevation runs the OTHER WAY from our pitch: positive is
// looking DOWN. That is not arbitrary - the scripts feed the elevation into
// the X slot of the engine Euler (CPlayer:SetupAction builds
// FromEuler(elevation, turn, 0)), and a positive rotation about X in a
// Y-up, Z-forward frame tilts forward toward -Y. Report our pitch without
// this and the horizontal aim is perfect while every shot goes as far wrong
// vertically as the player was looking. Its own inverse, like the turn.
static float EngineElevation(float camPitch) {
    return -camPitch;
}

// CAM.SetPos / CAM.SetAng - the scripts steering the view. A level seats the
// camera this way at load (CLevel:Synchronize while the mouse is unlocked
// pushes Lev.Pos and Lev.Ang out), and Game:Tick2 steers it this way during
// play. Angles arrive in degrees, in the engine's turn convention, so they
// come back through the same conversion - which is its own inverse.
int ScriptEngine::L_CAM_SetPos(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int i = 0; i < 3; ++i)
        self->camPos_[i] = float(luaL_optnumber(L, i + 1, self->camPos_[i]));
    self->camPoseDirty_ = true;
    return 0;
}

int ScriptEngine::L_CAM_SetAng(lua_State* L) {
    ScriptEngine* self = From(L);
    const float k = kPi / 180.f;
    self->camYaw_ = CamYawFromTurn(float(luaL_optnumber(L, 1, 0)) * k);
    self->camPitch_ = EngineElevation(float(luaL_optnumber(L, 2, 0)) * k);
    self->camPoseDirty_ = true;
    return 0;
}

bool ScriptEngine::TakeCameraPose(float pos[3], float& yaw, float& pitch) {
    if (!camPoseDirty_) return false;
    camPoseDirty_ = false;
    for (int i = 0; i < 3; ++i) pos[i] = camPos_[i] + camDisplacement_[i];
    yaw = camYaw_;
    pitch = camPitch_;
    return true;
}

int ScriptEngine::L_CAM_GetAng(lua_State* L) {
    ScriptEngine* self = From(L);
    const float k = 180.f / kPi;
    lua_pushnumber(L, EngineTurn(self->camYaw_) * k);
    lua_pushnumber(L, EngineElevation(self->camPitch_) * k);
    lua_pushnumber(L, 0);
    return 3;
}

int ScriptEngine::L_CAM_GetAngRad(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_pushnumber(L, EngineTurn(self->camYaw_));
    lua_pushnumber(L, EngineElevation(self->camPitch_));
    lua_pushnumber(L, 0);
    return 3;
}

// The head-to-camera offset the original applies when the pawn drives the
// view; zero until the crouch/land bob that feeds it exists.
int ScriptEngine::L_PLAYER_GetCameraFix(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// CAM.GetRawRotation() -> the accumulated look angles in DEGREES - Game's
// camera tick wraps them with math.mod(...,360). While the C++ loop drives
// the camera, these mirror its state and MOUSE.GetDelta reports no motion,
// so the script-side accumulation is a faithful no-op; handing the camera
// to the scripts entirely means feeding real deltas here instead.
int ScriptEngine::L_CAM_GetRawRotation(lua_State* L) {
    ScriptEngine* self = From(L);
    const float k = 180.f / kPi;
    lua_pushnumber(L, EngineTurn(self->camYaw_) * k);
    lua_pushnumber(L, EngineElevation(self->camPitch_) * k);
    return 2;
}

// MOUSE.GetDelta() -> look movement in degrees since the last call. This is
// the whole of the look input: Game:UpdateViewFromPlayer adds it onto
// CAM.GetRawRotation and writes the result back through CAM.SetAng, so the
// scripts own the view and the C++ camera follows them.
int ScriptEngine::L_MOUSE_GetDelta(lua_State* L) {
    ScriptEngine* self = From(L);
    float dx = 0.f, dy = 0.f;
    if (self->input_) self->input_->TakeLookDegrees(dx, dy);
    lua_pushnumber(L, dx);
    lua_pushnumber(L, dy);
    return 2;
}

int ScriptEngine::L_MOUSE_SetSensitivity(lua_State* L) {
    if (Input* in = From(L)->input_) in->SetSensitivity(float(luaL_optnumber(L, 1, 40)));
    return 0;
}

// CAM.SetPositionDisplacement(x, y, z) - an offset added to the camera
// position after it is set, which is how the engine shakes the view without
// disturbing where the player actually is. Held apart from camPos_ so the
// CAM.GetPos the scripts read stays the true eye position.
int ScriptEngine::L_CAM_SetPositionDisplacement(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int i = 0; i < 3; ++i)
        self->camDisplacement_[i] = float(luaL_optnumber(L, i + 1, 0));
    self->camPoseDirty_ = true;
    return 0;
}


}  // namespace painful
