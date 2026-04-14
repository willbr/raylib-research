CC := zig cc
CFLAGS := -std=c99 -O2
PROJECTS := fps rally 3rd-person rts soccer kart platformer zelda micromachines skate resi wipeout editor

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
  RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
  RAYLIB_LIBS := $(shell pkg-config --libs raylib)
  PLATFORM_LIBS := -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
  EXT :=
else
  RAYLIB_INCLUDE ?= $(USERPROFILE)\scoop\apps\vcpkg\current\installed\x64-windows\include
  RAYLIB_LIB ?= $(USERPROFILE)\scoop\apps\vcpkg\current\installed\x64-windows\lib
  RAYLIB_CFLAGS := -I$(RAYLIB_INCLUDE)
  RAYLIB_LIBS := -L$(RAYLIB_LIB) -lraylib
  PLATFORM_LIBS := -lgdi32 -lwinmm -luser32 -lshell32
  EXT := .exe
endif

.PHONY: all clean run $(PROJECTS)

all: $(PROJECTS)

$(PROJECTS):
	$(CC) $@/main.c -o $@/$@$(EXT) $(CFLAGS) $(RAYLIB_CFLAGS) $(RAYLIB_LIBS) $(PLATFORM_LIBS)

run:
ifndef GAME
	$(error Usage: make run GAME=fps)
endif
	@$(MAKE) $(GAME)
	./$(GAME)/$(GAME)$(EXT)

clean:
	rm -f $(foreach p,$(PROJECTS),$(p)/$(p)$(EXT))
