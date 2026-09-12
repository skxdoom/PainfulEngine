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
engine stores only the current label; adjusting the row calls
`PainMenu:SwapTextButtonEx` back into the script (not the row's action, which
is empty), and the script pushes the next label back through
`ChangeTextButtonExValue`. So the widget is a caption, not a combo box. The
click rules are under "Changing a value" below.

`AddSliderImage`, `AddNumEdit` and `AddPassword` still fall through to stubs.

Ends at: the Options screens work and write back to `Cfg`.

### Changing a value - what a click on a widget runs

Recovered from the `SendEvent` handlers (event 1 = button down, 2 = button up,
second argument 1 = left, 2 = right; a widget acts on the UP of the button
that went down over it):

- **Checkbox** (`MenuItemCheckbox::SendEvent`, 0x100656a0): plays
  `menu/menu/checkbox-click`, flips the value, runs the row's action, and
  `PainMenu:EnableApplyButton()` when the row is `applyRequired`.
- **TextButtonEx** (0x100876b0): the engine holds no list. A LEFT click plays
  `menu/menu/option-click` and runs `PainMenu:SwapTextButtonEx('<name>',1)`;
  a RIGHT click runs it with `0`. The script steps `currValue` through its
  `values` and pushes the label back through `ChangeTextButtonExValue`. Then
  `EnableApplyButton()` when `applyRequired`, and for every row except
  `GraphicsQuality`, `PainMenu:AfterControlChange('<name>')` - which resets
  the GraphicsQuality preset to Custom. The row's own action is NOT run (the
  shipped rows all declare `action = ""`).
- **Slider** (`MenuItemSlider::SendEvent`, 0x10086d30): `CheckMouse` answers
  1 for the left arrow, 2 for the right, 3 for the bar. An arrow click moves
  the value by ONE in the units the script passed - a float slider arrives
  multiplied by 100, so gamma moves by 0.01 - plays `menu/menu/scroller-move`
  and runs the action; at the end of the range it only stops the sound.
  `EnableApplyButton()` comes from `CalcSize` (0x10086810), which compares
  the value each frame, so a drag raises it too.
- **NumRange**: its `SendEvent` is not exported; the port steps it like a
  slider (left click +1, right click -1). An assumption, until a screen that
  uses one (the multiplayer limits) is checked against the original.

The port's `MenuSystem::Step` / `Toggle` / `Swap` / `ValueChanged` are these
four rules; keyboard left/right and Enter route through the same functions.

**Every row acts on the button's RELEASE, over the row that took its press**
(`MenuItem::SendEvent`, 0x1006ff70: the press sets the item's byte at 0x8b,
the release checks it). Two things follow, and the port lost both while it
acted on the press: a press that opens a screen cannot fire whatever the new
screen puts under the pointer, and Resume Game hands the game a button that
is already up - acting on the press let the game see the still-held button
and fire the weapon. A plain row also plays its accept sound on the choice
(`SetItemSounds`' second argument; PainMenu's default is
`menu/menu/option-accept`, the main menu's Quit `quit-accept`, Apply
`apply-accept`, Back `back-accept`), with or without an action.
Before this the mouse only ran a row's action, which every value widget
declares empty - so checkboxes, list rows and slider arrows did nothing, and a
click on an arrow set the slider to its end because the arrow zone was
hit-tested as the bar.

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
in red; hovering it runs `Hud_RenderLevelStats()`, the same board the in-game
Tab key shows, over the map. The ring has SIX fixed slots sixty degrees
apart, level 1 at the top; a chapter has four to six levels and the slots
past its count stay locked and unreachable. An open level gets its `cyferka`
digit drawn on its slot; every other slot wears the padlock, `HUD/Map/
question` (50 x 51, the same size as a digit - an earlier note here that the
locks were baked into the map was wrong; the map's band carries runes). The arched `okienko` plate is the SELECTOR, not a title, and
nothing follows the pointer: it sits on the chosen level's slot at radius
150, turned to follow the ring, with the digit at radius 163 in its cutout -
so with level 1 chosen it is the tab at the top - and when the choice moves
it SLIDES round the ring the short way, about a third of a second a slot;
the ring wraps. The turn is worked out in AUTHORING space, where the ring is
a circle, and the plate's four corners are then scaled to the screen one by
one (`HudRenderer::QuadCorners`): the screen scales the two axes differently
on a widescreen window, so a rectangle rotated ON SCREEN keeps its sides at
the wrong lengths at every angle but straight up and down - the plate on the
two o'clock slot came out squat while the top one was right. The tarot card
sits in its slot at (750,597) as the shipped `karta` art alone - the same
card whatever the level, as in the original - and the pentagram marker at
(824,588); both measured against a full-screen 3440x1440 pair. **The sketch
is drawn UNDER the map.** `Map.dds` is DXT3 and its black window is
transparent (alpha 0), so whatever is drawn before the map shows through the
window and stops under the opaque leather straps that cross it - that is
how the original's scrap sits behind the straps and is clipped to the
window, and why the window reads dark: it is the frame's clear colour. The
scrap's picture is the upper left of its texture; 474 units wide from
(383,145) it fills the window as in the full-screen pair, the same rule
placing a locked level's padlock plate. `PAINFUL_MAP_CURSOR=<k>` focuses the
k-th level for a capture. The level's tarot card sits bottom right in the
`karta` frame (glowing under the pointer) with the card's picture in its
window - `MagicCards` gives the picture for the level's `cardIndex` - and
opens the board. The `pentagra` marker beside it is the way back to the main
menu.

### The tarot board (`PMENU.SwitchToBoard`, the `MBOARD` natives)

`EngineGame::SwitchMagicBoard`: the engine calls `MagicBoard:Setup()` back
into Lua, which declares four slot rows through `MBOARD.SetupSlots(type,
count, y, w, h, spaceWidth, y2)` and `SetSlotPosition(type, i, x)` - the small
permanent-card row along the top, the two large selected-permanent slots,
the three large selected-tarot slots, the small tarot row along the bottom,
all in authoring units that `HUD/Board/board` is drawn to fit - and every card
through `MBOARD.AddCard(type, name, texture, desc, cost, available, selected,
bigImage)`. On the way out the engine runs `MagicBoard_UpdateCardsStatus()`,
which reads the selection back through `IsCardInSlot(type, i)`: for a small
row, whether card `i` of that kind still sits there (not selected); for a
large row, whether slot `i` holds a card.

Port: owned cards sit in their kind's small row at their own ordinal,
selected ones fill the large slots of their kind in order; a click moves a
card across while a large slot is free; the card under the pointer is shown
large with its name, cost and text; the crystal (or Escape) accepts and
returns to the map. **Stand-in:** equipping is free - the gold cost the
original charges through `MagicBoard::GetCash` / `SetCash`, and its counter
beside the crystal, are not done. A fresh game owns no cards, so the board
starts empty; cards arrive as levels are finished.

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
- **Checkboxes** sit before the label with no On/Off word, and are
  `HUD/ikonki/checkbox_pusty` / `checkbox_zaznaczony` (55 x 51, drawn about
  36 x 33): the bevelled bronze box with its red tick as one piece.
  (`HUD/ChkChecked` is the HUD's own tick, not the menu's; its `.bmp`
  neighbour is a 16-pixel Windows icon, which is why the texture index
  prefers `.dds`, then `.tga`, then `.bmp` when a name ships in several
  formats.) A centred checkbox centres box and label as a pair.
- **A centred row with a value** - `TextButtonEx` or `NumRange` with a
  negative x and `Center`/`None` alignment - is one string, "label: value",
  centred as a whole: "Speakers setup: Two Speakers" on the Sound screen.
- **Sliders follow the mouse**: press anywhere on the bar and the knob goes
  there, drag and it follows, holding on to the slider it started on. The
  bar runs through the middle of the text's LINE, not of its point size.
- **Static text with a rectangle** (`SetStaticTextRect(name, x1, y1, x2,
  y2)`) wraps into it with its lines centred - the yes/no prompt's question
  over (240,240)-(780,380). Every such text goes through `HUD.PrepareString`,
  which the engine uses to re-encode for its font; unbound it answered
  nothing and every prompt carried a nil question.
- **Escape** on an ordinary screen is the Back button: PainMenu adds it as
  the item `BackButton` carrying the screen's `backAction`, which on the
  option screens applies the settings on the way out. The main menu and a
  prompt have none, so Escape there closes the menu onto the game, or does
  nothing with no level up.
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

### The save table (`MenuItemTypes.LoadSave`) — **done**

Sources: `HUD/Menu/SaveGame/LoadSaveMenu.lua`, `PainMenu:AddLoadSave` /
`ReloadSaveGameList` / `SaveGame`; Engine.dll `MenuScreen::AddLoadSave`
0x10073540, the `MenuItemLoadSave` constructor 0x1006b310 (vtable
0x102b22f8), its sizing 0x1006b620, row drawing 0x1006adc0,
`MenuItemList::Render` 0x100688f0 / `SendEvent` 0x10068d50 / `AddItem`
0x10069510, and the natives `AddSaveGameToList` 0x1007bd50,
`GetSelectedSGSlot` 0x1007f520, `SetAllowSave` 0x1007f6a0, `ClearList`
0x1007f600. Flow in [`LuaHost.md`](LuaHost.md), "Saving and loading".

`PainMenu:AddLoadSave` creates the table with `PMENU.AddLoadSave(name, true)`
and then `ReloadSaveGameList` fills it: `ClearList`, a `"header"` row of four
empty strings (the column captions are the four sort BUTTONS the screen
declares at y 180 - Level, Playtime, Save time, Difficulty - each re-sorting
the list), an `"empty"` row reading `TXT.Menu.NewSave` when a save is allowed
(`SetAllowSave(true)`: a single-player game in progress below Trauma with a
live player), then one row per `SaveGames/NNN` whose `SaveGame.Info` matches
the screen - the Saves screen shows `Quick` / `Normal` / `NewLevel`, the
Autosaves screen `CheckPoint` / `AutoNewLevel`. Each row is
`(slot, level, playtime, "date time", difficulty)`; the slot is the folder
name, which is what `GetSelectedSGSlot` hands back and what
`SaveGame:Load('NNN')` / `Delete` take.

Layout, from the decompile, in authoring units off the item's (x, y) - the
screens put it at (100, 180): the table is 824 wide; its frame sits 20 out on
each side, 850 wide and `listMaxHeight + 40` tall, with a 40-unit header band
and columns of 400 / 126 / 200 / 140. The header row draws 4 units ABOVE y,
the first data row 16 below it, and rows are one text height apart (the
hit test starts at 14). Column texts: the level name at x (the header's is
centred in 380), playtime centred in [380, 510], saved-at in [506, 716],
difficulty in [716, 826] - the six floats right after the vtable. A row
under the pointer draws in the under-mouse colour and the CHOSEN row in the
disabled colour (the screen sets that to RGB(200,200,200) for exactly this).
The engine sizes the list to `listMaxHeight` and adds a scroller once the
rows overflow.

The buttons follow the selection, as the sizing code does each pass: with no
row chosen `DeleteButton`, `SaveButton` and `LoadButton` are all disabled;
with one, Save is enabled when saving is allowed and Delete / Load when the
row is not `"empty"`. A second click on the chosen row, or Enter, is the
Load button (the Save button on the new-save row); the arrows walk the rows
and leave the table at either end. `GetSelectedSGSlot` answers nil for the
new-save row, which is how `PainMenu:SaveGame` tells "save to a fresh slot"
from "overwrite this one, after a yes/no".

Not drawn: the original recolours a `[Quick]` / `[Auto]` / `[Checkpoint]`
prefix on the level name (grey and red literals in the row drawer) and can
drop a shadow under each row; the rows here are plain.

**Rows and the coloured prefix (2026-09-11).** The save table is a
`MenuItemList`, so its rows follow the list rule: the header 4 above y, the
first row at y + 16 plus one line (the port had it at y + 16, hard against
the header rule). Its `DrawElem` (0x1006adc0) then prints the save-type
prefix once more without its brackets, at the width of "[" in, over the
first column: `0xffe51010` red, or `0xffb8b8b8` grey for one of the three
`TXT.Menu` prefixes (`QuickPrefix` "[Quick]", `AutoPrefix` "[Auto]",
`CheckptPrefix` "[Chckpt]"; the strings are read from the script by the
LoadSave constructor, 0x1006b310). Which of the three is the grey one is
not settled - the decompile loses the order - and the port takes Auto. The
columns are the engine's: 380 / 130 / 210 / 110 wide at 0 / 380 / 506 / 716,
the last three centred (0x102b2358..236c).

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

### The key table's rows (recovered 2026-09-11)

The key rows are their own class (type 5, vtable 0x102b2098; ctor
0x10066e20, `CalcPosition` 0x10066b40, `Render` 0x10067280), and they are
placed by the SCREEN, not by the KeyBorder:

- x is the menu box's left edge (`(1024 - menuWidth) / 2`; ControlsConfig
  says 880, so 72); the three columns are 300 wide (0x102b0cdc). A row's
  label starts at x; the header's centres 20 left of the first column's
  middle; the primary key centres 20 right of the second column's middle,
  the alternative on the third column's middle.
- The header row (index 0) sits 10 above the screen's `topPos` (PainMenu's
  default 140, so 130 - centred in the KeyBorder's 50-unit band); row k sits
  at `topPos + 16 + k` lines, the line being the rows' font height (26 point,
  about 24 units), so the first row starts a line below the band.
- Rows outside the scroller's window are given y = -1 and not drawn.
  `MenuScreen::UpdateKeyConfig` (0x10070b60) cuts the window as
  `ceil((scrollerHeight - 130) * sy / line)`, the line measured through the
  two-argument `SetFont` at the authored size, and sets the scroller's range
  to `rows - window - 1`. `PMENU.AddScroller`'s seventh argument and
  `SetScrollerHeight` (440 for General, 546 for Advanced) are that height.

The port keeps the placement and cuts the window differently: as many rows
as fit between the first row and the KeyBorder's bottom. The engine's
formula overruns the Advanced tab's frame on a 16:9 screen (21 rows drawn
in a frame that holds 18), and its `rows - window - 1` range leaves the last
row unreachable when the window is one short. On 1600x900 the General tab
shows all fourteen rows without a scroller either way.

### The Messages screen

`MessagesConfig` declares one `MessagesKeys` item with `count = 18`, and
`PainMenu:ActivateScreen` expands it into 54 ordinary items before the
engine sees any of them: per row a `SimpleKeyConf` (x = 122, Center, 22
point: the bound key's name, `Cfg.MessagesKeys[i]`), a `Checkbox` (x = 204,
10 point, no label: `Cfg.MessagesSayAll[i]`) and a `TextEdit` (x = 242, 700
wide, courbd 22, 48 characters: `Cfg.MessagesTexts[i]`), 24 units apart from
y = 158. So the screen needs no widget of its own, only three rules:

- `AddSimpleKeyConf(name, keyName, keyEngName, index)`'s row (vtable
  0x102b2138) keeps `MenuItem::CalcPosition`, so the key name is placed like
  any text with the row's x and align.
- A checkbox whose font is under 11 points draws its box at half size
  (`MenuItemCheckbox::CalcSize`, 0x10065360).
- `MenuItemTextEdit::Render` (0x10088a50) prints the label at x and the
  value a space's width after it, with a `_` after the value while editing.

Typing into a text edit is not implemented in the port; the rows show and
the key column captures a key. The messages only reach multiplayer.

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
read directly, and `TextureFiltering` (`R3D.SetTexFiltering` and at boot; below).
Recorded but not yet honoured: `SmoothMouse`,
`WheelSensitivity`, gamma / brightness / contrast, and the render toggles
(shadows, texture quality, coronas, decals, dynamic lights,
weather) - each waits for the feature it names.

### Texture filtering (`R3D.SetTexFiltering`)

`Cfg.TextureFiltering` is one of `"Bilinear"`, `"Trilinear"`, `"Anisotropic"`
(VideoOptions and AdvancedVideoOptions offer all three; the GraphicsQuality
presets only ever pick the first two). The engine's
`MaterialSystem::SetTexFiltering` (0x100986d0) takes NO argument - the
`textureFiltering` slot of `R3D.ApplyVideoSettings` is ignored - and reads
`Cfg.TextureFiltering` back out of the script globals itself. It then walks
every loaded material, every pass, every stage, and rewrites the stage's
filter byte: `5` for Anisotropic, `4` for Trilinear, `3` for anything else
(Bilinear included), **except** a stage whose byte is `2`, the `texenv`
`point` word, which keeps it. So `bilinear_nomips` is overridden like the
rest, and only the deliberately pixelated surfaces escape the global choice.
`PainMenu:ApplyVideoSettings` calls it right after `R3D.ApplyVideoSettings`;
nothing calls it at boot, because the original's material loader reads the
same field when it builds each stage.

The port keeps one process-wide setting (`Render/TextureFilter.h`) and
applies it at bind time through `FilteredSampler(materialFlags)`, so a change
from the menu shows on the next frame with nothing reloaded, and boot reads
`Cfg.TextureFiltering` once the scripts have loaded the file. The mapping onto
bgfx: Bilinear = linear min/mag with `MIP_POINT`, Trilinear = bgfx's default
(linear on all three), Anisotropic = `MIN_ANISOTROPIC | MAG_ANISOTROPIC`.
The anisotropy level is not a setting: bgfx exposes only the
`BGFX_RESET_MAXANISOTROPY` cap, which the renderer always sets (16 on D3D11),
and only samplers that ask for the anisotropic filter use it. Before this the
world renderer asked for anisotropic filtering without the cap, which D3D11
serves as MaxAnisotropy 1 - plain trilinear - so the "anisotropic" the port
drew until now was not.

Which stages take it: the world's diffuse, lightmap, detail, terrain blend
and mask, the water normal map; the models' diffuse and second stage; the
sky layers; the decals. The 2D layer (HUD, fonts, menu art) and the shadow
depth passes keep their own flags - they are drawn at 1:1 or only alpha-tested.

### Multisample (`Cfg.Multisample`)

`Cfg.Multisample` is one of `"x0"`, `"x2"`, `"x4"`, `"x6"` (the menu's own
comment). `R3D.SetResolution` (0x101429d0, the row before
`SetContrastGammaAndBrightness` in the `R3D` table) reads it back out of the
globals with `sscanf(s, "x%d")` and passes the count as the last argument of
`GraphicsDevice::SetRes(1, w, h, fullscreen, samples)` - the D3D9
multisample type of the device. Nothing in the scripts calls `SetResolution`
directly; `R3D.ApplyVideoSettings` runs it, which is why the Video Options
screen applies both the resolution and the sample count at once.

The port: `Renderer::SetMsaa` turns the count into bgfx's backbuffer reset
flag and resets the device when it changes; `R3D.ApplyVideoSettings` reads
`Cfg.Multisample` the same way and hands it over, and boot reads it twice -
from `config.ini` before the window opens, from `Cfg` once the scripts have
loaded it. bgfx has 2, 4, 8 and 16 and no 6, so `"x6"` runs as 8x. With
bloom on the scene is drawn to an offscreen target, which is created with the
same sample count and resolved by bgfx before the composite reads it
([`Bloom.md`](Bloom.md)), so the picture is antialiased alike either way.
Every 3D draw already sets `BGFX_STATE_MSAA`. `PAINFUL_MSAA=N` pins the
count for a comparison.

### Deferred

Multiplayer and the server browser (~20 natives, and there is no networking
layer to sit under them), movies (`PlayMovie` is Bink - it answers false at
once and the callers carry on), the CD-key and registry-bonus DRM, and the
credits roll (`ShowCredits`). Also still stubs on the options screens:
`AddImageButton*`, `AddSliderImage`, `AddNumEdit`, `AddPassword`. The weapon
priority lists are done ("The weapon lists" below).

## Leaving the end-of-level screen

`EndLevel:LastClick` (`Templates/Processes/EndLevel.CProcess`) is the exit from
the stats screen, and it picks one of three destinations:

```lua
if Game.Difficulty == 3 then          PMENU.ActivateMap()
elseif ... math.random(100) == 32 then PMENU.SwitchToMap()
else                                   PMENU.SwitchToLevelSel()
end
PMENU.MapNextLevel()
```

All three land on the map, and `MapNextLevel` is what leaves the level just
unlocked as the selected one. The third is the ordinary case — the other two are
Trauma difficulty and a 1-in-100 flourish — and it was **the only one still a
stub**, so every normal finish took the branch that did nothing.

The failure had no error in it, which is why it read as a hang rather than a
crash: `LastClick` ran to the end, set `statStep = 0` and `startTime = 0`, and
returned. With no screen open the process kept ticking, so the stats crawl
started over from the first line, forever. The autosave earlier in the sequence
still happened, which is what made it look like the level had ended correctly.

`SwitchToLevelSel` and `SwitchToMenu` are wired now. In the original the level
select is a screen of its own, separate from the chapter map; we have one map
screen, so `SwitchToLevelSel` is the right destination with the wrong
presentation, and `SwitchToMenu` opens the main menu (the post-credits path).

Note when testing this headlessly: `PainfulTools` never calls
`SetActionRunner`, so `EnterMap` cannot run `Levels_FillMap()` and the map comes
up empty. Only `GameApp` wires the runner. The screen still reports
`PMENU.Active() == true`, which is what the test can check.

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

And when boxes overlap, the SMALLEST one under the pointer wins, not the
first declared. The save screen is the case: `DeleteButton` is a centred row
(`x = -1`, so its box is the whole 880-unit menu row) while `SaveButton` and
`LoadButton` sit on the same line at their own x. Taking the first match gave
Delete the pointer over all three once it was enabled, so Load could never
be hovered with a save selected (2026-09-03).

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
base name, so the engine appends the suffixes (`MenuItem::SetBackground`,
0x100701b0). The caps keep their own width and the middle tiles between them,
which is why it ships as three pieces and not one stretched image.

It is drawn under every row that asked for one. Its geometry is
`MenuItem::SetBGWidth` (0x1006e4d0) and `DrawBackground` (0x1006eb40), in
authoring units, with `width` = 400 because no script ever calls the width
setter (the engine applies the default on first draw):

```
scale  = (textHeight + 42) / 114        textHeight = the row's font, in units
capW   = round(110 * scale)             blaszka_lewa / _prawa are 110 x 114
tileW  = round(103 * scale * 0.8)       blaszka_centrum is 103 wide
count  = ceil(400 / tileW)              full tiles, the last one overdrawn by the cap
midX   = (1024 - 400) / 2 = 312         centred on the CANVAS, not the menu box
top    = item.y - 23
height = 42 + textHeight                (the 42 scales; the text height is pixels)
left cap  at midX - capW + 1,  right cap at midX + 400 - 1
```

For the main menu's 36-point rows that is a plate 74 units tall from 242 to
782, which is what the original's capture measures. An earlier port drew it 67
tall over the menu box less a margin, from a measurement; the rule above
replaces it.

Two more things `MenuItem::Render` (0x1006eac0) and `RenderDesc` (0x1006f7d0)
settle: a screen with `itemsDrawShadow` (PainMenu's default, through
`PMENU.SetItemsDrawShadow`) draws every label first one pixel down and right in
`0x50000000` black; and the focused row's description sits centred at
y = 700 (`descY` is -1 in every shipped screen, and -1 means the engine's
constant at 0x102b0cc8), over an opaque black copy one pixel down and right.

Seating the highlight on open turned up a second thing. Up and down have to
walk the screen the way it **looks** - top to bottom, then left to right - not
the way the items were declared. Declaration order is whatever order `next()`
happened to walk the screen's Lua table, which is arbitrary: the first attempt
seated the highlight on `Options` rather than on `Sign the Pact`.

### Row placement - what `CalcPosition` does with x and align

`MenuItem::CalcPosition` (0x1006e730), `MenuItemCheckbox::CalcPosition`
(0x10065420) and `MenuItemSlider::CalcPosition` (0x100864d0), with the
`MenuAlign` numbers (None 1, Left 2, Right 3, Center 4):

- **An explicit x** is the string's left edge; Right makes it the right edge
  (`x - width`), Center the middle (`x - width/2`). The tab titles are the
  case: `GeneralSettings` at x = 212 and `AdvancedSettings` at 392, Center,
  which is each tab's middle (the group's 122 + 90, + 180).
- **x = -1** places the row in the menu box (`SetMenuWidth`, 720 by default,
  centred on the screen): Left at the box's left edge, Right against its
  RIGHT edge (`menuRight - width`), None centred on the screen. There is no
  "value column": a list row's text already carries its value.
- **A list row's text is `label: value`** - `MenuItemTextButtonEx::ChangeValue`
  (0x10023950) rebuilds the string from the original text and the new value,
  so the row is one string wherever alignment puts it. Right-aligned rows
  therefore end flush with the box's right edge ("Sky: High").
- **A checkbox** is drawn at its art's own size, 55 x 51 for
  `HUD/ikonki/checkbox_*` (the art has clear margins, so the box reads about
  40 wide - an earlier measurement of "36 x 33" was the visible part). Its
  width is the label plus the box plus 4. Left rows start 10 units LEFT of
  the box edge, Right rows 10 units past its right edge, and a Right row
  puts the box at the far end, 3 in; every other row puts the box at x and
  the label 3 past it. The box sits at the row's y and the label drops to
  centre on it (`MenuItemCheckbox::Render`, 0x10065110).
- **A slider's** line (`kreska_duza`, 27 x 34) starts 16 units above the
  text's middle at its own height; the arrows (62 x 40) centre on the text;
  the knob (45 x 59) sits 6 units above the arrows' top; the value is centred
  in a "9.99"-wide slot after the right arrow (`MenuItemSlider::Render`,
  0x10085e90). The bar's length is `sliderWidth` LESS the width of "9.99" in
  the font the item had when `AddSlider` ran - always the engine default,
  painfont 40, because `SetItemFonts` comes later - which is why the 370-unit
  default draws a bar of about 280 (`SetSliderWidth`, 0x10085dd0). The left
  arrow stands at `(x + sliderCtrlWidth - bar - 2 x 62) - "9.99"` from the
  screen's left edge, x being the DECLARED value, -1 included, so a slider
  in a column is placed absolutely (`CalcSize`, 0x10086810, read from the
  disassembly: the decompile drops the FPU operands). The label of a Right
  slider ends 40 units in from the box's right edge after its own width plus
  `sliderCtrlWidth`; an unaligned slider centres `sliderCtrlWidth` on the
  screen (`MenuItemSlider::CalcPosition`, 0x100864d0).
- **The default font** every item is created with is painfont at 40 (small
  20): `MenuItem::MenuItem`, 0x1006ef00.
- **A slider's or a checkbox's explicit x is its left edge whatever `align`
  says** - their own `CalcPosition` only consults the alignment when x is -1.
  The Controls screen's sensitivity sliders are declared x = 380, Right, and
  the original draws the labels at 380.
- **The slider line rides high on tall screens in the original**: `DrawTiles`
  with height 0 draws the art at its unscaled 34 pixels from 16 units above
  the text's middle, so above 768 lines the line sits above the arrows' axis.
  The port scales the line and centres it on the arrows.
- **The font fill texture modulates alpha too.** `HUD::Print` binds
  `font_texturka_alpha` (a TGA with an alpha channel) as the second stage and
  the fixed-function modulate takes RGBA; the alpha holes are the torn edge
  the plate titles and the bottom row have. The port's `fs_hud` multiplied
  RGB only, which drew them clean.

The highlight (`underMouseColor`) and the description line belong to the
row UNDER THE POINTER - MenuItem keeps an under-mouse byte that the screen
clears when the pointer leaves - so nothing stays lit once the pointer is off
every row. The port keeps a keyboard focus for the arrow keys, and drops it
the moment the pointer leaves a row it lit.

### The tab strip

`MenuItemTabGroup::Render` (0x100639a0): every tab is 180 wide and 50 tall,
laid from the group's x, 180 apart; the group that is visible draws the whole
strip with its own tab full height in the DARK stripe fill (`tlo_paski_ciemne`,
the +0x15c texture) and the others starting 10 lower in the light one, and
the panel from y + 50 to the group's height. `MenuItemBorder::Render`
(0x100643b0) places the frame pieces exactly as the port's `DrawBorder` does
(top edge at y - 7, corners at y - 3 / y - 5, bottom at y + h - 11), so a
plain `Border` beside the strip at y = 68 sits two units above a dropped tab
at y + 10 = 70 in the original as well. **Which tab is the
group's own is its `align`**: Left draws the first tab raised, anything else
the second. Every group of a screen is declared at the same x and y
(ControlsConfig: both at 50, 60), so nothing else distinguishes them - a
port that sorted the groups by x drew the raised tab in the wrong place.
Screens with more than two tabs (Controls, Weapons, Messages) add the extra
tabs as plain `Border` items beside the strip, named `*SettingsBorder`.

**Draw order is by type, not by item.** `MenuScreen::Render` (0x10071070)
makes six passes over the item list, each in the order the items were added:
tab groups; borders whose name contains "Settings" (the tab boxes); the
other borders (the panels); Load/Save tables; everything else; checkboxes
last (which is why the Pickup list's checkbox shows over its header band).
Then each weapon list's border draws its scroller. The passes are what make
the screens work: the tab boxes overhang the panel top by ten units and the
panel, drawn in the later pass, covers it; and on the Controls screen the
hidden Advanced group is added FIRST and creates the shared `KeyBorder`
before the General group exists, so only the pass order puts the key table
over the General tab's panel. The port sorts the items by that pass, stably,
before drawing. Drawing in plain add order lost the key table's header and
dividers under the panel. The titles are
ordinary items the script repositions on each switch (`PainMenu:ShowTabGroup`:
the active title at y = 88, the other at 96).

### The weapon lists (`AddWeaponList`, `AddList`)

The Weapons tab's three columns are `MenuItemWeaponList`, a `MenuItemList`
(0x100688f0) with three overrides (vtable 0x102b2190: Render 0x10069090,
MoveItemUp 0x10069330, MoveItemDown 0x100693e0). What was recovered:

- The list owns a border 20 units out from (x, y), `SetListBorderWidth` wide
  and `listMaxHeight + 40` tall, with a 40-unit header band when the list was
  added with `useHeader` (`CalcSize` 0x10068be0, `CalcPosition` 0x10068620);
  x = -1 centres it in the menu box. The entries are `AddItemToList` strings,
  kept one per text (`AddItem` 0x10069510); with a header the first entry IS
  the header, drawn 4 units above y. The rows start at y + 16, one text
  height each, one further down when there is a header, and
  `floor(listMaxHeight / textHeight)` rows fit - the header counted among
  them. Past that a scroller appears (`AddItem` creates it and hands it to
  the border): `MenuItemBorder::AddScroller` (0x10063910) stands it at the
  border's right edge less 14, from 14 above the border's top, border height
  + 32 tall, so its arrows reach past the frame at both ends - the same for
  the Controls key table's scroller. `MenuItemScroller::Render` (0x100807c0)
  draws `strzalka_mala` flipped at the top and upright at the bottom,
  `kreska_mala` between, and the `dzwigienka_mala` lever at top + arrow +
  travel * t less 2, the travel being the height less 14 and the two arrows
  (`CalcSize` 0x100809f0). The scrollers are the LAST render pass, which is
  what keeps the next list's frame from covering them. The original draws the
  scroller art at its pixel size; the port scales it. Input: an arrow click
  steps one row; a press on the line takes hold of the lever and its centre
  follows the pointer along the travel until the button is released
  (`CalcSize`'s mode 3, which reads the pointer each frame). The key table's
  scroller and the lists' share this in the port (`ScrollerInput`).
- The line: `separator` is how many entries stand above it. Rows past it draw
  in `0xff505050` (chosen: `0xffa0a0a0`) and a one-pixel line in `textColor`,
  the row width plus 3 wide, sits two pixels above the first grey row.
  `PainMenu:SaveWeaponConfig` writes the count back as a `0` in
  `Cfg.WeaponPriority` / `BestWeapons1` / `BestWeapons2`, which is what the
  game's weapon pickup and best-weapon logic read.
- A click chooses the row under the pointer (`SendEvent` 0x10068d50); the
  chosen row draws in `disabledColor`. `MoveListItemUp` on the entry just
  under the line takes the line down with it as it swaps up; `Down` on the
  entry just above the line only moves the line up, and the line never
  reaches either end (the `sep == 0 -> 1` and `sep == count - 1` fix-ups).
- `AddList` (the multiplayer screens) is the same widget with no line.

Before this the whole tab was a trap: `PainMenu:SaveWeaponConfig` runs from
the Back action and from every tab title, and it calls `PMENU.GetListItems`,
which was a stub answering nil - so `table.getn(nil)` killed every way out.

### Text height is the glyphs' reach, and the size is the em

`HUD::GetTextHeight` (0x1008b360) returns the font record's first field, and
`GFont::CalcTextureSize` (0x1008f220) fills it with the tallest reach above
the baseline plus the deepest below it over the printable ASCII glyphs -
about 0.92 of the size for timesbd - not the font's ascender-to-descender
line. `GFont::Load` (0x10090240) sizes the face with `FT_Set_Pixel_Sizes`, so
the size IS the em in pixels. The port had baked with stb_truetype's
pixel-height scale (ascender to descender = size), which drew every face
about a tenth too small, and reported ascent - descent + gap as the line.
Both now follow the engine (`FontCache`): "Sign the Pact" measures 280 units
wide against the original's 278.

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

## Widescreen

The menu is drawn on the HUD's 4:3 canvas with anchoring off, so every
element is centred in the window, and so is its background: it fills the
canvas in whatever mode `HudAspect` is in, because the map screen and the
board place their pieces against it. Scaling it to cover the window instead
(tried first) misaligned the level markers on the map. In modes 1 and 2 the
sides of a wide window stay black; mode 0 stretches everything as the
original did. Docs/Reference/Hud.md, "Widescreen".
