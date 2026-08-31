#!/usr/bin/env python3
"""verify_new_game.py -- verify the full New Game -> prologue -> two movies
-> skip -> game session path works with no Lua errors.

Navigates the title menu, picks New Game (JPN) -> Leon A -> Standard, lets the
prologue run, skips the slideshow, waits for each opening movie entity to
appear, skips it, and finally asserts core.inGame() becomes true. Also greps
the captured engine log for Lua ERR lines.
"""

import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))
import driver as d


def tap_once(g, cmd, settle=0.5):
    d.tap(g, [cmd], ticks=1)
    time.sleep(settle)


def named_movies(g):
    return [n for n in d.named(g) if n and n.startswith("game.movie")]


def main():
    port = d.find_free_port()
    log_path = os.path.join(REPO, "bin", "Debug", "newgame_verify_capture.log")
    env = dict(os.environ)
    env["OPENRE_WEB_PORT"] = str(port)
    env["OPENRE_WEB_INPUT"] = "1"
    env["OPENRE_GAME"] = "re2"
    env["OPENRE_SKIP_INTRO"] = "1"
    env["OPENRE_RE2_DATA"] = d.find_data_dir()
    env.setdefault("OPENRE_SAVE_PATH", os.path.join(REPO, "bin", "Debug", "savedata") + os.sep)
    env.setdefault("OPENRE_SCRIPTS", os.path.join(REPO, "scripts"))
    with open(log_path, "wb") as f:
        proc = subprocess.Popen(
            [os.path.join(REPO, "bin", "Debug", "openre.exe")],
            env=env, cwd=REPO,
            stdout=f, stderr=subprocess.STDOUT,
            creationflags=(subprocess.CREATE_NEW_PROCESS_GROUP
                           | getattr(subprocess, "DETACHED_PROCESS", 0)))
    g = d.Game(port, proc)
    d.wait_health(g, timeout=60)

    d.wait_entity(g, "menu.pressStart", timeout=30)
    tap_once(g, "accept")
    d.wait_entity(g, "menu.item.1", timeout=15)
    tap_once(g, "accept")
    time.sleep(1.0)
    tap_once(g, "down", 0.4)
    tap_once(g, "down", 0.4)
    tap_once(g, "accept", 1.0)
    time.sleep(1.0)
    tap_once(g, "accept", 1.0)
    time.sleep(1.0)
    tap_once(g, "accept", 0.5)
    d.wait_entity(g, "prologue.canvas", timeout=30)
    print("prologue running")

    # Skip the slideshow; then catch each opening movie by entity name.
    g.post_raw("/api/eval",
               "return re.evalIn('module.game', 'require(\"script/prologue\").skip()')")
    print("slideshow skip requested; waiting for first movie entity...")
    d.wait_entity(g, "game.movie", timeout=20)
    print("first opening movie playing; skipping...")
    tap_once(g, "accept", 1.0)
    d.wait_entity(g, "game.movie", timeout=20)
    print("second opening movie playing; skipping...")
    tap_once(g, "accept", 1.5)

    # The prologue onFinished should now hand over to the legacy game.
    ok = False
    for _ in range(40):
        res = d.eval_lua(g, "return tostring(core.inGame())")
        v = res.get("result") if isinstance(res, dict) else str(res)
        if v == "true":
            ok = True
            break
        time.sleep(0.5)
    print("core.inGame() ->", v)
    alive = d._pid_alive(proc.pid)
    print("process alive:", alive)

    try:
        g.post("/api/quit", timeout=5)
    except Exception:
        pass
    time.sleep(1)
    d._force_kill_pid(proc.pid)
    proc = None

    # Grep the captured log for Lua errors.
    errs = 0
    with open(log_path, "rb") as f:
        for raw in f:
            line = raw.decode("utf-8", "replace")
            if "]: [script] game:" in line and ("ERR" in line or "error" in line.lower()):
                errs += 1
    print("lua error lines in log:", errs)

    if not ok:
        print("FAIL: core.inGame() never became true")
        return 1
    if not alive:
        print("FAIL: process died")
        return 1
    if errs:
        print("FAIL: %d lua error lines" % errs)
        return 1
    print("PASS: new game reached a live session with no Lua errors")
    return 0


if __name__ == "__main__":
    sys.exit(main())