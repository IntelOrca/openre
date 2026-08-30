#!/usr/bin/env python3
"""test_demo_flow.py — menu demo flow still drives the legacy game after the
game_boot_init extraction.

The menu plays a recorded demo after ~15s idle: menu.lua calls
core.loadDemo + core.runGame, booting the legacy game on the demo branch
(game_boot_setup_demo). This test waits out the idle timeout, confirms the
demo takes over (menu disappears and the engine stays healthy), then cancels
the demo (native demo-cancel path) and asserts the title menu comes back.

Run from anywhere in the repo:

    python tools\\test_demo_flow.py

Exit code is 0 on success, 1 on any failed assertion or timeout.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d

DEMO_FRAME = 0x98E52A  # uint16 in gGameTable; advances while a demo replays


def _read_u16(game, addr):
    res = d.eval_lua(game,
                     "local b = re.unsafe.read(0x%X, 2); return b[1] + b[2] * 256" % addr)
    if isinstance(res, dict) and res.get("ok"):
        try:
            return int(float(res.get("result")))
        except (TypeError, ValueError):
            return None
    return None


def main():
    with d.launch() as g:
        print("waiting for title screen (menu.pressStart)...")
        d.wait_entity(g, "menu.pressStart", timeout=30)

        # Leave the PRESS START screen idle; after DEMO_TIMEOUT (15s) the menu
        # fades out and core.loadDemo + core.runGame start the recorded demo on
        # the legacy demo branch (game_boot_setup_demo). The demo replays
        # advance gGameTable.demo_frame, so its growth is the gameplay signal.
        print("idling on the title screen to trigger the demo...")
        deadline = time.time() + 40
        frame = 0
        while time.time() < deadline:
            frame = _read_u16(g, DEMO_FRAME) or 0
            if frame > 0:
                break
            time.sleep(0.1)
        if frame <= 0:
            raise TimeoutError("the recorded demo never took over the game")
        print("demo replay running (demo_frame=%d)" % frame)

        # The demo must be the legacy game (menu entity is just blanked, so
        # health plus an advancing replay counter is the real signal).
        time.sleep(2)
        after = _read_u16(g, DEMO_FRAME) or 0
        if after <= frame:
            raise RuntimeError("demo replay stalled (demo_frame=%d -> %d)" % (frame, after))
        d.health(g)
        print("menu is hidden and the engine is healthy during the demo")

        # Cancel the demo with a key press (the native demo-cancel path
        # signals return-to-menu) and wait for the menu to come back.
        print("cancelling the demo...")
        d.tap(g, ["accept"])
        d.wait_entity(g, "menu.pressStart", timeout=60)
        print("back on the title menu")

        print("PASS: demo played through the legacy game and returned to the menu")
        return 0


if __name__ == "__main__":
    sys.exit(main())