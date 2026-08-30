#!/usr/bin/env python3
"""test_font_swap.py — realtime proof of the [options] font swap + glyph fallback.

Three halves:

1. Realtime swap. Boots the re2 menu (retro sprite fonts), reads the
   core.Text font cookies on every menu entity, flips the [options] "font"
   mode and refreshes the core Lua catalogue (the same effect as moving the
   in-game options menu Font row — the catalogue pinning is core.setRoleFont,
   driven by options.lua through ui.font.refresh()). The native swap walk then
   remaps every scene text entity onto the modern TTF role font; asserting the
   reverse after flipping back. Assertions compare the *resource path* behind
   each cookie (via /api/resources), not the raw cookie number: the swap walk
   compares FontResource pointers, so cookies are free to be renumbered by
   refcount churn between loads.

2. Consecutive-swap reliability. Immediately flips the option twice more to
   prove the role registry is not left in a pending state by repeated toggles.

3. Sprite->TTF fallback. Creates probe TextComponents through the web VM using
   the retro big/small fonts (gfx.loadFont of the same files the catalogue
   resolves) with Japanese and accented text (glyphs std.fnt does not
   contain). In retro mode those codepoints must be resolved through the
   fallback chain (bundled TTF -> system TTF -> tofu) rather than blank — the
   same path the Load Game screen takes for its Japanese save names. The probe
   also proves the swap walk remaps freshly created entities, not just the
   menu's pre-built ones, without ever releasing the role fonts.

Run from anywhere in the repo:

    python tools\\test_font_swap.py

Exit code is 0 on success, 1 on any failed assertion or timeout. Screenshots
go to bin\\Debug\\font_test\\.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver as d

SHOT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "bin", "Debug", "font_test")

RETRO_BIG_PATH = "fonts/std.fnt"
RETRO_SMALL_PATH = "fonts/sm.fnt"


def tap_once(g, cmd):
    """Single-frame edge; settle so the menu idles between presses."""
    d.tap(g, [cmd], ticks=1)
    time.sleep(0.15)


def eval_lua_ok(g, code):
    return d.eval_lua(g, code)


def check_eval(g, code, what):
    res = eval_lua_ok(g, code)
    ok = res.get("ok") if isinstance(res, dict) else True
    assert ok, "%s failed: %r" % (what, res)
    return res


def cookie_paths(g):
    """Map every live cookie -> resource path (via /api/resources)."""
    js = g.get("/api/resources")
    entries = js.get("resources", js) if isinstance(js, dict) else js
    out = {}
    for e in entries:
        for c in e.get("cookies") or []:
            out[c] = e.get("path")
    return out


def text_font_paths(g, name_prefix):
    """Entity key -> resource path for core.Text.font of matching entities."""
    paths = cookie_paths(g)
    out = {}
    for e in d.scene(g).get("entities", []):
        name = e.get("name") or ""
        if not name.startswith(name_prefix):
            continue
        for comp in e.get("components", []):
            if comp.get("type") == "core.Text":
                for f in comp.get("fields", []):
                    if f.get("n") == "font":
                        cookie = f.get("v")
                        out[e.get("id")] = paths.get(cookie)
    return out


def lua_str(s):
    """Quote s as a Lua string literal. Escapes only the backslash and the
    quote character. UTF-8 content passes through as raw bytes, which the
    engine Lua accepts and the text pipeline decodes as codepoints."""
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def uniform_font_path(g, name_prefix):
    """The single resource path backing every matching Text entity (None if
    not uniform or any cookie no longer resolves to a live resource)."""
    paths = text_font_paths(g, name_prefix)
    if not paths:
        return None
    unique = set(paths.values())
    if len(unique) != 1 or None in unique:
        return None
    return next(iter(unique))


def flip_font(g, mode):
    """Set the [options] font mode, then refresh the core ui.font catalogue.

    The catalogue lives in the game VM (it owns the per-(role, mode) handles
    and pins the roles into the native registry via core.setRoleFont), so the
    refresh must run there through re.evalIn; the web VM only shares the
    options bindings. After the refresh the native FontSwapSystem remaps all
    scene text on the next contiguous frame.
    """
    check_eval(g, "return options.set('font', %d)" % mode,
               "options.set('font', %d)" % mode)
    d.eval_game(g, 'require("ui.font").refresh()')
    time.sleep(0.6)


def assert_realtime_swap(g):
    # Uniform retro cookie first (every role 'big' resolves to one std.fnt).
    retro_path = uniform_font_path(g, "menu.item")
    assert retro_path == RETRO_BIG_PATH, \
        "menu text should be uniformly on the sprite big font (%s), got %r" \
        % (RETRO_BIG_PATH, retro_path)

    print("flipping [options] font to modern...")
    flip_font(g, 1)
    modern_path = uniform_font_path(g, "menu.item")
    assert modern_path is not None and modern_path.startswith("fonts/Barlow"), \
        "menu text not fully on the modern TTF role font after swap: %r" % modern_path
    print("retro->modern: menu text moved to", modern_path)

    print("flipping [options] font back to retro...")
    flip_font(g, 0)
    back_path = uniform_font_path(g, "menu.item")
    assert back_path == RETRO_BIG_PATH, \
        "menu text not back on the sprite font after swap: %r" % back_path
    print("modern->retro: menu text back on", back_path)
    return modern_path


def assert_second_cycle(g):
    """Flip twice more immediately after the first swap cycle: the role
    registry must survive consecutive toggles (no screenshots/probes between)."""
    flip_font(g, 1)
    mod = uniform_font_path(g, "menu.item")
    assert mod is not None and mod.startswith("fonts/Barlow"), \
        "second cycle did not swap: %r" % mod
    print("cycle 2 retro->modern OK:", mod)
    flip_font(g, 0)
    back = uniform_font_path(g, "menu.item")
    assert back == RETRO_BIG_PATH, "second cycle did not swap back: %r" % back
    print("cycle 2 modern->retro OK:", back)


def make_probe(g, name, text, font_expr):
    """Create a scene Text entity via the web VM; returns False on eval error.

    The font handle is retained in a persistent table (real game code keeps
    font handles alive for as long as text uses them), so the component's
    cookie stays valid for the swap walk to match by resource pointer."""
    code = (
        "local e = scene.createEntity(scene.getRoot('user'))\n"
        "scene.setField(e, 'name', %s)\n"
        "scene.setField(e, 'x', 320)\n"
        "scene.setField(e, 'y', 360)\n"
        "local f = %s\n"
        "local t = scene.addComponent(e, 'core.Text')\n"
        "scene.setField(t, 'font', f)\n"
        "scene.setField(t, 'text', %s)\n"
        "local s = scene.addComponent(e, 'core.UiSize')\n"
        "scene.setField(s, 'width', 1)\n"
        "scene.setField(s, 'height', 1)\n"
        "__probe_fonts = __probe_fonts or {}\n"
        "__probe_fonts[%s] = f\n"
        "return 'spawned'"
    ) % (lua_str(name), font_expr, lua_str(text), lua_str(name))
    res = eval_lua_ok(g, code)
    ok = res.get("ok") if isinstance(res, dict) else True
    if not ok:
        print("probe %r eval error: %r" % (name, res))
        return False
    return True


def main():
    os.makedirs(SHOT_DIR, exist_ok=True)
    with d.launch() as g:
        print("waiting for title screen (menu.pressStart)...")
        d.wait_entity(g, "menu.pressStart", timeout=30)

        print("accept -> main menu...")
        tap_once(g, "accept")
        d.wait_entity(g, "menu.item.1", timeout=15)
        print("main menu up:", [n for n in d.named(g) if n and n.startswith("menu.item")])

        boot = check_eval(g, "return options.get('font')", "boot font mode")
        mode = boot.get("result")
        assert mode == "0", "boot should be retro (font = 0), got %r" % mode
        print("boot mode: retro (font option = 0)")

        assert_realtime_swap(g)
        assert_second_cycle(g)

        d.screenshot(g, os.path.join(SHOT_DIR, "menu_retro.bmp"))
        print("retro menu screenshot:", os.path.join(SHOT_DIR, "menu_retro.bmp"))

        # Sprite->TTF fallback probe: std.fnt principal, Japanese + accents.
        # std.fnt has none of these codepoints, so in retro mode they must be
        # resolved by the fallback chain (bundled TTF -> system TTF -> tofu),
        # the exact path the Load Game screen takes for its save names. The
        # probes use the same *resource* the retro role fonts resolve to, so
        # the swap walk can match them by FontResource pointer.
        assert make_probe(g, "font.test.fallback",
                          "Fallback: \u30ec\u30aa\u30f3\uff3f\uff10\uff12\uff3f\u8b66\u5bdf\u7f72\u3000L'été déjà",
                          "gfx.loadFont('fonts/std.fnt')"), "fallback probe failed"
        # Small-role probe: uses the second role font (sm.fnt retro).
        assert make_probe(g, "font.test.small", "Small role",
                          "gfx.loadFont('fonts/sm.fnt')"), "small probe failed"
        time.sleep(0.8)

        node = d.find_entity(g, "font.test.fallback")
        assert node is not None, "fallback probe entity missing after layout"

        # The retro role fonts must still be live after the probes exercised
        # gfx.loadFont handles whose __gc releases must be refcount-balanced
        # against the handles acquired by the probe and the catalogue.
        assert uniform_font_path(g, "menu.item") == RETRO_BIG_PATH, \
            "menu lost its retro font after probe creation"

        d.screenshot(g, os.path.join(SHOT_DIR, "probes_retro.bmp"))
        print("retro probes screenshot:", os.path.join(SHOT_DIR, "probes_retro.bmp"))

        # Flip modern: the swap walk must remap the *freshly created* probe
        # entities too (they hold the retro role cookies) onto the same modern
        # role fonts the menu uses.
        flip_font(g, 1)
        _, mode = None, check_eval(g, "return options.get('font')", "options.get('font')").get("result")
        print("post-flip font mode:", mode)
        probe_big = d.entity_field(g, "font.test.fallback", "core.Text", "font")
        probe_small = d.entity_field(g, "font.test.small", "core.Text", "font")
        menu_path = uniform_font_path(g, "menu.item")
        print("menu path after flip:", menu_path)
        assert menu_path is not None and menu_path.startswith("fonts/Barlow"), \
            "menu not on the modern TTF after probe-side flip: %r" % menu_path
        paths = cookie_paths(g)
        probe_big_path = paths.get(probe_big)
        probe_small_path = paths.get(probe_small)
        assert probe_big_path == menu_path, \
            "probe not swapped to the same modern big font as the menu: %r vs %r" \
            % (probe_big_path, menu_path)
        assert probe_small_path is not None and probe_small_path != probe_big_path \
            and probe_small_path.startswith("fonts/Barlow"), \
            "small-role probe should use a different modern font (got %r)" % probe_small_path
        print("probes remapped by swap walk: big=%r small=%r" % (probe_big_path, probe_small_path))

        d.screenshot(g, os.path.join(SHOT_DIR, "probes_modern.bmp"))
        print("modern probes screenshot:", os.path.join(SHOT_DIR, "probes_modern.bmp"))

        # Flip back to retro: both the menu and the probes must return to the
        # sprite role fonts.
        flip_font(g, 0)
        back_path = uniform_font_path(g, "menu.item")
        print("menu path after flip back:", back_path)
        assert back_path == RETRO_BIG_PATH, \
            "menu text not back on the sprite font after flip back: %r" % back_path
        paths = cookie_paths(g)
        probe_big2_path = paths.get(d.entity_field(g, "font.test.fallback", "core.Text", "font"))
        probe_small2_path = paths.get(d.entity_field(g, "font.test.small", "core.Text", "font"))
        assert probe_big2_path == RETRO_BIG_PATH, \
            "probe not back on the retro big font: %r" % probe_big2_path
        assert probe_small2_path == RETRO_SMALL_PATH, \
            "small-role probe not back on the retro small font: %r" % probe_small2_path

        d.screenshot(g, os.path.join(SHOT_DIR, "menu_retro_final.bmp"))
        d.screenshot(g, os.path.join(SHOT_DIR, "probes_retro_final.bmp"))
        print("retro screenshots:", os.path.join(SHOT_DIR, "menu_retro_final.bmp"),
              os.path.join(SHOT_DIR, "probes_retro_final.bmp"))

        print("PASS: realtime retro/modern swap (menu + freshly created text), "
              "consecutive-toggling reliability, and sprite->TTF fallback for non-ASCII")
        return 0


if __name__ == "__main__":
    sys.exit(main())