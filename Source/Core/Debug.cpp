#include "Debug.h"

#include <cstdlib>
#include <cstring>

#include "Check.h"
#include "Log.h"
#include <string>

namespace painful {

namespace {

enum class Kind { kFlag, kInt, kFloat, kText };

struct Row {
	const char* name;
	Kind kind;
	const char* help;
	const char* value = nullptr; // filled on first access; null when unset
};

// Every PAINFUL_* switch the engine reads. Grouped the way the reports are.
// PAINFUL_CHECK_BREAK and PAINFUL_LOG are listed but read directly where they
// are used: Check and Log cannot call back into here without the three
// initialising each other.
Row g_rows[] = {
	// Run mode
	{"PAINFUL_HIDDEN", Kind::kFlag, "offscreen window, and silence - nobody is watching"},
	{"PAINFUL_WINDOWED", Kind::kText, "non-zero forces a window, whatever Cfg.Fullscreen says"},
	{"PAINFUL_DEV", Kind::kFlag, "developer build: the overlay, the F1-F4 toggles, noclip, debugMarek"},
	{"PAINFUL_QUIET", Kind::kFlag, "with -dev, drop the debug text"},
	{"PAINFUL_RES", Kind::kText, "override the window size, \"1920x1080\""},
	{"PAINFUL_RENDERER", Kind::kText, "bgfx backend: vulkan, d3d11, d3d12, opengl"},
	{"PAINFUL_SHOT_FRAME", Kind::kInt, "capture --shot at this frame instead of the default"},
	{"PAINFUL_AUDIO", Kind::kFlag, "attach the mixer in the headless reports"},
	{"PAINFUL_REALTIME", Kind::kFlag, "the lua report ticks against the clock, not a fixed step"},
	{"PAINFUL_CHECK_BREAK", Kind::kFlag, "trap at the first failed PAINFUL_CHECK"},
	{"PAINFUL_LOG", Kind::kText, "log level: warn, info, or trace (default trace)"},

	// Render
	{"PAINFUL_ECULL", Kind::kInt, "entity cull mode: 1 normal, 2 off (tells winding from missing geometry)"},
	{"PAINFUL_NOATEST", Kind::kFlag, "drop the alpha test on entities"},
	{"PAINFUL_SHADOWMAP", Kind::kInt, "flashlight shadow map size in texels; 0 turns both shadow maps off"},
	{"PAINFUL_SHADOWVIEW", Kind::kInt, "draw the shadow terms alone: 1 all of them, 2 the placed lights' only on the models"},
	{"PAINFUL_BLOOM", Kind::kInt, "0 turns the bloom post-process off (Render/Bloom.h)"},
	{"PAINFUL_SPECULAR", Kind::kText, "specular colour override, \"r,g,b\""},
	{"PAINFUL_SKYLAYER", Kind::kInt, "draw only this sky layer"},
	{"PAINFUL_NEAR", Kind::kFloat, "camera near plane"},
	{"PAINFUL_WIRE", Kind::kInt, "1 geometry wireframe, 2 collision wireframe"},
	{"PAINFUL_NAMEPLATES", Kind::kFlag, "entity name labels in the world"},

	// Physics and the player
	{"PAINFUL_CHAR_FRICTION",Kind::kFloat, "character body friction (default 0.1; higher stops stairs)"},
	{"PAINFUL_CHAR_TRACE", Kind::kFlag, "log a character the step moved unexpectedly"},
	{"PAINFUL_CHAR_TRACE_MIN",Kind::kFloat,"the velocity change CHAR_TRACE reports, units/s"},
	{"PAINFUL_PAWN_TRACE", Kind::kFlag, "log the player mover's steps and kicks"},
	{"PAINFUL_JUMPSCALE", Kind::kFloat, "jump strength multiplier (default 1.16)"},
	{"PAINFUL_WALK_FACTOR", Kind::kFloat, "slope walk factor (default 0.4)"},
	{"PAINFUL_PLAYER_AT", Kind::kText, "spawn the player at \"x,y,z\""},
	{"PAINFUL_NOAI", Kind::kFlag, "monsters do not think"},

	// Script and entity tracing
	{"PAINFUL_ANIM_TRACE", Kind::kFlag, "log animation slot changes"},
	{"PAINFUL_CONTACT_TRACE",Kind::kFlag, "log script body and ragdoll contacts"},
	{"PAINFUL_DECAL_TRACE", Kind::kFlag, "log what each decal spawn cut"},
	{"PAINFUL_VIEW_TRACE", Kind::kFlag, "log the view traces the weapons fire"},
	{"PAINFUL_MONSTER_TRACE",Kind::kInt, "dump monster ground state at this frame"},
	{"PAINFUL_ACTIVE_TRACE", Kind::kInt, "dump active-mesh drift at this frame"},
	{"PAINFUL_RAGDOLL_DEBUG",Kind::kFlag, "check the ragdoll pose round-trips through the solver"},

	// Menu
	{"PAINFUL_MAP_CURSOR", Kind::kInt, "move the map screen cursor to this index"},
	{"PAINFUL_MAP_PICK", Kind::kText, "select this level directory on the map screen"},
};

const char* const kKindName[] = {"flag", "int", "float", "text"};

// One pass over the environment, so DebugList and DebugActive can report
// without the accessors having been called.
void ReadOnce() {
	static bool done = false;
	if (done) return;
	done = true;
	for (Row& r : g_rows) r.value = std::getenv(r.name);
}

Row* Find(const char* name, Kind kind) {
	ReadOnce();
	for (Row& r : g_rows)
		if (std::strcmp(r.name, name) == 0) {
			if (!PAINFUL_CHECK(r.kind == kind, "%s is a %s, read as a %s", name,
					kKindName[int(r.kind)], kKindName[int(kind)]))
				return nullptr;
			return &r;
		}
	PAINFUL_CHECK(false, "%s is not in the switch table (Core/Debug.cpp)", name);
	return nullptr;
}

} // namespace

bool DebugFlag(const char* name) {
	const Row* r = Find(name, Kind::kFlag);
	return r && r->value;
}

int DebugInt(const char* name, int fallback) {
	const Row* r = Find(name, Kind::kInt);
	return r && r->value ? std::atoi(r->value) : fallback;
}

float DebugFloat(const char* name, float fallback) {
	const Row* r = Find(name, Kind::kFloat);
	return r && r->value ? float(std::atof(r->value)) : fallback;
}

const char* DebugText(const char* name) {
	const Row* r = Find(name, Kind::kText);
	return r ? r->value : nullptr;
}

void DebugList() {
	ReadOnce();
	LogInfo("PAINFUL_* diagnostic switches (set in the environment, read at startup)");
	for (const Row& r : g_rows) {
		if (r.value)
			LogInfo("  %-24s %-5s = %-12s %s", r.name, kKindName[int(r.kind)], r.value, r.help);
		else
			LogInfo("  %-24s %-5s %18s %s", r.name, kKindName[int(r.kind)], "", r.help);
	}
}

std::string DebugActive() {
	ReadOnce();
	std::string out;
	for (const Row& r : g_rows) {
		if (!r.value) continue;
		if (!out.empty()) out += " ";
		out += r.name;
		if (r.kind != Kind::kFlag) out += std::string("=") + r.value;
	}
	return out;
}

} // namespace painful
