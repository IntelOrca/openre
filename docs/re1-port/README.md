# RE1 port notes

Working notes for the long-lived RE1 -> RE2-engine port. Everything in this
directory is branch-local: it lives on the `re1-port` branch and is stripped
whenever progress is merged back into the source branch (see
`scripts/re1-merge.sh`).

## Index

- `re1-port-handoff.md` - the original handoff: RE1 event VM opcode reference,
  Ghidra DB access, file layout, and what the port means for the engine.
- `re1-rdt-mapping.md` - RE1 RDT -> RE2 RDT chunk/field mapping (first task).

## First task status: RE1 room conversion

- `src/rdt_re1.cpp` is a self-contained RE1 RDT loader + RE2 RDT writer; all
  RE1-side structs live in that file, `re2.h` structs are copy destinations.
- `openre.exe create-game <pack> --source <re1-data>` now also converts every
  `STAGE*/ROOM*.RDT` into `room/roomXXXX.rdt` in the pack.
- Verified headlessly against `biohazard-utils`: all 318 real rooms convert
  and parse cleanly as `Rdt2`; embedded object models (TMD -> MD1) are
  byte-identical to the reference `MeshConverter`.
- Remaining: visual verification inside the running engine (needs the Windows
  binary), plus loading/warp integration for converted rooms.

## Background conversion status

- RE1 cut backgrounds are LZ-compressed 16bpp TIMs, one per camera cut, in
  `STAGE*/RC<room3><cut>.pak` (e.g. `RC1011.pak` = room 101, cut 1). Room
  pairs (`ROOM1000`/`ROOM1001`) share the first 3 hex digits of the room code.
- The compression is an LZW-style format (FUN_0046cbb0) with codes 0x100 (EOF),
  0x101 (widen) and 0x102 (end of block); the decoder lives in `rdt_re1.cpp`
  and is verified lossless against every pak.
- `openre.exe create-game` now also decompresses every `RC*.pak` and writes it
  as `roomcut/<room>_<cut:02x>.bmp` (8bpp palettized when <= 256 colors, else
  24bpp; the one-off 316x236 status screen is skipped).
- At runtime `bg_load` tries `roomcut/<room>_<cut>.bmp` first (room code set
  via `set_re1_room_code`), split into the layered 256x256 + panels layout in
  `work_buffer` and fed to the original `bg_to_surface`; falls back to the
  RE2 `roomcut.bin` ADT path. `bg_load` is now hooked (0x004C4700).
- The RE1 room loader does not exist yet, so `set_re1_room_code` is the seam
  that will feed it the current room code (B4).

## Workflow reminder

Work happens on `re1-port`. To merge progress back into the source branch
(currently `ecs`, may become `master`) without bringing these notes along:

    scripts/re1-merge.sh [source-branch]