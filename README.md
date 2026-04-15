# raylib-research

Collection of standalone game prototypes built with [raylib](https://www.raylib.com/) in C. Each is a single-file experiment exploring a different genre or mechanic.

## Games

| Project | Genre | Description |
|---------|-------|-------------|
| **fps** | FPS | First-person shooter with mouse look, jumping, bullets, enemies |
| **3rd-person** | TPS | Third-person over-the-shoulder shooter |
| **rally** | Racing | Sega Rally style racer with surface types, AI opponents, drifting |
| **boat** | Racing | Wave Race style powerboat racer with animated waves, jumps, drafting, drifting, islands |
| **starfox** | On-rails shooter | Star Fox style with arwing, enemy waves, barrel roll, on-rails scrolling |
| **fighter** | Fighting | Street Fighter style 2D fighter with puppet animation, AI, combos, hadoukens |
| **pokemon** | RPG | Pokemon style with overworld, tall grass encounters, turn-based battles, catching, buildings |
| **biplane** | Flight sim | Arcade biplane with rings, terrain, crash debris, explosion particles |
| **goldensun** | RPG battle | Golden Sun GBA-style battle system |
| **rpgbattle** | RPG battle | FF7-style ATB battle system |
| **snowboard** | Sports | 1080 Snowboarding style |
| **zelda** | Action-adventure | Top-down Zelda style with rooms, enemies, items, sword combat |
| **platformer** | Platformer | Side-scrolling platformer |
| **kart** | Racing | Kart racer |
| **soccer** | Sports | Soccer game |
| **micromachines** | Racing | Top-down Micro Machines style racer |
| **skate** | Sports | Skateboarding |
| **resi** | Survival horror | Resident Evil style |
| **wipeout** | Racing | Wipeout style anti-gravity racer |
| **rts** | Strategy | RTS with unit selection and movement |

## Editor

Multi-mode visual editor for creating game assets:

- **Tiles** -- Paint tile-based 3D maps
- **Objects** -- Place, rotate, and scale 3D prefabs in the scene
- **Build 3D** -- Construct 3D objects from cube/sphere/cylinder/cone primitives
- **Build 2D** -- Draw 2D sprites from rect/circle/ellipse/triangle/line primitives (zoom, pan, color palette)
- **Puppet** -- Pose and animate puppet rigs made of sub-sprites. Select a part and press E to edit its sprite in Build 2D.

## Shared Libraries

Header-only libraries in `common/`:

- **objects3d.h** -- 3D object system with Part types, macros (`CUBE`, `SPHERE`, `CYL`, `CONE`), `.obj3d` file I/O, prefab library
- **sprites2d.h** -- 2D sprite system with drawing primitives, `.spr2d` file I/O, frame-based animation, puppet/skeletal animation (`.rig2d`/`.anim2d`), hitbox/hurtbox collision, 3D billboard rendering
- **map3d.h** -- Tile-based 3D map system with height levels, `.m3d` file I/O

## File Formats

All formats are human-readable text files editable in any text editor:

- **`.obj3d`** -- 3D objects (one primitive per line with position, size, color)
- **`.spr2d`** -- 2D sprites (rect/circle/ellipse/tri/line with coordinates and RGBA colors)
- **`.rig2d`** -- Puppet rigs (`part name spritefile.spr2d`, file order = draw order back-to-front)
- **`.anim2d`** -- Puppet animations (keyframes with per-part positions, rotation, scale, hitbox/hurtbox)
- **`.m3d`** -- Tile maps (width, height, tile grid)

## Build

Requires a Zig compiler and raylib installed.

```sh
zig build             # build all projects
zig build fps         # build one project
zig build run-fps     # build and run
```

Outputs to `zig-out/bin/`. Run games from the project root so they can find asset files.

On macOS, install dependencies with:
```sh
brew install raylib zig
```

## Architecture

- Each game is a standalone `main.c` in its own directory -- no shared game code between projects
- All rendering uses raylib primitives (no textures or image assets)
- Sprites and 3D models are built from geometric primitives defined in text files
- The puppet animation system lets you define characters once and animate by repositioning sub-sprites per keyframe
