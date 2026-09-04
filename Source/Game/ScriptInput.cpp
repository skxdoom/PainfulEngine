// ScriptEngine: the INP / MOUSE natives and the action state machine.

#include "ScriptEngineInternal.h"

namespace painful {

// ----------------------------------------------------------------- input
//
// The player's controls are a SCRIPT path in this engine, and these natives
// are its two ends. CPlayer:Tick reads INP.GetActionStatus into an Actions
// bitmask, overrides bits of it (weapon select, switched fire, rocket jump),
// stores it with ENTITY.PO_SetAction, and then calls PLAYER.ExecAction - and
// ExecAction is the entry to PhysicsObject::PlayerAction, the mover. Nothing
// here decides how the player moves; the mask decides, and PlayerPawn runs it.

// INP.GetActionStatus(e) -> the pressed-actions bitmask. With no input
// attached this is zero, which reads as a standing player rather than an
// error.
int ScriptEngine::L_INP_GetActionStatus(lua_State* L) {
    const ScriptEngine* self = From(L);
    lua_pushnumber(L, self->input_ ? double(self->input_->ActionMask()) : 0.0);
    return 1;
}

// INP.Action(mask) / INP.UIAction(mask) -> is that action pressed. The
// scripts pass one Actions.* constant at a time.
int ScriptEngine::L_INP_Action(lua_State* L) {
    const ScriptEngine* self = From(L);
    const uint32_t mask = uint32_t(luaL_optnumber(L, 1, 0));
    lua_pushboolean(L, self->input_ && self->input_->Action(mask));
    return 1;
}

// INP.RemoveUIAction(mask) - consume a UI action so it does not fire again
// while its key stays down. GameMP.lua needs it either side of the scoreboard
// toggle. Docs/Reference/PlayerMovement.md
int ScriptEngine::L_INP_RemoveUIAction(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->input_) self->input_->RemoveUIAction(uint32_t(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_INP_UIAction(lua_State* L) {
    const ScriptEngine* self = From(L);
    const uint32_t mask = uint32_t(luaL_optnumber(L, 1, 0));
    lua_pushboolean(L, self->input_ && self->input_->UIAction(mask));
    return 1;
}

// INP.Key(vk) -> 0 up, 1 pressed this frame, 2 held. Tri-state, not boolean:
// Game.lua pairs `==1` for a toggle with `==2` for a modifier held alongside
// it, so collapsing this to a bool breaks both halves.
int ScriptEngine::L_INP_Key(lua_State* L) {
    const ScriptEngine* self = From(L);
    const int vk = int(luaL_optnumber(L, 1, 0));
    lua_pushnumber(L, self->input_ ? self->input_->KeyState(vk) : 0);
    return 1;
}

int ScriptEngine::L_INP_IsFireSwitched(lua_State* L) {
    const ScriptEngine* self = From(L);
    lua_pushboolean(L, self->input_ && self->input_->fireSwitched());
    return 1;
}

// INP.LoadBindings() - the bindings live in the scripts' own Cfg table
// (Cfg.KeyPrimary<Action> / Cfg.KeyAlternative<Action>, holding engine key
// names), which Cfg.lua fills from its defaults and then config.ini. So this
// reads them straight back out of the Lua state; the options menu calls it
// again after a rebind.
int ScriptEngine::L_INP_LoadBindings(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->input_) return 0;
    self->input_->LoadBindings(
        [](void* ctx, const char* field) -> std::string {
            lua_State* S = static_cast<lua_State*>(ctx);
            lua_getglobal(S, "Cfg");
            if (!lua_istable(S, -1)) {
                lua_pop(S, 1);
                return {};
            }
            lua_pushstring(S, field);
            lua_gettable(S, -2);
            std::string value;
            if (lua_isstring(S, -1)) value = lua_tostring(S, -1);
            lua_pop(S, 2);
            return value;
        },
        L);
    return 0;
}

int ScriptEngine::L_INP_Reset(lua_State* L) {
    if (Input* in = From(L)->input_) in->Reset();
    return 0;
}

// ENTITY.PO_SetAction(e, mask) / PO_AddAction(e, mask) - the action bitmask
// on the physics object (PlayerAction reads it from this+0x78). SetAction
// replaces, AddAction ORs.
int ScriptEngine::L_PO_SetAction(lua_State* L) {
    if (Entity* e = From(L)->Find(HandleArg(L, 1)))
        e->action = uint32_t(luaL_optnumber(L, 2, 0));
    return 0;
}

int ScriptEngine::L_PO_AddAction(lua_State* L) {
    if (Entity* e = From(L)->Find(HandleArg(L, 1)))
        e->action |= uint32_t(luaL_optnumber(L, 2, 0));
    return 0;
}

// ENTITY.PO_IsActionState(e, mask) - is that bit set in the stored action.
// The weapon code reads its own fire bits back out this way, so it answers
// over the whole mask, not just the five bits the mover consumes.
int ScriptEngine::L_PO_IsActionState(lua_State* L) {
    const Entity* e = From(L)->Find(HandleArg(L, 1));
    const uint32_t mask = uint32_t(luaL_optnumber(L, 2, 0));
    lua_pushboolean(L, e && (e->action & mask) != 0);
    return 1;
}

// ENTITY.PO_JumpedInLastAction(e) - whether the last mover step left the
// ground, which the scripts use to gate landing behaviour.
int ScriptEngine::L_PO_JumpedInLastAction(lua_State* L) {
    const Entity* e = From(L)->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && e->jumpedLastAction);
    return 1;
}

// PLAYER.ExecAction(e, 0, fx,fy,fz, rx,ry,rz) - run the mover for one frame.
// The two vectors are the camera basis; PlayerAction takes them as Vector&
// param_1 and param_2 and builds the ground direction from the RIGHT one
// alone. Only the player has a pawn, so this is a no-op for anything else.
int ScriptEngine::L_PLAYER_ExecAction(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    Entity* e = self->Find(handle);
    if (!e || !self->pawn_ || handle != self->playerHandle_ || !self->pawnEnabled_)
        return 0;
    if (!self->physics_) return 0;

    const float right[3] = {float(luaL_optnumber(L, 6, 0)),
                            float(luaL_optnumber(L, 7, 0)),
                            float(luaL_optnumber(L, 8, 0))};
    self->pawn_->Move(*self->physics_, self->physics_->tweaks(), e->action, right,
                      self->frameDelta_);
    e->jumpedLastAction = self->pawn_->jumpedLastMove();
    self->SyncPlayerFromPawn();
    return 0;
}

// PLAYER.FloorCheck(e) - is the player standing on something. CPlayer gates
// the rocket jump on it, and the camera code on whether to bob.
int ScriptEngine::L_PLAYER_FloorCheck(lua_State* L) {
    const ScriptEngine* self = From(L);
    lua_pushboolean(L, self->pawn_ && HandleArg(L, 1) == self->playerHandle_ &&
                           self->pawn_->onGround());
    return 1;
}

int ScriptEngine::L_MOUSE_Lock(lua_State* L) {
    From(L)->mouseLocked_ = lua_toboolean(L, 1) != 0;
    return 0;
}

int ScriptEngine::L_MOUSE_IsLocked(lua_State* L) {
    lua_pushboolean(L, From(L)->mouseLocked_);
    return 1;
}


}  // namespace painful
