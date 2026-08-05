# Audio decompile progress

Checklist of RE2 audio functions that need decompiling as part of the SDL3 audio migration.

**Workflow:** each sub-agent picks one unchecked function, decompiles it (using IDA via `ida-pro-mcp-*` tools) into hand-written C++ code, replaces its `interop::call`/raw-pointer wrapper in `src/audio.cpp` (or the file noted below), ticks it off here, and makes a commit. Do not tick a function off until its wrapper is replaced and the build passes.

- Where the function lives in our code is noted per section: the `ss_*` / `snd_*` / `bgm_*` / `Xa_*` functions go in `src/audio.cpp` unless noted otherwise.
- Keep the original address as a `// 0x........` comment above the function.
- Order: decompile the `ss_*` layer first, then the game-logic layer (the game logic calls the `ss_*` wrappers).
- Hooks: after reimplementing a function, ensure its `interop::writeJmp` hook is (or stays) registered in `bgm_init_hooks()`. When all callers are implemented, hooks can be removed.

## ss_* layer (core buffer/mixer API) — decompile first

- [x] 0x00433830 SsClose (63)
- [x] 0x004338F0 SsPlay (812)
- [x] 0x00433C40 SsStopAll (370)
- [x] 0x00433DC0 SsShutdown (322)
- [x] 0x00433F10 SsUnloadGroup (528)
- [x] 0x00434140 SsUnloadBgm (160)
- [ ] 0x004341E0 SsStopGroup (668)
- [ ] 0x004344A0 SsLoadBanks (750)
- [ ] 0x004347B0 SsGetStatus (285)
- [ ] 0x004348F0 SsSetPan (414)
- [ ] 0x00434AB0 SsSetVol (530)
- [ ] 0x00434CF0 SsGetVolume (393)

## Game-logic layer (Snd_* / Xa_* / bgm_*)

- [ ] 0x004EC250 Snd_sys_init2 (238)
- [ ] 0x004EC350 Snd_sys_init_sub (185)
- [ ] 0x004EC410 Snd_sys_init_sub2 (64)
- [ ] 0x004EC450 Snd_load_core (639)
- [ ] 0x004EC6D0 Snd_load_arms (252)
- [ ] 0x004EC7D0 Snd_room_load (205)
- [ ] 0x004EC8A0 Snd_load_em (231)
- [ ] 0x004EC9C0 Snd_bgm_set (537)
- [ ] 0x004ECBE0 Snd_bgm_ck (250)
- [ ] 0x004ECCE0 Snd_bgm_play_ck (179)
- [ ] 0x004ED050 Snd_bgm_sub (518)
- [ ] 0x004ED260 Snd_bgm_fade_ON (137)
- [ ] 0x004ED2F0 Snd_bgm_ctr (1558)
- [ ] 0x004ED950 Snd_se_on (1241)
- [ ] 0x004EDE30 Snd_se_enem (263) — implement in `src/enemy.cpp` (wrapper is there)
- [ ] 0x004EDF40 Snd_se_walk (1032) — implement in `src/player.cpp` (wrapper is there)
- [ ] 0x004EE350 Snd_se_call (230)
- [ ] 0x004EE440 Snd_bgm_fade (814)
- [ ] 0x004EE780 Snd_se_3D (1067)
- [ ] 0x004EEBD0 Snd_se_dir_ck (82)
- [ ] 0x004EEC30 Xa_play (145)
- [ ] 0x004EECD0 Xa_stop (36)
- [ ] 0x004EED00 Xa_control (14)
- [ ] 0x004EED10 Xa_control_stop (25)
- [ ] 0x004EED30 Xa_control_init (8)
- [ ] 0x004EED40 Xa_control_play (59)
- [ ] 0x004EED80 Xa_control_end (71)
- [ ] 0x004EEDD0 Xa_set_volume (28)
- [ ] 0x004EEDF0 Cd_system_control (15)
- [ ] 0x004EEE00 SsSeqSetDecrescendo (50)
- [ ] 0x004EEE40 sub_4EEE40 (236)
- [ ] 0x004EEF30 Bgm_ck_room112 (29)
- [ ] 0x004EEF50 Bgm_ck_room115 (29)
- [ ] 0x004EEF70 Room_fs_ck (256)

## Out of scope (not audio)

- `sub_4EC010` / `sub_4EC050` / `sub_4EC080` / `sub_4EC180` / `sub_4EC340` — sprite/CLUT helpers for the item-name display (`sub_4EBDB0`), not audio. Do not decompile for this task.
- `unused_436820` / `unused_4368A0` — marked unused, folded into `bgm_channels_init`; only touched if the migration needs them.
