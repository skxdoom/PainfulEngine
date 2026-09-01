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

### Stage 4 - campaign flow

The level map and board (`SwitchToMap`, `ActivateMap`, `AddLevelToMap`,
`MapSetCurrLevel`, `MapNextLevel`), and the loading screen
(`ActivateLoadingScreen`, `LoadingProgress`, `SetLoadingScreenOverall`,
`SetProgressIcon`).

### Deferred

Multiplayer and the server browser (~20 natives, and there is no networking
layer to sit under them), movies (`PlayMovie` is Bink), and the CD-key and
registry-bonus DRM.

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

**It is `MODULATE2X` - the product, DOUBLED.** This took two wrong turns worth
recording. A plain modulate leaves the rows a muddy brown that vanishes into
the art: the pattern is a warm gold near `230, 170, 120` and the rows are
authored `RGBA(100, 100, 100)`. Treating the pattern as the colour source and
the item colour as alpha-only looks right on the main menu - and is wrong,
because it throws the hue away. `PainMenu` defaults `underMouseColor` to
`RGBA(166, 3, 3)`, a RED that has to survive as a colour, which is why the
hovered row was never turning red.

Doubling makes all three states land, and the authored numbers are what say
so: they sit near half scale, which is the signature of that fixed-function
op. Gold for a normal row (`0.90 x 0.39 x 2`), bright red under the pointer
(`0.65 x 0.90 x 2` clamps), washed out for a disabled one at 155.

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
