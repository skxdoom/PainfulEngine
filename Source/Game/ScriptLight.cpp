// ScriptEngine: the LIGHT.* family - the lights the scripts make at runtime.
//
// The flashlight, the torch a Leper carries, the flash an action fires off:
// each is an ETypes.Light entity whose properties these natives write. The
// level's own CLights come through the same door, because CLight:Apply is what
// places them. Docs/Reference/Lighting.md

#include "ScriptEngineInternal.h"
#include "../World/Lighting.h"

namespace painful {

struct LightNatives : ScriptNativesBase {
	static int L_LIGHT_Setup(lua_State* L);
	static int L_LIGHT_SetFalloff(lua_State* L);
	static int L_LIGHT_SetIntensity(lua_State* L);
	static int L_LIGHT_SetDynamicFlag(lua_State* L);
	static int L_LIGHT_SetFakeSpecularFlag(lua_State* L);
	static int L_LIGHT_SetLitParentFlag(lua_State* L);
	static int L_LIGHT_SetImportant(lua_State* L);
	static int L_LIGHT_SetProjector(lua_State* L);
	static int L_ENVIRONMENT_RemoveLight(lua_State* L);
	static int L_ENVIRONMENT_RemoveLights(lua_State* L);
	// The light an entity IS. Every LIGHT.* native but Setup acts on one that
	// already exists, and Setup is what makes it: CLight:Apply calls Setup
	// first and the rest in a row after it, and CreateLight does the same.
	static Entity* LightArg(lua_State* L, bool create = false) {
		ScriptEngine* self = From(L);
		Entity* e = self->Find(HandleArg(L, 1));
		if (!e) return nullptr;
		if (!e->hasLight && !create) return nullptr;
		e->hasLight = true;
		return e;
	}
};

namespace {

// LIGHT.SetDynamicFlag(e), SetFakeSpecularFlag(e), SetLitParentFlag(e),
// SetImportant(e) all read their second argument with Script::GetBool(2, TRUE),
// so a bare call turns the flag ON - which is how CreateLight's
// LIGHT.SetDynamicFlag(e) with one argument works.
bool FlagArg(lua_State* L) { return lua_isnone(L, 2) || lua_toboolean(L, 2) != 0; }

} // namespace

// LIGHT.Setup(e, type, colour, dx, dy, dz, intensity) - 0x101375F0.
//
// Type is Light::SetType (1 directional, 2 point, 3 spot), the colour is one
// packed D3D ARGB int as R3D.RGBA composes it, and the direction is a world
// vector - PlayerLight passes CAM.GetForwardVector() every tick, which is the
// whole of "the flashlight points where you look". The eighth argument some
// call sites pass is not read by the engine.
int LightNatives::L_LIGHT_Setup(lua_State* L) {
	Entity* e = LightArg(L, true);
	if (!e) return 0;
	LightSource& l = e->light;
	l.type = int(luaL_optnumber(L, 2, 0));
	// 0xAARRGGBB, the layout R3D.RGBA builds and Light::GetDirAndAttCol reads
	// back a byte at a time.
	const uint32_t argb = uint32_t(int64_t(luaL_optnumber(L, 3, 0)));
	l.color[0] = float((argb >> 16) & 0xff) / 255.f;
	l.color[1] = float((argb >> 8) & 0xff) / 255.f;
	l.color[2] = float(argb & 0xff) / 255.f;
	for (int c = 0; c < 3; ++c) l.dir[c] = float(luaL_optnumber(L, 4 + c, 0));
	if (l.dir.LengthSq() > 1e-12f) l.dir /= l.dir.Length();
	l.intensity = float(luaL_optnumber(L, 7, 0));
	return 0;
}

// LIGHT.SetFalloff(e, startFalloff, range, coneAngle) - 0x10137720. Note the
// argument order against the storage order: SetFalloff takes the START, then
// SetRadius the range. The cone angle gives both cone cosines; see SetCone.
int LightNatives::L_LIGHT_SetFalloff(lua_State* L) {
	Entity* e = LightArg(L);
	if (!e) return 0;
	e->light.startFalloff = float(luaL_optnumber(L, 2, 0));
	e->light.range = float(luaL_optnumber(L, 3, 0));
	SetCone(e->light, float(luaL_optnumber(L, 4, 0)));
	return 0;
}

// LIGHT.SetIntensity(e, v) - 0x10137820, the same field Setup's seventh
// argument writes. A torch flickers with this and PFadeInOutLight fades a
// flash out with it.
int LightNatives::L_LIGHT_SetIntensity(lua_State* L) {
	Entity* e = LightArg(L);
	if (!e) return 0;
	e->light.intensity = float(luaL_optnumber(L, 2, 0));
	return 0;
}

// Light::EnableDynamic (0x101d5f00) - flag 0x400000, and a place in the
// world's dynamic-light list. That list is what WorldMesh::Draw walks for its
// additive light passes, so this flag is what decides whether a light reaches
// the WALLS as well as the models standing against them.
int LightNatives::L_LIGHT_SetDynamicFlag(lua_State* L) {
	Entity* e = LightArg(L);
	if (!e) return 0;
	e->light.dynamic = FlagArg(L);
	return 0;
}

int LightNatives::L_LIGHT_SetFakeSpecularFlag(lua_State* L) {
	Entity* e = LightArg(L);
	if (!e) return 0;
	e->light.fakeSpecular = FlagArg(L);
	return 0;
}

// Light::SetImportantDynamic (0x101d4260) - keeps the world pass even with the
// dynamic-light video option off. PlayerLight is the one template that sets it.
int LightNatives::L_LIGHT_SetImportant(lua_State* L) {
	Entity* e = LightArg(L);
	if (!e) return 0;
	e->light.important = FlagArg(L);
	return 0;
}

// LIGHT.SetLitParentFlag - bit 0x80 at Entity+0x1a (0x10137a20). Whether the
// entity this light hangs off is lit by it; nothing here reads it yet, so it
// is recorded and not acted on.
int LightNatives::L_LIGHT_SetLitParentFlag(lua_State* L) {
	LightArg(L);
	return 0;
}

// LIGHT.SetProjector(e, material) - Light::SetProjectorTexture (0x101d4ce0).
// A cookie projected down a spot's axis. "special/flashlight" on PlayerLight
// is the only one the shipped data uses.
int LightNatives::L_LIGHT_SetProjector(lua_State* L) {
	Entity* e = LightArg(L);
	if (!e) return 0;
	const char* name = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
	e->light.projector = name ? name : "";
	return 0;
}

// ENVIRONMENT.RemoveLight / RemoveLights - CEnvironment:SetDependentLights
// clears its list and re-adds, and only a level that authored DependentLights
// reaches either. Recorded as a no-op: the CEnvironment boxes here overwrite
// ambient and the directional, which is what the property list drives, and
// nothing hangs a light off a box.
int LightNatives::L_ENVIRONMENT_RemoveLight(lua_State*) { return 0; }
int LightNatives::L_ENVIRONMENT_RemoveLights(lua_State*) { return 0; }

// Every light, where its entity is now.
//
// Type 0 is OFF, not a fourth kind of light: CLight's class default is 0 and
// PlayerLight toggles between 0 and 3 to switch the flashlight, so a type the
// engine does not recognise must light nothing. Intensity or range at zero is
// the same answer by another route - a flash spends most of its fade there.
void ScriptEngine::CollectLights(std::vector<LightSource>& out) const {
	out.clear();
	for (const auto& kv : entities_) {
		const Entity& e = kv.second;
		if (!e.hasLight || !e.visible || !e.inWorld) continue;
		if (e.light.type < LightSource::kDirectional || e.light.type > LightSource::kSpot)
			continue;
		if (e.light.intensity <= 0.f) continue;
		if (e.light.type != LightSource::kDirectional && e.light.range <= 0.f) continue;
		out.push_back(e.light);
		out.back().pos = e.pos;
	}
}

void BindLight(ScriptEngine& engine, LuaHost& host) {
	const ScriptNative natives[] = {
		{"LIGHT", "Setup", LightNatives::L_LIGHT_Setup},
		{"LIGHT", "SetFalloff", LightNatives::L_LIGHT_SetFalloff},
		{"LIGHT", "SetIntensity", LightNatives::L_LIGHT_SetIntensity},
		{"LIGHT", "SetDynamicFlag", LightNatives::L_LIGHT_SetDynamicFlag},
		{"LIGHT", "SetFakeSpecularFlag", LightNatives::L_LIGHT_SetFakeSpecularFlag},
		{"LIGHT", "SetLitParentFlag", LightNatives::L_LIGHT_SetLitParentFlag},
		{"LIGHT", "SetImportant", LightNatives::L_LIGHT_SetImportant},
		{"LIGHT", "SetProjector", LightNatives::L_LIGHT_SetProjector},
		{"ENVIRONMENT", "RemoveLight", LightNatives::L_ENVIRONMENT_RemoveLight},
		{"ENVIRONMENT", "RemoveLights", LightNatives::L_ENVIRONMENT_RemoveLights},
	};
	RegisterFamily(engine, host, natives);
}

} // namespace painful
