#!/usr/bin/env python3
"""test_menu_nav.py — example realtime test for the OpenRE web plugin.

Launches the game (with the intro skipped), accepts on PRESS START, drills the
root menu down to Options, opens the overlay and cancels back. Every assertion
is made against the live scene tree via /api/scene — no screenshots, no pixel
poking.

Run from anywhere in the repo:

    python tools\\test_menu_nav.py

Exit code is 0 on success, 1 on any failed assertion or timeout.

The taps use an explicit 1-frame window (ticks=1) so each press is consumed
exactly once by the menu — the "held until cleared" /api/input/set footgun and
the cascading-cursor failure mode are both avoided by construction.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d


def tap_once(g, cmd):
    """Single-frame edge; settle a moment so the menu idles between presses."""
    d.tap(g, [cmd], ticks=1)
    time.sleep(0.12)


def main():
    # The context manager guarantees the detached game is killed on success
    # AND on failure — a timed-out test must not leave a zombie holding its
    # port and locking the DLL for the next rebuild.
    with d.launch() as g:
                print("launching game (skip intro, web input on)...")

                print("waiting for title screen (menu.pressStart)...")
                d.wait_entity(g, "menu.pressStart", timeout=30)

                print("accept -> main menu...")
                tap_once(g, "accept")
                d.wait_entity(g, "menu.item.1", timeout=15)
                print("main menu up:", [n for n in d.named(g) if n and n.startswith("menu.item")])

                # ROOT_MENU order: Main Story, Extra Games, Bonuses, Options, Exit.
                # Selection starts on item 1, so three downs land on Options (index 4).
                # (The scene spawns menu.item.1..10 rows up front; only the active
                # submenu's entries are labelled, so count navigation, not entities.)
                for step in range(3):
                    tap_once(g, "down")

                print("accept -> Options overlay...")
                tap_once(g, "accept")
                d.wait_entity(g, "options", timeout=15)
                print("options overlay is open")

                print("cancel -> back to main menu...")
                tap_once(g, "cancel")
                d.wait_absent(g, "options", timeout=15)
                d.wait_entity(g, "menu.item.1", timeout=15)
                print("back on main menu")

                print("PASS: launched, skipped intro, reached the root menu, "
                      "opened Options and cancelled back")
                return 0


if __name__ == "__main__":
    sys.exit(main())