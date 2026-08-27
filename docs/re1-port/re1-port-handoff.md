# RE1 → RE2-Engine-Clone Port — Handoff Notes

**Purpose.** Working notes for a separate project: porting Resident Evil 1 (PC, 1996) to an
open-source clone of the RE2 engine. Scope: convert all rooms over (RDT files), their scripts
(SCD event streams), all the other files, and enemy AI. This document is everything gathered so
far plus how to fetch more.

**Sources.** The Ghidra reverse-engineering work that produced these notes lived on the
`re1-opcode-names` branch of `biohazard-utils` (`docs/re1-event-opcodes.md`, removed from master
at merge). The full recovered doc is embedded below (§4). The Ghidra DB is separate and survives
(§2).

---

## 1. The game and its files

- Binary: `Bio.exe` — RE1 PC 1996, MSVC PE32, base `0x400000`.
- Game data: `/mnt/f/games/re1` (WSL drvfs, mounted read-only).
  - Rooms: `US/STAGE*/ROOM*.RDT` (and `JPN/...`). **1934 US room events** across all rooms.
  - Script data lives inside the RDT event streams (SCD), not separate `.scd` files.
- An `.idc` pair of prior IDA work exists: `/mnt/f/games/re1/Bio.exe.idc` + `Bio.exe.types.idc`
  (function signatures + comments, imported into Ghidra — see §2).

## 2. RE1 Ghidra database — how to access it, rename symbols, fetch more

The whole setup lives in `~/projects/re1-ghidra`:

| Path | Purpose |
| --- | --- |
| `docker-compose.yml` | headless pyghidra-mcp server (`ghcr.io/clearbluejar/pyghidra-mcp:latest`) |
| `projects/` | **Persistent** Ghidra DB — `re1.gpr`, `re1.rep`, caches. Survives restarts. Re-analysis takes minutes; don't delete casually. |
| `exports/` | Decompiled handler dumps (`scd_handlers/`), generated symbol JSON (`idc_symbols.json`), `triage_report.txt` |
| `scripts/` | `idc_to_json.py` + `mcp_apply.py` (import prior IDA work over MCP) |

### Start the server
```sh
cd ~/projects/re1-ghidra
docker compose up -d        # if 'docker compose' is unavailable, try docker-compose
docker compose logs -f      # watch analysis progress
curl -s http://localhost:8000/mcp   # endpoint health check
```
Service binds `127.0.0.1:8000:8000`, runs as `user 1000:1000`, `-Xmx4g`, project name `re1`
at `/projects` inside the container (bind mount of `./projects`), mounts the game read-only at
`/data/re1`, and waits for analysis of `/data/re1/Bio.exe`.

### Connecting via MCP (how an agent fetches more)
Registered in `~/.config/opencode/opencode.json`:
```json
"re1-ghidra": { "type": "remote", "url": "http://localhost:8000/mcp", "enabled": true }
```
Available MCP tools (Ghidra server side):
- `decompile_function` / `disassemble` — pseudo-C / listing at an address or symbol
- `search_code` (semantic + literal), `search_strings`, `search_symbols_by_name`
- `list_xrefs`, `list_exports`, `list_imports`, `gen_callgraph`, `read_bytes`
- `rename_function`, `set_comment` (decompiler/plate/eol/…), `set_function_prototype`,
  `set_variable_type`, `rename_variable`
- `save` — persist DB changes (symbol renames live in the Ghidra DB; save them)
- `list_project_binaries`, `list_project_binary_metadata`

### Renaming symbols (workflow used to date)
1. Locate handler/function (address or current label) via search.
2. Rename + comment + set prototypes through the MCP tools above; `save` afterwards.
3. Keep a running list of renames (a "Ghidra DB changes" section, like §4's) and mirror names
   into `biohazard-utils` tables (`Bio1ConstantTable`, `Re1EventOpcode`).
Names already applied live in the DB — they are NOT in master's history anymore.

### Importing prior IDA work (one-off)
```sh
python3 scripts/idc_to_json.py exports/idc_symbols.json \
    /mnt/f/games/re1/Bio.exe.idc /mnt/f/games/re1/Bio.exe.types.idc
python3 scripts/mcp_apply.py
```
(Done over MCP because the running server holds the project lock.)

### Existing exports
- `exports/scd_handlers/` — 7 decompiled `.c` dumps: `scd_event_loop.c`, `event_opcode_08.c`,
  `event_opcode_block.c`, `event_opcode_fork.c`, `event_opcode_single.c`,
  `event_opcode_work_set.c`, `sub41B020_room_setup.c`.
- `exports/idc_symbols.json` — generated symbol JSON.
- `exports/triage_report.txt` — opcode attribution report.

## 3. VM architecture (short version)

- `scd_event_loop` @0x42E190 ticks every event task in the task array @0xC3F71C, stride 0x34.
- Task init: `ev_task_init` @0x42E110 — `PC = ev_script_table[0xAA7A94][event]`.
  Fork (top op 05) allocates a slot via `ev_spawn_event_task` @0x42E140; chain (op 08) re-inits.
- Three instruction streams share one ISA (see table in §4): top-level (switch in
  `scd_event_loop`, 1 op/tick), inner ISA (dispatch table @0x4ACB68 — 81 function pointers,
  opcodes 0x00–0x50), state-1 actor cmds (`event_state_1_logic` @0x42E520), state-2 tweens
  (`event_state_2_logic` @0x42EC90).
- `ev_status` @0xC33090 bits: `0x100000` cut/message, `0x40000` movie, `0x20000` XA stream,
  `0x2000` item-consumption, `0x400`/`0x800` view toggles.
- Inventory (item pocket) @0xAA8E48 `{id byte, count byte}` pairs; `item_find` @0x4141A0,
  `item_compact` @0x414010.

## 4. Recovered reference: RE1 Event Script VM — Opcode Reference

*(Recovered verbatim from `docs/re1-event-opcodes.md` on the old branch; symbol renames live
in the Ghidra DB, saved.)*

Derived from `Bio.exe` (RE1 PC 1996, MSVC PE32, base 0x400000) via the Ghidra
project `~/projects/re1-ghidra/projects/re1.gpr` (MCP: `localhost:8000/mcp`).
Session work of 2026-08-25/26; symbol renames live in the Ghidra DB (saved).

### VM architecture

`scd_event_loop` @0x42E190 ticks every event task in the task array
@0xC3F71C, stride 0x34:

```
+0x00 u8   state (0 run, 1 actor cmds, 2 tweens)
+0x02 u8   alive
+0x03 u8   SP (condition/loop stack pointer)
+0x04 ptr  target object (selected by top-level op 04)
+0x08 ptr  PC (points AT opcode byte)
+0x0C      ret-addr stack [SP] (if/else unwind)
+0x1C      do-condition stack [SP]
+0x2C      loop counters [SP] (i16)
```

Task init: `ev_task_init` @0x42E110 — `PC = ev_script_table[0xAA7A94][event]`.
Fork (op 05) allocates a free slot via `ev_spawn_event_task` @0x42E140;
chain (op 08) re-inits the current task.

Three instruction streams share one ISA:

| Stream | Dispatcher | Cadence |
|---|---|---|
| top-level | switch in `scd_event_loop` | 1 op/tick |
| inner ISA (block/single payloads) | dispatch table @0x4ACB68 — `ev_run_block_sync` @0x415030 runs it synchronously for blocks; `event_opcode_single` @0x42F140 one instr per tick for singles | see left |
| state-1 actor cmds (entered by op 01) | `event_state_1_logic` @0x42E520 | 1 op/tick |
| state-2 tweens (entered by ops 02/03) | `event_state_2_logic` @0x42EC90 | 1 op/tick |

The inner-ISA dispatch table @0x4ACB68 holds 81 function pointers
(opcodes 0x00–0x50). The same table serves FD enddo condition evaluation
(the `do` selector byte indexes it). Handler returns 0 = condition false /
halt. Full per-opcode map: plate comment on 0x4ACB68 in Ghidra.

Status bits in `ev_status` @0xC33090 observed:
`0x100000` cut/message active, `0x40000` movie playing (`movie_on`),
`0x20000` XA stream active, `0x2000` item-consumption related,
`0x400`/`0x800` view toggles.

Inventory (item pocket) @0xAA8E48: `{id byte, count byte}` pairs,
count @0xC386F7, selected index @0xC38719. `item_find` @0x4141A0,
`item_compact` @0x414010.

### Top-level opcodes (`Re1EventOpcode`)

| Op | Name | Encoding | Semantics |
|---|---|---|---|
| 00 | Nop | 1 | advance |
| 01 | ActorBegin | 1 | enter state 1 (actor command stream on target obj) |
| 02 | TweenBegin | 1 | enter state 2; zeroes obj +0x85/86/87 |
| 03 | TweenBeginAlt | 1 | also enters state 2 (difference TBD) |
| 04 | WorkSet | 3 `04 sel idx` | select target obj: 0=player, 1=enemy n, 2=obj table, 3=item obj table |
| 05 | Fork | 4 `05 b ev` | spawn new task running event `ev` |
| 06 | Block | 2 `06 len`; payload = u16-chunked inner code | runs synchronously via `ev_run_block_sync`, then `PC += len` |
| 07 | Single | 2 `07 len`; payload = inner code | resumes at start+len; executes one inner instr per tick |
| 08 | Chain | 2 `08 ev` | re-init current task with event `ev` |
| 09 | Disable | 2 `09 idx` | kill task slot `idx` |
| 80 | ActorReset | 1 | clears target +0x85, falls into 8B |
| 81 | ActorMotion | 2 / 10 | `act_motion:u` (2B: motion id -> obj+0xD8). With param low nibble set: `act_motion_path:uUUUuu` (10B: motion id, pos x or target sel, pos y or index, pos z, flags lo -> +0xD9 / hi -> +0xDA). TODO: motion 0x93 reinterprets word0/1 as selector+index |
| 82 | ActorMotionBitClear | 1 | obj+0xD8 &= ~0x10 |
| 83 | ActorAnimSeq | 7 | anim id16 (bit 12 = special), +0xC6/+0xC8 u16s, +0xDB byte |
| 84 | ActorAnimFlags | 4 | anim -> +0xBD, byte -> +0xDB, bits -> +0xE0 |
| 85 | ActorAnimSet | 4 | anim -> +0x86, bytes -> +0xBD/+0xDB |
| 86 | ActorIdle | 1 | +0x84=1, +0x86=0, +0x8A=0 |
| 87 | ActorFlagOp | 4 | mode 0=set/1=or/2=xor of mask16 into obj+0xE0 |
| 88 | ActorParamSet | 4 | u16 -> obj+0xDE |
| 89 | ActorActionA | 2 | aux motion id -> obj+0xBE (read by enemy AI) |
| 8A | ActorActionB | 2 | aux motion id + flags -> obj+0xBD/0xBF |
| 8B | ActorEnd | 1 | leave actor-cmd state (op 80 falls through into this) |
| F6 | PushCond | 1 | SP++; falls through to F7 |
| F7 | SkipIf | 1 | if status bit17 clear AND msg flag clear: skip next byte; SP-- |
| F8 | Sleep | 4 `F8 ? cnt16` | push counter (see note) |
| F9 | SleepTick | 1 | dec counter; done: PC+=3, SP-- |
| FA | For | 4 `FA ? cnt16` | counter = cnt16; ret[SP] = body PC |
| FB | EndFor | 1 | dec; 0 -> PC+=1, SP--; else PC = ret[SP] |
| FC | Do | 2 `FC n` | cond stack[SP] = PC+2 (selector byte); PC += n |
| FD | EndDo | 1 | evaluate cond fn = ISA table[selector]; 0 -> exit else loop |
| FE | Next | 1 | PC += 1 |
| FF | Finish | 1 | task done (falls into FE path); streams terminate with 0xFF |

### Inner ISA (block/single payloads; `_opcodes` / `OpcodeV1`)

### Sub-state ISAs (`_actorOpcodes` / `_tweenOpcodes`)

Rendered with `act_`/`tw_` prefixes. Rows 0x80–0x8B above are the actor ISA
(names `act_reset`, `act_motion`, `act_motion_path` (10-byte form),
`act_motion_bitclr`, `act_anim_seq`,
`act_anim_flags`, `act_anim_set`, `act_idle`, `act_flag_op`, `act_param_set`,
`act_action_a`, `act_action_b`, `act_end`; `00` = `act_nop`; `FF` =
`act_finish`). `80` is 1 byte and falls through into `8B`, ending the stream.

Tween ISA (`tw_*`, entered by ops 02/03; exits on op `01` = `tw_end`):

| Op | Name | Size | Notes |
|----|------|------|-------|
| 00 | tw_nop | 1 | semantics TODO |
| 01 | tw_end | 1 | leaves the tween state |
| 02 | tw_pos_add | 1 | TODO |
| 03 | tw_rot_add | 1 | TODO |
| 04 | tw_pos_rot_add | 1 | TODO |
| 05 | tw_set_pos | 4 | s8 position deltas TODO |
| 06 | tw_set_rot | 4 | s8 rotation deltas |
| 07 | tw_abs_pos | 8 | 3×u16 (+pad) |
| 08 | tw_set_field | 3 | obj[b1] = b2 |
| 09 | tw_set_8a | 2 | obj+0x8A = b1 |
| 0A | tw_set_sel | 4 | selector-based set TODO |
| 0B | tw_abs_rot | 8 | 3×u16 (+pad) |
| FF | tw_finish | 1 | terminates task |

Sizes: `_instructionSizes1` (verified by round-trip against all US rooms).
Names marked * are best-guess (TODO confirm); empty = still unknown.

| Op | Size | Name | Semantics |
|---|---|---|---|
| 00 | 2 | end | halt (SP=0, return 0) |
| 01 | 2 | if | push continuation addr (PC+1+b) |
| 02 | 2 | else | pop mark; PC += b |
| 03 | 2 | endif | pop; continue |
| 04 | 4 | ck | flag bit test across group tables (mode in operand) |
| 05 | 4 | set | flag bit set/clr/xor |
| 06 | 4 | cmpb | work byte vs imm, 6 modes (eq/lt/le/gt/ge/ne) |
| 07 | 6 | cmpw | work word vs imm16 |
| 08 | 4 | setb | work byte = imm |
| 09 | 2 | cutnext | save cur id, walk msg list, status \|= 0x100000 |
| 0A | 2 | cutcurr | restore saved id, clear 0x100000 |
| 0B | 4 | message | `msg_show` @0x491980 (sets msg-active flag tested by F7) |
| 0C | 26 | door_aot_set | full AOT entry write |
| 0D | 18 | aot_set | AOT entry write |
| 0E | 2 | nop | — |
| 0F | 8 | scene_setup* | stage/room globals, status low bits, `obj_inst_snapshot` |
| 10 | 2 | testitem | b == DAT_00c38702 (id copied from pickup flows) |
| 11 | 2 | testpickup | b == DAT_00c38703 |
| 12 | 10 | aot_reset | partial AOT rewrite |
| 13 | 4 | aot_delete | zero first fields of AOT n |
| 14 | 4 | evt_exec | spawn task(b1,b2) |
| 15 | 2 | bgm_play | set mask bit (hi+3) in bgm word 0xAA8D80; `snd_start` |
| 16 | 2 | bgm_stop | clear bit; `snd_stop` |
| 17 | 10 | se_play_3d | sub-op 0..3: xyz pos / banked play (`snd_play_banked`) |
| 18 | 26 | item_aot_set | item world spawn incl. model load + effects |
| 19 | 2 | setbyte | item obj ptr[id]->+2 = b |
| 1A | 2 | item_ck | item id present in inventory? |
| 1B | 22 | enemy | enemy slot init (0x18C stride table @0xC35334) |
| 1C | 6 | timer_setup* | mode, count16, enable bits -> counter @0xC38706 |
| 1D | 2 | ck_last_item* | inventory[sel-1].id == b |
| 1E | 1 | xa_on | XA/movie cmd; status \|= 0x20000 |
| 1F | 28 | obj | object placement (default handler async; own fn sync) |
| 20 | 14 | dir_set* | player rot/pos tween targets |
| 21 | 14 | pos_set* | enemy rot/pos targets (0x18C slots) |
| 22 | 4 | ck_item_count* | count inventory entries matching category vs imm |
| 23 | 2 | cut_auto | status 0x100000 = b |
| 24 | 4 | *(unknown)* | indirect call via thunk table PTR_LAB_004b60b8[b] |
| 25 | 4 | aot_switch* | enable/disable room entries by id (@0xAA7AD0 stride 0x24) |
| 26 | 0 | *(size 0)* | returns PC (always true); unparsable, kept as data |
| 27 | 2 | snd_fadeout* | `snd_fadeout_all`: ramp all SE to silence over t |
| 28 | 6 | eml_state* | sub-op 0..10 writes enemy slot state bytes |
| 29 | 2 | *(unknown)* | status \|= 0x400; stores byte+ptr (sub-mode?) |
| 2A | 12 | effect | `effect_spawn` (id, type, dur, target) |
| 2B | 4 | plw_anim* | writes player +0xBD/BE/BF + packed mode word |
| 2C | 2 | *(unknown)* | find-in-inventory + zero + compact (remove?) |
| 2D | 4 | *(unknown)* | shared tail; status \|= 0x400 ^= 0x800 |
| 2E | 0 | *(size 0)* | always-true like 26 |
| 2F | 4 | se_volume* | handle tbl[idx], volume pair |
| 30 | 12 | inst_cfg* | root+0x4C table entry field writes |
| 31 | 2 | setw | work word = imm16 |
| 32 | 2 | nop_wide* | advance only |
| 33 | 2 | sys_multi* | sub-op 0..10 misc system sets |
| 34 | 8 | model_op* | sub-op 0/1/2 -> model/costume swap helpers |
| 35 | 2 | objtbl_b_set* | obj/item table byte = b |
| 36 | 2 | ck_anim* | objtbl->+0x86 (cur anim) == hi |
| 37 | 2 | tbl37_set* | DAT_00c3882c[sel] = b |
| 38 | 4 | ck_bits* | test/invert mask on DAT_00c38710 |
| 39 | 2 | get_eml_state* | enemy state byte -> DAT_00c386FA |
| 3A | 4 | msgnode_set* | two bytes into msg list nodes |
| 3B | 6 | eml_rot* | if alive: two u16s -> rot fields |
| 3C | 6 | ck_counter* | ftol counter <= imm16 |
| 3D | 12 | effect_tracked* | effect_spawn + remember last (DAT_00c386FB) |
| 3E | 2 | effect_kill_a* | `effect_kill_matching`(9) |
| 3F | 6 | ck_tween* | \|cur-target\| <= \|dst-target\| |
| 40 | 16 | obj_xfm* | 7 words -> root+0x9C transform block |
| 41 | 2 | spd_set* | DAT_00c35242 = u16 |
| 42 | 2 | effect_kill_b* | `effect_kill_matching`(3) |
| 43 | 4 | se_rate* | guarded `snd_rate_set` |
| 44 | 2 | task_kill | zero task slot idx |
| 45 | 4 | spd_add* | DAT_00c35242 += s8 |
| 46 | 44 | msg_list* | build 5-node text list; FUN_00434300 activate |
| 47 | 14 | eml_pos* | rot/pos u16s into enemy slot |
| 48 | 2 | effect_clear* | zero 64-entry effect table @0xC330B4 |
| 49 | 2 | bank_bit* | DAT_00aa8e50 \|= bit (0xFF clears) |
| 4A | 2 | bgm_bank_down* | bgm mask >>= 8 + snd hooks |
| 4B | 2 | bgm_bank_up* | <<= 8 |
| 4C | 2 | swap_var | copies state byte between work RAM and AOT entry+9 (existing name fits) |
| 4D | 2 | objs_hide* | `obj_hide` on 13 work structs |
| 4E | 2 | mass_mask* | stride-0x84 table set/clr/xor u16 when active |
| 4F | 2 | xa_flag* | DAT_007a7544 = b&1 |
| 50 | 2 | xa_flag_ck* | returns DAT_007a7544 |

\* best-guess name, see TODO column notes in commit history.

### Known discrepancies / open questions

1. **0x83 size — RESOLVED: 7 bytes.** `event_state_1_logic` consumes 7 and a
   state-aware scan of every US room event stream parses cleanly with 7
   (x585 occurrences; the old C# value of 8 desyncs). Fixed in
   `Bio1ConstantTable.GetInstructionSize`.
2. **0x89/0x8A — CONFIRMED REAL** (x62 / x52 across US rooms), 2 bytes each.
   They set the aux motion id pair `obj+0xBD/0xBE` consumed by enemy AI
   (e.g. footstep/camera-shake table lookups in FUN_00470a90). Sizes + names
   (`evt_actor_action_a/b`) added.
3. **evt_plc_\* entries — PHANTOM, blanked.** After modeling the VM's real
   segmentation, bytes 0xEF–0xF4 never occur as top-level ops in any US room
   (the earlier apparent hits were misparsed state-stream bytes), and no
   dispatcher for them exists anywhere in Bio.exe's event VM. The names were
   removed from `_eventOpcodes`; the slots are empty.
4. **Task-state tracking — RESOLVED.** `ScdReader.ReadScript1Event` now
   mirrors `scd_event_loop`'s sub-state machine: after top-level ops 01/02/03
   it parses the actor-cmd ISA (state-1, `act_*`) or tween ISA (state-2,
   `tw_*`) until the exit opcode (actor: `8B`, or `80` which falls through
   into it; tween: `01`; `FF` terminates in any state). Control ops F6–FF
   keep their event-level encodings in every state (the VM dispatches them
   first). Unknown sub-ISA bytes resume top-level parsing (the VM's do/skip_if
   control flow can jump over the stream-end marker). `TweenBeginAlt` was
   corrected 3 → 1 byte (validated x1898). Round-trip stays byte-perfect and
   decompiled text now has correct boundaries. Fidelity test
   `TestRE1EventStates.AllRooms_EventStreamsAreWellFormed` walks all 1934 US
   room events with zero violations. Two byte-identical rooms (ROOM1060/6060
   ev8) exercise the resume path.
5. Op 24 dispatches one of 18 behavior thunks via `PTR_LAB_004b60b8`
   (thunk[0] is `return 0`, thunk[1] gates on an ev_status bit) — the legacy
   name `aot_on:usu` looks wrong but is left in place pending proof. Op 2D
   invokes the same handler then flips ev_status bits 0x400/0x800.
6. Remaining best-guess names are marked * in the table above.

### Scanner

The validation tool lives at `/tmp/opencode/re1scan` (console app referencing
this library; walks every `/mnt/f/games/re1/JPN/STAGE*/ROOM*.RDT` event
stream twice: linear like `ScdReader`, and state-aware like
`scd_event_loop`). ~28 rooms fail chunk extraction inside `Rdt1` itself
("Length was larger than the chunk") and are skipped.

### Ghidra DB changes (2026-08-26)

Renamed: `ev_task_init`, `ev_spawn_event_task`, `ev_run_block_sync`,
`flg_test`, `flg_clear`, `msg_show`, `msg_cut_begin`,
`item_find`, `item_compact`, `snd_start`, `snd_stop`, `snd_vol_set`,
`snd_play_banked`, `snd_vol_set_idx`, `snd_rate_set`, `snd_fadeout_all`,
`effect_spawn`, `effect_kill_matching`, `movie_flag_set/get`,
`movie_cleanup`, `room_entry_enable/disable`, `enemy_def_lookup`,
`obj_inst_snapshot`, `obj_hide`, `ftol_helper`,
`evop_00_end`, `evop_04_ck`, `evop_05_set` (remaining table targets exist
only as labels; full map documented in the plate comment @0x4ACB68).
Plate comments installed on: 0x4ACB68, scd_event_loop, event_opcode_single,
ev_run_block_sync, ev_task_init, event_state_1_logic, event_state_2_logic.

### Round-trip

Baseline solved on `re1-scd-roundtrip` (2026-08-25):
`TestReassemble.RE1_100 / RE1_Chris / RE1_Jill` pass byte-perfect.
This branch renames symbols/strings and corrects three state-1 op sizes
(0x83 -> 7B, adds 0x89/0x8A at 2B) with all six TestReassemble tests green.

---

## 5. How to fetch more (fastest paths)

- **Any opcode handler**: `decompile_function` its address on the inner-ISA dispatch table
  @0x4ACB68 (plate comment there documents the full 0x00–0x50 map). Handlers' pseudo-C dumps
  already on disk: `exports/scd_handlers/`.
- **Data addresses**: `read_bytes` / `disassemble` at the globals listed in §3/§4
  (task array @0xC3F71C, ev_status @0xC33090, inventory @0xAA8E48, enemy slots @0xC35334, …).
- **Cross-references**: `list_xrefs` on any function to find consumers (e.g. who reads `obj+0xBD`).
- **Pattern search**: `search_code` (semantic or literal) for handler bodies that share logic.
- **Round-trip validation**: run the `biohazard-utils` tests
  (`TestReassemble.RE1_100/Chris/Jill`, `TestRE1EventStates.AllRooms_EventStreamsAreWellFormed`)
  and `/tmp/opencode/re1scan` after any size/name change.

## 6. What this means for the port

- **Room conversion**: RDT files → the event streams parse with the state machine implemented in
  `biohazard-utils` (`ScdReader.ReadScript1Event`). The VM semantics above (§4) are the spec for
  reimplementing the scheduler in the RE2 clone.
- **Scripts**: `ScdAssembler` + `ScriptDecompiler` + `Bio1ConstantTable` in `biohazard-utils`
  are the reference compiler/decompiler for these streams.
- **Enemy AI**: enemies are initialized by inner op `1B` (enemy slot init, 22B; 0x18C-stride table
  @0xC35334). Aux motion ids in `obj+0xBD/0xBE` drive AI behaviour (set by `act_action_a/b`,
  consumed in FUN_00470a90 footstep/camera-shake lookups). Enemy state bytes via `eml_state`
  (op 28) and `get_eml_state` (op 39); positions via `eml_pos` (op 47) / `pos_set` (op 21).
- **Tweens / movement**: `tw_*` ISA for tweened pos/rot; `obj_xfm` (op 40) writes a 7-word
  transform block at root+0x9C; `dir_set`/`pos_set` (ops 20/21) set player/enemy targets.
- **Placeholder work**: tween semantics (`tw_*`) and several best-guess names (marked *) still
  need confirming — fetch a handler's decompiled code and document the true behaviour.

## 7. Related repos/tools

- `biohazard-utils` — github.com/IntelOrca/biohazard-utils (this repo; reference C# implementation).
- `~/projects/re1-ghidra` — Ghidra project + MCP server + exports (the DB of record).
- `/tmp/opencode/re1scan` — standalone event-stream scanner.
- Open-source RE2 engine clone — target engine for the port (separate project).