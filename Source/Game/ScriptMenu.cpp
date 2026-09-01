// ScriptEngine: the PMENU natives over the retained widget model.

#include "ScriptEngineInternal.h"

namespace painful {

// --------------------------------------------------------------- the menu
//
// The scripts declare a screen and the engine owns it from there: layout,
// hit-testing, keyboard navigation and drawing are all on this side. Items are
// addressed by NAME, which is what Engine.dll's MenuScreen::FindItem does and
// why every setter below takes a name string first. See Docs/Menu.md.
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
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::Border);
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
