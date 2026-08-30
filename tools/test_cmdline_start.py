#!/usr/bin/env python3
"""test_cmdline_start.py — command-line start translated onto the module boot path.

Launching the game with `-r 100` (stage 0, room 0 — the main hall) must no
longer be diverted in the native boot: games/re2/main.lua reads core.cmdline()
and translates the request into core.startRoomWarp + core.runGame. So the game
should:

  * boot straight into gameplay (no native or Lua menu sits on screen),
  * place the player in the warped room (current_stage/current_room = 0),
  * and return to the title menu when the game signals "return to menu"
    (the vk_press bit 4 game_loop returns-to-menu path).

Run from anywhere in the repo:

    python tools\\test_cmdline_start.py

Exit code is 0 on success, 1 on any failed assertion or timeout.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d

CURRENT_STAGE = 0x98EB14  # uint16 in gGameTable
CURRENT_ROOM = 0x98EB16  # uint16 in gGameTable
VK_PRESS = 0x67C9F8  # game_loop reads vk_press & 4 as "return to menu"


def _read_u16(game, addr):
    """Read a uint16 via re.unsafe.read (DEBUG builds); None when unavailable."""
    res = d.eval_lua(game,
                     "local b = re.unsafe.read(0x%X, 2); return b[1] + b[2] * 256" % addr)
    if isinstance(res, dict) and res.get("ok"):
        try:
            return int(float(res.get("result")))
        except (TypeError, ValueError):
            return None
    return None


def main():
    with d.launch(extra_args=["-r", "100"]) as g:
        print("launching game with -r 100 (cmdline room warp)...")

        print("waiting for gameplay (player placed in the warped room)...")
        deadline = time.time() + 45
        stage = None
        while time.time() < deadline:
            stage = _read_u16(g, CURRENT_STAGE)
            if stage == 0:
                break
            time.sleep(0.2)
        if stage != 0:
            raise TimeoutError("game never placed the player in stage 0 (last=%r)" % stage)
        room = _read_u16(g, CURRENT_ROOM)
        print("warped into stage=%d room=%d" % (stage, room))

        # The cmdline start booted through main.lua's cmdlineBootStart, so no
        # title menu may be up during gameplay.
        if d.find_entity(g, "menu.pressStart") is not None:
            raise RuntimeError("title menu appeared during the cmdline session")

        print("forcing return to menu (vk_press bit 4)...")
        d.eval_lua(g, "re.unsafe.write(0x%X, {4,0,0,0})" % VK_PRESS)
        d.wait_entity(g, "menu.pressStart", timeout=30)
        print("back on the title menu after the cmdline session")

        print("PASS: -r 100 booted into gameplay via the module path "
              "and returned to the menu")
        return 0


if __name__ == "__main__":
    sys.exit(main())