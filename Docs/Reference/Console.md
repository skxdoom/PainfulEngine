# The console

`~` drops the console down over the game. It is where the cheats live
(`pkgod`, `pkweapons`, `pkhealth`, ...), where the developers' own commands
live (`fov`, `msensitivity`, `pos`, `showfps`), and in multiplayer where chat
was typed. This page is the split between what the engine does and what the
scripts do, and where each rule came from.

## The split

The engine owns the **panel**: the key that opens it, the input line, the
history, the scroll, the log, and the strip of recent lines it shows on
screen once the panel is down. It knows nothing about any command.

The scripts own the **commands**. Enter hands the typed line to
`Hud_OnConsoleCommand(text)` (`HUD/HUD.lua`), which calls
`Console:OnCommand`; that strips a leading `\`, `/` or `.`, upper-cases the
first word and looks for `Console.Cmd_<WORD>` in `HUD/Console.lua`. Every
command is one such method - about a hundred of them - and the rest of the
line, split on spaces, is its argument list. Tab hands the line to
`Hud_OnConsoleTab`, and `Console:OnPrompt` completes it against the same
method names and writes the result back through `CONSOLE.SetCurrentText`.

So the console is a script feature with a small native surface, like the
HUD, and implementing it meant implementing that surface exactly.

## Where the natives live

Registration table at `0x102AEF88`, 17 entries. The console object is
embedded in the HUD (`renderer + 0x5d6bd0`); every thunk opens on
`GEngine->renderer != 0` and does nothing headless.

| native | thunk | what it does |
|---|---|---|
| `Activate(on = true, mode = 0)` | `0x10029d70` → `0x10029860` | opens or closes; `mode` is `ConsoleMode` (0 full, 1 say-all, 2 say-team) |
| `IsActive()` | `0x10027870` | the active byte at `+0x10` |
| `AddMessage(text, color = -17798)` | `0x1002a7b0` | one line into the log and the strip; the default colour is `0xFFFFBA7A`, the interface tan |
| `Print(text)` | `0x1002a930` | the same two lists |
| `SetCurrentText(text)` | `0x10029760` | the input line |
| `GetCurrentText()` | `0x10028150` | |
| `GetCursorPos()` | `0x10027a20` | the SCROLL position (`+0x88`), 1 = newest at the bottom |
| `SetFont(name, size)` | `0x10028210` | the panel's font; the constructor's default is `courbd` 20 |
| `SetMPMsgColor(r, g, b)` / `SetMPMsgPosition(x, y)` / `SetMPMsgFont(name, tex, size)` | `0x100278c0` / `0x10027980` / `0x10027fc0` | the strip's look, set from `Hud:LoadData` |
| `Demo*` (6) | | the demo recorder; stubs here |

A message with `'\n'` in it becomes several lines (`strchr` in
`0x1002a530`). The log keeps 100 lines (`+0x84`); the strip keeps 4
(`0x100294a0` drops the oldest at 4).

## Opening it

`EngineGame::SwitchConsole` (`0x1001cdd0`) is the whole of the `~` handling:

```
if not Game.Paused then console.Activate(not console.active, 0)
```

The key itself is dispatched by `Painkiller.exe`, which imports
`SwitchConsole`; the engine's own key handler closes on `VK_OEM_8` (`0xDF`),
a layout leftover. Ours toggles on `~` (192, `Keys.Tylda` in
Definitions.lua) both ways and also closes on Escape, which the original
swallows and ignores.

`Activate(true)` (`0x100283e0`) clears the input line, homes the caret and
the scroll, resets the panel size for the mode, sets the prompt to `>`, and
**zeroes the input system's action masks** - the player stops running when
the panel drops. The log is left alone.

## Keys, while it is up

`InputSystem::ProcessEvents` (`0x1003e670`) offers every key event to the
menu, the map, the tarot board and then the console (`0x1002aef0`), and a
consumed event never reaches the scripts' key state - so while the panel is
up the scripts see every key released, except Shift and Ctrl, which pass
through regardless. Mouse buttons are keys too and are swallowed the same way;
mouse look is not touched.

| key | handler | does |
|---|---|---|
| PageUp / PageDown | `0x10027d60` / `0x10027770` | scroll by half the panel's height in lines |
| Up / Down | `0x1002aaa0` / `0x10028780` | the history, newest first; Down past the end empties the line |
| Left / Right | `0x100285c0` / `0x10028650` | the caret |
| Enter, keypad Enter (252) | `0x1002ad00` | echo the line into the log, push it onto the history, queue it for the scripts, clear |
| Backspace / Delete | `0x10028a30` / `0x10028cc0` | edit |
| Tab | | `Hud_OnConsoleTab(text)` |
| Ctrl+V | | the clipboard's first line, appended |
| anything else | `0x10028f60` | inserted if it translates to a printable character, **except `#`** - the colour marker `HUD.PrintXY` would eat |

The queued line is dispatched from the tick, not the key handler
(`0x10027e90`): mode 1 calls `Hud_OnSayToAll(text)`, mode 2
`Hud_OnSayToTeam(text, 0xFF00FF00)`, otherwise `Hud_OnConsoleCommand(text)`.
Ours does the same at the top of the frame, paused or not.

## What it draws

`0x100298f0`, in authoring units against 1024x768 (`x * W/1024`,
`y * H/768`, as everything in [`Hud.md`](Hud.md)):

- The panel is a `MenuItemBorder` - the menu's carved frame,
  [`Menu.md`](Menu.md) - at (30, 30), 964 wide, 360 tall; 42 tall in the
  chat modes, which show the input line alone.
- Text starts 10 in from the panel's left edge. The input line sits with 10
  clear beneath it and 20 above the panel's bottom (12 in chat); it is the
  prompt, the text scrolled so the caret stays in view, and a `_` caret.
- The log stacks upward from the input line at the font's height, newest
  first, cut off at the panel's top edge. Lines wrap by character at the
  panel's width less 20, on the way in.
- Everything is the tan `0xFFFFBA7A`, including the echo of a typed line.
  The log keeps a colour per line for chat.
- The strip, drawn only while the panel is down: the last four lines at
  `HUD.mpMsgPosition` (shipped `{0, 0}`) plus 10, in `HUD.mpMsgFont`
  (`courbd` 20), each over a one-pixel black shadow, and each dropped 15
  seconds after it arrived (`0x100293f0`). A line with colour `-1` takes the
  strip's colour; `AddMessage`'s default is not `-1`, so cheat confirmations
  arrive in their own tan.

The constructor (`0x10029f10`) also sets a history depth of 200 and a
default strip colour of `-1`; the strip's `SetMPMsgFont` texture argument is
the glyph-fill texture the menu rows use, shipped as `""`, and is not drawn.

## The commands, and multiplayer

`Console.lua` defines about a hundred `Cmd_` methods. The single-player ones
run as written: the cheats (`pkammo`, `pkweapons`, `pkhealth`, `pkpower`,
`pkgod`, `pkalwaysgib`, `pkweakenemies`, `pkgold`, `pkhaste`, `pkdemon`,
`pkweaponmodifier`, `pkkeepbodies`, `pkkeepdecals`, `pkcards` in developer
mode) each gate themselves on `Game.GMode == SingleGame` and
`Game.Difficulty <= 1` and answer through `CONSOLE.AddMessage(TXT.Cheats.*)`;
the settings (`fov`, `msensitivity`, `msmooth`, `crosshair`, `hudsize`,
`showfps`, `showtimer`, `name`, `tpp`, `speedmeter`, `bind`) write `Cfg` and
save it; `pos` / `rot` read the developer camera globals.

The multiplayer commands - `say`, `team`, `kick`, `callvote`, `vote`,
`gamemode`, `maxplayers`, the `netstats` family, `connect`, `server`,
`disconnect`, `reconnect` and the rest - are **not stubbed on the engine
side and need not be**: each one checks `Game.GMode` itself and either
returns or prints "Command not available in Single Player mode". The few
that reach a `NET.*` or `GAMESPY.*` native anyway do so through the
auto-stub module tables, which log and return nothing. Measured: every
`Cmd_` except the level loaders and `quit` runs through
`Hud_OnConsoleCommand` on Cathedral with 0 script errors.

`map <name>` and `benchmark` load a level through `Game:LoadLevel` /
`Game:OnPlay` from inside the command; the windowed loop's level-change
hook picks that up the same way the menu's map screen does.

## Not carried

- The demo recorder (`CONSOLE.Demo*`): no recording format was recovered.
- `HUD.PrintXY`'s `#n` colour codes inside a console line: the panel prints
  the text as it is.
- The exact colour of the echoed command line: the log's per-line colour
  for it was passed in a register the decompile did not resolve; the tan is
  what the panel visibly uses for everything else.
