#!/usr/bin/env python3
"""test_pause_menu.py — realtime test for the Lua in-game pause menu.

Launches the game with a room warp (stage 0), opens the pause menu with the
`menu` command, and exercises every item:

  * Restart (no save present) -> stays paused, no dialog (invalid sound).
  * Load Game -> Load screen in pause mode; cancel returns to the paused menu.
  * Options -> options popup; cancel returns to the paused menu.
  * Quit -> confirmation dialog; No returns to the paused menu.
  * Quit -> Yes -> returns to the title menu.

Run from anywhere in the repo:

    python tools\\test_pause_menu.py

Exit code is 0 on success, 1 on any failed assertion or timeout.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d


def tap_once(g, cmd):
    """Single-frame edge; settle so the pause menu idles between presses."""
    d.tap(g, [cmd], ticks=1)
    time.sleep(0.15)


def pause_names(g):
    return [n for n in d.named(g) if n and n.startswith("pause")]


def main():
    with d.launch(extra_args=["-r", "100", "-p", "0", "-s", "a"]) as g:
        print("waiting for gameplay (legacy node)...")
        d.wait_entity(g, "game", timeout=30)
        time.sleep(2.0)  # let the warped room settle
        print("in gameplay")

        # ---- open the pause menu ------------------------------------------
        tap_once(g, "menu")
        d.wait_entity(g, "pause", timeout=15)
        names = pause_names(g)
        print("pause menu open:", names)
        for want in ("pause.heading", "pause.item.1", "pause.item.2",
                     "pause.item.3", "pause.item.4"):
            assert d.find_entity(g, want), "missing " + want
        heading = d.entity_field(g, "pause.heading", "core.Text", "text")
        print("heading text:", heading)
        assert heading and "PAUSE MENU" in heading
        item1 = d.entity_field(g, "pause.item.1", "core.Text", "text")
        print("item1 text:", item1)
        assert item1 and "Restart" in item1

        # ---- Restart with no save: stays paused, no dialog ---------------
        tap_once(g, "accept")
        time.sleep(0.3)
        assert d.find_entity(g, "dialog") is None, "no-save Restart opened a dialog"
        assert d.find_entity(g, "pause") is not None, "Restart closed the pause menu"
        print("Restart (no save) kept the menu open")

        # ---- Load Game (item 2) -> load screen -> cancel back -------------
        tap_once(g, "down")
        tap_once(g, "accept")
        d.wait_entity(g, "load", timeout=15)
        print("Load screen opened from pause")
        tap_once(g, "cancel")
        d.wait_absent(g, "load", timeout=15)
        assert d.find_entity(g, "pause") is not None, "cancel did not return to pause menu"
        assert d.find_entity(g, "game") is not None, "game node vanished"
        print("Load screen cancelled back to the pause menu")

        # ---- Options (item 3) -> popup -> cancel back ---------------------
        tap_once(g, "down")
        tap_once(g, "accept")
        d.wait_entity(g, "options", timeout=15)
        print("Options popup opened from pause")
        tap_once(g, "cancel")
        d.wait_absent(g, "options", timeout=15)
        assert d.find_entity(g, "pause") is not None, "cancel did not return to pause menu"
        print("Options cancelled back to the pause menu")

        # ---- Quit (item 4) -> dialog -> No --------------------------------
        tap_once(g, "down")
        tap_once(g, "accept")
        d.wait_entity(g, "dialog", timeout=15)
        msg = d.entity_field(g, "dialog.message", "core.Text", "text")
        print("Quit dialog message:", msg)
        assert msg and "Quit the game and return to the main menu?" in msg
        assert "(Any unsaved data will be lost.)" in msg
        # The box is a 75%-wide, 50%-tall vertical flex container.
        box_w = d.entity_field(g, "dialog.box", "core.UiSize", "width")
        box_h = d.entity_field(g, "dialog.box", "core.UiSize", "height")
        print("dialog box size:", box_w, box_h)
        assert box_w == 640 * 0.75 and box_h == 480 * 0.5
        assert d.entity_field(g, "dialog.box", "core.UiFlex", "orientation") == "vertical"
        assert d.entity_field(g, "dialog.message", "core.UiSize", "weight") == 1.0
        for b in ("dialog.button.1", "dialog.button.2"):
            assert d.find_entity(g, b), "missing " + b
        tap_once(g, "cancel")  # cancel == No
        d.wait_absent(g, "dialog", timeout=15)
        assert d.find_entity(g, "pause") is not None, "No did not return to pause menu"
        print("Quit dialog No returned to the pause menu")

        # ---- Quit -> Yes -> title menu ------------------------------------
        tap_once(g, "accept")
        d.wait_entity(g, "dialog", timeout=15)
        tap_once(g, "accept")  # Yes is the default selection
        d.wait_absent(g, "dialog", timeout=15)
        d.wait_absent(g, "pause", timeout=15)
        d.wait_entity(g, "menu.pressStart", timeout=30)
        print("Quit -> Yes returned to the title menu")

        print("PASS: pause menu opened, all items navigated, "
              "popups/dialog cancelled back, Quit returned to the title")
        return 0


if __name__ == "__main__":
    sys.exit(main())