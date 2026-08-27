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

## Workflow reminder

Work happens on `re1-port`. To merge progress back into the source branch
(currently `ecs`, may become `master`) without bringing these notes along:

    scripts/re1-merge.sh [source-branch]