# OpenRE

OpenRE is an open-source game engine for classic style Resident Evil games.
The engine is written in C++ and contains the core logic for the graphics, and common game machanics. All the resources and scripts are loaded from a portable package that is provided. There will be a package for each Resident Evil game as well as those for custom games or large mods.

Smaller mods can overlaid which replace or add additional scripts/resources to enhance the base package experience.

The engine tries to avoid as many assumptions as possible to allow maximum flexability from the game packages. As such all interfaces, game flow, and specical mechanics are provided by the package. The game engine just a simple scripting API (with helpers) for achieving that.

## Architecture
* Shell - Handles OS, Graphics, Audio, Input
    * LUAvm - LUA VM for base game package
    * LUAvm - LUA VM for mod 1
    * LUAvm - LUA VM for mod 2
    * REvm - Stores the global game state (flags, inventory, player state)
    * ROOMvm - Stores the room state (cleared when next room is loaded)

## Filesystem
```
~/.openre/base
    re1.biopkg
    re2.biopkg
    re3.biopkg
    kendos_cut.biopkg
    wip_mod/
        manifest.ini
        main.lua

~/.openre/mods
    re2hd.biopkg
    re2r_inventory.biopkg
    srt/
        main.lua
```

## Package example
```
re2.biopkg
    /main.lua
    /script
        /inventory.luac
        /zombie.lua
        /licker.lua
    /texture
        /splash.bmp
        /splash_jp.bmp
        /titlebg1.adt
        /titlebg2.adt
        /inventory.tim
    /room
        /room100.rdt
        /room101.rdt
        /room200.rdt
    /door
        /door00.do2
        /door0A.do2
    /entity
        /em010.dat
        /em01E.dat
    /item
        /item0001.dat
        /item0002.dat
        /item00A3.dat
    /sound
        /biohazard.wav
        /residentevil.ogg
    /bgm
        /bgm0010.ogg
        /bgm0011.sap
        /bgm002A.wav
    /voice
        /v100.ogg
        /v101.sap
        /v102.wav
        /intro.mpg
        /room00.mp4
```

## New Data Format
Format consistent for all files, with type to distinguish, e.g. room, door, enemy. The chunk index specifies the type of chunk. So each file must have consistent chunk order.

```
  0x00|MAGIC          |VER|   |TYPE     |
  0x08|PADDING                |# CHUNKS |
  0x10|CHUNK 0 OFFSET | CHUNK 1 OFFSET  |
  0x18|CHUNK 2 OFFSET | CHUNK 3 OFFSET  |
  0x20|CHUNK 0                          |
  0x80|CHUNK 1                          |
  0xA0|CHUNK 2                          |
  0xF0|CHUNK 3                          |
```

A single chunk can be added / replaced with a standalone file.
```
/entity/em010.dat
/entity/em010$3.md1
/entity/em010$4.tim
```
