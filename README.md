# Save The Shapes — PS2 fan demake

Fan demake inspired by *Save The Shapes* by Nukepop (https://nukepop.itch.io/savetheshapes).
All code and visuals here are original; no assets from the PC game are used.
The room layout is a placeholder, not the layout of the original house.

## Build

Requires PS2SDK (with gsKit) and `PS2SDK` set in your environment.

    make

This produces `savetheshapes.elf`.

## Run

- PCSX2: File > Run ELF
- Real hardware: launch through uLaunchELF or OPL

## Controls

- Left stick or D-pad: move and turn
- START: restart after winning or losing

## Goal

Rescue all 5 shapes. Diamond only moves when you are not looking at it.

## Optimizations

- Raycaster: one gsKit sprite per 2 px column (320 rays), no textures
- Sin/cos lookup tables and a precomputed per-column camera table
- No malloc anywhere; fixed-size static arrays
- Integer map lookups with a bounded DDA loop
- Single flat-shaded primitives, ZBuffer off, double buffering synced to vsync

## Known limits

- Not compiled or tested yet; I had no PS2SDK toolchain to check it against
- Sprites are not depth-sorted against each other
- No sound or text yet
