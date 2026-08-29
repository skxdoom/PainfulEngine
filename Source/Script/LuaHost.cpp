#include "LuaHost.h"

#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

namespace painful {

namespace {

// Registry key under which the host pointer is stored, so lua_CFunctions can
// find their way back without globals.
const char* const kHostKey = "painful.host";

bool StartsWithCI(const std::string& s, const char* prefix) {
    size_t n = 0;
    for (; prefix[n]; ++n) {
        if (n >= s.size()) return false;
        char a = s[n], b = prefix[n];
        if (a == '\\') a = '/';
        if (std::tolower(static_cast<unsigned char>(a)) !=
            std::tolower(static_cast<unsigned char>(b)))
            return false;
    }
    return true;
}

// The message handler for pcall: appends a traceback via debug.traceback.
int Traceback(lua_State* L) {
    lua_getglobal(L, "debug");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 1; }
    lua_pushstring(L, "traceback");
    lua_gettable(L, -2);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return 1; }
    lua_pushvalue(L, 1);       // message
    lua_pushnumber(L, 2);      // skip this handler
    lua_call(L, 2, 1);
    return 1;
}

} // namespace

LuaHost::~LuaHost() {
    if (L_) lua_close(L_);
}

bool LuaHost::Init(const std::string& dataRoot) {
    dataRoot_ = dataRoot;
    while (!dataRoot_.empty() && (dataRoot_.back() == '/' || dataRoot_.back() == '\\'))
        dataRoot_.pop_back();

    L_ = lua_open();
    if (!L_) return false;

    // The 5.0 idiom: each opener leaves its library table on the stack.
    luaopen_base(L_);
    luaopen_table(L_);
    luaopen_string(L_);
    luaopen_math(L_);
    luaopen_io(L_);        // also provides os.* in 5.0
    luaopen_debug(L_);
    lua_settop(L_, 0);

    lua_pushstring(L_, kHostKey);
    lua_pushlightuserdata(L_, this);
    lua_settable(L_, LUA_REGISTRYINDEX);

    RegisterNatives(*this);
    return true;
}

LuaHost* LuaHost::FromState(lua_State* L) {
    lua_pushstring(L, kHostKey);
    lua_gettable(L, LUA_REGISTRYINDEX);
    LuaHost* host = static_cast<LuaHost*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return host;
}

std::string LuaHost::ResolvePath(const std::string& scriptPath) const {
    // The original engine's working directory is Bin/, so scripts say
    // "../Data/LScripts/...". The "../Data" component maps onto the mounted
    // root - which keeps a Data_Extracted root working with the same scripts.
    if (StartsWithCI(scriptPath, "../data/"))
        return dataRoot_ + "/" + scriptPath.substr(8);
    if (StartsWithCI(scriptPath, "../data") && scriptPath.size() == 7)
        return dataRoot_;
    if (StartsWithCI(scriptPath, "../")) {
        const size_t slash = dataRoot_.find_last_of("/\\");
        const std::string parent =
            slash == std::string::npos ? std::string(".") : dataRoot_.substr(0, slash);
        return parent + "/" + scriptPath.substr(3);
    }
    return scriptPath;
}

bool LuaHost::DoFile(const std::string& scriptPath, bool required) {
    const std::string path = ResolvePath(scriptPath);

    std::vector<uint8_t> bytes;
    if (!FileSystem::Get().Exists(path) || !ReadFile(path, bytes)) {
        if (required) {
            LogWarn("script missing: %s", path.c_str());
            ++filesMissing_;
        }
        return false;
    }

    const std::string chunkName = "@" + path;
    if (luaL_loadbuffer(L_, reinterpret_cast<const char*>(bytes.data()), bytes.size(),
                        chunkName.c_str()) != 0) {
        LogWarn("script parse error: %s", lua_tostring(L_, -1));
        lua_pop(L_, 1);
        ++scriptErrors_;
        return false;
    }

    lua_pushcfunction(L_, Traceback);
    lua_insert(L_, -2);                     // handler below the chunk
    if (lua_pcall(L_, 0, 0, -2) != 0) {
        LogWarn("script error: %s", lua_tostring(L_, -1));
        lua_pop(L_, 2);                     // message + handler
        ++scriptErrors_;
        return false;
    }
    lua_pop(L_, 1);                         // handler

    ++filesLoaded_;
    return true;
}

bool LuaHost::Boot() {
    // Loader.lua is the engine's boot script; everything else hangs off it.
    return DoFile("../Data/LScripts/Loader.lua");
}

bool LuaHost::CallGlobal(const char* name, const double* args, int nargs) {
    lua_pushcfunction(L_, Traceback);
    lua_getglobal(L_, name);
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 2);
        return false;
    }
    for (int i = 0; i < nargs; ++i) lua_pushnumber(L_, args[i]);
    if (lua_pcall(L_, nargs, 0, -nargs - 2) != 0) {
        LogWarn("%s: %s", name, lua_tostring(L_, -1));
        lua_pop(L_, 2);
        ++scriptErrors_;
        return false;
    }
    lua_pop(L_, 1);
    return true;
}

bool LuaHost::CallGameInit() {
    // Method call: Game:Init() - getglobal Game, fetch Init, pass Game as self.
    lua_pushcfunction(L_, Traceback);
    lua_getglobal(L_, "Game");
    if (!lua_istable(L_, -1)) {
        lua_pop(L_, 2);
        LogWarn("no Game object - boot did not complete");
        return false;
    }
    lua_pushstring(L_, "Init");
    lua_gettable(L_, -2);
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 3);
        LogWarn("Game.Init is missing");
        return false;
    }
    lua_insert(L_, -2);          // function below its self argument
    if (lua_pcall(L_, 1, 0, -3) != 0) {
        LogWarn("Game:Init: %s", lua_tostring(L_, -1));
        lua_pop(L_, 2);
        ++scriptErrors_;
        return false;
    }
    lua_pop(L_, 1);
    return true;
}

void LuaHost::FrameTick(double delta) {
    const double d[1] = {delta};
    CallGlobal("Game_Tick", d, 1);        // before physics
    CallGlobal("Game_Tick2", d, 1);       // after physics, before world tick
    CallGlobal("Game_Tick3", d, 1);       // after world tick
    CallGlobal("Game_Render", d, 1);
    CallGlobal("Game_PostRender", d, 1);
    CallGlobal("Game_GC", nullptr, 0);
}

bool LuaHost::PostMsg(const char* msg) {
    lua_pushcfunction(L_, Traceback);
    lua_getglobal(L_, "Game_GetMsg");
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 2);
        return false;
    }
    lua_pushstring(L_, msg);
    if (lua_pcall(L_, 1, 0, -3) != 0) {
        LogWarn("Game_GetMsg(%s): %s", msg, lua_tostring(L_, -1));
        lua_pop(L_, 2);
        ++scriptErrors_;
        return false;
    }
    lua_pop(L_, 1);
    return true;
}

void LuaHost::RecordNativeCall(const char* fullName, lua_State* L) {
    const uint64_t count = ++nativeCalls_[fullName];
    if (count > 3) return;

    // Format the arguments for the first few calls - this is the signature
    // recovery data the stub exists for.
    std::string args;
    const int top = lua_gettop(L);
    for (int i = 1; i <= top; ++i) {
        if (!args.empty()) args += ", ";
        switch (lua_type(L, i)) {
        case LUA_TNIL:     args += "nil"; break;
        case LUA_TBOOLEAN: args += lua_toboolean(L, i) ? "true" : "false"; break;
        case LUA_TNUMBER: {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.6g", lua_tonumber(L, i));
            args += buf;
            break;
        }
        case LUA_TSTRING: {
            std::string s = lua_tostring(L, i);
            if (s.size() > 48) s = s.substr(0, 48) + "...";
            args += "\"" + s + "\"";
            break;
        }
        case LUA_TTABLE:    args += "{}"; break;
        case LUA_TFUNCTION: args += "fn"; break;
        default:            args += lua_typename(L, lua_type(L, i)); break;
        }
    }
    LogInfo("[stub] %s(%s)", fullName, args.c_str());
}

void LuaHost::PrintCallReport(size_t top) const {
    std::vector<std::pair<std::string, uint64_t>> rows(nativeCalls_.begin(),
                                                       nativeCalls_.end());
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    uint64_t total = 0;
    for (const auto& r : rows) total += r.second;

    LogInfo("");
    LogInfo("boot: %zu files loaded, %zu missing, %zu script errors", filesLoaded_,
            filesMissing_, scriptErrors_);
    LogInfo("unimplemented natives hit: %zu distinct, %llu calls", rows.size(),
            static_cast<unsigned long long>(total));
    for (size_t i = 0; i < rows.size() && i < top; ++i)
        LogInfo("  %6llu  %s", static_cast<unsigned long long>(rows[i].second),
                rows[i].first.c_str());
    if (rows.size() > top) LogInfo("  ... (+%zu more)", rows.size() - top);
}

} // namespace painful
