-- main.lua - game module boot.
--
-- The boot sequence (splash -> intro movie -> title menu) is driven by the
-- registered `app.*` systems in script/app.lua instead of a scheduler plus a
-- re.subscribe(HookKind.tick) closure. GameFlow is the single transition
-- authority and app.Splash drives the splash, so this file only seeds the
-- persistent app entity the systems read/write.

require('script/app').init()
