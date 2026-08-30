#include "LuaHost.h"

#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// The native surface the shipped scripts call. Module tables of plain
// functions taking an entity handle first (ENTITY.SetVelocity(e, x, y, z)),
// plus bare globals - the same shape Engine.dll registers. The full function
// list is generated into NativeList.inc from Docs/Engine_LuaAPI.md; module
// names were recovered by usage vote over the scripts (see
// Tools/GenNativeList.ps1).
//
// Everything unimplemented is an instrumented stub via LuaHost::RecordNativeCall.
// The stubs return no values: nil is falsy, which makes the Is*/Get* family
// read as "no" by default, and any call site that genuinely needs a value
// errors loudly - which is the signal to implement that native next.

namespace painful {

namespace {

// ---------------------------------------------------------------- stubs

int NativeStub(lua_State* L) {
    LuaHost::FromState(L)->RecordNativeCall(lua_tostring(L, lua_upvalueindex(1)), L);
    return 0;
}

void PushStub(lua_State* L, const char* fullName) {
    lua_pushstring(L, fullName);
    lua_pushcclosure(L, NativeStub, 1);
}

// __index for module tables: any name the generated list missed still gets a
// logging stub on first access (and is cached in the table), so a mismapped
// or unlisted native shows up in the report instead of as "call to nil".
int ModuleAutoIndex(lua_State* L) {
    const char* module = lua_tostring(L, lua_upvalueindex(1));
    const char* key = luaL_checkstring(L, 2);
    const std::string full = std::string(module) + "." + key;
    PushStub(L, full.c_str());
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -2);
    lua_rawset(L, 1);          // cache: module[key] = stub
    return 1;
}

// Returns with the module table on the stack, creating it (with the
// auto-stub metatable) on first sight.
void GetOrCreateModule(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    if (lua_istable(L, -1)) return;
    lua_pop(L, 1);

    lua_newtable(L);
    lua_newtable(L);                       // metatable
    lua_pushstring(L, "__index");
    lua_pushstring(L, name);
    lua_pushcclosure(L, ModuleAutoIndex, 1);
    lua_settable(L, -3);
    lua_setmetatable(L, -2);

    lua_pushvalue(L, -1);
    lua_setglobal(L, name);
}

// ---------------------------------------------------------------- helpers

double Arg(lua_State* L, int i) { return luaL_checknumber(L, i); }

uint32_t ArgU32(lua_State* L, int i) {
    return static_cast<uint32_t>(static_cast<int64_t>(luaL_checknumber(L, i)));
}

struct Quat { double w, x, y, z; };

Quat QuatMul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

int PushQuat(lua_State* L, const Quat& q) {
    lua_pushnumber(L, q.w);
    lua_pushnumber(L, q.x);
    lua_pushnumber(L, q.y);
    lua_pushnumber(L, q.z);
    return 4;
}

// ---------------------------------------------------------------- files

// DoFile(path [, required]) - the engine's script loader. The second
// argument is false for the optional .editor/.xbox probes in Loader.lua.
int L_DoFile(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    const bool required = !(lua_isboolean(L, 2) && !lua_toboolean(L, 2));
    LuaHost::FromState(L)->DoFile(path, required);
    return 0;
}

int L_DoString(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    if (luaL_loadbuffer(L, s, len, "@DoString") != 0 || lua_pcall(L, 0, 0, 0) != 0) {
        LogWarn("DoString error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return 0;
}

// VFS-aware replacements for the base library's loadfile/dofile - Loader.lua
// probes for an optional local.lua through loadfile, and files may live
// inside the archives.
int L_loadfile(lua_State* L) {
    LuaHost* host = LuaHost::FromState(L);
    const std::string path = host->ResolvePath(luaL_checkstring(L, 1));

    std::vector<uint8_t> bytes;
    if (!FileSystem::Get().Exists(path) || !ReadFile(path, bytes)) {
        lua_pushnil(L);
        lua_pushfstring(L, "cannot open %s", path.c_str());
        return 2;
    }
    const std::string chunkName = "@" + path;
    if (luaL_loadbuffer(L, reinterpret_cast<const char*>(bytes.data()), bytes.size(),
                        chunkName.c_str()) != 0) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

int L_dofile(lua_State* L) {
    const int base = lua_gettop(L);
    lua_pushcfunction(L, L_loadfile);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 2);
    if (lua_isnil(L, -2)) {
        lua_remove(L, -2);
        return lua_error(L);          // message is on top
    }
    lua_pop(L, 1);                    // drop the nil message slot
    lua_call(L, 0, LUA_MULTRET);
    return lua_gettop(L) - base;
}

// ---------------------------------------------------------------- misc

int L_Log(lua_State* L) {
    const char* s = lua_tostring(L, 1);   // converts numbers too
    LogInfo("[lua] %s", s ? s : "");
    return 0;
}

int L_MsgBox(lua_State* L) {
    const char* s = lua_tostring(L, 1);
    LogWarn("[lua msgbox] %s", s ? s : "");
    return 0;
}

int L_Exit(lua_State* L) {
    LuaHost::FromState(L)->RequestQuit();
    return 0;
}

int L_NoOp(lua_State*) { return 0; }      // the luaProfiler_* family

int L_True(lua_State* L)  { lua_pushboolean(L, 1); return 1; }
int L_False(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// Game:Init refuses to run unless BOTH version strings are exactly "1.4" -
// the internal engine/exe version pair, not the marketing patch number.
// GetPainkillerVersionString is registered by Painkiller.exe rather than
// Engine.dll, which is why the recovered native list does not carry it.
int L_GetEngineVersionString(lua_State* L) {
    lua_pushstring(L, "1.4");
    return 1;
}

int L_GetGCCount(lua_State* L) {
    lua_pushnumber(L, lua_getgccount(L));
    return 1;
}

// Debug decoration in Game:Print calls; concatenated into strings, so it
// must return a string, never nil.
int L_GetCallStackInfo(lua_State* L) {
    lua_pushstring(L, "");
    return 1;
}

// The CD check family - always satisfied.
int L_LabelOk(lua_State* L) { lua_pushboolean(L, 1); return 1; }
int L_GetCDLetter(lua_State* L) { lua_pushstring(L, ""); return 1; }
int L_GetCDLabel(lua_State* L) { lua_pushstring(L, ""); return 1; }
int L_GetDriveLetter(lua_State* L) { lua_pushstring(L, "C:"); return 1; }

// ---------------------------------------------------------------- bit flags

int L_AddBitFlag(lua_State* L) {
    lua_pushnumber(L, ArgU32(L, 1) | ArgU32(L, 2));
    return 1;
}
int L_RemoveBitFlag(lua_State* L) {
    lua_pushnumber(L, ArgU32(L, 1) & ~ArgU32(L, 2));
    return 1;
}
int L_IsBitFlag(lua_State* L) {
    lua_pushboolean(L, (ArgU32(L, 1) & ArgU32(L, 2)) != 0);
    return 1;
}
int L_ReplaceBitFlag(lua_State* L) {
    lua_pushnumber(L, (ArgU32(L, 1) & ~ArgU32(L, 2)) | ArgU32(L, 3));
    return 1;
}

// ---------------------------------------------------------------- rotation

// Engine quaternions are (w, x, y, z) - confirmed in Engine.dll (the Havok
// bridge stores hkQuaternion (x,y,z,w) into engine order). The scripts pass
// them as flat multi-values: EulerToQuat(ax,ay,az) -> w,x,y,z.
//
// The composition order is the engine's own, read out of the native behind
// 0x1011C390 (the work is in FUN_1011bea0). With half-angles it builds
//
//   w = cz*cy*cx + sx*sz*sy      x = cz*cy*sx - sz*sy*cx
//   y = sy*cz*cx + sz*sx*cy      z = sz*cy*cx - sy*sx*cz
//
// which is exactly qz * qy * qx, output as [w,x,y,z]. So the rotation is
// Rz*Ry*Rx - X applied first. This used to compose qx*qy*qz, the reverse,
// and a reversed quaternion product is a different rotation, not the inverse
// of one; every scripted Euler rotation was wrong away from the axes.

Quat QuatFromAxisAngle(double angle, double x, double y, double z) {
    const double len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-12) return {1, 0, 0, 0};
    const double s = std::sin(angle * 0.5) / len;
    return {std::cos(angle * 0.5), x * s, y * s, z * s};
}

int L_EulerToQuat(lua_State* L) {
    float q[4];
    EngineEulerToQuat(float(Arg(L, 1)), float(Arg(L, 2)), float(Arg(L, 3)), q);
    return PushQuat(L, {q[0], q[1], q[2], q[3]});
}

int L_QuatToEuler(lua_State* L) {
    const double w = Arg(L, 1), x = Arg(L, 2), y = Arg(L, 3), z = Arg(L, 4);
    // Inverse of the Rz*Ry*Rx composition above, read off that matrix:
    // -R20 gives Y, R21/R22 give X and R10/R00 give Z.
    const double sy = 2 * (w * y - x * z);
    const double ay = std::asin(sy < -1 ? -1.0 : (sy > 1 ? 1.0 : sy));
    const double ax = std::atan2(2 * (y * z + w * x), 1 - 2 * (x * x + y * y));
    const double az = std::atan2(2 * (x * y + w * z), 1 - 2 * (y * y + z * z));
    lua_pushnumber(L, ax);
    lua_pushnumber(L, ay);
    lua_pushnumber(L, az);
    return 3;
}

int RotateVector(lua_State* L, bool inverse) {
    const double vx = Arg(L, 1), vy = Arg(L, 2), vz = Arg(L, 3);
    Quat q = {Arg(L, 4), Arg(L, 5), Arg(L, 6), Arg(L, 7)};
    if (inverse) { q.x = -q.x; q.y = -q.y; q.z = -q.z; }
    const Quat inv = {q.w, -q.x, -q.y, -q.z};
    const Quat r = QuatMul(QuatMul(q, {0, vx, vy, vz}), inv);
    lua_pushnumber(L, r.x);
    lua_pushnumber(L, r.y);
    lua_pushnumber(L, r.z);
    return 3;
}

int L_VectorRotateByQuat(lua_State* L) { return RotateVector(L, true); }
int L_VectorInverseRotateByQuat(lua_State* L) { return RotateVector(L, false); }

// VectorRotate(x,y,z, ax,ay,az) -> the vector turned by Euler angles.
//
// Engine.dll (0x1013B660) builds a matrix from the three angles and pushes the
// vector through FUN_100A1610, which is
//     out.x = v.x*m[0] + v.y*m[4] + v.z*m[8] + m[12]     (and so on)
// - the row-vector convention this port already uses everywhere. So this is
// the same Euler composition EulerToQuat produces, applied the same way
// VectorRotateByQuat applies a quaternion, and it is written as exactly that
// rather than as a second copy of the convention that could drift from it.
//
// This one is not decoration: CAiBrain, farattack and jumpUp all build their
// movement directions with it, and it was returning nothing - so `mvx` came
// back nil and the arithmetic in CActor aborted the tick.
int L_VectorRotate(lua_State* L) {
    const double vx = Arg(L, 1), vy = Arg(L, 2), vz = Arg(L, 3);
    float q[4];
    EngineEulerToQuat(float(Arg(L, 4)), float(Arg(L, 5)), float(Arg(L, 6)), q);
    const Quat quat = {q[0], q[1], q[2], q[3]};
    const Quat inv = {quat.w, -quat.x, -quat.y, -quat.z};
    // Same handedness as VectorRotateByQuat: the engine rotates a vector as
    // q^-1 * v * q, which is why that native negates the axis before use.
    const Quat r = QuatMul(QuatMul(inv, {0, vx, vy, vz}), quat);
    lua_pushnumber(L, r.x);
    lua_pushnumber(L, r.y);
    lua_pushnumber(L, r.z);
    return 3;
}

int L_RotateQuatByAxisAngle(lua_State* L) {
    const Quat q = {Arg(L, 1), Arg(L, 2), Arg(L, 3), Arg(L, 4)};
    const Quat r = QuatFromAxisAngle(Arg(L, 5), Arg(L, 6), Arg(L, 7), Arg(L, 8));
    return PushQuat(L, QuatMul(r, q));
}

// NormalNToQuat: the shortest-arc rotation taking basis axis N onto the
// given normal. The Lua Quaternion class routes all three through the Z
// variant, so exactness beyond Z matters little.
//
// Built in the ENGINE's convention, which rotates a vector as q^-1 * v * q -
// so this is the conjugate of the textbook shortest arc, and the round trip
// that matters is `NormalZToQuat(n)` then `TransformVector(0,0,1)` giving
// back n. Getting it the textbook way round points every shot backwards:
// the weapons build their fire direction exactly that way, from the player's
// forward vector.
int NormalToQuat(lua_State* L, double axisX, double axisY, double axisZ) {
    double nx = Arg(L, 1), ny = Arg(L, 2), nz = Arg(L, 3);
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12) return PushQuat(L, {1, 0, 0, 0});
    nx /= len; ny /= len; nz /= len;

    const double d = axisX * nx + axisY * ny + axisZ * nz;
    if (d > 1.0 - 1e-9) return PushQuat(L, {1, 0, 0, 0});
    if (d < -1.0 + 1e-9) {
        // Opposite: half-turn about any perpendicular axis.
        double px = -axisY, py = axisX, pz = 0;
        if (std::abs(axisZ) > 0.9) { px = 1; py = 0; pz = 0; }
        return PushQuat(L, {0, px, py, pz});
    }
    // The cross runs normal-to-axis rather than axis-to-normal, which is the
    // conjugation.
    const double cx = ny * axisZ - nz * axisY;
    const double cy = nz * axisX - nx * axisZ;
    const double cz = nx * axisY - ny * axisX;
    const double w = std::sqrt((1.0 + d) * 2.0);
    return PushQuat(L, {w * 0.5, cx / w, cy / w, cz / w});
}

int L_NormalXToQuat(lua_State* L) { return NormalToQuat(L, 1, 0, 0); }
int L_NormalYToQuat(lua_State* L) { return NormalToQuat(L, 0, 1, 0); }
int L_NormalZToQuat(lua_State* L) { return NormalToQuat(L, 0, 0, 1); }

// ---------------------------------------------------------- Lua extensions

// PCF extended their Lua (LuaPlus-derived) with table.setconstant and
// table.isconstant - Game:SetConstant marks the big shared tables constant
// after init. The original enforces immutability in the VM; here the mark is
// advisory (a weak-keyed registry), which keeps isconstant truthful without
// risking metatable side effects - Clone() and friends branch on
// getmetatable(t), so a read-only metatable would change behaviour.
const char* const kConstantsKey = "painful.constants";

void PushConstantsRegistry(lua_State* L) {
    lua_pushstring(L, kConstantsKey);
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (lua_istable(L, -1)) return;
    lua_pop(L, 1);
    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "__mode");
    lua_pushstring(L, "k");
    lua_settable(L, -3);
    lua_setmetatable(L, -2);
    lua_pushstring(L, kConstantsKey);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);
}

int L_table_setconstant(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    PushConstantsRegistry(L);
    lua_pushvalue(L, 1);
    lua_pushboolean(L, 1);
    lua_settable(L, -3);
    lua_pop(L, 1);
    return 0;
}

int L_table_isconstant(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    PushConstantsRegistry(L);
    lua_pushvalue(L, 1);
    lua_gettable(L, -2);
    lua_pushboolean(L, lua_toboolean(L, -1));
    return 1;
}

// ---------------------------------------------------------------- modules

// LANG.ParseLangFile(path): reads a Lang_*.txt and feeds every line to the
// Lua-side Languages_ParseLangLine - the split of work the original uses
// (the parsing rules live in Languages.lua, only the file walk is native).
int L_LANG_ParseLangFile(lua_State* L) {
    LuaHost* host = LuaHost::FromState(L);
    const std::string path = host->ResolvePath(luaL_checkstring(L, 1));

    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        LogWarn("LANG.ParseLangFile: cannot read %s", path.c_str());
        return 0;
    }
    const std::string text(bytes.begin(), bytes.end());
    size_t from = 0;
    while (from <= text.size()) {
        size_t to = text.find('\n', from);
        if (to == std::string::npos) to = text.size();
        size_t end = to;
        if (end > from && text[end - 1] == '\r') --end;

        lua_getglobal(L, "Languages_ParseLangLine");
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); break; }
        lua_pushlstring(L, text.data() + from, end - from);
        if (lua_pcall(L, 1, 0, 0) != 0) {
            LogWarn("Languages_ParseLangLine: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            break;
        }
        from = to + 1;
    }
    return 0;
}

// R3D.RGB(r,g,b): packs a colour for the HUD/menu layer. Packed as D3D ARGB
// with full alpha; only consumers we also implement ever unpack it, so the
// layout is ours to keep consistent.
int L_R3D_RGB(lua_State* L) {
    const uint32_t r = ArgU32(L, 1) & 0xFF, g = ArgU32(L, 2) & 0xFF,
                   b = ArgU32(L, 3) & 0xFF;
    lua_pushnumber(L, static_cast<double>(0xFF000000u | (r << 16) | (g << 8) | b));
    return 1;
}

int L_R3D_RGBA(lua_State* L) {
    const uint32_t r = ArgU32(L, 1) & 0xFF, g = ArgU32(L, 2) & 0xFF,
                   b = ArgU32(L, 3) & 0xFF, a = ArgU32(L, 4) & 0xFF;
    lua_pushnumber(L, static_cast<double>((a << 24) | (r << 16) | (g << 8) | b));
    return 1;
}

// HUD.ColorSubstr(text, len): the original trims a string to a display
// length while skipping inline colour codes. Until HUD text lands, the
// identity is the faithful-enough answer.
int L_HUD_ColorSubstr(lua_State* L) {
    lua_pushvalue(L, 1);
    return 1;
}

// INP.GetTime(): seconds on the engine clock. Feeds math.randomseed and the
// timing code, so it must be a real monotonic number.
int L_INP_GetTime(lua_State* L) {
    static const auto start = std::chrono::steady_clock::now();
    const std::chrono::duration<double> t = std::chrono::steady_clock::now() - start;
    lua_pushnumber(L, t.count());
    return 1;
}

// INP.GetTimeMultiplier(): the bullet-time factor; 1 at normal speed. The
// scripts divide by it, so nil would poison every timer.
int L_INP_GetTimeMultiplier(lua_State* L) {
    lua_pushnumber(L, 1.0);
    return 1;
}

int L_R3D_ScreenSize(lua_State* L) {
    // The resolution the shipped interface was authored at. ScriptEngine::Bind
    // replaces this with the real window size.
    lua_pushnumber(L, 1024);
    lua_pushnumber(L, 768);
    return 2;
}

int L_MOUSE_GetPos(lua_State* L) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
}

// FS.GetBaseObjInfo(path): pre-scans an instance file for its BaseObj
// assignment and sets it on the global `o`. Load-bearing: instance files
// reference template fields on their FIRST line (o.Explosion.Range = ...)
// while declaring `o.BaseObj = "Explosion.CAction"` at the bottom, so LoadObj
// must know the base to clone BEFORE running the file.
int L_FS_GetBaseObjInfo(lua_State* L) {
    const std::string path = LuaHost::FromState(L)->ResolvePath(luaL_checkstring(L, 1));
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) return 0;
    const std::string text(bytes.begin(), bytes.end());

    const std::string key = "BaseObj";
    for (size_t at = text.find(key); at != std::string::npos;
         at = text.find(key, at + key.size())) {
        size_t p = at + key.size();
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (p >= text.size() || text[p] != '=') continue;
        ++p;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (p >= text.size() || text[p] != '"') continue;
        const size_t end = text.find('"', p + 1);
        if (end == std::string::npos) continue;

        lua_getglobal(L, "o");
        if (lua_istable(L, -1)) {
            lua_pushstring(L, "BaseObj");
            lua_pushlstring(L, text.data() + p + 1, end - p - 1);
            lua_settable(L, -3);
        }
        lua_pop(L, 1);
        break;
    }
    return 0;
}

// Camera reads: benign defaults until the game loop wires the real camera in.
int L_CAM_GetPos(lua_State* L) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
}

int L_CAM_GetForwardVector(lua_State* L) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1);
    return 3;
}

// CAM.GetAng() -> angles in degrees (CActor converts with -x * 3.14/180).
int L_CAM_GetAng(lua_State* L) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
}

// WORLD.LoadSky(mapPath) -> number of sky-dome layers. Zero means "no sky",
// which CLevel:ReloadSky handles as the low-quality/absent path. This is the
// bare-host fallback; ScriptEngine::Bind overrides it with the real reader.
int L_WORLD_LoadSky(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// The loading-screen progress counter; Cache:PrecacheLevel does arithmetic
// on it. Real once PMENU exists.
int L_Zero(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// MATERIAL.Size(mat) -> width, height. The bare host has no texture cache,
// so it answers what the original answers for a null material: -1, -1. A
// script that lays itself out against that is then visibly wrong rather than
// subtly wrong. ScriptEngine::Bind replaces this with the real sizes.
int L_MaterialSize(lua_State* L) {
    lua_pushnumber(L, -1);
    lua_pushnumber(L, -1);
    return 2;
}

// FS.FindFiles(pattern, wantFiles, wantDirs) -> array of bare child names.
// Non-recursive - PreloadTemplates does its own recursion - and the mask
// follows FindFirstFile semantics, where "*.*" matches names without a dot.
int L_FS_FindFiles(lua_State* L) {
    const std::string pattern = luaL_checkstring(L, 1);
    const bool wantFiles = lua_tonumber(L, 2) != 0;
    const bool wantDirs = lua_tonumber(L, 3) != 0;

    const size_t slash = pattern.find_last_of("/\\");
    std::string mask = slash == std::string::npos ? pattern : pattern.substr(slash + 1);
    const std::string dir =
        LuaHost::FromState(L)->ResolvePath(slash == std::string::npos
                                               ? std::string(".")
                                               : pattern.substr(0, slash));
    if (mask == "*.*") mask = "*";

    // Glob -> case-insensitive regex-free matcher.
    auto matches = [&mask](const std::string& name) {
        const auto lower = [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        // Classic backtracking wildcard match on * and ?.
        size_t n = 0, m = 0, star = std::string::npos, backtrack = 0;
        while (n < name.size()) {
            if (m < mask.size() &&
                (mask[m] == '?' || lower(mask[m]) == lower(name[n]))) {
                ++n; ++m;
            } else if (m < mask.size() && mask[m] == '*') {
                star = m++;
                backtrack = n;
            } else if (star != std::string::npos) {
                m = star + 1;
                n = ++backtrack;
            } else {
                return false;
            }
        }
        while (m < mask.size() && mask[m] == '*') ++m;
        return m == mask.size();
    };

    lua_newtable(L);
    int idx = 1;
    for (const DirEntry& e : FileSystem::Get().List(dir)) {
        if (e.isDirectory ? !wantDirs : !wantFiles) continue;
        if (!matches(e.name)) continue;
        lua_pushnumber(L, idx++);
        lua_pushstring(L, e.name.c_str());
        lua_rawset(L, -3);
    }
    return 1;
}

// ---------------------------------------------------------------- tables

const luaL_reg kGlobalImpls[] = {
    {"DoFile", L_DoFile},
    {"DoString", L_DoString},
    {"loadfile", L_loadfile},
    {"dofile", L_dofile},
    {"Log", L_Log},
    {"MsgBox", L_MsgBox},
    {"_ALERT", L_MsgBox},
    {"Exit", L_Exit},
    {"GetEngineVersionString", L_GetEngineVersionString},
    {"GetPainkillerVersionString", L_GetEngineVersionString},
    {"GetGCCount", L_GetGCCount},
    {"GetCallStackInfo", L_GetCallStackInfo},

    // Build/edition flags. This install is treated as Black Edition (base
    // game + Battle out of Hell), which the C6+ content in Data confirms.
    {"IsFinalBuild", L_True},
    {"IsPKInstalled", L_True},
    {"IsBooHInstalled", L_True},
    {"IsBlackEdition", L_True},
    {"IsDedicatedServer", L_False},
    {"IsMPDemo", L_False},

    {"LabelOk", L_LabelOk},
    {"GetCDLetter", L_GetCDLetter},
    {"GetCDLabel", L_GetCDLabel},
    {"GetDriveLetter", L_GetDriveLetter},

    {"AddBitFlag", L_AddBitFlag},
    {"RemoveBitFlag", L_RemoveBitFlag},
    {"IsBitFlag", L_IsBitFlag},
    {"ReplaceBitFlag", L_ReplaceBitFlag},

    {"EulerToQuat", L_EulerToQuat},
    {"QuatToEuler", L_QuatToEuler},
    {"VectorRotate", L_VectorRotate},
    {"VectorRotateByQuat", L_VectorRotateByQuat},
    {"VectorInverseRotateByQuat", L_VectorInverseRotateByQuat},
    {"RotateQuatByAxisAngle", L_RotateQuatByAxisAngle},
    {"NormalXToQuat", L_NormalXToQuat},
    {"NormalYToQuat", L_NormalYToQuat},
    {"NormalZToQuat", L_NormalZToQuat},

    {"luaProfiler_LOGIC1", L_NoOp},
    {"luaProfiler_LOGIC2a", L_NoOp},
    {"luaProfiler_LOGIC2b", L_NoOp},
    {"luaProfiler_LOGIC2c", L_NoOp},
    {"luaProfiler_LOGIC3a", L_NoOp},
    {"luaProfiler_LOGIC3b", L_NoOp},
    {"luaProfiler_LOGIC3c", L_NoOp},
    {"luaProfiler_LOGIC4a", L_NoOp},
    {"luaProfiler_LOGIC5", L_NoOp},
    {nullptr, nullptr},
};

// Module tables the vote could not derive from the generated list but the
// scripts demonstrably use: SOUND2D mirrors SOUND3D's instance API for
// unpositioned sounds, EDITOR backs the in-game editor scripts, LANG is the
// localisation loader, and the rest surfaced one by one as level loading
// reached them (billboards, menu, trigger regions, vertex arrays, fog
// volumes, AI floors).
const char* const kExtraModules[] = {"SOUND2D", "EDITOR", "LANG",   "BILLBOARD",
                                     "MENU",    "VARRAY", "REGION", "FOGVOL",
                                     "FLOOR"};

// Real module-function implementations; installed over their stubs.
struct ModuleImpl {
    const char* module;
    const char* name;
    lua_CFunction fn;
};
const ModuleImpl kModuleImpls[] = {
    {"LANG", "ParseLangFile", L_LANG_ParseLangFile},
    {"R3D", "RGB", L_R3D_RGB},
    {"R3D", "RGBA", L_R3D_RGBA},
    {"HUD", "ColorSubstr", L_HUD_ColorSubstr},
    {"INP", "GetTime", L_INP_GetTime},
    {"INP", "GetTimeMultiplier", L_INP_GetTimeMultiplier},
    {"R3D", "ScreenSize", L_R3D_ScreenSize},
    {"MOUSE", "GetPos", L_MOUSE_GetPos},
    {"FS", "FindFiles", L_FS_FindFiles},
    {"FS", "GetBaseObjInfo", L_FS_GetBaseObjInfo},
    {"CAM", "GetPos", L_CAM_GetPos},
    {"CAM", "GetForwardVector", L_CAM_GetForwardVector},
    {"CAM", "GetAng", L_CAM_GetAng},
    {"WORLD", "LoadSky", L_WORLD_LoadSky},
    {"WORLD", "LoadLowQualitySky", L_WORLD_LoadSky},
    {"PMENU", "GetLoadingScreenOverall", L_Zero},
    // Same "missing" convention as SetAnim: joints resolve once skeletal
    // animation lands, and the scripts handle -1 as "no such joint".
    // 0 = "not animating", which skips the animation-event loop cleanly.
    // Material dimensions for HUD layout; real when the HUD renderer lands.
    {"MATERIAL", "Size", L_MaterialSize},
};

} // namespace

void LuaHost::RegisterNative(const char* module, const char* name,
                             int (*fn)(lua_State*), void* ctx) {
    lua_State* L = L_;
    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, fn, 1);
    if (module) {
        GetOrCreateModule(L, module);
        lua_insert(L, -2);            // table below the closure
        lua_pushstring(L, name);
        lua_insert(L, -2);            // table, key, closure
        lua_rawset(L, -3);
        lua_pop(L, 1);
    } else {
        lua_setglobal(L, name);
    }
}

void RegisterNatives(LuaHost& host) {
    lua_State* L = host.state();

    // 1. Stubs for the whole recovered surface.
    struct Entry { const char* module; const char* name; };
    static const Entry kNatives[] = {
#define PK_NATIVE(m, n) {m, n},
#define PK_GLOBAL(n) {nullptr, n},
#include "NativeList.inc"
#undef PK_NATIVE
#undef PK_GLOBAL
    };

    for (const Entry& e : kNatives) {
        if (e.module) {
            GetOrCreateModule(L, e.module);
            lua_pushstring(L, e.name);
            PushStub(L, (std::string(e.module) + "." + e.name).c_str());
            lua_rawset(L, -3);
            lua_pop(L, 1);
        } else {
            PushStub(L, e.name);
            lua_setglobal(L, e.name);
        }
    }
    for (const char* m : kExtraModules) {
        GetOrCreateModule(L, m);
        lua_pop(L, 1);
    }

    // 2. Real implementations overwrite their stubs.
    for (const luaL_reg* r = kGlobalImpls; r->name; ++r) {
        lua_pushcfunction(L, r->func);
        lua_setglobal(L, r->name);
    }
    for (const ModuleImpl& m : kModuleImpls) {
        GetOrCreateModule(L, m.module);
        lua_pushstring(L, m.name);
        lua_pushcfunction(L, m.fn);
        lua_rawset(L, -3);
        lua_pop(L, 1);
    }

    // 3. The engine's Lua extensions.
    lua_getglobal(L, "table");
    lua_pushstring(L, "setconstant");
    lua_pushcfunction(L, L_table_setconstant);
    lua_rawset(L, -3);
    lua_pushstring(L, "isconstant");
    lua_pushcfunction(L, L_table_isconstant);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

} // namespace painful
