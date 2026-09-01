#pragma once

// Shared by the ScriptEngine translation units.
//
// ScriptEngine is one class split across ScriptEngine.cpp - lifecycle, the
// entity registry and the subsystem attachments - plus one Script*.cpp per
// family of natives. They all need the same headers and the same two small
// helpers, so both live here rather than being repeated fourteen times.
// Anything used by only one unit stays file-local to that unit.

#include "ScriptEngine.h"

#include "../Assets/Dat.h"
#include "../Assets/Pkmdl.h"
#include "../Assets/Emitter.h"
#include "../Assets/Properties.h"
#include "../Assets/Skeleton.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
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

}  // namespace painful
