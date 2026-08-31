#!/usr/bin/env python3
"""test_hot_reload.py - the web plugin survives a game-module hot reload.

Boot skip-intro, reach the title, touch a game-module script to trigger the
engine's module reload, and verify the web plugin keeps answering and the
module reboots to the title (fast-reload path) with the scene rebuilt.

Run from anywhere in the repo:

    python tools\\test_hot_reload.py

Exit code is 0 on success, 1 on any failed assertion or timeout.
"""

import os
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d

TOUCH_FILE = os.path.join(REPO_ROOT, "games", "re2", "script", "app.lua")


def main():
    port = d.find_free_port()
    with d.launch(exe=None, port=port, data_dir=d.find_data_dir(None),
                  skip_intro=True, web_input=True) as game:
        d.wait_health(game, timeout=60)

        def title_up():
            try:
                return d.find_entity(game, "menu.pressStart") is not None
            except Exception:
                return False
        d.wait_for(game, title_up, what="title before reload", timeout=30)
        print("title up before reload")

        # Touch the game-module script: the engine reloads the whole module.
        mtime = os.path.getmtime(TOUCH_FILE)
        os.utime(TOUCH_FILE, (mtime, mtime + 2))

        # Web must survive; the scene is cleared and rebuilt on fast reload.
        def reloaded():
            try:
                return d.find_entity(game, "menu.pressStart") is not None
            except Exception:
                return False
        d.wait_health(game, timeout=15)  # web still answering?
        print("web alive right after touch")
        d.wait_for(game, reloaded, what="title after reload", timeout=30)
        print("title up after reload")

        if game.proc.poll() is not None:
            print("FAIL: process exited with code %d" % game.proc.returncode)
            sys.exit(1)
        print("process alive - OK")


if __name__ == "__main__":
    main()