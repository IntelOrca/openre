-- main.lua - game module boot.
--
-- The boot sequence (splash -> intro movie -> title menu) plus the screens and
-- handoffs are driven by registered `app.*` managed systems (app.GameFlow,
-- app.Splash, app.Scheduler, app.Menu, app.Load, app.Options, app.Prologue)
-- instead of per-frame tick hooks. GameFlow is the single transition authority,
-- so this file only seeds the persistent app entity the systems read/write.

require('script/app').init()
