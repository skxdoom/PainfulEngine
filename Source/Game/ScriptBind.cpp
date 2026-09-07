// ScriptEngine: binding, one call per native family.
//
// Every native used to be declared in ScriptEngine.h and listed in one table
// here, so adding one recompiled all twenty Script*.cpp and everything above
// them. Each family now declares its own struct and keeps its own table in its
// own translation unit; this file only calls them, and only ADDING A FAMILY
// touches the header.

#include "ScriptEngineInternal.h"

namespace painful {

// The family structs are friends of ScriptEngine; friendship is not inherited,
// so the base hands out no private access of its own - it forwards only the
// class-scope names the native bodies used when they were members.
ScriptEngine* ScriptNativesBase::From(lua_State* L) { return ScriptEngine::From(L); }

const ScriptEngine::Entity::AnimSlot* ScriptNativesBase::AnimSlotArg(const Entity* e, lua_State* L,
		int arg) {
	return ScriptEngine::AnimSlotArg(e, L, arg);
}

int ScriptNativesBase::TraceCommon(lua_State* L, bool staticOnly) {
	return ScriptEngine::TraceCommon(L, staticOnly);
}

int ScriptNativesBase::ResolveCurveBone(Entity::AnimSlot& slot, const SkeletonCache::Entry& skel) {
	return ScriptEngine::ResolveCurveBone(slot, skel);
}

void RegisterFamily(ScriptEngine& engine, LuaHost& host, const ScriptNative* rows, size_t count) {
	for (size_t i = 0; i < count; ++i)
		host.RegisterNative(rows[i].module, rows[i].name, rows[i].fn, &engine);
}

void ScriptEngine::Bind(LuaHost& host) {
	host_ = &host;
	BindEntity(*this, host);
	BindPlayer(*this, host);
	BindWorld(*this, host);
	BindAnim(*this, host);
	BindSound(*this, host);
	BindHud(*this, host);
	BindMenu(*this, host);
	BindConsole(*this, host);
	BindInput(*this, host);
	BindTrace(*this, host);
	BindCollision(*this, host);
	BindExplosion(*this, host);
	BindDeath(*this, host);
	BindLimbs(*this, host);
	BindDecal(*this, host);
	BindWater(*this, host);
	BindSave(*this, host);
}

} // namespace painful
