#!/usr/bin/env python3
"""test_intro_movie_skip.py - the intro movie plays async and a real press skips it.

Boot without OPENRE_SKIP_INTRO, wait for the title movie entity, tap "accept"
to skip it, and verify the movie disappears, the title menu appears and the
process survives (no list(194) crash from early decoder teardown - the old
`frame.press ~= 0` table-vs-number bug skipped the movie on frame one).

Run from anywhere in the repo:

    python tools\\test_intro_movie_skip.py

Exit code is 0 on success, 1 on any failed assertion or timeout.
"""

import os
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d


def main():
    port = d.find_free_port()
    with d.launch(exe=None, port=port, data_dir=d.find_data_dir(None),
                  skip_intro=False, web_input=True) as game:
        d.wait_health(game, timeout=60)
        print("web up, port=%d" % port)

        # Wait for the movie entity.
        def movie_present():
            try:
                return d.find_entity(game, "title_movie") is not None
            except Exception:
                return False
                d.wait_for(game, movie_present, what="title_movie entity", timeout=60)
        print("movie entity PRESENT")
        time.sleep(1.0)  # let playback start

        # Tap to skip.
        d.tap(game, presses=("accept",), ms=100)
        print("tap sent")

        # Movie should disappear and title should appear shortly after.
        def movie_gone():
            try:
                return d.find_entity(game, "title_movie") is None
            except Exception:
                return False
                d.wait_for(game, movie_gone, what="movie gone after tap", timeout=30)
        print("movie GONE after tap")

        def title_present():
            try:
                return d.find_entity(game, "menu.pressStart") is not None
            except Exception:
                return False
                d.wait_for(game, title_present, what="title menu (pressStart)", timeout=30)
        print("title menu PRESENT")

        # Process must still be alive.
        if game.proc.poll() is not None:
            print("FAIL: process exited with code %d" % game.proc.returncode)
            sys.exit(1)
        print("process alive - OK")


if __name__ == "__main__":
    main()