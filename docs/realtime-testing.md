# Realtime testing — full reference

This is the detailed companion to the "Realtime testing" section of
`AGENTS.md`. It documents the web plugin, the HTTP API and the testing
ground rules. Agents that want the minimum-to-run should just read AGENTS.md
and `tools\driver.py --help` / `tools\test_menu_nav.py`.

## What it is

The engine ships a web plugin, `scripts/web.lua`, that binds
`127.0.0.1:OPENRE_WEB_PORT` (default 8080) and exposes the live scene tree,
input injection and Lua eval over HTTP. It is engine-generic: it knows
nothing about the RE2 game. Use it before ever reaching for screenshots —
asserting against the scene tree is faster and far more reliable than image
processing.

The Python driver `tools\driver.py` (stdlib only) wraps every endpoint for
both CLI use and as an importable module:

```python
import driver as d
with d.launch() as g:          # detaches the game, waits for the plugin
    d.wait_entity(g, "menu.pressStart")
    d.tap(g, ["accept"])
    assert d.find_entity(g, "menu.item.1")
```

## Launching

Driver (recommended — auto-picks a free port, sets `OPENRE_SKIP_INTRO=1`
and `OPENRE_WEB_INPUT=1`, inherits/auto-detects the RE2 data dir):

```
python tools\driver.py launch --wait
python tools\driver.py launch --port 8091 --no-skip-intro
```

Manual (used by run.bat; the engine resolves `games/<game>/main.lua` from
the process cwd or the exe's base dir, so run from the repo root):

```
set OPENRE_WEB_INPUT=1
set OPENRE_WEB_PORT=<unique port>
set OPENRE_SKIP_INTRO=1
set OPENRE_GAME=re2
set OPENRE_RE2_DATA=<data dir>
set OPENRE_SAVE_PATH=bin\Debug\savedata\
set OPENRE_SCRIPTS=scripts
set OPENRE_LOG_VERBOSITY=info
bin\Debug\openre.exe
```

`OPENRE_SKIP_INTRO=1` skips the splash and the title movie
(`pl0\zmovie\title_l.bin`) so a test boot lands straight on the title menu
(`menu.pressStart` is usually already in the scene by the time the plugin
answers ~1s later).

## Endpoint cheat-sheet

| Endpoint | Purpose |
|---|---|
| `GET /api/health` | alive probe; confirms the port/plugin are up |
| `GET /api/scene` | full scene tree: entity `name`, `parent`, `children`, component fields |
| `GET /api/input` | current injected input state |
| `POST /api/input/tap` | auto-clearing single edge: `{"press":["accept"],"ms":200}` — use for menu navigation |
| `POST /api/input/set` | held input (`{"state":[...],"press":[...]}`) until `/api/input/clear` — a held key |
| `POST /api/input/clear` | clear injected input |
| `POST /api/eval` | run Lua in the web VM (body is raw Lua, not JSON). Reach game-module state with `re.evalIn("module.game", "return ...")` |
| `GET /api/screenshot` | raw BMP — last resort, prefer scene assertions |
| `POST /api/quit` | graceful shutdown (`scene.exit`) — lets the engine unwind instead of force-killing |
| `POST /api/inspector/select` | set which entity the in-game guide lines track: `{"entity":<id>}` or `{"entity":null}` to clear |
| `GET /api/resources`·`/api/systems`·`/api/components` | inspector data for the web page |

## Driver reference

The driver's state/input helpers (`wait_entity`, `wait_absent`, `tap`,
`set_input`, `clear_input`, `eval_lua`, `screenshot`) take a `Game` handle
(returned by `launch()`, or `driver.Game(port)` to attach to an already
running instance).

Useful CLI subcommands (see `python tools\driver.py --help`):

- `launch [--wait|--no-wait] [--port N] [--no-skip-intro] [--timeout N]`
- `wait_entity <name>` / `wait_absent <name>` — poll the scene tree
- `tap --press down --ticks 1` — single-frame edge (see taps below)
- `scene [--names]` — dump the tree or just names
- `input` / `clear` — inspect/clear injected input
- `eval "return 1+1"` — run Lua
- `screenshot --out file.bmp`
- `kill` — force-kill every stray `openre.exe` (CIM Terminate)

## Ground rules

### Assert on the scene tree first

`menu.pressStart`, `menu.item.N`, `options` (the options overlay root),
`menu.background`, etc. are all entities with names. Query `/api/scene` (or
`d.wait_entity`/`d.wait_absent`) and assert on names/text fields before
considering a screenshot. Screenshots are the last resort for things the
scene tree cannot express (e.g. actual rendered pixels).

Rendered text is stored as markup in the `core.Text` `text` field, e.g.
greyed-out locked menu items are `<span color="#9a9a9a…">???</span>` while
live items use `#ffffff`. "Grayed out / dim / coloured" assertions should
read that markup from the scene — no screenshot needed.

### Taps, not held input

`/api/input/set` press **persists until cleared** — it behaves like a held
key, so a menu that consumes one edge per frame cascades through items
("cursoring through the menu very quickly"). Use `/api/input/tap`, which
arms the commands for a short window and auto-clears.

The menu consumes input per frame, so for cursor moves prefer a
short/`ticks` window so a press is consumed exactly once:

```
d.tap(g, ["down"], ticks=1)   # exactly one menu step, no cascade
```

A tap with `ms` is converted to frames at a nominal 60fps; if you know the
real frame rate (or the exact timing is fragile), pass `ticks`.

### Unique port per session

Multiple concurrent sessions/instances clash on the default 8080. `launch()`
auto-picks a free port by bind-probing from 8080 upward, so parallel sessions
don't fight. Set `OPENRE_WEB_PORT` explicitly when you need a known port.

### Never leak a game process (zombies)

`launch()` detaches the game from your test process, so a test that finishes
— or fails with an exception — leaves the game window running unless you
clean up. A stray instance holds its port and locks the DLL for the next
rebuild (the build link fails while `openre.exe` runs).

Rules:
- In scenario scripts: `with d.launch() as g:` — the context manager sends
  `POST /api/quit` (graceful) then force-kills on exit, pass or fail.
- For one-shot CLI runs: `python tools\driver.py kill` when done.
- **Never launch and forget.**

Why this keeps catching people: `Stop-Process`/`taskkill` can silently fail
against the game when it hangs in another session, and `taskkill /IM` is
blocked in some agent sandboxes — so a "kill" that appears to run may do
nothing. The driver's kill uses CIM Terminate, which terminates hung games
across session boundaries.

### Kill before rebuilding

If you ever launch manually, kill `openre.exe` before rebuilding — the DLL
is locked while the process runs. `python tools\driver.py kill` handles it.

### Skip the intro movie

`OPENRE_SKIP_INTRO=1` avoids waiting out the splash and
`pl0\zmovie\title_l.bin` on every boot.

## Example scenario

`python tools\test_menu_nav.py` is the canonical end-to-end example: launch,
wait for `menu.pressStart`, tap accept into the root menu (entities
`menu.item.1..10`; ROOT_MENU has five entries, selection starts on item 1),
tap down three times to reach Options, open the overlay (entity `options`
appears), cancel back (entity gone), and assert throughout via the scene
tree. It exits 0 on success and self-cleans through the context manager.

## Debugging

- A boot that answers `/api/health` but shows only `user/game/movie/fade/
  overlay/web_input` in the scene means the game module never loaded — the
  engine resolves `games/<game>/main.lua` from the process cwd or the exe
  base dir, so the cwd must be the repo root (or `bin\Debug\games` must
  exist).
- `re.evalIn('module.game', 'return 1+1')` → `false, "no script VM registered
  under 'module.game'"` is exactly that failure mode.
- A listener on `127.0.0.1:8080` that accepts connections but never answers
  usually means the process is hung (often a zombie from an earlier run) —
  check for stray `openre.exe` processes and `python tools\driver.py kill`.
- A browser tab pointed at the inspector page keeps `:8080` connections
  alive, which can make a stale instance look healthy.