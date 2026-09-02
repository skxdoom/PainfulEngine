# The menu system

The HUD and the menu look like the same problem and are not. The HUD is
**immediate mode**: the scripts draw it, every frame, with `HUD.DrawQuad` and
`HUD.PrintXY`, and the engine only rasterises. The menu is **retained mode**:
the scripts *declare* widgets and then never draw them. The engine owns the
widget tree, the layout, the mouse, the keyboard, the sounds and the fades.

That inversion is the whole shape of the work. There is no `PMENU.Draw`.

## How it fits together

A menu screen is a plain Lua table of data
(`LScripts/HUD/Menu/MainMenu.lua`, in the shipped scripts):

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
$ PainfulTools lua Data 3 C1L1_Cathedral 'print(TXT.Menu.SignAPact)'
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

### Stage 3 - the frame — **partly done**

`MenuItemBorder` is in, and with it `HUD.DrawBorder`, which the original also
builds out of that widget rather than drawing as an outline - so the HUD and
the menu now share one frame.

`MenuItemBorder::Render` at `0x100643b0` turns out to be a nine-slice with a
striped fill, and the ten piece names come out of the constructor at
`0x10064a90` (Polish: `naroznik` is corner, `ramka` is frame, `tlo_paski` is
striped background):

```
naroznik_lewy_gora  prawy_gora  lewy_dol  prawy_dol      the four corners
ramka_gorna_srodek  dolna_srodek  lewa  prawa            the four edges
tlo_paski  tlo_paski_ciemne                              the fill, light and dark
```

Every piece is TILED at its native size, not stretched, through
`HUD::DrawTiles(tex, x, y, w, h)` - which is why the art is small (the fill is
32x32, the edges about 30 across). A width or height of **zero** means "one
texture across", and that is how an edge repeats along a single axis. The
overhangs (-3, -5, -7, -11, -22...) are raw unscaled pixels: the frame sits
slightly OUTSIDE the rectangle it is given, so the panel's content area is the
rectangle itself.

When a border has columns they alternate light and dark, which is what gives a
list its banding, and the LAST column takes whatever width is left so rounding
never opens a gap at the right edge.

Still open in this stage: lists and scrollers, the fade-in
(`SetItemsFadeLength`, `SetShowItemsFrame`), and `EnableItemBG`'s `blaszka`
plate behind a row.

`AddTabGroup` is in - it is a border as far as drawing goes, and what makes it
a group is entirely script-side. Tab visibility works: 19 `false` calls arrive
and `Coronas` and `Shadows` end hidden while `Resolution` stays visible.

(An earlier note here claimed `HideTabGroup` never fired. That was wrong, and
wrong for an avoidable reason: the probe output was truncated at 14 lines and
the `false` calls all come later. Measuring and then reading only the head of
the measurement is worse than not measuring, because it looks like evidence.)

### Stage 4 - campaign flow — **done, the map as a stand-in layout**

Landed 2026-09-02. Three facts from the binaries set the shape:

- **The original boots with no level.** Painkiller.exe calls `Game:Init(true)`
  (the string is in the exe; the reports' `Game:Init()` makes the empty
  "NoName" level instead), and Engine.dll then runs
  `PainMenu:ActivateScreen(MainMenu)` itself. `PainfulEngine.exe` with no
  level does the same; a named level goes straight in, for the probes.
- **The map is engine UI.** `PMENU.SwitchToMap` / `ActivateMap`
  (0x10074930 / 0x10074880) are one call, `EngineGame::SwitchMapSelect`. The
  engine clears the screen, calls `Levels_FillMap()` back into Lua - which
  declares every level through `PMENU.AddLevelToMap(chapter, dir, name,
  sketch, cardCondition, cardIndex, status)`, status 0 unavailable, 1 current,
  2 finished, 3 locked - and takes the screen over. Choosing a level runs
  `Game:LoadLevel('<dir>')` (the format string is in Painkiller.exe) and
  `Game:OnPlay`.
- **A level switch is a session.** `Game:LoadLevel` runs `Game:Clear` (the
  scripts release their own entities) and then `WORLD.LoadMap`, so the app's
  world renderer, sky, corona collision and camera seat are a pair of
  functions - tear down, bring up - rather than a one-off at boot, and
  `WORLD.LoadMap` drops what the scripts never see: the engine-made
  active-mesh entities and the water (`ScriptEngine::ResetLevelState`). The
  load is deferred to the top of the next frame, since the menu action that
  asks for it is running over the world it will tear down; a level also has to
  load with the mouse UNLOCKED, or `CLevel:Synchronize` pulls our camera into
  `Lev.Pos` instead of pushing the level's seat out.

**The map's layout is measured off a capture of the original, not read out
of `MapSelect`'s renderer.** The dial at the left is the chapter selector: the
five `klawisz1..5` files are the pentagram's wedges with their Roman numerals
baked in (each cut to its own size: 178x106, 77x136, 125x92, 132x109, 89x140),
drawn clean, glowing under the pointer, and pressed-red for the chapter on
show, centred a hundred units from the dial's centre at (270,278) on the five
points clockwise from the top. The arched `okienko` plate sits on the ring's
top with the `cyferka` digit in its cutout. The `krysztal` crystal in the
centre is the button that starts the level - lit when it can be played,
brighter under the pointer, dark when locked - and the arrows either side of
the plate, part of the map's own art, are previous and next level. The black
panel at the upper right takes the level's sketch: a parchment scrap centred
on a transparent 512-square, drawn 450 units wide over the panel's centre so
the scrap spans the window. The info panel at the bottom left is a menu
BORDER, not a map texture, reading "Chapter N / Level N / name" with the name
in red. `pentagra_czysty` sits bottom right. Not placed yet: the `karta*`
tarot card of the level's reward.

The loading screen is one frame - the `HUD/loading/loading` art, the sketch
and the name - drawn before the load. The original's progress bar
(`LoadingProgress`, `Menu_RenderLoadingScreen`) would need the renderer
re-entered from inside a native; the load is synchronous and short.

**Diagnostics:** `PAINFUL_MAP_PICK=<dir>` chooses a level the moment the map
opens, so `game <root> "" --exec ... --shot` drives menu → difficulty → map →
level without a hand on the mouse (the exec chunk must hook `Game_Render`,
not `Game_Tick`: the menu pauses the tick chain).

### The widgets, from the shipped art

Compared against captures of the original screens, 2026-09-02:

- **Plates** (`EnableItemBG "blaszka"`) go under EVERY row that asked for one
  - the shipped Options screen shows five - not only the focused one, at the
  art's proportions: `blaszka_lewa` / `_prawa` are 110 x 114, so a cap is
  110/114 of the plate's height, and the plate stands 67 authoring units tall
  on the 80-unit row pitch, centred on the text, spanning the menu box less
  84 units each side (the original's is 553 wide in a 720-unit box).
  `MaterialSize` can report a padded texture size, which is what stretched
  the caps before; the files' own numbers are used.
- **Sliders** are the LARGE `HUD/border` set: `strzalka_duza` points right
  (mirrored for the left end), `kreska_duza` tiles the line, `dzwigienka_duza`
  is the upright knob. The value is right-aligned to `menuLeft +
  sliderCtrlWidth` (AddSlider's ninth argument, 700 by default), the bar of
  `sliderWidth` ends an arrow and a value-slot short of it; a row with its
  own x lays the bar after its label. A float slider holds its value times
  100 (`PainMenu:AddItem` scales it up, `ApplySlider` back down) and shows it
  divided - Gamma reads 1.00, not 100.00.
- **Checkboxes** sit before the label with no On/Off word. `HUD/ChkChecked.tga`
  (40 x 37) is the red diamond tick ALONE on transparency and `ChkUnchecked`
  is empty; the bevelled box under them is drawn as a dark fill with a rim in
  the item's colour. The `.bmp` beside the `.tga` is a 16-pixel Windows icon,
  which is why the texture index now prefers `.dds`, then `.tga`, then `.bmp`
  when a name ships in several formats.
- **Plates** repeat `blaszka_centrum` (103 x 114) at the plate's scale
  between the caps rather than stretching it; **slider** spearheads point
  INTO the line.
- **Tab groups** (`AddTabGroup`) draw a 180 x 52 tab box ten units in from
  the group's x and eight down, the next 172 along, over a panel that starts
  fifty units below the group's y: VideoOptions' group at (122,70) 776x560
  puts its panel from y 120 to 630, and ControlsConfig declares the same
  panel as an explicit `EmptyBorder` at y 110. Every group's tab box shows;
  only the visible group's panel. The tab LABELS are ordinary rows the script
  places itself, dropping the inactive one eight units.
- **The list scroller** is the SMALL set: `strzalka_mala` points down (flipped
  for the top), `kreska_mala` is vertical line, `dzwigienka_mala` the thumb.
- `PainMenu_PrintGameVersion()` is run every menu frame, as the engine does,
  for the "Version: 1.64" at the top right.
- `PAINFUL_QUIET=1` drops the debug overlay for captures of a screen's top.

### The key table (ControlsConfig) — **done**

`PMENU.AddKeyControl(name, label, primaryOption, alternativeOption,
primaryText, alternativeText [, primaryKey, alternativeKey])` (0x100764c0,
eight strings) declares one action's row and `SetKeyItemIndex` places it,
index 0 being the disabled header row. The rows carry no position: they are a
TABLE inside the border the script names `KeyBorder` - (50,110), 924 x 410, a
50-high header band, three columns of 328 / 308 / 308 authoring units - the
label left in its column, the two keys CENTRED in theirs, the header row
centred throughout, 27 units a row so thirteen of the fourteen show (the
original's count) and the rest scroll as the focus moves, with the scroller
drawn beside the table.

Choosing a row opens a capture; the next key or mouse button pressed lands in
the column the pointer (or left / right) picked, Escape keeps the old key,
Backspace and Delete bind `None`, and a key already bound elsewhere moves
(`MenuScreen::IsKeyInUse`). The engine's own `PainMenu:AfterControlChange(name)`
hook runs after each change. `GetPrimaryKey` / `GetAlternateKey` hand the
engine key names back to `PainMenu:ApplyControlConfig`, which writes `Cfg`,
and `ApplyControlSettings` runs `INP.LoadBindings` and `Cfg:Save` - the
original's own path from a rebind to config.ini.

Two things the script does on this screen are traps: the border is tied to its
scroller through `SetBorderScroller` OR `SetScrollerForBorder`, chosen by
`math.random(40) == 12`, and each must RETURN a different one of its arguments
(the border's name, the scroller's name) or the script bounces to the main
menu - it reads as a tamper check.

`INP.GetKeyNameByEngName` answers the engine name itself (the per-language
table is the same strings in English); `GetShortNameByEngName` is the HUD's
abbreviation table ("LMB", "RCtrl", "WheelFwd").

### config.ini: read and written in the same place

`Cfg:Save` writes `config.ini` through `io.open`, and the plain library opens
that against the PROCESS working directory - while `Cfg:Load` reads it through
`DoFile`, which the host resolves against the executable's directory. Launched
from anywhere but `Bin/`, every setting the menu applied was saved to a copy
elsewhere and gone by the next start. `io.open` now resolves a bare path the
same way (`LuaHost::IoOpenResolved`). Verified: the Controls screen's apply
rewrites `Bin/config.ini` byte-identical when nothing changed, and nothing
appears in the working directory.

What of `Cfg` reaches the engine: the key bindings (`INP.LoadBindings`),
`MouseSensitivity` and `InvertMouse` (`MOUSE.SetSensitivity` / `SetInverse`),
the volumes (`SOUND.ApplySoundSettings`), `FOV` (`R3D.SetCameraFOV`, held as
the HORIZONTAL angle - the shipped config's 115 on a 3440x1440 display is a
horizontal figure - and turned into the vertical one for the window's aspect
each frame), `Resolution` and `Fullscreen` (`R3D.ApplyVideoSettings` and at
boot, `Window::SetMode`; `PAINFUL_WINDOWED=1` and `PAINFUL_RES=WxH` override
a diagnostic run), `Language`, and the HUD's own fields, which the HUD scripts
read directly. Recorded but not yet honoured: `SmoothMouse`,
`WheelSensitivity`, gamma / brightness / contrast, and the render toggles
(shadows, texture quality and filtering, coronas, decals, dynamic lights,
weather) - each waits for the feature it names.

### Deferred

Multiplayer and the server browser (~20 natives, and there is no networking
layer to sit under them), movies (`PlayMovie` is Bink - it answers false at
once and the callers carry on), the CD-key and registry-bonus DRM, the credits
roll (`ShowCredits`), and the save / load lists (`AddLoadSave`,
`AddSaveGameToList`, `GetSelectedSGSlot`) which wait on `WORLD.SaveGame` /
`LoadGame` themselves. Also still stubs on the options screens: the weapon
priority lists (`AddList` / `MoveListItemUp` / `Down` / `GetListItems`),
`SetStaticTextRect`, `AddImageButton*`, `AddSliderImage`, `AddNumEdit`,
`AddPassword`.

## Three things play-testing found

### An absent argument is not nil

`PMENU.ShowMouse()` is called with **no argument at all**, and Lua distinguishes
that from an explicit `nil`: an absent argument is `LUA_TNONE`, which
`lua_isnil` does **not** match. Reading it as "false" left the menu with no
cursor and the mouse still captured, so moving it steered the player while the
menu was up - two symptoms, one cause. The test is `lua_isnoneornil`, and every
optional boolean in the menu natives now uses it.

This is worth watching for across the whole native API: any native that reads
an optional flag with `lua_isnil` silently takes the *opposite* default the
moment a script omits the argument.

### Pause has to cover the render section too

Freezing the simulation is not the same as freezing everything that advances.
`particles.Tick` and `billboards.Update` were being called from the RENDER
part of the frame, past the pause gate, so effects kept running behind the
menu. They take a delta of zero while paused now - they keep their last frame
on screen rather than vanishing, but they stop moving:

```
menu up     particles: 0 live in 295 emitters
no menu     particles: 10926 live in 346 emitters
```

Mouse deltas are **dropped** while the menu is up rather than accumulated: the
tick that consumes them is paused, so banking them would store a frame's worth
of motion per menu frame and snap the view on the way out.

### The font texture supplies the colour

`PMENU.SetItemFontsTex` binds a texture the glyphs are filled *with* -
`HUD/font_texturka_alpha`, which 46 shipped screens ask for. `HUD::Print` binds
it as a second texture stage alongside the glyph atlas.

Two things about it are not guessable:

**It is a plain MODULATE - pattern times colour, not doubled.** This took
three turns. A plain modulate looked like "a muddy brown that vanishes into
the art", so the blend was doubled (`MODULATE2X`), argued from the authored
numbers sitting near half scale: gold rows, bright red under the pointer.
Captures of the original settled it the other way (2026-09-02): on the bronze
plates the rows ARE a dark bronze-brown, and the hovered row a dark red - the
gold pattern near `230, 170, 120` times `RGBA(100, 100, 100)`, and times
`RGBA(166, 3, 3)` under the pointer, undoubled. The earlier judgement was made
with no plates under the rows; on the plates, dark is the look. Treating the
pattern as the colour source and the item colour as alpha-only remains wrong
for the same reason as before: it throws the red away.

**It is sampled in screen space, not with the glyph's atlas UVs.** The original
can use its own atlas coordinates because its font texture was authored against
its own atlas layout. Ours is packed by `stb_truetype` and shares no layout
with it, so atlas UVs cut each glyph a random patch of the pattern - the text
came out almost invisible. Screen space reproduces the look and is independent
of packing.

The exact fixed-function stage state in `HUD::SetRenderState` has not been
read, so the blend is inferred from the art and the authored colours rather
than from the binary. It matches what the shipped menu looks like; it is not
proven identical.

### The hit target is the row, not the word

Hover only highlighted an item while the pointer was literally over the
letters. Two reasons, both measured:

```
before   SignAPact  x=511..769   y=197..250      the glyphs
after    SignAPact  x=190..1090  y=197..272      the row
```

`PMENU.SetMenuWidth` is what says how wide a row is - `PainMenu` defaults it
to 720 authoring units - so hit-testing the text left most of the row dead.
And rows are spaced further apart than a line is tall (80 units against about
60), leaving a dead band between them; each row in a column now grows down to
meet the next, so a column hit-tests as one continuous strip.

The system cursor is hidden while the menu is up, since the menu draws its
own. Relative mode hides it during play anyway, which is why this only shows
up once capture is released for the menu - two pointers on screen.

### A negative x is not "centre" - it is "let alignment place me"

The real cause of the `VideoOptions` overlap. `TextureQualityWeapons` and
`TextureQualityCharacters` are **both** declared `x = -1, y = 330`, in the same
visible tab group, and differ only in `align` - `Left` against `Right`. They
are one two-column row, and centring both drew them on top of each other.

So a negative x means "place me by my alignment inside the menu box", and the
box is `menuWidth` wide (720 authoring units by default) centred on screen:

| align | with x < 0 | with an explicit x |
|---|---|---|
| `Left` (2) | the left half of the box | text starts at x |
| `Right` (3) | the right half of the box | text ENDS at x |
| `Center` (4), `None` (1) | centred on screen | text starts at x |

Note what `Right` means with a negative x: the right-hand **column**, not
right-aligned text. Right-aligning the label against the menu's edge leaves its
value nowhere to go - it lands past the screen, which is exactly how
`Characters` and `Skies` first drew, with no setting beside them.

Within a half row the value right-aligns to that half's own right edge rather
than sitting a `sliderWidth` along: 340 authoring units against a half of 360
would run the value into the next column's label. A gutter keeps the left
half's value off the right half's label.

This is the third distinct thing `MenuAlign` has cost, after being read as
zero-based. It is worth stating plainly: alignment in this menu decides
*layout*, not just text justification.

### The row plate, and what navigation order means

`PMENU.EnableItemBG(name, "blaszka")` turns on the bevelled plate a row sits
on. The art is a three-slice under `HUD/blachy_menu` - `_lewa`, `_centrum`,
`_prawa`: left cap, tiled middle, right cap - and the script passes only the
base name, so the engine appends the suffixes. The caps keep their own width
and the middle tiles between them, which is why it ships as three pieces and
not one stretched image.

It is drawn only under the FOCUSED row. The plates are opaque bronze; under
every row they would tile the whole column over the background and lose the
menu artwork entirely. As a highlight, it is what makes the selected row read
as pressed - and it is what finally showed `underMouseColor` working, since
`Options` came up red on the plate.

Seating the highlight on open turned up a second thing. Up and down have to
walk the screen the way it **looks** - top to bottom, then left to right - not
the way the items were declared. Declaration order is whatever order `next()`
happened to walk the screen's Lua table, which is arbitrary: the first attempt
seated the highlight on `Options` rather than on `Sign the Pact`.

### The fade-in is not implemented, deliberately

`SetItemsFadeLength` and `SetShowItemsFrame` are timed against the menu's
BACKGROUND MOVIE, not against a clock. Every screen carries `bgStartFrame` and
`bgEndFrame` triples, and `MainMenu` asks for its items at frame 80 of that
movie. The backgrounds are Bink (`PMENU.PlayMovie`), which we do not play.

Implementing the timing without the movie would hide every item for eighty
frames of nothing and then fade them in against a still image - worse than not
having it. It belongs with movie playback, whenever that happens, and the
natives stay stubs until then.

### The mouse belongs to whoever is in charge

Resuming from the menu used to leave the game unfocused: the Windows cursor
came back and the player had to click before the view would steer. Capture is
now driven from the frame rather than from a click - playing means captured,
the menu means released - so closing the menu hands the mouse straight back.

The system cursor is never shown in the game loop at all. In the menu we draw
our own; in play the mouse is captured, which hides it anyway. Tying its
visibility to the menu state instead made it flash back on for the frame after
a resume.

Capture only happens while the window has focus, and focus loss releases it -
otherwise alt-tabbing away would snatch the pointer straight back into a window
nobody is looking at.

The `run` diagnostic viewer keeps the old click-to-capture and
Escape-to-release, because it has no menu to hand the mouse to.
