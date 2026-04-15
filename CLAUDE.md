# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Collection of standalone raylib game prototypes in C, plus shared libraries and an editor.

### Game Prototypes
Each subdirectory is an independent single-file game (`main.c`):

- **fps/** — First-person shooter with mouse look, jumping, bullets, enemies
- **3rd-person/** — Third-person over-the-shoulder shooter
- **rally/** — Sega Rally style racer with car physics, AI, surface types
- **rts/** — RTS with unit selection and movement
- **starfox/** — Star Fox on-rails shooter with arwing, enemies, barrel roll
- **boat/** — Wave Race style powerboat racer with waves, jumps, drafting, drifting
- **pokemon/** — Pokemon RPG with overworld, battles, catching, buildings, sprites from files
- **fighter/** — Street Fighter style 2D fighter with puppet animation system
- **biplane/** — Biplane flight sim with rings, terrain, crash physics
- **goldensun/** — Golden Sun GBA-style battle
- **snowboard/** — 1080 snowboarding
- **rpgbattle/** — FF7-style ATB battle
- Plus: soccer, kart, platformer, zelda, micromachines, skate, resi, wipeout

### Shared Libraries (`common/`)
- **objects3d.h** — 3D object system: Part/Object3D types, CUBE/SPHERE/CYL/CONE macros, .obj3d file load/save, prefab library (human, car, tree, etc.)
- **sprites2d.h** — 2D sprite system: Sprite2DPart types, SRECT/SCIRCLE/SELLIPSE/STRIANGLE/SLINE macros, .spr2d file load/save, plus:
  - Frame-based animation (SpriteAnim, .spr2d numbered files)
  - Puppet/skeletal animation (PuppetRig, PuppetAnim, .rig2d/.anim2d files)
  - Hitbox/hurtbox system for fighting games
  - 3D billboard rendering (DrawSprite2DAsPlane)
- **map3d.h** — Tile-based 3D map system with height, .m3d file load/save

### Editor (`editor/`)
Multi-mode editor with:
- **Tiles** — Paint tile maps
- **Objects** — Place/rotate/scale 3D prefabs
- **Build 3D** — Construct new 3D objects from primitives
- **Build 2D** — Draw 2D sprites from primitives (zoom/pan, shift+click to select/move)
- **Puppet** — Pose puppet rigs, edit animations, preview playback. Press E on a selected part to edit its sub-sprite in Build 2D mode.

## Build

`zig cc` as the C compiler. Makefile with pkg-config on macOS, vcpkg on Windows:
```
make all          # build everything
make fps          # build one project
make clean        # remove binaries
```

**Dependencies**: Zig compiler, raylib (Homebrew on macOS, vcpkg on Windows).

There is no test suite or linter.

## File Formats

- **.obj3d** — 3D objects (text, one primitive per line)
- **.spr2d** — 2D sprites (text: rect/circle/ellipse/tri/line with colors)
- **.rig2d** — Puppet rigs (text: `part name spritefile.spr2d`, draw order = file order)
- **.anim2d** — Puppet animations (text: keyframes with part positions, hitbox/hurtbox, duration)
- **.m3d** — Tile maps (text: width, height, tile data)

## Code Conventions

- Single-file C programs (`main.c`) per prototype — all game logic lives in one file
- Uses `raylib.h`, `raymath.h`, `rlgl.h` headers
- Game structs (Player, Enemy, Bullet, Unit, etc.) defined at file top, game loop in `main()`
- Constants defined as `#define` macros or `const` locals near point of use
- Header-only libraries in `common/` — just `#include`, no separate compilation
- Sprites/assets loaded from text files at runtime where possible (editable without recompiling)
