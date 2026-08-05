# Handoff — OpenRE audio → SDL3 migration

Fresh agent starting point for the audio SDL3 migration in the **IntelOrca/openre** project.

## Project / workspace

- Repo: `IntelOrca/openre` (open-source reimplementation of Resident Evil 2; Win32 DLL injected into `bio2 1.10.exe`).
- Branch: `intelorca-migrate-audio-to-sdl3` (worktree at `M:\git\copilot-worktrees\openre\intelorca-urban-spoon`).
- Session folder (artifacts/plan/todos): `C:\Users\Ted\.copilot\session-state\f239b518-8f6e-4b20-a172-2ccc03b6cc82`
- Main checkout (do NOT edit): `M:\git\openre` (currently on `hybrid-sdl3`).

## Current state

- A plan was created and **approved** (`plan.md` in the session folder above; interactive mode, not yet executed).
- The session is in **planning→interactive** handoff: no code changes have been made on this branch yet (working tree clean, branch points at `master`).
- A progress checklist was just created: **`docs/audio-progress.md`** in the repo — all 46 functions needing decompiling, grouped by layer.

## Task summary

Migrate the RE2 audio subsystem (`src/audio.cpp`) from Win32/DirectSound/ACM/mmio to SDL3:

1. **New module `src/system_audio.h` / `src/system_audio.cpp`** fully encapsulating SDL3 (device + software mixer, voice/buffer store keyed by `(type, sub)` mirroring the `audio_Buffer*` GameTable slots, play/stop/status/vol/pan/unload, RIFF WAV parsing + hand-rolled MS-ADPCM decoder + SDL3 resample, init/shutdown). SDL-free API in `openre::system::audio`.
2. **`audio.cpp` becomes pure game logic**: no platform APIs, no SDL3 — delegates to `system::audio`; SAP file reads move to `system::fs`.
3. **Decompile + reimplement all 46 audio stubs** (currently `interop::call`/raw-pointer). 12 `Ss*` layer functions + 34 `Snd_*`/`Xa_*`/`bgm_*` game-logic functions (incl. `snd_se_walk`→`player.cpp`, `snd_se_enem`→`enemy.cpp`). Keep the `// 0x........` address comment above each.
4. **Rework already-decompiled** `ss_init`, `ss_create_buffer`, `ss_load_sap/steps/bgm`, `ss_init_buffers`, `ss_init_2`, `ss_voice_load/parse` to drop Win32 APIs; remove the `acmDriverEnumCallback` hook (0x4329B0).
5. **vcxproj**: add `system_audio.cpp/.h`; drop `dsound.lib;msacm32.lib` (only audio.cpp uses them). Keep `winmm.lib` while `timeGetTime` remains.
6. **Validate**: `format.bat`, `build.bat`, `run.bat`.

## Key decisions already made (do not revisit)

- **Branch base**: stack the branch onto **`hybrid-sdl3`** first (user chose this). `hybrid-sdl3` already has `system_window`, `system_filesystem`, `system_config`, SDL3 project wiring (`SDL3.lib` + `CopySDL3.dll` target). `audio.cpp`/`audio.h` are byte-identical between `master` and `hybrid-sdl3`.
- The gitignored `lib\` dir (SDL3 headers/libs/dll) is **not** in the worktree — copy from `M:\git\openre\lib` (or point vcxproj paths there) so the build works.
- **Out of scope (not audio):** `sub_4EC010/50/80/180/340` are sprite/CLUT helpers for the item-name display (`sub_4EBDB0`). `unused_436820/4368A0` are marked unused. See `docs/audio-progress.md`.

## How to continue

1. Rebase the branch onto `hybrid-sdl3` (`git reset --hard hybrid-sdl3` — branch has no unique commits) and ensure `lib\` (SDL3) is present.
2. Create `src/system_audio.h/.cpp` (reference `system_window`/`system_filesystem` on `hybrid-sdl3` for the pattern).
3. Work through `docs/audio-progress.md`: **ss_* layer first**, then game logic. Each sub-agent decompiles one function (IDA is available via `ida-pro-mcp-*` tools; target binary `bio2 1.10.exe`), replaces its wrapper, ticks the checkbox, commits.
4. Rework the already-decompiled functions, update the vcxproj, then build + run.

## Technical reference (from analysis)

- DirectSoundBuffer vtable offsets used by `SsPlay`/`SsStopAll`/etc.: `+36` SetCurrentPosition, `+48` Play, `+52` Stop, `+60` SetVolume, `+64` SetPan, `+72` GetStatus. `SsGetStatus` returns bit 0 = playing.
- Buffer globals: `audio_pMarniSnd` (0x669CF0), `audio_BufferSBgm[2]` 0x669CF4, `audio_BufferDoor[4]` 0x669D00, `audio_BufferEnemy[32]` 0x669D10, `audio_BufferBgm[3]` 0x669D90, `audio_BufferRoom[48]` 0x669DA0, `audio_BufferCore[22]` 0x669E60, `audio_BufferArms[32]` 0x669EB8, `audio_BufferVoice[2]` 0x669F38, `MarniSnd_Frequency` 0x669D9C, `MarniSnd_SoundDepth` 0x669F40, `XA_idx` 0x669F44.
- `enable_dsound` (0x524EB6) is the master audio-on gate; keep it.
- `SsSetVol` maps PS1 vol (0–127) × global (`Bgm_vol` 0x693471 / `Sfx_vol` 0x693C48) → DirectSound centibels (`259*v−10000` clamped to [−10000,0], or `2*(9*v−1143)` for v≥32). SDL3 needs linear gain.
- SAP files are RIFF/WAV containers with `fmt ` + `data` chunks; MS-ADPCM (tag 2) or PCM (tag 1). MS-ADPCM must be decoded by hand (ACM is being removed).
- All xrefs to the DirectSound globals are inside the ss_* set covered by this migration — self-contained.
- The `.bgm` sequencer data (`byte_6D730C`, `vab_id`, `seq_ctr`) is game data; playback still routes through `Ss*` → `system::audio`.

## Todo tracking

The session SQL DB has the migration todos (`rebase-hybrid-sdl3`, `system-audio-module`, `decompile-ss-layer`, `rework-audio-cpp`, `decompile-game-logic`, `vcxproj-audio`, `validate-build`) with dependencies. Query with the `sql` tool (`todos`, `todo_deps` tables). The fine-grained decompile checklist is `docs/audio-progress.md`.

## Suggested skills

- **decompile-batch** — decompile several RE2 functions (in parallel) into hand-written C++ code replacing their `interop::call` wrappers; the main tool for the 46-function decompile effort.
- **commit** — each sub-agent commits after ticking a function off in `docs/audio-progress.md` (per the working agreement: no `git add`/`commit` without explicit instruction; use the commit skill when the user asks).
- **handoff** — for any further session-to-session handoffs.

## Redaction note

No API keys, passwords, or personal data were handled in this conversation.
