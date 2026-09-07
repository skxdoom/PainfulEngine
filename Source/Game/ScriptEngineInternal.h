#pragma once

// Shared by the ScriptEngine translation units.
//
// ScriptEngine is one class split across ScriptEngine.cpp - lifecycle, the
// entity registry and the subsystem attachments - plus one Script*.cpp per
// family of natives, each declaring its own struct and its own binding table.
// The shared headers, the two small argument helpers, ScriptNativesBase and
// the family binders live here. Anything used by only one unit stays local to
// it. Docs/Reference/LuaHost.md, "The native families".

#include "ScriptEngine.h"

#include "../Assets/Dat.h"
#include "../Assets/Pkmdl.h"
#include "../Assets/Emitter.h"
#include "../Assets/Properties.h"
#include "../Assets/Skeleton.h"
#include "../Core/Debug.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include "../Core/Vectors.h"
#include "../Render/BillboardRenderer.h"
#include "../Render/EntityRenderer.h"
#include "../Render/HudRenderer.h"
#include "../Render/ParticleRenderer.h"
#include "../Render/TextureCache.h"
#include "../Audio/AudioEngine.h"
#include "PlayerPawn.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace painful {

// Entity handles are plain integers pushed as Lua numbers - scripts store them
// (self._Entity), pass them back as the first native argument, and use them as
// EntityToObject keys, all of which numbers satisfy. Zero is never handed out,
// so nil/absent arguments read as "no entity".
inline int HandleArg(lua_State* L, int idx) {
	return lua_isnumber(L, idx) ? int(lua_tonumber(L, idx)) : 0;
}

// Bone names come from the model file, the names to match come from a
// template's aiParams, and the two do not agree on case.
inline bool EqualsCI(const std::string& a, const char* b) {
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
		if (std::tolower(static_cast<unsigned char>(a[i])) !=
				std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	return i == a.size() && !b[i];
}

// The names a native body uses unqualified, which used to come from being a
// member of ScriptEngine. Each family struct derives from this; friendship is
// not inherited, so each still needs its own friend line in ScriptEngine.h.
struct ScriptNativesBase {
	using Entity = ScriptEngine::Entity;
	using EType = ScriptEngine::EType;
	using Route = ScriptEngine::Route;
	using Destructible = ScriptEngine::Destructible;
	using LimbHit = ScriptEngine::LimbHit;
	using WaterSurface = ScriptEngine::WaterSurface;
	using WorldState = ScriptEngine::WorldState;
	using enum ScriptEngine::EType;
	static ScriptEngine* From(lua_State* L);
	// Static helpers the native bodies call unqualified, forwarded for the
	// same reason as From.
	static const Entity::AnimSlot* AnimSlotArg(const Entity* e, lua_State* L, int arg);
	static int TraceCommon(lua_State* L, bool staticOnly);
	static int ResolveCurveBone(Entity::AnimSlot& slot, const SkeletonCache::Entry& skel);
	static const int kLimbHandleBase = 0x40000000;
};

// One row of a family's native table. A null module means a bare global.
struct ScriptNative {
	const char* module;
	const char* name;
	int (*fn)(lua_State*);
};

void RegisterFamily(ScriptEngine& engine, LuaHost& host, const ScriptNative* rows, size_t count);

template <size_t N>
inline void RegisterFamily(ScriptEngine& engine, LuaHost& host, const ScriptNative (&rows)[N]) {
	RegisterFamily(engine, host, rows, N);
}

// One binder per family, each defined in its own Script*.cpp beside the
// natives it registers. ScriptEngine::Bind calls them in turn; adding a native
// touches only that file, and only adding a FAMILY touches this header.
void BindDecal(ScriptEngine& engine, LuaHost& host);
void BindMenu(ScriptEngine& engine, LuaHost& host);
void BindSound(ScriptEngine& engine, LuaHost& host);
void BindEntity(ScriptEngine& engine, LuaHost& host);
void BindPlayer(ScriptEngine& engine, LuaHost& host);
void BindHud(ScriptEngine& engine, LuaHost& host);
void BindDeath(ScriptEngine& engine, LuaHost& host);
void BindAnim(ScriptEngine& engine, LuaHost& host);
void BindWorld(ScriptEngine& engine, LuaHost& host);
void BindInput(ScriptEngine& engine, LuaHost& host);
void BindTrace(ScriptEngine& engine, LuaHost& host);
void BindConsole(ScriptEngine& engine, LuaHost& host);
void BindLimbs(ScriptEngine& engine, LuaHost& host);
void BindCollision(ScriptEngine& engine, LuaHost& host);
void BindExplosion(ScriptEngine& engine, LuaHost& host);
void BindSave(ScriptEngine& engine, LuaHost& host);
void BindWater(ScriptEngine& engine, LuaHost& host);

} // namespace painful
