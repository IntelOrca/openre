#!/usr/bin/env python3
"""driver.py -- realtime test driver for the OpenRE web plugin.

Launch the game with the engine web plugin (scripts/web.lua) enabled and drive
it over HTTP: taps, scene-tree queries, Lua eval and screenshots. Standard
library only (subprocess, socket, http.client).

See AGENTS.md "Realtime testing" for the workflow this script is part of.

CLI examples:
    python tools\\driver.py launch --wait
    python tools\\driver.py wait_entity menu.pressStart
    python tools\\driver.py tap accept
    python tools\\driver.py scene --names
    python tools\\driver.py eval "return 1+1"
    python tools\\driver.py kill

Module usage for scenario scripts (the pattern that works):
    import driver as d
    g = d.launch()
    d.wait_entity(g, "menu.pressStart")
    d.tap(g, ["accept"])
    assert d.find_entity(g, "menu.item.1"), "menu items did not appear"
"""

import argparse
import http.client
import json
import os
import socket
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXE = os.path.join(REPO_ROOT, "bin", "Debug", "openre.exe")
DEFAULT_SCRIPT_DIR = os.path.join(REPO_ROOT, "scripts")


def find_free_port(start=8080):
    """Pick a free 127.0.0.1 port by binding, so parallel sessions don't clash."""
    for port in range(start, start + 64):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                s.bind(("127.0.0.1", port))
                return port
            except OSError:
                continue
    raise RuntimeError("no free localhost port found")


def find_data_dir(override=None):
    """Locate the RE2 data directory: env override, repo-local, or run.bat default."""
    if override:
        return override
    env_dir = os.environ.get("OPENRE_RE2_DATA")
    if env_dir:
        return env_dir
    candidates = [
        os.path.join(REPO_ROOT, "data"),
        r"F:\games\openre\data",  # run.bat's default
    ]
    for c in candidates:
        if os.path.isdir(c):
            return c
    return None


class Game:
    """Handle to a launched game instance tied to one web-plugin port."""

    def __init__(self, port, proc=None):
        self.port = port
        self.proc = proc

    # ---- low-level HTTP -------------------------------------------------

    def http(self, method, path, body=None, timeout=10):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=timeout)
        raw = json.dumps(body) if body is not None else None
        conn.request(method, path, body=raw,
                     headers={"Content-Type": "application/json"})
        resp = conn.getresponse()
        data = resp.read()
        conn.close()
        if resp.status != 200:
            raise RuntimeError("HTTP %d %s: %s"
                               % (resp.status, path,
                                  data.decode("utf-8", "replace")))
        try:
            return json.loads(data.decode("utf-8"))
        except ValueError:
            return data

    def get(self, path, timeout=10):
        return self.http("GET", path, timeout=timeout)

    def post(self, path, body=None, timeout=10):
        return self.http("POST", path, body, timeout=timeout)

    def post_raw(self, path, raw, content_type="text/plain"):
        """POST with a raw (non-JSON) body — /api/eval takes plain Lua.
        The body may carry non-ASCII UTF-8 (e.g. Japanese save names), so
        strings are encoded as UTF-8 rather than the HTTP client's default
        latin-1."""
        if isinstance(raw, str):
            raw = raw.encode("utf-8")
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        conn.request("POST", path, body=raw,
                     headers={"Content-Type": content_type})
        resp = conn.getresponse()
        data = resp.read()
        conn.close()
        if resp.status != 200:
            raise RuntimeError("HTTP %d %s: %s"
                               % (resp.status, path,
                                  data.decode("utf-8", "replace")))
        try:
            return json.loads(data.decode("utf-8"))
        except ValueError:
            return data

    # ---- lifecycle -------------------------------------------------------

    def kill(self, graceful=True, timeout=10):
        """Stop this game: graceful /api/quit first, then force-kill.

        Detached games otherwise survive the test that launched them and pile
        up as zombies holding their ports (and locking the DLL on rebuild).
        """
        pid = self.proc.pid if self.proc else None
        if graceful and pid is not None:
            try:
                self.post("/api/quit", timeout=5)
            except Exception:
                pass  # plugin unreachable — a hard kill is the only option
            deadline = time.time() + timeout
            while time.time() < deadline:
                if not _pid_alive(pid):
                    return
                time.sleep(0.1)
        if pid is not None:
            _force_kill_pid(pid)
        else:
            kill_openre()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.kill()
        return False


def launch(exe=None, port=None, data_dir=None, skip_intro=True,
           web_input=True, wait=True, timeout=60):
    """Launch the engine detached with the web plugin enabled.

    Inherits the calling environment (so OPENRE_RE2_DATA etc. either pass
    through or can be overridden via data_dir). Returns a Game handle.
    """
    if port is None:
        port = find_free_port()
    env = dict(os.environ)
    env["OPENRE_WEB_PORT"] = str(port)
    env["OPENRE_WEB_INPUT"] = "1" if web_input else "0"
    env["OPENRE_GAME"] = "re2"
    if skip_intro:
        env["OPENRE_SKIP_INTRO"] = "1"
    data_dir = find_data_dir(data_dir)
    if data_dir:
        env["OPENRE_RE2_DATA"] = data_dir
    elif env.get("OPENRE_RE2_DATA"):
        del env["OPENRE_RE2_DATA"]  # never pass an unset/invalid path through
    env.setdefault("OPENRE_SAVE_PATH",
               os.path.join(REPO_ROOT, "bin", "Debug", "savedata") + os.sep)
    env.setdefault("OPENRE_SCRIPTS", DEFAULT_SCRIPT_DIR)

    cmd = [exe or DEFAULT_EXE]
    # The engine resolves the game module (games/<game>/main.lua) from the
    # process cwd or the exe's base directory (src/app.cpp), so the child must
    # run from the repo root — mirroring run.bat — or only an empty scene
    # boots with no menu.
    launch_cwd = REPO_ROOT if os.path.isdir(os.path.join(REPO_ROOT, "games")) else os.path.dirname(exe or DEFAULT_EXE)
    if sys.platform == "win32":
        proc = subprocess.Popen(
            cmd, env=env, cwd=launch_cwd,
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=(subprocess.CREATE_NEW_PROCESS_GROUP
                           | getattr(subprocess, "DETACHED_PROCESS", 0)))
    else:
        proc = subprocess.Popen(cmd, env=env, cwd=launch_cwd,
                                stdin=subprocess.DEVNULL,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL,
                                start_new_session=True)

    game = Game(port, proc)
    if wait:
        try:
            wait_health(game, timeout=timeout)
        except Exception:
            # A failed launch must not leak a detached game. The exception is
            # raised inside launch(), before any `with d.launch() as g:`
            # __exit__ can run, so clean up here.
            _force_kill_pid(proc.pid)
            raise
    return game


# ---- state probes --------------------------------------------------------

def health(game, timeout=5):
    return game.get("/api/health")


def wait_health(game, timeout=60):
    """Block until the plugin answers /api/health on the game's port."""
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            return game.get("/api/health")
        except Exception as exc:  # connection refused until the engine listens
            last = exc
            time.sleep(0.25)
    raise RuntimeError("web plugin did not come up on 127.0.0.1:%d within %ds: %s"
                       % (game.port, timeout, last))


def scene(game):
    """Full scene tree from /api/scene: {entities: [{id,name,parent,children,components}]}."""
    out = game.get("/api/scene")
    return out if isinstance(out, dict) else {"entities": []}


def named(game):
    """Just the entity names in walk order (the usual test assertion input)."""
    return [e.get("name") for e in scene(game).get("entities", [])]


def find_entity(game, name, timeout=0):
    """Return the first scene node with the given name, or None.

    With timeout > 0, poll until it appears (use wait_entity for that).
    """
    if timeout:
        return wait_entity(game, name, timeout)
    for e in scene(game).get("entities", []):
        if e.get("name") == name:
            return e
    return None


def entity_field(game, name, component, field):
    """Read a field off the first matching entity/component, or None."""
    node = find_entity(game, name)
    if not node:
        return None
    for comp in node.get("components", []):
        if comp.get("type") == component:
            for f in comp.get("fields", []):
                if f.get("n") == field:
                    return f.get("v")
    return None


def wait_entity(game, name, timeout=15):
    """Poll the scene until an entity with this name exists."""
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            node = find_entity(game, name, timeout=0)
            if node is not None:
                return node
        except Exception as exc:
            last = exc
        time.sleep(0.1)
    raise TimeoutError("entity %r did not appear within %.1fs%s"
                       % (name, timeout, (": %s" % last) if last else ""))


def wait_absent(game, name, timeout=15):
    """Poll the scene until an entity with this name is gone (PRESS START fix check)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if find_entity(game, name, timeout=0) is None:
            return True
        time.sleep(0.1)
    raise TimeoutError("entity %r still present after %.1fs" % (name, timeout))


def wait_for(game, predicate, what, timeout=15):
    """Generic poll: call predicate() until truthy, else TimeoutError."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(0.1)
    raise TimeoutError("%s not satisfied within %.1fs" % (what, timeout))


# ---- input ---------------------------------------------------------------

def input_state(game):
    return game.get("/api/input")


def tap(game, presses=("accept",), ms=200, ticks=None):
    """Auto-clearing single edge. This is the one to use for menu navigation."""
    body = {"press": list(presses), "ms": ms}
    if ticks is not None:
        body["ticks"] = ticks
    res = game.post("/api/input/tap", body)
    if not res.get("ok"):
        raise RuntimeError("tap failed: %s" % res)
    return res


def set_input(game, state=(), presses=()):
    """Hold input until cleared (a held key -- avoid for menus)."""
    res = game.post("/api/input/set", {"state": list(state), "press": list(presses)})
    if not res.get("ok"):
        raise RuntimeError("input/set failed: %s" % res)
    return res


def clear_input(game):
    res = game.post("/api/input/clear")
    if not res.get("ok"):
        raise RuntimeError("input/clear failed: %s" % res)
    return res


def eval_lua(game, code):
    """Run Lua in the web VM. Game-module state needs re.evalIn (see AGENTS.md)."""
    res = game.post_raw("/api/eval", code)  # raw LUA string body, not JSON-wrapped
    if isinstance(res, dict) and not res.get("ok"):
        raise RuntimeError("eval failed: %s" % res)
    return res


def lua_quote(s):
    """Quote s as a Lua string literal (escapes backslash + quote; UTF-8 passthrough)."""
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def eval_game(game, code):
    """Run Lua inside the game module VM (the only VM that can require core ui modules).

    The catalogue (require("ui.font")) and its handle cache live there, so font
    refreshes must run in this VM; the web VM only shares the options bindings.
    Returns the /api/eval response; asserts the re.evalIn call itself succeeded.
    """
    res = eval_lua(game, "return re.evalIn('module.game', " + lua_quote(code) + ")")
    if isinstance(res, dict) and not res.get("ok"):
        raise RuntimeError("evalIn wrapper failed: %s" % res)
    # re.evalIn pushes ok + the game-VM result string; the web plugin serializes
    # the first return, so result "false" means the game VM reported an error.
    result = res.get("result") if isinstance(res, dict) else ""
    if result == "false":
        raise RuntimeError("evalIn in module.game failed for: %s" % code)
    return res


# ---- misc ----------------------------------------------------------------

def screenshot(game, out_path="screenshot.bmp"):
    conn = http.client.HTTPConnection("127.0.0.1", game.port, timeout=15)
    conn.request("GET", "/api/screenshot")
    resp = conn.getresponse()
    data = resp.read()
    conn.close()
    if resp.status != 200:
        raise RuntimeError("screenshot failed: %d" % resp.status)
    with open(out_path, "wb") as f:
        f.write(data)
    return out_path


def _pid_alive(pid):
    if sys.platform == "win32":
        out = subprocess.run(["tasklist", "/FI", "PID eq %d" % pid, "/NH"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        text = out.stdout.decode("utf-8", "replace")
        return "openre.exe" in text
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def _force_kill_pid(pid):
    """Terminate one PID across session boundaries.

    Stop-Process/taskkill occasionally fail against a game hung in another
    (user) session; CIM Terminate carries the needed rights and reported
    ReturnValue 0 in testing where those did not.
    """
    if sys.platform != "win32":
        subprocess.run(["kill", "-9", str(pid)], check=False)
        return
    script = ("$p = Get-CimInstance Win32_Process -Filter \"ProcessId=%d\" "
              "-ErrorAction SilentlyContinue; "
              "if ($p) { Invoke-CimMethod -InputObject $p -MethodName Terminate | Out-Null }" % pid)
    subprocess.run(["powershell", "-NoProfile", "-Command", script],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def kill_openre():
    """Kill every stray game instance (the DLL is locked while one runs)."""
    if sys.platform != "win32":
        subprocess.run(["pkill", "-9", "-f", "openre.exe"], check=False)
        return
    script = ("Get-CimInstance Win32_Process -Filter \"Name='openre.exe'\" "
              "-ErrorAction SilentlyContinue | "
              "Invoke-CimMethod -MethodName Terminate | Out-Null")
    subprocess.run(["powershell", "-NoProfile", "-Command", script],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


# ---- CLI ----------------------------------------------------------------

def _resolve_port(args):
    if args.port:
        return args.port
    env_port = os.environ.get("OPENRE_WEB_PORT")
    return int(env_port) if env_port else 8080


def cmd_launch(args):
    game = launch(exe=args.exe, port=args.port, data_dir=args.data_dir,
                  skip_intro=not args.no_skip_intro,
                  web_input=not args.no_input, wait=not args.no_wait, timeout=args.timeout)
    print(json.dumps({"ok": True, "port": game.port, "pid": game.proc.pid}))


def cmd_health(args):
    g = Game(_resolve_port(args))
    print(json.dumps(health(g)))


def cmd_scene(args):
    g = Game(_resolve_port(args))
    if args.names:
        for n in named(g):
            print(n)
    else:
        print(json.dumps(scene(g)))


def cmd_wait_entity(args):
    g = Game(_resolve_port(args))
    node = wait_entity(g, args.what, args.timeout)
    print(json.dumps({"ok": True, "id": node.get("id"), "name": node.get("name")}))


def cmd_wait_absent(args):
    g = Game(_resolve_port(args))
    wait_absent(g, args.what, args.timeout)
    print(json.dumps({"ok": True, "absent": args.what}))


def cmd_tap(args):
    g = Game(_resolve_port(args))
    res = tap(g, args.press, ms=args.ms, ticks=args.ticks)
    print(json.dumps(res))


def cmd_input(args):
    g = Game(_resolve_port(args))
    print(json.dumps(input_state(g)))


def cmd_eval(args):
    g = Game(_resolve_port(args))
    print(json.dumps(eval_lua(g, args.code)))


def cmd_screenshot(args):
    g = Game(_resolve_port(args))
    print(screenshot(g, args.out))


def cmd_kill(args):
    kill_openre()
    print(json.dumps({"ok": True}))


def main(argv=None):
    p = argparse.ArgumentParser(prog="driver.py", description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    pl = sub.add_parser("launch", help="launch the game detached and wait for the plugin")
    pl.add_argument("--exe", default=None, help="path to openre.exe (default: bin\\Debug\\openre.exe)")
    pl.add_argument("--port", type=int, default=None, help="explicit port (default: auto-pick free)")
    pl.add_argument("--data-dir", default=None, help="OPENRE_RE2_DATA override)")
    pl.add_argument("--wait", dest="no_wait", action="store_false", help="wait for the plugin to come up (default)")
    pl.add_argument("--no-wait", dest="no_wait", action="store_true", help="launch and return once the process is up")
    pl.set_defaults(no_wait=False)
    pl.add_argument("--no-skip-intro", action="store_true", help="let the intro movie play")
    pl.add_argument("--no-input", action="store_true", help="disable web input injection")
    pl.add_argument("--timeout", type=int, default=60, help="seconds to wait for the plugin")
    pl.set_defaults(fn=cmd_launch)

    ph = sub.add_parser("health", help="GET /api/health")
    _add_port(ph); ph.set_defaults(fn=cmd_health)

    ps = sub.add_parser("scene", help="dump the scene tree")
    ps.add_argument("--names", action="store_true", help="print entity names only")
    _add_port(ps); ps.set_defaults(fn=cmd_scene)

    pw = sub.add_parser("wait_entity", help="wait until an entity name exists")
    pw.add_argument("what"); pw.add_argument("--timeout", type=int, default=15)
    _add_port(pw); pw.set_defaults(fn=cmd_wait_entity)

    pwa = sub.add_parser("wait_absent", help="wait until an entity name is gone")
    pwa.add_argument("what"); pwa.add_argument("--timeout", type=int, default=15)
    _add_port(pwa); pwa.set_defaults(fn=cmd_wait_absent)

    pt = sub.add_parser("tap", help="auto-clearing single input edge")
    pt.add_argument("--press", nargs="+", default=["accept"], help="commands, e.g. --press accept up")
    pt.add_argument("--ms", type=int, default=200)
    pt.add_argument("--ticks", type=int, default=None, help="exact frame count override")
    _add_port(pt); pt.set_defaults(fn=cmd_tap)

    pi = sub.add_parser("input", help="show current injected input state")
    _add_port(pi); pi.set_defaults(fn=cmd_input)

    pe = sub.add_parser("eval", help="run Lua in the web VM")
    pe.add_argument("code")
    _add_port(pe); pe.set_defaults(fn=cmd_eval)

    psc = sub.add_parser("screenshot", help="save a screenshot to a BMP file")
    psc.add_argument("--out", default="screenshot.bmp")
    _add_port(psc); psc.set_defaults(fn=cmd_screenshot)

    pk = sub.add_parser("kill", help="taskkill all openre.exe instances")
    pk.set_defaults(fn=cmd_kill)

    args = p.parse_args(argv)
    args.fn(args)


def _add_port(sub):
    sub.add_argument("--port", type=int, default=None,
                     help="plugin port (default: $OPENRE_WEB_PORT or 8080)")


if __name__ == "__main__":
    main()