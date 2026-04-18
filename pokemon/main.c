#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "../common/objects3d.h"
#include "../common/sprites2d.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Pokemon-style RPG: top-down overworld, tall grass encounters, turn-based battles

#define TILE_SIZE   16
#define MAP_W       30
#define MAP_H       22
#define ZOOM        3.0f
#define PLAYER_SPEED 80.0f

// Directions
#define DIR_DOWN  0
#define DIR_UP    1
#define DIR_LEFT  2
#define DIR_RIGHT 3

// Tile types
#define T_GRASS     0
#define T_TALLGRASS 1
#define T_WALL      2
#define T_WATER     3
#define T_PATH      4
#define T_TREE      5
#define T_HOUSE     6
#define T_DOOR      7
#define T_FENCE     8
#define T_FLOWER    9
#define T_SIGN     10
#define T_POKECENTER 11
#define T_FLOOR    12
#define T_COUNTER  13
#define T_SHELF    14
#define T_EXIT     15
#define T_RUG      16

// Interior rooms
#define ROOM_W     10
#define ROOM_H      8
#define MAX_INTERIORS 2

// --- Pokemon Data ---
#define MAX_MOVES    4
#define MAX_PARTY    6
#define MAX_WILD     8

typedef enum {
    TYPE_NORMAL, TYPE_FIRE, TYPE_WATER, TYPE_GRASS, TYPE_ELECTRIC, TYPE_POISON
} PokemonType;

typedef struct {
    const char *name;
    PokemonType type;
    int power;
    int accuracy;  // 0-100
} Move;

typedef struct {
    const char *name;
    PokemonType type;
    int hp, maxHp;
    int attack, defense, speed;
    int level;
    int xp, xpToNext;
    Move moves[MAX_MOVES];
    int moveCount;
    bool fainted;
} Pokemon;

// Status effect from moves
typedef enum { STATUS_NONE, STATUS_POISON, STATUS_BURN, STATUS_PARALYZE } StatusEffect;

// --- Type effectiveness ---
float TypeEffectiveness(PokemonType atk, PokemonType def) {
    if (atk == TYPE_FIRE && def == TYPE_GRASS) return 2.0f;
    if (atk == TYPE_FIRE && def == TYPE_WATER) return 0.5f;
    if (atk == TYPE_WATER && def == TYPE_FIRE) return 2.0f;
    if (atk == TYPE_WATER && def == TYPE_GRASS) return 0.5f;
    if (atk == TYPE_GRASS && def == TYPE_WATER) return 2.0f;
    if (atk == TYPE_GRASS && def == TYPE_FIRE) return 0.5f;
    if (atk == TYPE_ELECTRIC && def == TYPE_WATER) return 2.0f;
    if (atk == TYPE_ELECTRIC && def == TYPE_GRASS) return 0.5f;
    if (atk == TYPE_POISON && def == TYPE_GRASS) return 2.0f;
    return 1.0f;
}

const char *TypeName(PokemonType t) {
    switch (t) {
        case TYPE_FIRE: return "FIRE";
        case TYPE_WATER: return "WATER";
        case TYPE_GRASS: return "GRASS";
        case TYPE_ELECTRIC: return "ELEC";
        case TYPE_POISON: return "PSN";
        default: return "NORMAL";
    }
}

Color TypeColor(PokemonType t) {
    switch (t) {
        case TYPE_FIRE: return (Color){240, 80, 40, 255};
        case TYPE_WATER: return (Color){50, 120, 220, 255};
        case TYPE_GRASS: return (Color){60, 180, 60, 255};
        case TYPE_ELECTRIC: return (Color){240, 200, 40, 255};
        case TYPE_POISON: return (Color){160, 60, 180, 255};
        default: return (Color){180, 180, 180, 255};
    }
}

// Create a pokemon with stats scaled by level
Pokemon MakePokemon(const char *name, PokemonType type, int level, Move *moves, int moveCount) {
    Pokemon p = {0};
    p.name = name;
    p.type = type;
    p.level = level;
    p.maxHp = 20 + level * 5 + GetRandomValue(0, level * 2);
    p.hp = p.maxHp;
    p.attack = 8 + level * 2 + GetRandomValue(0, level);
    p.defense = 6 + level * 2 + GetRandomValue(0, level);
    p.speed = 7 + level * 2 + GetRandomValue(0, level);
    p.xp = 0;
    p.xpToNext = level * level * 5 + 20;
    p.moveCount = moveCount;
    for (int i = 0; i < moveCount && i < MAX_MOVES; i++) p.moves[i] = moves[i];
    p.fainted = false;
    return p;
}

// --- Move database ---
static Move moveDB[] = {
    {"Tackle",     TYPE_NORMAL,   40, 95},
    {"Scratch",    TYPE_NORMAL,   40, 95},
    {"Ember",      TYPE_FIRE,     40, 100},
    {"Flamethrower",TYPE_FIRE,    90, 100},
    {"Water Gun",  TYPE_WATER,    40, 100},
    {"Surf",       TYPE_WATER,    90, 100},
    {"Vine Whip",  TYPE_GRASS,    45, 100},
    {"Razor Leaf", TYPE_GRASS,    55, 95},
    {"ThunderShock",TYPE_ELECTRIC, 40, 100},
    {"Thunderbolt",TYPE_ELECTRIC, 90, 100},
    {"Poison Sting",TYPE_POISON,  15, 100},
    {"Sludge Bomb",TYPE_POISON,   90, 100},
    {"Bite",       TYPE_NORMAL,   60, 100},
    {"Headbutt",   TYPE_NORMAL,   70, 100},
};
#define MOVE(name) moveDB[name]
enum { M_TACKLE, M_SCRATCH, M_EMBER, M_FLAMETHROWER, M_WATERGUN, M_SURF,
       M_VINEWHIP, M_RAZORLEAF, M_THUNDERSHOCK, M_THUNDERBOLT,
       M_POISONSTING, M_SLUDGEBOMB, M_BITE, M_HEADBUTT };

// --- Wild encounter templates ---
typedef struct {
    const char *name;
    PokemonType type;
    int minLevel, maxLevel;
    int moves[4];
    int moveCount;
} WildTemplate;

static WildTemplate wildTemplates[] = {
    {"Rattata",   TYPE_NORMAL, 2, 5,  {M_TACKLE, M_BITE},           2},
    {"Pidgey",    TYPE_NORMAL, 2, 5,  {M_TACKLE, M_HEADBUTT},       2},
    {"Caterpie",  TYPE_POISON, 2, 4,  {M_TACKLE, M_POISONSTING},    2},
    {"Oddish",    TYPE_GRASS,  3, 6,  {M_VINEWHIP, M_POISONSTING},  2},
    {"Poliwag",   TYPE_WATER,  3, 6,  {M_WATERGUN, M_TACKLE},       2},
    {"Vulpix",    TYPE_FIRE,   4, 7,  {M_EMBER, M_TACKLE},          2},
    {"Pikachu",   TYPE_ELECTRIC,3, 6, {M_THUNDERSHOCK, M_TACKLE},   2},
    {"Ekans",     TYPE_POISON, 3, 6,  {M_POISONSTING, M_BITE},      2},
};
#define NUM_WILD_TEMPLATES (int)(sizeof(wildTemplates)/sizeof(wildTemplates[0]))

// --- Sprites loaded from .spr2d files ---
#define MAX_SPRITE_PARTS 32

typedef struct {
    Sprite2DPart parts[MAX_SPRITE_PARTS];
    int count;
} SpriteData;

// Tile sprites
static SpriteData tileSpr[20];   // indexed by tile type
static SpriteData tileSprFloorDark;
static SpriteData tileSprRugPC;

// Character sprites
static SpriteData sprPlayerDown, sprPlayerUp, sprPlayerSide, sprNPC;

void LoadAllSprites(void) {
    // Tiles
    tileSpr[T_GRASS].count     = LoadSprite2D("pokemon/sprites/tiles/grass.spr2d",      tileSpr[T_GRASS].parts, MAX_SPRITE_PARTS);
    tileSpr[T_PATH].count      = LoadSprite2D("pokemon/sprites/tiles/path.spr2d",       tileSpr[T_PATH].parts, MAX_SPRITE_PARTS);
    tileSpr[T_WALL].count      = LoadSprite2D("pokemon/sprites/tiles/wall.spr2d",       tileSpr[T_WALL].parts, MAX_SPRITE_PARTS);
    tileSpr[T_TREE].count      = LoadSprite2D("pokemon/sprites/tiles/tree.spr2d",       tileSpr[T_TREE].parts, MAX_SPRITE_PARTS);
    tileSpr[T_FENCE].count     = LoadSprite2D("pokemon/sprites/tiles/fence.spr2d",      tileSpr[T_FENCE].parts, MAX_SPRITE_PARTS);
    tileSpr[T_HOUSE].count     = LoadSprite2D("pokemon/sprites/tiles/house.spr2d",      tileSpr[T_HOUSE].parts, MAX_SPRITE_PARTS);
    tileSpr[T_DOOR].count      = LoadSprite2D("pokemon/sprites/tiles/door.spr2d",       tileSpr[T_DOOR].parts, MAX_SPRITE_PARTS);
    tileSpr[T_SIGN].count      = LoadSprite2D("pokemon/sprites/tiles/sign.spr2d",       tileSpr[T_SIGN].parts, MAX_SPRITE_PARTS);
    tileSpr[T_POKECENTER].count= LoadSprite2D("pokemon/sprites/tiles/pokecenter.spr2d", tileSpr[T_POKECENTER].parts, MAX_SPRITE_PARTS);
    tileSpr[T_FLOOR].count     = LoadSprite2D("pokemon/sprites/tiles/floor.spr2d",      tileSpr[T_FLOOR].parts, MAX_SPRITE_PARTS);
    tileSprFloorDark.count     = LoadSprite2D("pokemon/sprites/tiles/floor_dark.spr2d", tileSprFloorDark.parts, MAX_SPRITE_PARTS);
    tileSpr[T_COUNTER].count   = LoadSprite2D("pokemon/sprites/tiles/counter.spr2d",    tileSpr[T_COUNTER].parts, MAX_SPRITE_PARTS);
    tileSpr[T_SHELF].count     = LoadSprite2D("pokemon/sprites/tiles/shelf.spr2d",      tileSpr[T_SHELF].parts, MAX_SPRITE_PARTS);
    tileSpr[T_EXIT].count      = LoadSprite2D("pokemon/sprites/tiles/exit.spr2d",       tileSpr[T_EXIT].parts, MAX_SPRITE_PARTS);
    tileSpr[T_RUG].count       = LoadSprite2D("pokemon/sprites/tiles/rug_house.spr2d",  tileSpr[T_RUG].parts, MAX_SPRITE_PARTS);
    tileSprRugPC.count         = LoadSprite2D("pokemon/sprites/tiles/rug_pokecenter.spr2d", tileSprRugPC.parts, MAX_SPRITE_PARTS);

    // Characters
    sprPlayerDown.count = LoadSprite2D("pokemon/sprites/player_down.spr2d", sprPlayerDown.parts, MAX_SPRITE_PARTS);
    sprPlayerUp.count   = LoadSprite2D("pokemon/sprites/player_up.spr2d",   sprPlayerUp.parts, MAX_SPRITE_PARTS);
    sprPlayerSide.count = LoadSprite2D("pokemon/sprites/player_side.spr2d", sprPlayerSide.parts, MAX_SPRITE_PARTS);
    sprNPC.count        = LoadSprite2D("pokemon/sprites/npc.spr2d",         sprNPC.parts, MAX_SPRITE_PARTS);
}

// --- Pokemon sprites loaded from .spr2d files ---
#define MAX_PKM_SPRITE_PARTS 48
#define MAX_PKM_SPRITES 10

typedef struct {
    char name[32];
    Sprite2DPart parts[MAX_PKM_SPRITE_PARTS];
    int count;
} PokemonSprite;

static PokemonSprite pkmSprites[MAX_PKM_SPRITES];
static int numPkmSprites = 0;

// Load all pokemon sprite files from sprites/ directory
void LoadPokemonSprites(void) {
    numPkmSprites = 0;
    const char *spriteFiles[] = {
        "charmander", "squirtle", "bulbasaur", "pikachu",
        "rattata", "pidgey", "oddish", "vulpix", "ekans", "poliwag"
    };
    int numFiles = sizeof(spriteFiles) / sizeof(spriteFiles[0]);
    for (int i = 0; i < numFiles && numPkmSprites < MAX_PKM_SPRITES; i++) {
        char path[64];
        snprintf(path, sizeof(path), "pokemon/sprites/%s.spr2d", spriteFiles[i]);
        int count = LoadSprite2D(path, pkmSprites[numPkmSprites].parts, MAX_PKM_SPRITE_PARTS);
        if (count > 0) {
            strncpy(pkmSprites[numPkmSprites].name, spriteFiles[i], 31);
            pkmSprites[numPkmSprites].count = count;
            numPkmSprites++;
        }
    }
}

// Find a pokemon sprite by name, returns parts/count. Falls back to first sprite.
void GetPokemonSpriteByName(const char *name, Sprite2DPart **parts, int *count) {
    // Try exact match (lowercase name)
    for (int i = 0; i < numPkmSprites; i++) {
        // Case-insensitive prefix match
        const char *a = name;
        const char *b = pkmSprites[i].name;
        bool match = true;
        while (*b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
            if (ca != cb) { match = false; break; }
            a++; b++;
        }
        if (match) {
            *parts = pkmSprites[i].parts;
            *count = pkmSprites[i].count;
            return;
        }
    }
    // Fallback to first loaded sprite
    if (numPkmSprites > 0) {
        *parts = pkmSprites[0].parts;
        *count = pkmSprites[0].count;
    } else {
        *parts = NULL;
        *count = 0;
    }
}

Pokemon SpawnWild(void) {
    int idx = GetRandomValue(0, NUM_WILD_TEMPLATES - 1);
    WildTemplate *wt = &wildTemplates[idx];
    int level = GetRandomValue(wt->minLevel, wt->maxLevel);
    Move moves[4];
    for (int i = 0; i < wt->moveCount; i++) moves[i] = moveDB[wt->moves[i]];
    return MakePokemon(wt->name, wt->type, level, moves, wt->moveCount);
}

// --- Game state ---
typedef enum { STATE_OVERWORLD, STATE_BATTLE, STATE_DIALOG, STATE_POKEMENU } GameState;
typedef enum { BTURN_SELECT, BTURN_PLAYER_ATTACK, BTURN_ENEMY_ATTACK,
               BTURN_MESSAGE, BTURN_WIN, BTURN_LOSE, BTURN_RUN, BTURN_CATCH } BattleTurn;

typedef struct {
    Vector2 pos;
    int dir;
    float animTimer;
    int stepFrame;
    bool moving;
} Player;

typedef struct {
    Vector2 pos;
    int dir;
    const char *dialog;
    bool talked;
} NPC;

#define MAX_NPCS 8

// Battle state
typedef struct {
    Pokemon wild;
    int playerPokemonIdx;  // which party member is fighting
    BattleTurn turn;
    int selectedMove;
    char message[128];
    float messageTimer;
    float animTimer;
    int damageDealt;       // for showing damage numbers
    bool playerTurn;       // who just attacked
    float shakeTimer;      // screen shake on hit
    int catchAttempts;
} Battle;

// Map
static int tileMap[MAP_H][MAP_W];
static Player player;
static GameState state = STATE_OVERWORLD;
static Battle battle;
static Pokemon party[MAX_PARTY];
static int partyCount = 0;
static int pokeballs = 5;
static int potions = 3;
static NPC npcs[MAX_NPCS];
static int numNPCs = 0;
static char dialogText[256] = {0};
static float encounterCooldown = 0;
// Transition: fade out (phase 0) -> callback changes scene -> fade in (phase 1)
static float transitionTimer = 0;
static int transitionPhase = -1;  // -1=none, 0=fading out, 1=fading in
#define TRANSITION_TIME 0.25f

// Pending transition action
typedef enum { TRANS_NONE, TRANS_ENTER_BUILDING, TRANS_EXIT_BUILDING, TRANS_START_BATTLE, TRANS_END_BATTLE } TransAction;
static TransAction pendingTransition = TRANS_NONE;
static int pendingInteriorIdx = -1;
static float gameTime = 0;

// Interior system
typedef struct {
    int tiles[ROOM_H][ROOM_W];
    Vector2 spawnPos;       // where player appears inside
    Vector2 exitWorldPos;   // where player returns to outside
    const char *name;
    bool isPokecenter;
} Interior;

static Interior interiors[MAX_INTERIORS];
static int currentInterior = -1;  // -1 = overworld
static Vector2 savedWorldPos;     // player pos before entering

// --- Map generation ---
void GenerateMap(void) {
    // Fill with grass
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            tileMap[y][x] = T_GRASS;

    // Paths
    for (int x = 0; x < MAP_W; x++) { tileMap[8][x] = T_PATH; tileMap[9][x] = T_PATH; }
    for (int y = 0; y < MAP_H; y++) { tileMap[y][14] = T_PATH; tileMap[y][15] = T_PATH; }

    // Tall grass patches
    for (int y = 2; y < 7; y++)
        for (int x = 2; x < 8; x++) tileMap[y][x] = T_TALLGRASS;
    for (int y = 11; y < 16; y++)
        for (int x = 3; x < 10; x++) tileMap[y][x] = T_TALLGRASS;
    for (int y = 3; y < 8; y++)
        for (int x = 20; x < 27; x++) tileMap[y][x] = T_TALLGRASS;
    for (int y = 14; y < 20; y++)
        for (int x = 18; x < 25; x++) tileMap[y][x] = T_TALLGRASS;

    // Trees around edges
    for (int x = 0; x < MAP_W; x++) { tileMap[0][x] = T_TREE; tileMap[MAP_H-1][x] = T_TREE; }
    for (int y = 0; y < MAP_H; y++) { tileMap[y][0] = T_TREE; tileMap[y][MAP_W-1] = T_TREE; }
    // Scattered trees
    int treePts[][2] = {{5,10},{6,10},{10,3},{10,4},{17,5},{18,5},{12,17},{13,17},{25,12},{26,12}};
    for (int i = 0; i < 10; i++) tileMap[treePts[i][1]][treePts[i][0]] = T_TREE;

    // Water pond
    for (int y = 16; y < 20; y++)
        for (int x = 10; x < 15; x++) tileMap[y][x] = T_WATER;

    // Houses
    tileMap[6][14] = T_HOUSE; tileMap[6][15] = T_HOUSE;
    tileMap[7][14] = T_DOOR;  tileMap[7][15] = T_HOUSE;

    // Pokemon center
    tileMap[6][18] = T_POKECENTER; tileMap[6][19] = T_POKECENTER;
    tileMap[7][18] = T_DOOR;       tileMap[7][19] = T_POKECENTER;

    // Fences
    for (int x = 2; x < 8; x++) { tileMap[1][x] = T_FENCE; tileMap[7][x] = T_FENCE; }

    // Flowers
    tileMap[10][10] = T_FLOWER; tileMap[10][11] = T_FLOWER;
    tileMap[10][12] = T_FLOWER; tileMap[10][13] = T_FLOWER;

    // Signs
    tileMap[8][12] = T_SIGN;
    tileMap[8][20] = T_SIGN;
}

void StartBattle(void);  // forward declaration

void BeginTransition(TransAction action, int param) {
    pendingTransition = action;
    pendingInteriorIdx = param;
    transitionPhase = 0;  // start fading out
    transitionTimer = TRANSITION_TIME;
}

void ExecuteTransition(void) {
    switch (pendingTransition) {
        case TRANS_ENTER_BUILDING:
            savedWorldPos = player.pos;
            currentInterior = pendingInteriorIdx;
            player.pos = interiors[currentInterior].spawnPos;
            player.dir = DIR_UP;
            break;
        case TRANS_EXIT_BUILDING:
            player.pos = interiors[currentInterior].exitWorldPos;
            player.dir = DIR_DOWN;
            currentInterior = -1;
            break;
        case TRANS_START_BATTLE:
            StartBattle();
            break;
        case TRANS_END_BATTLE:
            state = STATE_OVERWORLD;
            encounterCooldown = 2.0f;
            break;
        default: break;
    }
    pendingTransition = TRANS_NONE;
}

void GenerateInteriors(void) {
    // Interior 0: House
    Interior *house = &interiors[0];
    house->name = "House";
    house->isPokecenter = false;
    house->spawnPos = (Vector2){4 * TILE_SIZE, 6 * TILE_SIZE};
    house->exitWorldPos = (Vector2){14 * TILE_SIZE, 8 * TILE_SIZE};
    for (int y = 0; y < ROOM_H; y++)
        for (int x = 0; x < ROOM_W; x++)
            house->tiles[y][x] = T_FLOOR;
    // Walls around edges
    for (int x = 0; x < ROOM_W; x++) { house->tiles[0][x] = T_WALL; house->tiles[ROOM_H-1][x] = T_WALL; }
    for (int y = 0; y < ROOM_H; y++) { house->tiles[y][0] = T_WALL; house->tiles[y][ROOM_W-1] = T_WALL; }
    // Exit door at bottom center
    house->tiles[ROOM_H-1][4] = T_EXIT;
    house->tiles[ROOM_H-1][5] = T_EXIT;
    // Furniture
    house->tiles[1][1] = T_SHELF; house->tiles[1][2] = T_SHELF;
    house->tiles[1][7] = T_SHELF; house->tiles[1][8] = T_SHELF;
    house->tiles[2][7] = T_COUNTER;
    // Rug
    house->tiles[4][3] = T_RUG; house->tiles[4][4] = T_RUG;
    house->tiles[4][5] = T_RUG; house->tiles[4][6] = T_RUG;
    house->tiles[5][3] = T_RUG; house->tiles[5][4] = T_RUG;
    house->tiles[5][5] = T_RUG; house->tiles[5][6] = T_RUG;

    // Interior 1: Pokemon Center
    Interior *pc = &interiors[1];
    pc->name = "Pokemon Center";
    pc->isPokecenter = true;
    pc->spawnPos = (Vector2){4 * TILE_SIZE, 6 * TILE_SIZE};
    pc->exitWorldPos = (Vector2){18 * TILE_SIZE, 8 * TILE_SIZE};
    for (int y = 0; y < ROOM_H; y++)
        for (int x = 0; x < ROOM_W; x++)
            pc->tiles[y][x] = T_FLOOR;
    for (int x = 0; x < ROOM_W; x++) { pc->tiles[0][x] = T_WALL; pc->tiles[ROOM_H-1][x] = T_WALL; }
    for (int y = 0; y < ROOM_H; y++) { pc->tiles[y][0] = T_WALL; pc->tiles[y][ROOM_W-1] = T_WALL; }
    // Exit
    pc->tiles[ROOM_H-1][4] = T_EXIT;
    pc->tiles[ROOM_H-1][5] = T_EXIT;
    // Healing counter across the top
    for (int x = 2; x < 8; x++) pc->tiles[1][x] = T_COUNTER;
    // Red cross on floor
    pc->tiles[3][4] = T_RUG; pc->tiles[3][5] = T_RUG;
    pc->tiles[4][3] = T_RUG; pc->tiles[4][4] = T_RUG;
    pc->tiles[4][5] = T_RUG; pc->tiles[4][6] = T_RUG;
    pc->tiles[5][4] = T_RUG; pc->tiles[5][5] = T_RUG;
}

int GetTile(int tx, int ty) {
    if (currentInterior >= 0) {
        if (tx < 0 || tx >= ROOM_W || ty < 0 || ty >= ROOM_H) return T_WALL;
        return interiors[currentInterior].tiles[ty][tx];
    }
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return T_WALL;
    return tileMap[ty][tx];
}

bool IsSolid(int tx, int ty) {
    int t = GetTile(tx, ty);
    return t == T_WALL || t == T_TREE || t == T_WATER || t == T_HOUSE ||
           t == T_FENCE || t == T_POKECENTER || t == T_COUNTER || t == T_SHELF || t == T_SIGN || t == T_DOOR;
}

// --- Drawing tiles ---
void DrawTileSpr(SpriteData *sd, float x, float y) {
    if (sd->count > 0) DrawSprite2D(sd->parts, sd->count, x, y, 1.0f);
}

void DrawTile(int tx, int ty, Vector2 offset) {
    float x = offset.x + tx * TILE_SIZE;
    float y = offset.y + ty * TILE_SIZE;
    int t = GetTile(tx, ty);
    switch (t) {
        case T_GRASS:
            DrawTileSpr(&tileSpr[T_GRASS], x, y);
            break;
        case T_TALLGRASS:
            DrawTileSpr(&tileSpr[T_GRASS], x, y);
            // Animated grass blades overlay
            for (int i = 0; i < 3; i++) {
                float gx = x + 3 + i * 5;
                float sway = sinf(gameTime * 2.0f + tx * 0.5f + i) * 1.5f;
                DrawLine(gx, y + TILE_SIZE, gx + sway, y + 4, (Color){40, 140, 40, 255});
                DrawLine(gx + 2, y + TILE_SIZE, gx + 2 + sway, y + 6, (Color){50, 160, 50, 255});
            }
            break;
        case T_WALL:
            DrawTileSpr(&tileSpr[T_WALL], x, y);
            break;
        case T_WATER: {
            float wave = sinf(gameTime * 3.0f + tx + ty * 0.7f) * 0.3f;
            Color wc = {(unsigned char)(40 + wave * 20), (unsigned char)(100 + wave * 30),
                        (unsigned char)(200 + wave * 20), 255};
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, wc);
            // Shimmer
            float shimmer = sinf(gameTime * 5.0f + tx * 1.3f + ty * 0.9f);
            if (shimmer > 0.7f)
                DrawRectangle(x + 4, y + 4, 3, 1, (Color){180, 220, 255, 150});
            break;
        }
        case T_PATH:
            DrawTileSpr(&tileSpr[T_PATH], x, y);
            break;
        case T_TREE: {
            DrawTileSpr(&tileSpr[T_TREE], x, y);
            // Animated dappled sunlight overlay
            int seed = tx * 7 + ty * 13;
            if (seed % 3 == 0) DrawCircle(x + 4, y + 3, 1, (Color){80, 200, 80, (unsigned char)(150 + sinf(gameTime + seed)*50)});
            if (seed % 5 == 0) DrawCircle(x + 10, y + 6, 1, (Color){75, 190, 75, (unsigned char)(150 + sinf(gameTime*1.3f + seed)*50)});
            break;
        }
        case T_HOUSE:
            DrawTileSpr(&tileSpr[T_HOUSE], x, y);
            break;
        case T_DOOR:
            DrawTileSpr(&tileSpr[T_DOOR], x, y);
            break;
        case T_FENCE:
            DrawTileSpr(&tileSpr[T_FENCE], x, y);
            break;
        case T_FLOWER: {
            DrawTileSpr(&tileSpr[T_GRASS], x, y);
            int fseed = tx * 11 + ty * 7;
            // Two flowers per tile, varied by position
            float sway = sinf(gameTime * 1.5f + fseed) * 0.8f;
            for (int f = 0; f < 2; f++) {
                float fx2 = x + 4 + f * 7 + (fseed % 3);
                float fy2 = y + 6 + (f * 4) + (fseed % 2);
                // Stem
                DrawLine(fx2, fy2 + 3, fx2 + sway, fy2 - 1, (Color){40, 130, 40, 255});
                // Leaf
                DrawEllipse(fx2 + 2, fy2 + 1, 2, 1, (Color){50, 150, 50, 255});
                // Petals (5 around center)
                Color petalColors[] = {
                    {255, 80, 100, 255}, {255, 180, 60, 255}, {220, 100, 220, 255},
                    {255, 130, 80, 255}, {180, 100, 240, 255}, {255, 200, 100, 255}
                };
                Color pc = petalColors[(fseed + f) % 6];
                for (int p = 0; p < 5; p++) {
                    float pa = (float)p / 5 * 2.0f * PI + sway * 0.2f;
                    float pr = 2.2f;
                    DrawCircle(fx2 + sway + cosf(pa) * pr, fy2 - 1 + sinf(pa) * pr, 1.5f, pc);
                }
                // Center
                DrawCircle(fx2 + sway, fy2 - 1, 1.2f, (Color){255, 230, 80, 255});
            }
            break;
        }
        case T_SIGN:
            DrawTileSpr(&tileSpr[T_SIGN], x, y);
            break;
        case T_POKECENTER:
            DrawTileSpr(&tileSpr[T_POKECENTER], x, y);
            break;
        case T_FLOOR: {
            bool light = ((tx + ty) % 2 == 0);
            DrawTileSpr(light ? &tileSpr[T_FLOOR] : &tileSprFloorDark, x, y);
            break;
        }
        case T_COUNTER:
            DrawTileSpr(&tileSpr[T_COUNTER], x, y);
            // Pokecenter items overlay
            if (currentInterior >= 0 && interiors[currentInterior].isPokecenter) {
                DrawCircle(x + 5, y + 3, 2, (Color){220, 50, 50, 255});
                DrawRectangle(x + 3, y + 2, 4, 1, (Color){40,40,40,255});
                DrawCircle(x + 5, y + 3, 1, WHITE);
                DrawRectangle(x + 9, y + 1, 5, 4, (Color){60, 60, 70, 255});
                DrawRectangle(x + 10, y + 1, 3, 3, (Color){100, 200, 140, 255});
            }
            break;
        case T_SHELF:
            DrawTileSpr(&tileSpr[T_SHELF], x, y);
            break;
        case T_EXIT:
            DrawTileSpr(&tileSpr[T_EXIT], x, y);
            break;
        case T_RUG:
            if (currentInterior >= 0 && interiors[currentInterior].isPokecenter)
                DrawTileSpr(&tileSprRugPC, x, y);
            else
                DrawTileSpr(&tileSpr[T_RUG], x, y);
            break;
    }
}

// --- Draw player ---
void DrawPlayer(Vector2 offset) {
    float px = offset.x + player.pos.x + 8;
    float py = offset.y + player.pos.y + 8;
    float bob = player.moving ? sinf(player.animTimer * 10.0f) * 1.5f : 0;
    py += bob;

    SpriteData *sd;
    switch (player.dir) {
        case DIR_UP:    sd = &sprPlayerUp;   break;
        case DIR_LEFT:
        case DIR_RIGHT: sd = &sprPlayerSide; break;
        default:        sd = &sprPlayerDown; break;
    }
    // Flip for left-facing (mirror X) — sprite eye faces right by default
    if (player.dir == DIR_LEFT) {
        for (int i = 0; i < sd->count; i++) {
            Sprite2DPart p = sd->parts[i];
            p.x = -p.x;
            DrawSprite2DPart(&p, px, py, 1.0f);
        }
    } else {
        DrawSprite2D(sd->parts, sd->count, px, py, 1.0f);
    }
}

void DrawPokemonSprite(float x, float y, float size, Pokemon *p, float t) {
    float bob = sinf(t * 3.0f) * 2.0f;
    Sprite2DPart *spr; int cnt;
    GetPokemonSpriteByName(p->name, &spr, &cnt);
    if (!spr || cnt == 0) return;
    float scale = size / 24.0f;
    DrawSprite2D(spr, cnt, x, y + bob, scale);
}

// --- HP bar ---
void DrawHPBar(float x, float y, float w, float h, int hp, int maxHp) {
    float pct = (float)hp / maxHp;
    Color hpCol = pct > 0.5f ? (Color){60,200,60,255} : pct > 0.2f ? (Color){240,200,40,255} : (Color){240,50,50,255};
    DrawRectangle(x, y, w, h, (Color){40,40,40,255});
    DrawRectangle(x, y, (int)(w * pct), h, hpCol);
    DrawRectangleLines(x, y, w, h, (Color){80,80,80,255});
}

// --- NPC setup ---
void SetupNPCs(void) {
    numNPCs = 0;
    npcs[numNPCs++] = (NPC){{12*TILE_SIZE, 5*TILE_SIZE}, DIR_DOWN,
        "Welcome to the world of\nPokemon! Walk into the\ntall grass to find wild\nPokemon!", false};
    npcs[numNPCs++] = (NPC){{20*TILE_SIZE, 10*TILE_SIZE}, DIR_LEFT,
        "Tip: You can catch weakened\nPokemon with Pokeballs!\nPress C during battle.", false};
    npcs[numNPCs++] = (NPC){{16*TILE_SIZE, 16*TILE_SIZE}, DIR_RIGHT,
        "The Pokemon Center heals\nyour team! Just walk in\nthrough the door.", false};
}

// --- Battle logic ---
int CalcDamage(Pokemon *attacker, Pokemon *defender, Move *move) {
    float effectiveness = TypeEffectiveness(move->type, defender->type);
    float stab = (move->type == attacker->type) ? 1.5f : 1.0f;
    float base = ((2.0f * attacker->level / 5.0f + 2.0f) * move->power *
                  ((float)attacker->attack / defender->defense)) / 50.0f + 2.0f;
    base *= effectiveness * stab;
    // Random factor
    base *= (float)GetRandomValue(85, 100) / 100.0f;
    int dmg = (int)base;
    if (dmg < 1) dmg = 1;
    return dmg;
}

void StartBattle(void) {
    state = STATE_BATTLE;
    battle.wild = SpawnWild();
    battle.playerPokemonIdx = -1;
    // Find first non-fainted party member
    for (int i = 0; i < partyCount; i++) {
        if (!party[i].fainted) { battle.playerPokemonIdx = i; break; }
    }
    if (battle.playerPokemonIdx < 0) { state = STATE_OVERWORLD; return; }
    battle.turn = BTURN_SELECT;
    battle.selectedMove = 0;
    battle.messageTimer = 0;
    battle.animTimer = 0;
    battle.shakeTimer = 0;
    battle.catchAttempts = 0;
    snprintf(battle.message, sizeof(battle.message), "A wild %s appeared!", battle.wild.name);
    battle.turn = BTURN_MESSAGE;
    battle.messageTimer = 1.5f;
}

void BattleMessage(const char *msg, float duration) {
    snprintf(battle.message, sizeof(battle.message), "%s", msg);
    battle.turn = BTURN_MESSAGE;
    battle.messageTimer = duration;
}

void DoBattleTurn(void) {
    Pokemon *pp = &party[battle.playerPokemonIdx];
    Pokemon *wp = &battle.wild;

    // Determine who goes first
    bool playerFirst = pp->speed >= wp->speed;

    for (int phase = 0; phase < 2; phase++) {
        bool isPlayer = (phase == 0) ? playerFirst : !playerFirst;
        Pokemon *attacker = isPlayer ? pp : wp;
        Pokemon *defender = isPlayer ? wp : pp;

        if (attacker->fainted || defender->fainted) continue;

        Move *move;
        if (isPlayer) {
            move = &pp->moves[battle.selectedMove];
        } else {
            int mi = GetRandomValue(0, wp->moveCount - 1);
            move = &wp->moves[mi];
        }

        // Accuracy check
        if (GetRandomValue(1, 100) > move->accuracy) {
            snprintf(battle.message, sizeof(battle.message), "%s's %s missed!",
                attacker->name, move->name);
        } else {
            int dmg = CalcDamage(attacker, defender, move);
            defender->hp -= dmg;
            if (defender->hp < 0) defender->hp = 0;
            battle.damageDealt = dmg;
            battle.playerTurn = isPlayer;
            battle.shakeTimer = 0.2f;

            float eff = TypeEffectiveness(move->type, defender->type);
            if (eff > 1.5f)
                snprintf(battle.message, sizeof(battle.message),
                    "%s used %s!\n%d damage! Super effective!", attacker->name, move->name, dmg);
            else if (eff < 0.8f)
                snprintf(battle.message, sizeof(battle.message),
                    "%s used %s!\n%d damage. Not very effective...", attacker->name, move->name, dmg);
            else
                snprintf(battle.message, sizeof(battle.message),
                    "%s used %s!\n%d damage!", attacker->name, move->name, dmg);
        }

        if (defender->hp <= 0) {
            defender->fainted = true;
            if (defender == wp) {
                int xpGain = wp->level * 8 + 10;
                pp->xp += xpGain;
                snprintf(battle.message, sizeof(battle.message),
                    "%s fainted!\n%s gained %d XP!", wp->name, pp->name, xpGain);
                // Level up check
                if (pp->xp >= pp->xpToNext) {
                    pp->level++;
                    pp->xp -= pp->xpToNext;
                    pp->xpToNext = pp->level * pp->level * 5 + 20;
                    pp->maxHp += 3 + GetRandomValue(0, 2);
                    pp->hp = pp->maxHp;
                    pp->attack += 1 + GetRandomValue(0, 1);
                    pp->defense += 1 + GetRandomValue(0, 1);
                    pp->speed += 1 + GetRandomValue(0, 1);
                }
                battle.turn = BTURN_WIN;
                battle.messageTimer = 2.0f;
                return;
            } else {
                // Player pokemon fainted — try to switch
                bool anyAlive = false;
                for (int i = 0; i < partyCount; i++) {
                    if (!party[i].fainted) { anyAlive = true; break; }
                }
                if (!anyAlive) {
                    snprintf(battle.message, sizeof(battle.message), "All your Pokemon fainted!");
                    battle.turn = BTURN_LOSE;
                    battle.messageTimer = 2.0f;
                    return;
                }
                // Auto-switch to next alive
                for (int i = 0; i < partyCount; i++) {
                    if (!party[i].fainted) { battle.playerPokemonIdx = i; break; }
                }
                snprintf(battle.message, sizeof(battle.message),
                    "%s fainted!\nGo, %s!", pp->name, party[battle.playerPokemonIdx].name);
                battle.turn = BTURN_MESSAGE;
                battle.messageTimer = 2.0f;
                return;
            }
        }
    }
    battle.turn = BTURN_MESSAGE;
    battle.messageTimer = 1.5f;
}

// --- Init ---
void InitGame(void) {
    GenerateMap();
    GenerateInteriors();
    SetupNPCs();
    LoadPokemonSprites();
    LoadAllSprites();
    currentInterior = -1;

    player.pos = (Vector2){14 * TILE_SIZE, 10 * TILE_SIZE};
    player.dir = DIR_DOWN;
    player.animTimer = 0;
    player.stepFrame = 0;
    player.moving = false;

    // Starter pokemon
    Move starterMoves[] = {moveDB[M_TACKLE], moveDB[M_EMBER], moveDB[M_BITE]};
    party[0] = MakePokemon("Charmander", TYPE_FIRE, 5, starterMoves, 3);
    Move starterMoves2[] = {moveDB[M_TACKLE], moveDB[M_WATERGUN]};
    party[1] = MakePokemon("Squirtle", TYPE_WATER, 5, starterMoves2, 2);
    partyCount = 2;
    pokeballs = 5;
    potions = 3;

    state = STATE_OVERWORLD;
    encounterCooldown = 0;
    gameTime = 0;
}

// --- Main ---
int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Pokemon");
    MaximizeWindow();
    SetExitKey(0);  // disable ESC quit — we use ESC in menus
    SetTargetFPS(60);

    InitGame();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        gameTime += dt;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        // Transition: two-phase fade out then fade in
        if (transitionPhase >= 0) {
            transitionTimer -= dt;
            if (transitionTimer <= 0) {
                if (transitionPhase == 0) {
                    // Fully black — execute the scene switch
                    ExecuteTransition();
                    transitionPhase = 1;
                    transitionTimer = TRANSITION_TIME;
                } else {
                    // Fade in complete
                    transitionPhase = -1;
                }
            }
        }

        if (state == STATE_OVERWORLD) {
            // --- Movement ---
            Vector2 moveDir = {0, 0};
            player.moving = false;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  { moveDir.x = -1; player.dir = DIR_LEFT; player.moving = true; }
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) { moveDir.x = 1; player.dir = DIR_RIGHT; player.moving = true; }
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    { moveDir.y = -1; player.dir = DIR_UP; player.moving = true; }
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))  { moveDir.y = 1; player.dir = DIR_DOWN; player.moving = true; }

            if (player.moving) {
                player.animTimer += dt;
                if (player.animTimer > 0.2f) { player.animTimer = 0; player.stepFrame++; }
                Vector2 newPos = {
                    player.pos.x + moveDir.x * PLAYER_SPEED * dt,
                    player.pos.y + moveDir.y * PLAYER_SPEED * dt
                };
                // Collision check
                int tx1 = (int)(newPos.x + 2) / TILE_SIZE;
                int ty1 = (int)(newPos.y + 8) / TILE_SIZE;
                int tx2 = (int)(newPos.x + 14) / TILE_SIZE;
                int ty2 = (int)(newPos.y + 15) / TILE_SIZE;
                bool blocked = false;
                for (int cy = ty1; cy <= ty2; cy++)
                    for (int cx = tx1; cx <= tx2; cx++)
                        if (IsSolid(cx, cy)) blocked = true;
                // NPC collision
                if (!blocked && currentInterior < 0) {
                    for (int i = 0; i < numNPCs; i++) {
                        float dx = (newPos.x + 8) - (npcs[i].pos.x + 8);
                        float dy = (newPos.y + 8) - (npcs[i].pos.y + 8);
                        if (dx * dx + dy * dy < 12.0f * 12.0f) { blocked = true; break; }
                    }
                }
                if (!blocked) player.pos = newPos;
            }

            // Door/exit trigger: check tile player is facing
            {
                int ptx = (int)(player.pos.x + 8) / TILE_SIZE;
                int pty = (int)(player.pos.y + 12) / TILE_SIZE;
                // Check the tile ahead of the player
                int ftx = ptx, fty = pty;
                if (player.dir == DIR_UP) fty--;
                else if (player.dir == DIR_DOWN) fty++;
                int facedTile = GetTile(ftx, fty);
                int standTile = GetTile(ptx, pty);
                // Enter building: facing up at a door, and close to it
                if (currentInterior < 0 && facedTile == T_DOOR && player.dir == DIR_UP &&
                    player.moving && transitionPhase < 0) {
                    int interiorIdx = (ftx == 18) ? 1 : 0;
                    BeginTransition(TRANS_ENTER_BUILDING, interiorIdx);
                }
                // Exit interior: standing on exit tile and walking down
                if (currentInterior >= 0 && standTile == T_EXIT && player.dir == DIR_DOWN && transitionPhase < 0) {
                    BeginTransition(TRANS_EXIT_BUILDING, 0);
                }
            }

            // Encounter check (overworld only)
            encounterCooldown -= dt;
            if (currentInterior < 0 && encounterCooldown <= 0 && player.moving) {
                int ptx = (int)(player.pos.x + 8) / TILE_SIZE;
                int pty = (int)(player.pos.y + 12) / TILE_SIZE;
                if (GetTile(ptx, pty) == T_TALLGRASS) {
                    if (GetRandomValue(1, 100) <= 8 && transitionPhase < 0) {
                        BeginTransition(TRANS_START_BATTLE, 0);
                        encounterCooldown = 1.0f;
                    }
                }
            }

            // NPC interaction
            if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER)) {
                int fx = (int)(player.pos.x + 8) / TILE_SIZE;
                int fy = (int)(player.pos.y + 12) / TILE_SIZE;
                switch (player.dir) {
                    case DIR_UP:    fy--; break;
                    case DIR_DOWN:  fy++; break;
                    case DIR_LEFT:  fx--; break;
                    case DIR_RIGHT: fx++; break;
                }
                // Check NPCs (pixel distance — face position vs NPC center)
                float faceX = player.pos.x + 8;
                float faceY = player.pos.y + 12;
                switch (player.dir) {
                    case DIR_UP:    faceY -= TILE_SIZE; break;
                    case DIR_DOWN:  faceY += TILE_SIZE; break;
                    case DIR_LEFT:  faceX -= TILE_SIZE; break;
                    case DIR_RIGHT: faceX += TILE_SIZE; break;
                }
                for (int i = 0; i < numNPCs; i++) {
                    float dx = faceX - (npcs[i].pos.x + 8);
                    float dy = faceY - (npcs[i].pos.y + 8);
                    if (dx * dx + dy * dy < TILE_SIZE * TILE_SIZE) {
                        strncpy(dialogText, npcs[i].dialog, 255);
                        state = STATE_DIALOG;
                        npcs[i].talked = true;
                        break;
                    }
                }
                // Check signs, counters, etc.
                if (state == STATE_OVERWORLD) {
                    int facedTile = GetTile(fx, fy);
                    if (facedTile == T_SIGN) {
                        if (currentInterior < 0 && fx == 12)
                            strncpy(dialogText, "Route 1\nBeware of wild Pokemon\nin the tall grass!", 255);
                        else
                            strncpy(dialogText, "Pokemon Center ahead\nHeal your Pokemon!", 255);
                        state = STATE_DIALOG;
                    }
                    // Counter interaction in Pokemon Center
                    if (facedTile == T_COUNTER && currentInterior >= 0 && interiors[currentInterior].isPokecenter) {
                        for (int i = 0; i < partyCount; i++) {
                            party[i].hp = party[i].maxHp;
                            party[i].fainted = false;
                        }
                        strncpy(dialogText, "Welcome! Your Pokemon\nhave been fully healed!\nWe hope to see you again!", 255);
                        state = STATE_DIALOG;
                    }
                    // Shelf interaction in house
                    if (facedTile == T_SHELF && currentInterior >= 0 && !interiors[currentInterior].isPokecenter) {
                        strncpy(dialogText, "Books about Pokemon\ntraining techniques...", 255);
                        state = STATE_DIALOG;
                    }
                }
            }

            // Open party menu
            if (IsKeyPressed(KEY_P)) state = STATE_POKEMENU;

        } else if (state == STATE_DIALOG) {
            if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
                state = STATE_OVERWORLD;

        } else if (state == STATE_POKEMENU) {
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) state = STATE_OVERWORLD;
            // Use potion on selected pokemon
            static int menuSelect = 0;
            if (IsKeyPressed(KEY_UP)) menuSelect--;
            if (IsKeyPressed(KEY_DOWN)) menuSelect++;
            if (menuSelect < 0) menuSelect = 0;
            if (menuSelect >= partyCount) menuSelect = partyCount - 1;
            if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER)) {
                if (potions > 0 && party[menuSelect].hp < party[menuSelect].maxHp && !party[menuSelect].fainted) {
                    party[menuSelect].hp += 20;
                    if (party[menuSelect].hp > party[menuSelect].maxHp)
                        party[menuSelect].hp = party[menuSelect].maxHp;
                    potions--;
                }
            }

        } else if (state == STATE_BATTLE) {
            battle.animTimer += dt;
            if (battle.shakeTimer > 0) battle.shakeTimer -= dt;

            if (battle.turn == BTURN_MESSAGE) {
                battle.messageTimer -= dt;
                if (battle.messageTimer <= 0) {
                    battle.turn = BTURN_SELECT;
                }
            } else if (battle.turn == BTURN_WIN) {
                battle.messageTimer -= dt;
                if (battle.messageTimer <= 0 && transitionPhase < 0) {
                    BeginTransition(TRANS_END_BATTLE, 0);
                }
            } else if (battle.turn == BTURN_LOSE) {
                battle.messageTimer -= dt;
                if (battle.messageTimer <= 0 && transitionPhase < 0) {
                    // Heal all and return to overworld
                    for (int i = 0; i < partyCount; i++) {
                        party[i].hp = party[i].maxHp;
                        party[i].fainted = false;
                    }
                    player.pos = (Vector2){14 * TILE_SIZE, 10 * TILE_SIZE};
                    BeginTransition(TRANS_END_BATTLE, 0);
                }
            } else if (battle.turn == BTURN_RUN) {
                battle.messageTimer -= dt;
                if (battle.messageTimer <= 0 && transitionPhase < 0) {
                    BeginTransition(TRANS_END_BATTLE, 0);
                }
            } else if (battle.turn == BTURN_CATCH) {
                battle.messageTimer -= dt;
                if (battle.messageTimer <= 0 && transitionPhase < 0) {
                    BeginTransition(TRANS_END_BATTLE, 0);
                }
            } else if (battle.turn == BTURN_SELECT) {
                Pokemon *pp = &party[battle.playerPokemonIdx];
                // Move selection
                if (IsKeyPressed(KEY_UP)) battle.selectedMove--;
                if (IsKeyPressed(KEY_DOWN)) battle.selectedMove++;
                if (battle.selectedMove < 0) battle.selectedMove = pp->moveCount - 1;
                if (battle.selectedMove >= pp->moveCount) battle.selectedMove = 0;

                // Confirm move
                if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER)) {
                    DoBattleTurn();
                }
                // Run
                if (IsKeyPressed(KEY_X) || IsKeyPressed(KEY_ESCAPE)) {
                    if (GetRandomValue(1, 100) <= 60) {
                        BattleMessage("Got away safely!", 1.0f);
                        battle.turn = BTURN_RUN;
                    } else {
                        BattleMessage("Can't escape!", 1.0f);
                    }
                }
                // Catch
                if (IsKeyPressed(KEY_C) && pokeballs > 0) {
                    pokeballs--;
                    battle.catchAttempts++;
                    float catchRate = (1.0f - (float)battle.wild.hp / battle.wild.maxHp) * 60.0f + 20.0f;
                    catchRate += battle.catchAttempts * 5;
                    if (GetRandomValue(1, 100) <= (int)catchRate && partyCount < MAX_PARTY) {
                        party[partyCount] = battle.wild;
                        party[partyCount].fainted = false;
                        partyCount++;
                        snprintf(battle.message, sizeof(battle.message),
                            "Gotcha! %s was caught!", battle.wild.name);
                        battle.turn = BTURN_CATCH;
                        battle.messageTimer = 2.0f;
                    } else {
                        BattleMessage("Oh no! It broke free!", 1.0f);
                    }
                }
                // Potion
                if (IsKeyPressed(KEY_V) && potions > 0) {
                    Pokemon *cur = &party[battle.playerPokemonIdx];
                    if (cur->hp < cur->maxHp) {
                        potions--;
                        cur->hp += 20;
                        if (cur->hp > cur->maxHp) cur->hp = cur->maxHp;
                        snprintf(battle.message, sizeof(battle.message),
                            "Used a Potion!\n%s recovered HP!", cur->name);
                        battle.turn = BTURN_MESSAGE;
                        battle.messageTimer = 1.0f;
                    }
                }
            }
        }

        // UI scale based on window height (designed for 720p)
        float ui = (float)sh / 720.0f;
        if (ui < 1.0f) ui = 1.0f;

        // --- Draw ---
        BeginDrawing();
        ClearBackground(BLACK);

        if (state == STATE_OVERWORLD || state == STATE_DIALOG || state == STATE_POKEMENU) {
            // Camera centered on player
            float camX = player.pos.x + 8 - (sw / ZOOM) / 2;
            float camY = player.pos.y + 8 - (sh / ZOOM) / 2;
            Vector2 offset = {-camX, -camY};

            BeginScissorMode(0, 0, sw, sh);
            rlPushMatrix();
            rlScalef(ZOOM, ZOOM, 1);

            // Draw tiles
            int mapW = (currentInterior >= 0) ? ROOM_W : MAP_W;
            int mapH = (currentInterior >= 0) ? ROOM_H : MAP_H;
            int startTX = (int)(camX / TILE_SIZE) - 1;
            int startTY = (int)(camY / TILE_SIZE) - 1;
            int endTX = startTX + (int)(sw / ZOOM / TILE_SIZE) + 3;
            int endTY = startTY + (int)(sh / ZOOM / TILE_SIZE) + 3;
            if (startTX < 0) startTX = 0; if (startTY < 0) startTY = 0;
            if (endTX > mapW) endTX = mapW; if (endTY > mapH) endTY = mapH;

            for (int ty = startTY; ty < endTY; ty++)
                for (int tx = startTX; tx < endTX; tx++)
                    DrawTile(tx, ty, offset);

            // Draw NPCs (overworld only)
            if (currentInterior < 0) for (int i = 0; i < numNPCs; i++) {
                float nx = offset.x + npcs[i].pos.x + 8;
                float ny = offset.y + npcs[i].pos.y + 8;
                DrawSprite2D(sprNPC.parts, sprNPC.count, nx, ny, 1.0f);
                if (!npcs[i].talked) {
                    DrawText("!", nx - 2, ny - 18, 8, RED);
                }
            }

            // Draw player
            DrawPlayer(offset);

            rlPopMatrix();
            EndScissorMode();

            // Dialog box
            if (state == STATE_DIALOG) {
                int boxH = (int)(120 * ui);
                int margin = (int)(20 * ui);
                DrawRectangle(margin, sh - boxH - margin, sw - margin*2, boxH, (Color){20, 20, 30, 230});
                DrawRectangleLinesEx((Rectangle){margin, sh - boxH - margin, sw - margin*2, boxH}, 2, WHITE);
                DrawText(dialogText, margin*2, sh - boxH - margin + (int)(15*ui), (int)(20*ui), WHITE);
                int contW = MeasureText("[Z/ENTER] Continue", (int)(14*ui));
                DrawText("[Z/ENTER] Continue", sw - contW - margin*2, sh - margin - (int)(16*ui), (int)(14*ui), (Color){150,150,150,255});
            }

            // Pokemon menu overlay
            if (state == STATE_POKEMENU) {
                int mx = sw/4, mw = sw/2, mtop = (int)(40*ui);
                DrawRectangle(mx, mtop, mw, sh - mtop*2, (Color){20, 30, 50, 240});
                DrawRectangleLinesEx((Rectangle){mx, mtop, mw, sh - mtop*2}, 2, (Color){100,150,200,255});
                DrawText("PARTY", mx + (int)(20*ui), mtop + (int)(15*ui), (int)(24*ui), WHITE);
                static int menuSelect = 0;
                int rowH = (int)(70*ui);
                for (int i = 0; i < partyCount; i++) {
                    int yy = mtop + (int)(50*ui) + i * rowH;
                    bool sel = (i == menuSelect);
                    Color bg = sel ? (Color){40,60,90,255} : (Color){30,40,60,255};
                    DrawRectangle(mx + (int)(10*ui), yy, mw - (int)(20*ui), rowH - (int)(6*ui), bg);
                    if (sel) DrawRectangleLinesEx((Rectangle){mx + (int)(10*ui), yy, mw - (int)(20*ui), rowH - (int)(6*ui)}, 1, GOLD);
                    DrawText(TextFormat("Lv%d %s", party[i].level, party[i].name),
                        mx + (int)(20*ui), yy + (int)(5*ui), (int)(18*ui), party[i].fainted ? RED : WHITE);
                    DrawText(TextFormat("%s", TypeName(party[i].type)),
                        mx + (int)(20*ui), yy + (int)(28*ui), (int)(14*ui), TypeColor(party[i].type));
                    int barX = mx + mw/2 + (int)(10*ui);
                    DrawHPBar(barX, yy + (int)(8*ui), (int)(120*ui), (int)(12*ui), party[i].hp, party[i].maxHp);
                    DrawText(TextFormat("%d/%d", party[i].hp, party[i].maxHp),
                        barX, yy + (int)(24*ui), (int)(14*ui), (Color){180,180,180,255});
                }
                DrawText(TextFormat("Potions: %d  [Z] Use Potion", potions), mx + (int)(20*ui), sh - (int)(120*ui), (int)(14*ui), (Color){150,200,150,255});
                DrawText("[P/ESC] Close", mx + (int)(20*ui), sh - (int)(98*ui), (int)(14*ui), (Color){150,150,150,255});
            }

            // Overworld HUD
            if (state == STATE_OVERWORLD) {
                // Mini party bar at top
                int barW = (int)(100*ui), barH = (int)(28*ui);
                for (int i = 0; i < partyCount; i++) {
                    int bx = (int)(10*ui) + i * (barW + (int)(8*ui));
                    DrawRectangle(bx, (int)(8*ui), barW - (int)(4*ui), barH, (Color){20,20,30,200});
                    DrawText(TextFormat("Lv%d %s", party[i].level, party[i].name), bx + (int)(4*ui), (int)(10*ui), (int)(10*ui), WHITE);
                    DrawHPBar(bx + (int)(4*ui), (int)(10*ui) + (int)(13*ui), barW - (int)(12*ui), (int)(6*ui), party[i].hp, party[i].maxHp);
                }
                DrawText(TextFormat("Pokeballs: %d  Potions: %d", pokeballs, potions), (int)(10*ui), (int)(8*ui) + barH + (int)(4*ui), (int)(12*ui), (Color){180,180,180,255});
                int ctrlW = MeasureText("[Z] Talk  [P] Party", (int)(12*ui));
                DrawText("[Z] Talk  [P] Party", sw - ctrlW - (int)(10*ui), sh - (int)(22*ui), (int)(12*ui), (Color){100,100,120,255});
            }

        } else if (state == STATE_BATTLE) {
            // Battle screen
            Pokemon *pp = &party[battle.playerPokemonIdx];
            Pokemon *wp = &battle.wild;

            // Background
            Color bgTop = {100, 180, 100, 255};
            Color bgBot = {60, 120, 60, 255};
            for (int y = 0; y < sh/2; y++) {
                float t = (float)y / (sh/2);
                Color c = {
                    (unsigned char)(bgTop.r + (bgBot.r - bgTop.r) * t),
                    (unsigned char)(bgTop.g + (bgBot.g - bgTop.g) * t),
                    (unsigned char)(bgTop.b + (bgBot.b - bgTop.b) * t), 255
                };
                DrawRectangle(0, y, sw, 1, c);
            }
            DrawRectangle(0, sh/2, sw, sh/2, (Color){220, 210, 180, 255});

            float shakeX = 0, shakeY = 0;
            if (battle.shakeTimer > 0) {
                shakeX = (float)GetRandomValue(-3, 3);
                shakeY = (float)GetRandomValue(-3, 3);
            }

            // Wild pokemon (top right)
            float wpX = sw * 0.7f + shakeX * (!battle.playerTurn ? 1 : 0);
            float wpY = sh * 0.25f + shakeY * (!battle.playerTurn ? 1 : 0);
            if (!wp->fainted) DrawPokemonSprite(wpX, wpY, 50.0f * ui, wp, battle.animTimer);
            // Wild info box
            int ibW = (int)(260*ui), ibH = (int)(70*ui), ibM = (int)(20*ui);
            DrawRectangle(ibM, (int)(30*ui), ibW, ibH, (Color){240, 240, 230, 240});
            DrawRectangleLines(ibM, (int)(30*ui), ibW, ibH, (Color){60,60,60,255});
            DrawText(TextFormat("%s  Lv%d", wp->name, wp->level), ibM+(int)(10*ui), (int)(38*ui), (int)(18*ui), BLACK);
            DrawText(TypeName(wp->type), ibM+(int)(10*ui), (int)(58*ui), (int)(12*ui), TypeColor(wp->type));
            DrawHPBar(ibM+(int)(80*ui), (int)(58*ui), (int)(150*ui), (int)(10*ui), wp->hp, wp->maxHp);
            DrawText(TextFormat("%d/%d", wp->hp, wp->maxHp), ibM+(int)(80*ui), (int)(72*ui), (int)(12*ui), (Color){80,80,80,255});

            // Player pokemon (bottom left)
            float ppX = sw * 0.25f + shakeX * (battle.playerTurn ? 1 : 0);
            float ppY = sh * 0.55f + shakeY * (battle.playerTurn ? 1 : 0);
            if (!pp->fainted) DrawPokemonSprite(ppX, ppY, 55.0f * ui, pp, battle.animTimer);
            // Player info box
            int piW = (int)(270*ui), piH = (int)(80*ui);
            int piX = sw - piW - ibM;
            int piY = sh/2 + (int)(20*ui);
            DrawRectangle(piX, piY, piW, piH, (Color){240, 240, 230, 240});
            DrawRectangleLines(piX, piY, piW, piH, (Color){60,60,60,255});
            DrawText(TextFormat("%s  Lv%d", pp->name, pp->level), piX+(int)(10*ui), piY+(int)(8*ui), (int)(18*ui), BLACK);
            DrawText(TypeName(pp->type), piX+(int)(10*ui), piY+(int)(28*ui), (int)(12*ui), TypeColor(pp->type));
            int hpX = piX + piW/2;
            DrawHPBar(hpX, piY+(int)(10*ui), (int)(120*ui), (int)(10*ui), pp->hp, pp->maxHp);
            DrawText(TextFormat("%d/%d", pp->hp, pp->maxHp), hpX, piY+(int)(24*ui), (int)(12*ui), (Color){80,80,80,255});
            // XP bar
            float xpPct = (float)pp->xp / pp->xpToNext;
            DrawRectangle(hpX, piY+(int)(38*ui), (int)(120*ui), (int)(6*ui), (Color){40,40,40,255});
            DrawRectangle(hpX, piY+(int)(38*ui), (int)(120*ui * xpPct), (int)(6*ui), (Color){80,150,255,255});

            // Bottom panel
            int panelH = (int)(160*ui);
            int panelY = sh - panelH;
            DrawRectangle(0, panelY, sw, panelH, (Color){240, 240, 230, 255});
            DrawRectangleLines(0, panelY, sw, panelH, (Color){60,60,60,255});

            if (battle.turn == BTURN_SELECT) {
                // Move list
                DrawText("FIGHT:", (int)(20*ui), panelY + (int)(10*ui), (int)(18*ui), BLACK);
                for (int i = 0; i < pp->moveCount; i++) {
                    int my = panelY + (int)(35*ui) + i * (int)(28*ui);
                    bool sel = (i == battle.selectedMove);
                    if (sel) DrawRectangle((int)(18*ui), my - (int)(2*ui), (int)(220*ui), (int)(24*ui), (Color){200, 220, 255, 255});
                    Color mc = TypeColor(pp->moves[i].type);
                    DrawText(TextFormat("%s %s  Pwr:%d",
                        sel ? ">" : " ", pp->moves[i].name, pp->moves[i].power),
                        (int)(24*ui), my, (int)(16*ui), sel ? BLACK : (Color){60,60,60,255});
                    DrawRectangle((int)(200*ui), my + (int)(2*ui), (int)(8*ui), (int)(14*ui), mc);
                }
                // Actions
                int actX = sw - (int)(250*ui);
                DrawText("[Z] Attack", actX, panelY + (int)(15*ui), (int)(16*ui), BLACK);
                DrawText("[X] Run", actX, panelY + (int)(38*ui), (int)(16*ui), BLACK);
                DrawText(TextFormat("[C] Catch (%d balls)", pokeballs), actX, panelY + (int)(61*ui), (int)(16*ui),
                    pokeballs > 0 ? BLACK : (Color){180,180,180,255});
                DrawText(TextFormat("[V] Potion (%d)", potions), actX, panelY + (int)(84*ui), (int)(16*ui),
                    potions > 0 ? BLACK : (Color){180,180,180,255});
            } else {
                // Message display
                DrawText(battle.message, (int)(30*ui), panelY + (int)(20*ui), (int)(20*ui), BLACK);
            }
        }

        // Transition effect: phase 0 = fading to black, phase 1 = fading from black
        if (transitionPhase >= 0) {
            float t = transitionTimer / TRANSITION_TIME;  // 1 = start, 0 = end
            float alpha;
            if (transitionPhase == 0) alpha = 1.0f - t;  // getting darker
            else alpha = t;                                // getting lighter
            if (alpha < 0) alpha = 0; if (alpha > 1) alpha = 1;
            DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, (unsigned char)(alpha * 255)});
        }

        DrawFPS(sw - 80, sh - 20);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
