// ScriptEngine: the PMENU natives over the retained widget model.

#include "ScriptEngineInternal.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace painful {

// --------------------------------------------------------------- the menu
//
// The scripts declare a screen and the engine owns it from there: layout,
// hit-testing, keyboard navigation and drawing are all on this side. Items are
// addressed by NAME, which is what Engine.dll's MenuScreen::FindItem does and
// why every setter below takes a name string first. See Docs/Reference/Menu.md.
//
// Stage 1: static text, text buttons, and the screen lifecycle. Everything
// else is still an instrumented stub, so the call report keeps counting what
// the shipped menus actually reach for.

namespace {

// Every SetItem* native is "find by name, write one field". A miss is not an
// error - the scripts configure items they have not added yet on screens that
// were never activated - so it returns quietly.
MenuSystem::Item* MenuItemArg(ScriptEngine* self, lua_State* L, MenuSystem** outMenu);

} // namespace

int ScriptEngine::L_PMENU_Activate(lua_State* L) {
    ScriptEngine* self = From(L);
    // The argument is "activate", and PainMenu passes false to LEAVE the menu.
    // lua_isnoneornil, not lua_isnil: an ABSENT argument is LUA_TNONE, and
    // lua_isnil only catches an explicit nil. PMENU.ShowMouse() is called with
    // no argument at all, and reading that as "false" is what left the menu
    // with no cursor and the mouse still steering the player.
    const bool on = lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0);
    self->menu_.Activate(on);
    return 0;
}

int ScriptEngine::L_PMENU_Active(lua_State* L) {
    lua_pushboolean(L, From(L)->menu_.active() ? 1 : 0);
    return 1;
}

int ScriptEngine::L_PMENU_Clear(lua_State* L) {
    From(L)->menu_.Clear();
    return 0;
}

int ScriptEngine::L_PMENU_ClearScreen(lua_State* L) {
    From(L)->menu_.ClearScreen();
    return 0;
}

// PMENU.SetBackground(material, type). The type selects how the artwork is
// fitted; we stretch to the window either way, because a menu background is
// artwork rather than a layout element.
int ScriptEngine::L_PMENU_SetBackground(lua_State* L) {
    From(L)->menu_.SetBackground(luaL_optstring(L, 1, ""), int(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PMENU_SetMenuWidth(lua_State* L) {
    From(L)->menu_.SetMenuWidth(float(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_PMENU_SetTopPosition(lua_State* L) {
    From(L)->menu_.SetTopPosition(float(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_PMENU_ShowMouse(lua_State* L) {
    // ShowMouse() with no argument means SHOW - see L_PMENU_Activate.
    From(L)->menu_.ShowMouse(lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0));
    return 0;
}

// PMENU.ShowMenu() / PMENU.ReturnToGame() - the same transition Escape makes,
// exposed because the scripts drive it too: a dropped multiplayer connection
// or a bad CD key forces the menu up from Lua.
int ScriptEngine::L_PMENU_ShowMenu(lua_State* L) {
    From(L)->menu_.Open();
    return 0;
}

int ScriptEngine::L_PMENU_ReturnToGame(lua_State* L) {
    From(L)->menu_.Close();
    return 0;
}

// ---------------------------------------------------------------- the map
//
// PMENU.SwitchToMap / ActivateMap (0x10074930 / 0x10074880) are one call:
// EngineGame::SwitchMapSelect(true). The engine clears the screen, calls
// Levels_FillMap() back into Lua, and draws its own map; choosing a level
// runs Game:LoadLevel('<dir>') - the strings Engine.dll and Painkiller.exe
// carry. MenuSystem::EnterMap does the same.
int ScriptEngine::L_PMENU_SwitchToMap(lua_State* L) {
    From(L)->menu_.EnterMap();
    return 0;
}

// PMENU.AddLevelToMap(chapter, dir, name, sketch, cardCondition, cardIndex,
// status) - Levels_FillMap's one call per level. status: 0 unavailable, 1
// the current level, 2 finished, 3 locked by difficulty.
int ScriptEngine::L_PMENU_AddLevelToMap(lua_State* L) {
    MenuSystem::MapLevel level;
    level.chapter = int(luaL_optnumber(L, 1, 1));
    level.dir = luaL_optstring(L, 2, "");
    level.name = luaL_optstring(L, 3, "");
    level.sketch = luaL_optstring(L, 4, "");
    level.cardCondition = luaL_optstring(L, 5, "");
    level.cardIndex = int(luaL_optnumber(L, 6, 0));
    level.status = int(luaL_optnumber(L, 7, 0));
    From(L)->menu_.AddMapLevel(level);
    return 0;
}

int ScriptEngine::L_PMENU_MapReset(lua_State* L) {
    From(L)->menu_.MapReset();
    return 0;
}

// PMENU.MapSetCurrLevel(level, chapter), both 1-based (0x10075250 takes one
// off each). SaveGame.lua restores the marker with it.
int ScriptEngine::L_PMENU_MapSetCurrLevel(lua_State* L) {
    From(L)->menu_.MapSetCurrent(int(luaL_optnumber(L, 1, 1)), int(luaL_optnumber(L, 2, 1)));
    return 0;
}

int ScriptEngine::L_PMENU_MapNextLevel(lua_State* L) {
    From(L)->menu_.MapNextLevel();
    return 0;
}

int ScriptEngine::L_PMENU_MapGetCurrLevel(lua_State* L) {
    lua_pushnumber(L, From(L)->menu_.mapCurrLevel());
    return 1;
}

int ScriptEngine::L_PMENU_MapGetCurrChapter(lua_State* L) {
    lua_pushnumber(L, From(L)->menu_.mapCurrChapter());
    return 1;
}

int ScriptEngine::L_PMENU_MapGetCurrLevelName(lua_State* L) {
    const MenuSystem::MapLevel* level = From(L)->menu_.mapCurrent();
    lua_pushstring(L, level ? level->name.c_str() : "");
    return 1;
}

int ScriptEngine::L_PMENU_MapGetCurrLevelCardCondition(lua_State* L) {
    const MenuSystem::MapLevel* level = From(L)->menu_.mapCurrent();
    lua_pushstring(L, level ? level->cardCondition.c_str() : "");
    return 1;
}

int ScriptEngine::L_PMENU_MapGetCurrLevelCardIndex(lua_State* L) {
    const MenuSystem::MapLevel* level = From(L)->menu_.mapCurrent();
    lua_pushnumber(L, level ? level->cardIndex : 0);
    return 1;
}

// ---------------------------------------------------------------- the board

int ScriptEngine::L_PMENU_SwitchToBoard(lua_State* L) {
    From(L)->menu_.EnterBoard();
    return 0;
}

// MBOARD.SetupSlots(type, count, y, width, height, spaceWidth, y2) - seven
// ints (0x?); y2 is -1 on every shipped row.
int ScriptEngine::L_MBOARD_SetupSlots(lua_State* L) {
    From(L)->menu_.BoardSetupSlots(int(luaL_optnumber(L, 1, 0)), int(luaL_optnumber(L, 2, 0)),
                                   float(luaL_optnumber(L, 3, 0)), float(luaL_optnumber(L, 4, 0)),
                                   float(luaL_optnumber(L, 5, 0)), float(luaL_optnumber(L, 6, 0)));
    return 0;
}

int ScriptEngine::L_MBOARD_SetSlotPosition(lua_State* L) {
    From(L)->menu_.BoardSetSlotX(int(luaL_optnumber(L, 1, 0)), int(luaL_optnumber(L, 2, 0)),
                                 float(luaL_optnumber(L, 3, 0)));
    return 0;
}

// MBOARD.AddCard(type, name, texture, desc, cost, available, selected,
// bigImage): int, three strings, int, two bools, string.
int ScriptEngine::L_MBOARD_AddCard(lua_State* L) {
    MenuSystem::BoardCard card;
    card.type = int(luaL_optnumber(L, 1, 1));
    card.name = luaL_optstring(L, 2, "");
    card.texture = luaL_optstring(L, 3, "");
    card.desc = luaL_optstring(L, 4, "");
    card.cost = int(luaL_optnumber(L, 5, 0));
    card.available = lua_toboolean(L, 6) != 0;
    card.selected = lua_toboolean(L, 7) != 0;
    card.bigImage = luaL_optstring(L, 8, "");
    From(L)->menu_.BoardAddCard(card);
    return 0;
}

int ScriptEngine::L_MBOARD_IsCardInSlot(lua_State* L) {
    lua_pushboolean(L, From(L)->menu_.BoardCardInSlot(int(luaL_optnumber(L, 1, 0)),
                                                       int(luaL_optnumber(L, 2, 0))));
    return 1;
}

// PMENU.PlayMovie(path, soundTrack) -> played? The movies are Bink and there
// is no decoder here, so the answer is false at once: the logo reel and the
// intro are skipped, and PainMenu:SelectDifficulty carries on to the map
// regardless of the result.
int ScriptEngine::L_PMENU_PlayMovie(lua_State* L) {
    LogInfo("PMENU.PlayMovie(%s): no Bink decoder, skipped", luaL_optstring(L, 1, ""));
    lua_pushboolean(L, 0);
    return 1;
}

int ScriptEngine::L_PMENU_NoOp(lua_State*) { return 0; }

// ---------------------------------------------------------------- key rows

// PMENU.AddKeyControl(name, label, primaryOption, alternativeOption,
// primaryText, alternativeText [, primaryKey, alternativeKey]) - 0x100764c0
// takes eight strings, the last two defaulting. The header row passes only
// the six, its texts being the column titles.
int ScriptEngine::L_PMENU_AddKeyControl(lua_State* L) {
    ScriptEngine* self = From(L);
    const std::string name = luaL_optstring(L, 1, "");
    if (name.empty()) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::KeyControl);
    item.text = luaL_optstring(L, 2, "");
    item.keyPrimaryText = luaL_optstring(L, 5, "");
    item.keyAltText = luaL_optstring(L, 6, "");
    item.keyPrimary = luaL_optstring(L, 7, "");
    item.keyAlt = luaL_optstring(L, 8, "");
    return 0;
}

// PMENU.AddSimpleKeyConf(name, keyText, key, index) - 0x10076a20: one key,
// the message-macro rows.
int ScriptEngine::L_PMENU_AddSimpleKeyConf(lua_State* L) {
    ScriptEngine* self = From(L);
    const std::string name = luaL_optstring(L, 1, "");
    if (name.empty()) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::KeyControl);
    item.keySingle = true;
    item.keyPrimaryText = luaL_optstring(L, 2, "");
    item.keyPrimary = luaL_optstring(L, 3, "");
    item.keyIndex = int(luaL_optnumber(L, 4, 1));
    return 0;
}

int ScriptEngine::L_PMENU_SetKeyItemIndex(lua_State* L) {
    if (MenuSystem::Item* item = From(L)->menu_.Find(luaL_optstring(L, 1, "")))
        item->keyIndex = int(luaL_optnumber(L, 2, 0));
    return 0;
}

int ScriptEngine::L_PMENU_GetPrimaryKey(lua_State* L) {
    const MenuSystem::Item* item = From(L)->menu_.Find(luaL_optstring(L, 1, ""));
    lua_pushstring(L, item ? item->keyPrimary.c_str() : "None");
    return 1;
}

int ScriptEngine::L_PMENU_GetAlternateKey(lua_State* L) {
    const MenuSystem::Item* item = From(L)->menu_.Find(luaL_optstring(L, 1, ""));
    lua_pushstring(L, item ? item->keyAlt.c_str() : "None");
    return 1;
}

int ScriptEngine::L_PMENU_GetSimpleKey(lua_State* L) {
    const MenuSystem::Item* item = From(L)->menu_.Find(luaL_optstring(L, 1, ""));
    lua_pushstring(L, item ? item->keyPrimary.c_str() : "None");
    return 1;
}

// PMENU.AddScroller(name, text, desc, min, max, value, height): the key
// table's scroll bar. Declared so the border can be tied to it; the table
// scrolls itself with the focus and the bar is not drawn yet.
int ScriptEngine::L_PMENU_AddScroller(lua_State* L) {
    const std::string name = luaL_optstring(L, 1, "");
    if (!name.empty()) From(L)->menu_.Add(name, MenuSystem::Kind::Scroller);
    return 0;
}

// The two ways to tie a border to a scroller return DIFFERENT names, and
// PainMenu:AddControlConfig checks them:
//
//     if PMENU.SetBorderScroller("KeyBorder","KeyScroller") ~= "KeyBorder"
//     if PMENU.SetScrollerForBorder("KeyBorder","KeyScroller") ~= "KeyScroller"
//
// either mismatch bouncing the player to the main menu. One is picked at
// random each visit (math.random(40) == 12), which reads as a tamper check.
int ScriptEngine::L_PMENU_SetScrollerForBorder(lua_State* L) {
    lua_pushstring(L, luaL_optstring(L, 2, ""));
    return 1;
}

int ScriptEngine::L_PMENU_SetBorderScroller(lua_State* L) {
    lua_pushstring(L, luaL_optstring(L, 1, ""));
    return 1;
}

// INP.GetKeyNameByEngName(eng) -> the name shown for a key. The engine keeps
// a per-language table; in English the two are the same strings, which is
// what is answered here for every language.
int ScriptEngine::L_INP_GetKeyNameByEngName(lua_State* L) {
    lua_pushstring(L, luaL_optstring(L, 1, "None"));
    return 1;
}

int ScriptEngine::L_INP_GetShortNameByEngName(lua_State* L) {
    lua_pushstring(L, Input::ShortNameForEngName(luaL_optstring(L, 1, "None")).c_str());
    return 1;
}

int ScriptEngine::L_MOUSE_SetInverse(lua_State* L) {
    if (Input* in = From(L)->input_) in->SetInvert(lua_toboolean(L, 1) != 0);
    return 0;
}

// MOUSE.SetSmooth / SetWheelSensitivity: recorded nowhere yet. Smoothing
// would filter the deltas; the wheel has no repeat rate to scale here.
int ScriptEngine::L_MOUSE_SetSmooth(lua_State*) { return 0; }
int ScriptEngine::L_MOUSE_SetWheelSensitivity(lua_State*) { return 0; }

// PMENU.LaunchURL(url) - the demo's pre-order link. Handed to the shell.
int ScriptEngine::L_PMENU_LaunchURL(lua_State* L) {
    const char* url = luaL_optstring(L, 1, "");
    LogInfo("PMENU.LaunchURL(%s)", url);
#ifdef _WIN32
    if (url[0]) ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#endif
    return 0;
}

// WORLD.SetGamePaused(bool) / IsGamePaused(). Engine.dll keeps this as a byte
// on the World object; no shipped script ever SETS it, which is what says the
// engine owns the pause - the scripts only ask (PainKiller.lua guards its
// tick on it). The menu sets it on the way in and clears it on the way out.
int ScriptEngine::L_WORLD_SetGamePaused(lua_State* L) {
    From(L)->gamePaused_ = lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0);
    return 0;
}

int ScriptEngine::L_WORLD_IsGamePaused(lua_State* L) {
    lua_pushboolean(L, From(L)->gamePaused_ ? 1 : 0);
    return 1;
}

// PMENU.AddStaticText(name, text) and AddTextButton(name, text, desc).
//
// The third argument of AddTextButton is the DESCRIPTION, not the action -
// PainMenu:SetupScreen passes o.desc there and sets the action separately with
// SetItemAction. (Engine.dll's own AddTextButton takes three strings; which of
// them is which is settled by the call site, not by the decompile.)
int ScriptEngine::L_PMENU_AddStaticText(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::StaticText);
    item.text = luaL_optstring(L, 2, "");
    return 0;
}

int ScriptEngine::L_PMENU_AddTextButton(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::TextButton);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    return 0;
}

int ScriptEngine::L_PMENU_SetItemText(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->text = luaL_optstring(L, 2, "");
    return 0;
}

int ScriptEngine::L_PMENU_SetItemDesc(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->desc = luaL_optstring(L, 2, "");
    return 0;
}

// The action is a string of LUA SOURCE, run when the item is chosen:
//   action = "PainMenu:ActivateScreen(GameMenu)"
int ScriptEngine::L_PMENU_SetItemAction(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->action = luaL_optstring(L, 2, "");
    return 0;
}

int ScriptEngine::L_PMENU_SetItemPosition(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->x = float(luaL_optnumber(L, 2, -1));
        item->y = float(luaL_optnumber(L, 3, 0));
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemColors(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->textColor      = uint32_t(int64_t(luaL_optnumber(L, 2, 0xFF646464u)));
        item->disabledColor  = uint32_t(int64_t(luaL_optnumber(L, 3, 0xFF9B9B9Bu)));
        item->underMouseColor= uint32_t(int64_t(luaL_optnumber(L, 4, 0xFFFFFFFFu)));
        item->descColor      = uint32_t(int64_t(luaL_optnumber(L, 5, 0xFFFFFFFFu)));
    }
    return 0;
}

// PMENU.SetItemFontsTex(name, bigTex, smallTex) - the texture the glyphs are
// filled with, not another font. See MenuSystem::Item::fontBigTex.
int ScriptEngine::L_PMENU_SetItemFontsTex(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        const std::string big = luaL_optstring(L, 2, "");
        const std::string small = luaL_optstring(L, 3, "");
        if (big != item->fontBigTex) { item->fontBigTex = big; item->fontBigTexMat = -1; }
        if (small != item->fontSmallTex) { item->fontSmallTex = small; item->fontSmallTexMat = -1; }
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemFonts(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->fontBig      = luaL_optstring(L, 2, "timesbd");
        item->fontBigSize  = int(luaL_optnumber(L, 3, 26));
        item->fontSmall    = luaL_optstring(L, 4, "timesbd");
        item->fontSmallSize= int(luaL_optnumber(L, 5, 22));
        if (item->fontBig.empty())   item->fontBig = "timesbd";
        if (item->fontSmall.empty()) item->fontSmall = "timesbd";
        if (item->fontBigSize   <= 0) item->fontBigSize = 26;
        if (item->fontSmallSize <= 0) item->fontSmallSize = 22;
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemVisibility(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->visible = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    return 0;
}

// PMENU.SetStaticTextRect(name, x1, y1, x2, y2) - 0x?: four ints after the
// name, the corners of the box a static text wraps into.
int ScriptEngine::L_PMENU_SetStaticTextRect(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->hasTextRect = true;
        for (int i = 0; i < 4; ++i) item->textRect[i] = float(luaL_optnumber(L, 2 + i, 0));
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemAlign(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->align = int(luaL_optnumber(L, 2, 0));
    return 0;
}

int ScriptEngine::L_PMENU_SetItemWidth(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->width = float(luaL_optnumber(L, 2, 0));
    return 0;
}

// PMENU.SetItemSounds(name, accept, lightOn). The call site settles the order:
// PainMenu passes o.sndAccept then o.sndLightOn, so the FOCUS sound is the
// third argument, not the second. Only that one is used yet.
// PMENU.EnableItemBG(name, "blaszka") - turn on the plate behind a row. The
// second argument is the BASE name of a three-slice under HUD/blachy_menu.
int ScriptEngine::L_PMENU_EnableItemBG(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->itemBG = luaL_optstring(L, 2, "");
        item->itemBGMat[0] = item->itemBGMat[1] = item->itemBGMat[2] = -1;
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemSounds(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->sndLightOn = luaL_optstring(L, 3, "");
    return 0;
}

int ScriptEngine::L_PMENU_DisableItem(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) item->disabled = true;
    return 0;
}

int ScriptEngine::L_PMENU_EnableItem(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) item->disabled = false;
    return 0;
}

namespace {

MenuSystem::Item* MenuItemArg(ScriptEngine* self, lua_State* L, MenuSystem** outMenu) {
    *outMenu = &self->menu();
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return nullptr;
    return (*outMenu)->Find(name);
}

} // namespace

// MOUSE.GetPos() -> the absolute cursor in window pixels, which is what the
// menu hit-tests against. The bare-host stub answers 0,0; this answers where
// the pointer actually is.
int ScriptEngine::L_MOUSE_GetPos(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->input_) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 2;
    }
    lua_pushnumber(L, self->input_->mouseX());
    lua_pushnumber(L, self->input_->mouseY());
    return 2;
}


// SOUND.ApplySoundSettings(master, music, sfx, speakers, pan, reverse, provider)
//
// Game.lua:222 calls this at startup with the values out of config.ini, which
// is how a player's saved volume reaches the mixer. Without it every sound
// plays at full gain no matter what the options say - and the shipped config
// has MasterVolume at 10, so "ignored" is a factor of ten too loud.
//
// The originals are percentages. Engine.dll multiplies argument 1 by 0.01 into
// MilesEngine::SetMasterVolumeLevel and argument THREE by 0.01 into
// Set3DDigitalEffectsVolume; argument 2 (music) is not used here, because the
// streams carry their own volume through SOUND.StreamSetVolume.
//
// Miles has two buses and we have one, so the two are composed: master scales
// everything and sfx scales the 3D effects, and effects are very nearly all we
// play. A separate music bus is worth splitting out when streaming lands.
// The 3D sound providers - Miles' list in the original ("Miles Fast 2D
// Positional Audio", the EAX variants). PainMenu:FillSoundProviders walks
// GetNumOfProviders / Get3DSoundProviderName(i) / Set3DSoundProvider(name)
// and shows the one GetCurrent3DSoundProviderName names. There is one here:
// the port's own mixer.
namespace {
const char* const kSoundProvider = "PainfulEngine Mixer";
}
int ScriptEngine::L_SOUND_GetNumOfProviders(lua_State* L) {
    lua_pushnumber(L, 1);
    return 1;
}
int ScriptEngine::L_SOUND_Get3DSoundProviderName(lua_State* L) {
    lua_pushstring(L, kSoundProvider);
    return 1;
}
int ScriptEngine::L_SOUND_GetCurrent3DSoundProviderName(lua_State* L) {
    lua_pushstring(L, kSoundProvider);
    return 1;
}
int ScriptEngine::L_SOUND_Set3DSoundProvider(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

int ScriptEngine::L_SOUND_ApplySoundSettings(lua_State* L) {
    ScriptEngine* self = From(L);
    const double master = luaL_optnumber(L, 1, 100.0) * 0.01;
    const double sfx    = luaL_optnumber(L, 3, 100.0) * 0.01;
    const double gain = std::max(0.0, std::min(1.0, master)) *
                        std::max(0.0, std::min(1.0, sfx));
    if (self->audio_) self->audio_->SetMasterVolume(float(gain));
    LogInfo("audio: master %.0f%%, sfx %.0f%% -> gain %.2f", master * 100.0, sfx * 100.0,
            gain);
    return 0;
}

int ScriptEngine::L_SOUND_SetMasterVolume(lua_State* L) {
    ScriptEngine* self = From(L);
    const double v = luaL_optnumber(L, 1, 100.0) * 0.01;
    if (self->audio_) self->audio_->SetMasterVolume(float(std::max(0.0, std::min(1.0, v))));
    return 0;
}


// --- stage 2: the widgets that carry a value -------------------------------
//
// The Options screens are almost entirely these. Each declares `option =
// "MasterVolume"`, PainMenu:AddItem seeds it from Cfg[option], and
// PainMenu:ApplySettings reads it back through the accessors below and writes
// Cfg. So getting the accessors right is what makes the settings round-trip.

// PMENU.AddCheckbox(name, text, desc, value)
int ScriptEngine::L_PMENU_AddCheckbox(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::Checkbox);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    // The script seeds this from Cfg, where a flag is a real Lua boolean.
    item.value = (lua_isboolean(L, 4) ? lua_toboolean(L, 4) != 0
                                      : luaL_optnumber(L, 4, 0) != 0)
                     ? 1.0 : 0.0;
    return 0;
}

// PMENU.AddSlider(name, text, desc, min, max, isFloat, value, width, ctrlWidth)
//
// PainMenu multiplies a float slider's bounds AND value by 100 before calling
// this, then divides on the way back out, so what arrives here is always in
// the same units whichever kind it is.
int ScriptEngine::L_PMENU_AddSlider(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::Slider);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    item.minValue = luaL_optnumber(L, 4, 0);
    item.maxValue = luaL_optnumber(L, 5, 100);
    item.isFloat = lua_isboolean(L, 6) ? lua_toboolean(L, 6) != 0
                                       : luaL_optnumber(L, 6, 0) != 0;
    item.value = luaL_optnumber(L, 7, item.minValue);
    if (const double w = luaL_optnumber(L, 8, 0); w > 0) item.sliderWidth = float(w);
    if (const double c = luaL_optnumber(L, 9, 0); c > 0) item.sliderCtrlWidth = float(c);
    return 0;
}

// PMENU.AddNumRange(name, text, desc, min, max, value). A maximum of -1 means
// unbounded, which is how the scripts spell "no frag limit".
int ScriptEngine::L_PMENU_AddNumRange(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::NumRange);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    item.minValue = luaL_optnumber(L, 4, 0);
    item.maxValue = luaL_optnumber(L, 5, -1);
    item.value = luaL_optnumber(L, 6, item.minValue);
    return 0;
}

// PMENU.AddTextButtonEx(name, text, desc, valueLabel)
//
// The row whose value is one of a list - resolution, texture quality, speaker
// setup. The ENGINE does not hold the list: the script keeps it, and every
// change runs the item's action, which calls ChangeTextButtonExValue with the
// next label. So this stores a caption and nothing more.
int ScriptEngine::L_PMENU_AddTextButtonEx(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::TextButtonEx);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    item.valueText = luaL_optstring(L, 4, "");
    return 0;
}

int ScriptEngine::L_PMENU_ChangeTextButtonExValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->valueText = luaL_optstring(L, 2, "");
    return 0;
}

// PMENU.AddTextEdit(name, text, desc, maxLength, value), and AddNumEdit which
// is the same field restricted to digits.
int ScriptEngine::L_PMENU_AddTextEdit(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::TextEdit);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    item.maxLength = size_t(luaL_optnumber(L, 4, 0));
    item.valueText = luaL_optstring(L, 5, "");
    return 0;
}

// --- reading the values back ----------------------------------------------

int ScriptEngine::L_PMENU_GetSliderValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    lua_pushnumber(L, item ? item->value : 0.0);
    return 1;
}

int ScriptEngine::L_PMENU_IsSliderFloat(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    lua_pushboolean(L, (item && item->isFloat) ? 1 : 0);
    return 1;
}

int ScriptEngine::L_PMENU_GetNumRangeValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    lua_pushnumber(L, item ? item->value : 0.0);
    return 1;
}

// Returns a BOOLEAN: PainMenu:ApplyCheckbox assigns it straight into Cfg,
// where the shipped config.ini writes true/false rather than 1/0.
int ScriptEngine::L_PMENU_IsItemChecked(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    lua_pushboolean(L, (item && item->value != 0.0) ? 1 : 0);
    return 1;
}

int ScriptEngine::L_PMENU_SetCheckboxValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->value = (lua_isboolean(L, 2) ? lua_toboolean(L, 2) != 0
                                           : luaL_optnumber(L, 2, 0) != 0)
                          ? 1.0 : 0.0;
    return 0;
}

int ScriptEngine::L_PMENU_GetTextEditValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    const std::string& s = item ? item->valueText : std::string();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}


// --- stage 3: the frame -----------------------------------------------------

// PMENU.AddBorder(name, dark), then SetBorderSize / SetBorderHeader /
// SetBorderColCount / SetBorderColumn configure it. The Options screens open
// with one of these and lay their rows out inside it.
int ScriptEngine::L_PMENU_AddBorder(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::Border);
    item.dark = lua_isboolean(L, 2) ? lua_toboolean(L, 2) != 0
                                    : luaL_optnumber(L, 2, 0) != 0;
    return 0;
}

// PMENU.AddTabGroup(name, dark). A framed container whose children the script
// shows and hides wholesale - PainMenu:ShowTabGroup just calls
// SetItemVisibility down the group's item list. It takes SetBorderSize like a
// border does, so it IS one as far as drawing goes; what makes it a group is
// entirely on the script side.
int ScriptEngine::L_PMENU_AddTabGroup(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::TabGroup);
    item.dark = lua_isboolean(L, 2) ? lua_toboolean(L, 2) != 0
                                    : luaL_optnumber(L, 2, 0) != 0;
    return 0;
}

int ScriptEngine::L_PMENU_SetBorderSize(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->width = float(luaL_optnumber(L, 2, 0));
        item->height = float(luaL_optnumber(L, 3, 0));
    }
    return 0;
}

// The dark band across the top of a panel, where a list puts its column
// captions. The argument is its height in authoring units.
int ScriptEngine::L_PMENU_SetBorderHeader(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->headerHeight = float(luaL_optnumber(L, 2, 0));
    return 0;
}

int ScriptEngine::L_PMENU_SetBorderColCount(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        const int n = int(luaL_optnumber(L, 2, 0));
        item->columns.assign(size_t(n < 0 ? 0 : n), 0.f);
    }
    return 0;
}

// SetBorderColumn(name, index, width) - and the index is ZERO-based, which
// PainMenu:SetupScreen shows plainly where it configures FireBorder with
// columns 0 through 3.
int ScriptEngine::L_PMENU_SetBorderColumn(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        const int index = int(luaL_optnumber(L, 2, -1));
        if (index >= 0 && size_t(index) < item->columns.size())
            item->columns[size_t(index)] = float(luaL_optnumber(L, 3, 0));
    }
    return 0;
}


// R3D.SetCameraFOV(degrees) / GetCameraFOV(). Cfg.FOV, applied by Game:Init;
// PainMenu:OpenMenu sets 90 for the menu and puts the old value back. Held
// here as the HORIZONTAL angle - the shipped config's 115 on a 3440x1440
// display is a horizontal figure - and turned into the vertical one for the
// window's aspect by the app.
int ScriptEngine::L_R3D_SetCameraFOV(lua_State* L) {
    ScriptEngine* self = From(L);
    const float fov = float(luaL_optnumber(L, 1, 90));
    if (fov >= 10.f && fov <= 170.f) self->cameraFov_ = fov;
    return 0;
}

int ScriptEngine::L_R3D_GetCameraFOV(lua_State* L) {
    lua_pushnumber(L, From(L)->cameraFov_);
    return 1;
}

// R3D.ApplyVideoSettings(resolution, fullscreen, gamma, brightness, contrast,
// shadows, textureQuality, weatherEffects, viewWeaponModel, textureFiltering,
// dynamicLights, projectors, coronas, decals, decalsStay) - what
// PainMenu:ApplyVideoSettings hands over after the Video Options screen.
// The mode is the part that reaches anything yet; the rest is recorded in
// Cfg by the scripts and waits for the renderer features it names.
int ScriptEngine::L_R3D_ApplyVideoSettings(lua_State* L) {
    ScriptEngine* self = From(L);
    const std::string res = luaL_optstring(L, 1, "");
    const bool fullscreen = lua_toboolean(L, 2) != 0;
    int w = 0, h = 0;
    if (std::sscanf(res.c_str(), "%d%*[xX]%d", &w, &h) == 2 && w > 0 && h > 0 && self->setVideoMode_)
        self->setVideoMode_(w, h, fullscreen);
    return 0;
}

// R3D.GetAvailableResolutions() -> an array of "WIDTHxHEIGHT" strings.
//
// PainMenu builds the Resolution row directly out of this and calls table.getn
// on it, so a missing native takes the whole VideoOptions screen down rather
// than degrading. The screen upper-cases each entry and compares against
// Cfg.Resolution, which the shipped config writes as "3440X1440" - so the
// separator has to be an 'x' and nothing else.
int ScriptEngine::L_R3D_GetAvailableResolutions(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_newtable(L);
    int n = 0;
    for (const std::string& mode : self->resolutions_) {
        lua_pushnumber(L, ++n);
        lua_pushlstring(L, mode.data(), mode.size());
        lua_settable(L, -3);
    }
    // Never hand back an empty table: the screen indexes visible[currValue]
    // and would then draw a nil. The current window is always a valid mode.
    if (n == 0) {
        char buf[32];
        snprintf(buf, sizeof buf, "%dx%d", self->screenW_, self->screenH_);
        lua_pushnumber(L, 1);
        lua_pushstring(L, buf);
        lua_settable(L, -3);
    }
    return 1;
}



}  // namespace painful
