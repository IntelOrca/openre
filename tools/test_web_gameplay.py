#!/usr/bin/env python3
"""test_web_gameplay.py - web plugin stays alive and responsive during a non-blocking runGame session.

Start a gameplay session (room warp) via the module path and verify the web
plugin still answers eval/tap while the legacy game (LegacyGame node) drives
the frame (core.runGame is non-blocking), then that it returns to the menu.

Run from anywhere in the repo:

    python tools\\test_web_gameplay.py

Exit code is 0 on success, 1 on any failed assertion or timeout.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d

CURRENT_STAGE = 0x98EB14
CURRENT_ROOM = 0x98EB16
VK_PRESS = 0x67C9F8


def _read_u16(game, addr):
    res = d.eval_lua(game, "local b = re.unsafe.read(0x%X, 2); return b[1] + b[2] * 256" % addr)
    if isinstance(res, dict) and res.get("ok"):
        try:
            return int(float(res.get("result")))
        except (TypeError, ValueError):
            return None
    return None


with d.launch(skip_intro=True, extra_args=["-r", "100"]) as g:
    print("waiting for gameplay (stage 0)...")
    deadline = time.time() + 45
    stage = None
    while time.time() < deadline:
        stage = _read_u16(g, CURRENT_STAGE)
        if stage == 0:
            break
        time.sleep(0.2)
    if stage != 0:
        raise TimeoutError("game never reached gameplay (stage=%r)" % stage)
    print("gameplay running (stage=%d room=%d)" % (stage, _read_u16(g, CURRENT_ROOM)))
    time.sleep(1.0)

    r1 = d.eval_lua(g, "return 1 + 1")
    print("eval_lua:", r1)
    if not (isinstance(r1, dict) and r1.get("ok") and r1.get("result") == "2"):
        raise AssertionError("eval_lua did not return 2: %r" % r1)

    # eval_game's /api/eval result serializes the ok flag of the game-VM eval,
    # so select(2, re.evalIn(...)) retrieves the actual game-VM return value.
    r2 = d.eval_lua(
        g,
        "return select(2, re.evalIn('module.game', " + d.lua_quote("return 2 * 3") + "))",
    )
    print("eval_game:", r2)
    if not (isinstance(r2, dict) and r2.get("ok") and r2.get("result") == "6"):
        raise AssertionError("eval_game did not return 6: %r" % r2)

    # The module path is mid-session here, so the managed core.inGame() binding
    # must report the legacy node enabled.
    r3 = d.eval_lua(
        g,
        "return select(2, re.evalIn('module.game', "
        + d.lua_quote("return core.inGame() and 'ingame' or 'menu'")
        + "))",
    )
    print("core.inGame:", r3)
    if not (isinstance(r3, dict) and r3.get("ok") and r3.get("result") == "ingame"):
        raise AssertionError("core.inGame() should be 'ingame' during the session: %r" % r3)

    d.tap(g, ["down"])
    print("tap sent")
    time.sleep(0.5)
    h = d.eval_lua(g, "return 7")
    print("health after tap:", h)
    if not (isinstance(h, dict) and h.get("ok") and h.get("result") == "7"):
        raise AssertionError("web plugin unresponsive after tap: %r" % h)

    print("forcing return to menu (vk_press bit 4)...")
    d.eval_lua(g, "re.unsafe.write(0x%X, {4,0,0,0})" % VK_PRESS)
    d.wait_entity(g, "menu.pressStart", timeout=30)
    print("back on the title menu")
    print("PROBE OK")
