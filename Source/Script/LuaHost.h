#pragma once
#include <cstdint>
#include <map>
#include <string>

struct lua_State;

namespace painful {

// Hosts the game's scripting layer on Lua 5.0.2 - the exact interpreter the
// original engine statically links. The version matters: the shipped scripts
// use 5.0-only forms (generic `for k,v in <table> do`, `getn`, `mod`) that
// later interpreters reject.
//
// The native API the scripts call is registered by Natives.cpp: module tables
// (ENTITY, MDL, WORLD, PMENU, ...) of plain functions taking an entity handle
// as their first argument, plus a set of bare globals (Log, DoFile, ...).
// Anything not yet implemented is an instrumented stub that counts and logs
// its calls - the recovery loop is: boot, read the report, implement what the
// scripts actually hit.
class LuaHost {
public:
    LuaHost() = default;
    ~LuaHost();
    LuaHost(const LuaHost&) = delete;
    LuaHost& operator=(const LuaHost&) = delete;

    // Creates the state, opens the Lua 5.0.2 standard libraries and registers
    // the native surface. dataRoot is the mounted Data directory.
    bool Init(const std::string& dataRoot);

    // Runs LScripts/Loader.lua - the engine's boot script, which DoFiles the
    // whole class system. Returns false when the loader itself fails to run;
    // per-file script errors are logged and counted but do not stop the boot,
    // so one broken file still leaves the rest of the report usable.
    bool Boot();

    // Loads and runs one script through the virtual filesystem. scriptPath is
    // engine-style ("../Data/LScripts/x.lua"). A missing optional file
    // (required=false) is silent - the loader probes for .xbox/.editor
    // variants that retail data does not ship.
    bool DoFile(const std::string& scriptPath, bool required = true);

    // Maps a script-side path onto the mounted data root: the original
    // resolves paths relative to Bin/, so "../Data/<x>" means <dataRoot>/<x>
    // whatever the root directory is actually called.
    std::string ResolvePath(const std::string& scriptPath) const;

    // --- the engine -> Lua contract (names recovered from Engine.dll's
    // string table; definitions in Game.lua / HUD.lua) ---

    // Game:Init() - the engine calls it once after Loader.lua. Creates the
    // empty "NoName" level, loads config/bindings, applies settings.
    bool CallGameInit();

    // One frame of the script layer, in the engine's documented order:
    // Game_Tick (before physics), Game_Tick2 (after physics, before world
    // tick), Game_Tick3 (after world tick), Game_Render, Game_PostRender,
    // Game_GC. The physics/world steps themselves happen between these on
    // the C++ side.
    void FrameTick(double delta);

    // Game_GetMsg(msg, ...) - the engine's event pump into the scripts
    // (EXPLOSION, ENTITY_CREATE, REGION_ENTERED, ...). Arguments must be
    // pushed by the caller before invoking; this variant covers the no-arg
    // case.
    bool PostMsg(const char* msg);

    // Calls a global function with `nargs` numeric arguments. Errors are
    // logged and counted, never propagated.
    bool CallGlobal(const char* name, const double* args, int nargs);

    lua_State* state() const { return L_; }
    const std::string& dataRoot() const { return dataRoot_; }

    // Recovers the host from a lua_State inside a lua_CFunction.
    static LuaHost* FromState(lua_State* L);

    void RequestQuit() { quit_ = true; }
    bool quitRequested() const { return quit_; }

    // --- instrumentation ---
    // Called by every unimplemented native stub; logs the first few calls
    // with their arguments and counts the rest.
    void RecordNativeCall(const char* fullName, lua_State* L);
    void PrintCallReport(size_t top) const;

    size_t filesLoaded() const { return filesLoaded_; }
    size_t filesMissing() const { return filesMissing_; }
    size_t scriptErrors() const { return scriptErrors_; }

private:
    lua_State* L_ = nullptr;
    std::string dataRoot_;
    bool quit_ = false;

    size_t filesLoaded_ = 0;
    size_t filesMissing_ = 0;
    size_t scriptErrors_ = 0;
    std::map<std::string, uint64_t> nativeCalls_;
};

// Implemented in Natives.cpp: builds the module tables and globals on the
// freshly opened state.
void RegisterNatives(LuaHost& host);

} // namespace painful
