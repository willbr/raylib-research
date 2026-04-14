# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Collection of standalone raylib game prototypes in C. Each subdirectory is an independent single-file game experiment:

- **fps/** — First-person shooter with mouse look, jumping, rapid-fire bullets, and enemy targeting
- **3rd-person/** — Third-person over-the-shoulder shooter with aiming
- **rally/** — Top-down racing game (Sega Rally style) with car physics
- **rts/** — RTS with unit selection and movement on a ground plane

## Build

Two cross-platform build systems are available. Both use `zig cc` as the C compiler.

**Makefile** (uses pkg-config on macOS, vcpkg paths on Windows):
```
make all          # build everything
make fps          # build one project
make clean        # remove binaries
```

**Zig build system** (outputs to zig-out/bin/):
```
zig build             # build all projects
zig build fps         # build one project
zig build run-fps     # build and run
```

**Legacy**: Each subdirectory also has a `build.cmd` (Windows batch) for standalone builds.

**Dependencies**: Zig compiler, raylib (Homebrew on macOS, vcpkg on Windows).

There is no test suite or linter.

## Code Conventions

- Single-file C programs (`main.c`) per prototype — all game logic lives in one file
- Uses `raylib.h` and `raymath.h` headers
- Game structs (Player, Enemy, Bullet, Unit, etc.) defined at file top, game loop in `main()`
- Constants defined as `#define` macros or `const` locals near point of use
