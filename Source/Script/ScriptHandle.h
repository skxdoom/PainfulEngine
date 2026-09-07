#pragma once
#include <cstdint>

extern "C" {
#include <lua.h>
}

// Opaque handles the scripts hold and hand back.
//
// The original hands back pointers the scripts treat as cookies; we answer with
// small integers. As bare lightuserdata every kind shared one namespace, so a
// material reaching FS.UnregisterPack was indistinguishable from a pack. The
// kind is tagged into the value; it stays lightuserdata, so scripts can still
// key tables by it.
namespace painful {

enum class HandleKind : uintptr_t {
	kMaterial = 1,
	kPack = 2,
};

inline void PushHandle(lua_State* L, HandleKind kind, int handle) {
	if (handle <= 0) {
		lua_pushnil(L);
		return;
	}
	const uintptr_t tagged = (static_cast<uintptr_t>(kind) << 24) | uintptr_t(handle);
	lua_pushlightuserdata(L, reinterpret_cast<void*>(tagged));
}

// The handle, or 0 when the argument is absent, nil, or a handle of another
// kind. Scripts pass a literal 0 for "none", which arrives as a number.
inline int ToHandle(lua_State* L, int index, HandleKind kind) {
	if (!lua_islightuserdata(L, index)) return 0;
	const uintptr_t tagged = reinterpret_cast<uintptr_t>(lua_touserdata(L, index));
	if ((tagged >> 24) != static_cast<uintptr_t>(kind)) return 0;
	return int(tagged & 0xFFFFFF);
}

} // namespace painful
