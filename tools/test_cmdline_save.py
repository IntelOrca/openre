#!/usr/bin/env python3
"""test_cmdline_save.py — command-line save start translated onto the module boot path.

Launching the game with a save-file path argument must no longer be diverted
in the native boot: games/re2/main.lua reads core.cmdline() and translates the
request into core.loadSave + core.runGame. So the game should:

  * boot straight into gameplay restoring the saved session (no menu sits up),
  * and return to the title menu when the game signals "return to menu"
    (the vk_press bit 4 game_loop returns-to-menu path).

Uses the bundled test save (bin/Debug/savedata/test.BIOHAZARD2). Run from
anywhere in the repo:

    python tools\\test_cmdline_save.py

Exit code is 0 on success, 1 on any failed assertion or timeout.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d

VK_PRESS = 0x67C9F8  # game_loop reads vk_press & 4 as "return to menu"


def main():
    save = os.path.join(d.REPO_ROOT, "bin", "Debug", "savedata", "test.BIOHAZARD2")
    if not os.path.isfile(save):
        print("SKIP: no test save at %s" % save)
        return 0

    with d.launch(extra_args=[save]) as g:
        print("launching game with a cmdline save path...")

        print("waiting for gameplay (saved session restored)...")
        deadline = time.time() + 45
        room = None
        while time.time() < deadline:
            res = d.eval_lua(g, "local b = re.unsafe.read(0x98EB16, 2); return b[1] + b[2] * 256")
            if isinstance(res, dict) and res.get("ok"):
                try:
                    room = int(float(res.get("result")))
                except (TypeError, ValueError):
                    pass
            if room is not None:
                break
            time.sleep(0.2)
        if room is None:
            raise TimeoutError("game never reached gameplay (no room set)")
        print("saved session in room=%d" % room)

        # The cmdline start booted through main.lua's cmdlineBootStart, so no
        # title menu may be up during gameplay.
        if d.find_entity(g, "menu.pressStart") is not None:
            raise RuntimeError("title menu appeared during the cmdline session")

        print("forcing return to menu (vk_press bit 4)...")
        d.eval_lua(g, "re.unsafe.write(0x%X, {4,0,0,0})" % VK_PRESS)
        d.wait_entity(g, "menu.pressStart", timeout=30)
        print("back on the title menu after the cmdline session")

        print("PASS: cmdline save booted into gameplay via the module path "
              "and returned to the menu")
        return 0


if __name__ == "__main__":
    sys.exit(main())