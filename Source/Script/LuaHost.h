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

    // Runs one Lua chunk - the diagnostic console's spine. Errors are logged
    // and counted, never propagated.
    bool RunString(const std::string& chunk);

    // --- the engine -> Lua contract (names recovered from Engine.dll's
    // string table; definitions in Game.lua / HUD.lua) ---

    // Game:Init() - the engine calls it once after Loader.lua. Creates the
    // empty "NoName" level, loads config/bindings, applies settings.
    bool CallGameInit();

    // Game:LoadLevel(name) - the script-side level load: reads the .CLevel,
    // preloads the level's templates, LoadObj's every entity instance file,
    // then Apply()s the level and every object through the native API.
    bool CallGameLoadLevel(const std::string& levelName);

    // Game:OnPlay(true) - the transition into gameplay: creates the player
    // (CreatePlayerSP -> CreatePlayer), launches the level's OnPlay actions,
    // and sets Game.Active.
    bool CallGameOnPlay();

    // One frame of the script layer, in the engine's documented order:
    // Game_Tick (before physics), Game_Tick2 (after physics, before world
    // tick), Game_Tick3 (after world tick), Game_Render, Game_PostRender,
    // Game_GC. The physics/world steps themselves happen between these on
    // the C++ side.
    void FrameTick(double delta);

    // Game_GetMsg(msg, ...) - the engine's event pump into the scripts
    // (REGION_ENTERED, PLAYER_HIT_GROUND, EXPLOSION, ...). Numeric arguments
    // follow the message name, the shape every handler reads via arg[N].
    bool PostMsg(const char* msg, const double* args = nullptr, int nargs = 0);

    // Calls a global function with `nargs` numeric arguments. Errors are
    // logged and counted, never propagated.
    bool CallGlobal(const char* name, const double* args, int nargs);

    // Reads <global>.<field>.{X,Y,Z} - e.g. Lev.Pos, the level's authored
    // start position. False when any link of the chain is missing.
    bool ReadVec3(const char* globalName, const char* field, float out[3]);

    lua_State* state() const { return L_; }
    const std::string& dataRoot() const { return dataRoot_; }

    // Recovers the host from a lua_State inside a lua_CFunction.
    static LuaHost* FromState(lua_State* L);

    // Installs a real native over its stub: a module-table function, or a
    // global when module is null. ctx (may be null) reaches fn as
    // light-userdata upvalue 1 - the pattern the binding layers use to reach
    // their engine-side state.
    void RegisterNative(const char* module, const char* name, int (*fn)(lua_State*),
                        void* ctx);

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
    bool CallGameMethod(const char* method, const char* stringArg);

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
