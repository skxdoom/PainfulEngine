# The menu system

The HUD and the menu look like the same problem and are not. The HUD is
**immediate mode**: the scripts draw it, every frame, with `HUD.DrawQuad` and
`HUD.PrintXY`, and the engine only rasterises. The menu is **retained mode**:
the scripts *declare* widgets and then never draw them. The engine owns the
widget tree, the layout, the mouse, the keyboard, the sounds and the fades.

That inversion is the whole shape of the work. There is no `PMENU.Draw`.

## How it fits together

A menu screen is a plain Lua table of data
([MainMenu.lua](../../Data_Extracted/LScripts/HUD/Menu/MainMenu.lua)):

```lua
MainMenu = {
    firstTimeShowItems = 80,
    textColor   = R3D.RGBA( 100, 100, 100, 255 ),
    fontBigTex  = "HUD/font_texturka_alpha",
    items = {
        SignAPact = {
            text   = TXT.Menu.SignAPact,
            desc   = TXT.MenuDesc.SignAPact,
            x = -1,  y = 210,
            action = "PainMenu:ActivateScreen(GameMenu)",
            sndLightOn = "menu/menu/option-light-on_main",
        },
        ...
```

`PainMenu:ActivateScreen(screen)` walks that table and turns it into engine
calls:

```lua
PMENU.ClearScreen()
PainMenu:SetupScreen( screen )      -- the Add*/SetItem* loop
PMENU.SetBackground( screen.background, screen.bgType )
PMENU.SetMenuWidth( screen.menuWidth )
PMENU.SetTopPosition( screen.topPos )
PMENU.SetShowItemsFrame( screen.firstTimeShowItems )
```

and inside `SetupScreen`, per item:

```lua
PMENU.AddTextButton( itemName, o.text, o.desc )
PMENU.SetItemPosition( itemName, o.x, o.y )
PMENU.SetItemFonts( itemName, o.fontBig, o.fontBigSize, o.fontSmall, o.fontSmallSize )
PMENU.SetItemColors( itemName, o.textColor, o.disabledColor, o.underMouseColor, o.descColor )
```

So `PainMenu.lua` - 3040 lines of it - is a bridge we do not have to write. We
have to provide what it talks to.

### Items are keyed by name, not by handle

`AddTextButton` at `0x10075a40` takes three strings (name, text, action) and
opens with

```c
pMVar2 = MenuScreen::FindItem(GEngine->renderer->menu);
if (pMVar2 == NULL) { ... create ... }
```

`MenuScreen::FindItem` is a lookup **by name**, and the item is only created
when the name is not already present. That is why every one of the ~25
`SetItem*` natives takes a name string as its first argument rather than a
handle, and it means the item store is a name-keyed map, not an array.

The `MenuScreen` lives at `GEngine + 0xf0 → + 0x5d6bd4`, immediately after the
`HUD` at `+ 0x5d6bd0`. Same object, adjacent slots - which is a hint that they
share the 2D drawing path, and is why our `MenuSystem` draws through
`HudRenderer` rather than owning a second batcher.

### Actions are Lua source strings

`action = "PainMenu:ActivateScreen(GameMenu)"` is not a function - it is text
the engine runs when the item is chosen. So the menu needs a "run this chunk"
hook into the Lua host, exactly like `game --exec`.

## Where the natives live

The `PMENU` table sits at `0x102b2588` in Engine.dll's native registry, found
the same way as the HUD's: locate the name string, then find the pointer to it.

| native | thunk |
|---|---|
| `PMENU.ClearScreen` | `0x10074810` |
| `PMENU.AddTextButton` | `0x10075a40` |
| `PMENU.AddCheckbox` | `0x10075c90` |
| `PMENU.AddTextButtonEx` | `0x10075f00` |
| `PMENU.AddSlider` | `0x100761e0` |
| `PMENU.AddKeyControl` | `0x100764c0` |
| `PMENU.AddSimpleKeyConf` | `0x10076a20` |
| `PMENU.AddScroller` | `0x10076c80` |
| `PMENU.AddTextEdit` | `0x10076ed0` |
| `PMENU.AddSliderImage` | `0x100771c0` |
| `PMENU.AddServerList` | `0x100774a0` |
| `PMENU.AddNumEdit` | `0x10077720` |
| `PMENU.AddPassword` | `0x10077a10` |
| `PMENU.AddList` | `0x10077d00` |
| `PMENU.AddWeaponList` | `0x10077e60` |

There are 138 `PMENU` natives in total across the shipped scripts. They break
down roughly as:

| group | count | notes |
|---|---|---|
| widget creation (`Add*`) | 28 | the actual work |
| item configuration (`SetItem*`, `Set*`) | ~40 | mostly one setter, one field |
| value access (`Get*`, `Is*`) | ~20 | |
| screen and flow control | ~15 | |
| multiplayer / server browser | ~20 | needs networking |
| campaign map and board | ~10 | the chapter select |
| loading screen | ~5 | |
| movies, CD key, registry | ~10 | Bink and DRM |

Most of the configuration setters are trivial. The cost is concentrated in the
widget *types* - each one is layout, hit-testing, drawing and interaction.

## Localization is already done

This was the surprise going in. Localization needs no new native: it is pure
Lua. `LANG.ParseLangFile` (already implemented) reads a `Lang_*.txt` of
`N: text` lines into `Languages.Texts[N]`, `Languages.lua` picks the file from
`Cfg.Language`, and then builds a name-keyed table that `TXT` points at.

Verified working:

```
$ PainfulEngine lua Data 3 C1L1_Cathedral 'print(TXT.Menu.SignAPact)'
Sign the Pact
```

Eight languages ship: English, French, German, Italian, Spanish, Polish,
Russian, Czech.

**The open question is the codepage, not the strings.** Each file declares its
encoding on line 1 - `iso-8859-1` for the Western languages, with a comment
pointing at `cp1250` for Central European. Our `FontCache` bakes codepoints
32..255 straight out of the TTF, which is exactly Latin-1; a cp1250 or cp1251
byte would then draw the Latin-1 glyph at that code rather than the intended
one. Polish, Czech and Russian will render as the wrong letters until the
atlas bakes per declared codepage. Nothing else about localization is in the
way.

## Staging

The menu is the largest remaining subsystem, so it is split into slices that
each end somewhere usable.

### Stage 1 - the item model and a navigable main menu — **done**

The screen lifecycle (`Activate`, `Active`, `Clear`, `ClearScreen`,
`ShowMenu`, `SwitchToMenu`, `SetBackground`, `SetMenuWidth`, `SetTopPosition`,
`ReturnToGame`), `AddStaticText` and `AddTextButton`, the common `SetItem*`
setters, `DisableItem`/`EnableItem`, mouse hover and click, keyboard
up/down/enter/escape, and running `action` strings.

Also an absolute cursor position: `Input` currently carries only mouse
*deltas*, because during play the mouse is captured. `PMENU.ShowMouse` is what
releases it, and `MOUSE.GetPos` has to answer truthfully for hit-testing.

Ends at: the real main menu, drawn from the shipped scripts, navigable, running
real actions.

Two things bit on the way, and both were settled by measurement rather than by
reading the decompile.

**`MenuAlign` is ONE-based.** `Definitions.lua` declares `None = 1, Left = 2,
Right = 3, Center = 4`. Read as the usual zero-based enum, `Left` is taken for
`Right` and every left-aligned item is drawn at `x - width` - which put
`BackButton` at x = **-100**, off the left edge - while every right-aligned one
runs off the right. Inspection had not found it; a one-line probe printing each
item's `x`, `align`, measured width and final position found it immediately:

```
PROBE item BackButton  x=72  y=660 align=2 size=34 w=190 -> (-100,619) "Resume Game"
PROBE item BackToMap   x=952 y=660 align=3 size=34 w=196 -> (1190,619) "Return to Map"
```

**`SetItemSounds` takes the accept sound before the focus sound.**
`PainMenu:AddItem` calls `PMENU.SetItemSounds( itemName, o.sndAccept,
o.sndLightOn )`, so the sound that plays when focus arrives is argument
**three**. Taking argument two gives every row the wrong sound, quietly.

Neither is visible in Engine.dll's `AddTextButton`, whose three string
arguments are indistinguishable in the decompile. The call site is the
authority for argument *meaning*; the binary is the authority for argument
*count* and for behaviour. Both are needed.

**The cursor is `HUD/kursor`** - Polish for cursor, 32x32, and named in
Engine.dll rather than in any script, which is why no amount of grepping the
Lua turns it up. `PMENU.ShowMouse` at `0x10075540` only sets a byte at
`MenuScreen + 0x3ad`; the drawing is the engine's. The same string table gives
`HUD/border` and `HUD/blachy_menu` for stage 3, and `HUD/loading` for stage 4.

**The pause is engine-owned too.** `WORLD.SetGamePaused` exists in the native
table beside `IsGamePaused`, and **no shipped script ever calls it** - the
scripts only ever ask (`PainKiller.lua` guards its tick on `IsGamePaused`).
Engine.dll keeps the flag as a byte on the World object at `+0x10`. So the
same transition that raises the menu freezes the world, and it belongs on the
transition rather than on the Escape handler: a script forcing the menu up on
a dropped connection has to pause as well.

Paused freezes the simulation and nothing else - no actor tick, no physics
step, no animation - while rendering and the render callbacks carry on, so the
HUD still draws behind the menu. Verified by dropping the player from a height
with the menu up: the camera stays at `0.00 2.00 0.00` at frames 100 and 400,
where an unpaused run falls to `2.02`.

### Stage 2 - the input widgets — **done**

`AddCheckbox`, `AddSlider`, `AddNumRange`, `AddTextEdit` and `AddTextButtonEx`,
plus the accessors (`GetSliderValue`, `IsItemChecked`, `SetCheckboxValue`,
`GetNumRangeValue`, `GetTextEditValue`, `IsSliderFloat`,
`ChangeTextButtonExValue`). Left and right adjust the focused widget.

The round trip that matters is `option`: a row declares `option =
"MasterVolume"`, `PainMenu:AddItem` seeds the widget from `Cfg[option]`, and
`PainMenu:ApplySettings` reads it back through the accessors and writes `Cfg`.
Both halves verified against the shipped `SoundOptions`:

```
before Cfg.MasterVolume=10 slider=10
after  Cfg.EAXAcoustics=false          -- after SetCheckboxValue + ApplySettings
```

**`AddTextButtonEx` holds no list.** The row whose value cycles through a set -
resolution, texture quality, speaker setup - keeps that set in the SCRIPT. The
engine stores only the current label; adjusting the row runs its action, and
the action pushes the next label back through `ChangeTextButtonExValue`. So
the widget is a caption, not a combo box.

`AddSliderImage`, `AddNumEdit` and `AddPassword` still fall through to stubs.

Ends at: the Options screens work and write back to `Cfg`.

### Stage 3 - lists and chrome

`AddList` / `AddItemToList` / `AddScroller`, and `MenuItemBorder` - the carved
stone frame, which is what `HUD.DrawBorder` builds in the original and which
our HUD currently approximates with a plain outline. Item sounds
(`SetItemSounds`, `sndLightOn`), the fade-in (`SetItemsFadeLength`,
`SetShowItemsFrame`) and the save-game list.

### Stage 4 - campaign flow

The level map and board (`SwitchToMap`, `ActivateMap`, `AddLevelToMap`,
`MapSetCurrLevel`, `MapNextLevel`), and the loading screen
(`ActivateLoadingScreen`, `LoadingProgress`, `SetLoadingScreenOverall`,
`SetProgressIcon`).

### Deferred

Multiplayer and the server browser (~20 natives, and there is no networking
layer to sit under them), movies (`PlayMovie` is Bink), and the CD-key and
registry-bonus DRM.
