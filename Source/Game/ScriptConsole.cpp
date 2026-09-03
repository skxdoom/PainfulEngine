// ScriptEngine: the CONSOLE natives over the Console object.
//
// Registration table at Engine.dll 0x102AEF88. Every one of them is a thin
// thunk onto the HUD's embedded console; the argument shapes are read from
// the thunks (Activate 0x10029d70, AddMessage 0x1002a7b0, Print 0x1002a930,
// SetCurrentText 0x10029760, GetCurrentText 0x10028150, GetCursorPos
// 0x10027a20, IsActive 0x10027870, SetFont 0x10028210). The Demo* family
// stays stubbed: there is no demo recorder. Docs/Reference/Console.md.

#include "ScriptEngineInternal.h"

namespace painful {

// CONSOLE.Activate(on = true, mode = 0)
int ScriptEngine::L_CONSOLE_Activate(lua_State* L) {
    const bool on = lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0);
    From(L)->console_.Activate(on, int(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_CONSOLE_IsActive(lua_State* L) {
    lua_pushboolean(L, From(L)->console_.active() ? 1 : 0);
    return 1;
}

// CONSOLE.AddMessage(text, color = the interface tan). The colour is the
// packed ARGB R3D.RGB builds; the thunk's own default is -17798, which is the
// same number as a signed 32-bit int.
int ScriptEngine::L_CONSOLE_AddMessage(lua_State* L) {
    const char* text = luaL_optstring(L, 1, "");
    uint32_t argb = Console::kMessageColor;
    if (lua_isnumber(L, 2)) argb = uint32_t(int64_t(lua_tonumber(L, 2)));
    From(L)->console_.AddMessage(text, argb);
    return 0;
}

// CONSOLE.Print(text): the same two lists as AddMessage, in the default colour.
int ScriptEngine::L_CONSOLE_Print(lua_State* L) {
    From(L)->console_.AddMessage(luaL_optstring(L, 1, ""));
    return 0;
}

int ScriptEngine::L_CONSOLE_SetCurrentText(lua_State* L) {
    From(L)->console_.SetCurrentText(luaL_optstring(L, 1, ""));
    return 0;
}

int ScriptEngine::L_CONSOLE_GetCurrentText(lua_State* L) {
    lua_pushstring(L, From(L)->console_.currentText().c_str());
    return 1;
}

int ScriptEngine::L_CONSOLE_GetCursorPos(lua_State* L) {
    lua_pushnumber(L, From(L)->console_.scrollPos());
    return 1;
}

// CONSOLE.SetFont(name, size)
int ScriptEngine::L_CONSOLE_SetFont(lua_State* L) {
    From(L)->console_.SetFont(luaL_optstring(L, 1, ""), int(luaL_optnumber(L, 2, 0)));
    return 0;
}

// The message strip's look, from HUD:LoadData: colour as r,g,b, position in
// authoring pixels, font as name, texture, size (the texture is the glyph
// fill the menu rows use; the shipped value is "", so it is not drawn).
int ScriptEngine::L_CONSOLE_SetMPMsgColor(lua_State* L) {
    From(L)->console_.SetMessageColor(int(luaL_optnumber(L, 1, 255)),
                                      int(luaL_optnumber(L, 2, 255)),
                                      int(luaL_optnumber(L, 3, 255)));
    return 0;
}

int ScriptEngine::L_CONSOLE_SetMPMsgPosition(lua_State* L) {
    From(L)->console_.SetMessagePosition(float(luaL_optnumber(L, 1, 0)),
                                         float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_CONSOLE_SetMPMsgFont(lua_State* L) {
    From(L)->console_.SetMessageFont(luaL_optstring(L, 1, ""), int(luaL_optnumber(L, 3, 0)));
    return 0;
}

}  // namespace painful
