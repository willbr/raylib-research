#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Screen / tile
#define SCREEN_W 800
#define SCREEN_H 600
#define TILE_SIZE    16
#define ROOM_W       16   // tiles per room
#define ROOM_H       12
#define WORLD_W       4   // rooms across
#define WORLD_H       4   // rooms down
#define MAP_W        (ROOM_W * WORLD_W)
#define MAP_H        (ROOM_H * WORLD_H)
#define ZOOM          3.0f

// Gameplay
#define PLAYER_SPEED    80.0f
#define SWORD_DURATION   0.25f
#define SWORD_COOLDOWN   0.15f
#define SWORD_REACH     14.0f
#define SWORD_WIDTH      8.0f
#define MAX_ENEMIES     32
#define MAX_ITEMS       16
#define MAX_PARTICLES   64
#define MAX_PROJECTILES  8
#define KNOCKBACK_SPEED 200.0f
#define KNOCKBACK_TIME   0.15f
#define IFRAME_TIME      1.0f
#define PROJ_SPEED     120.0f

// Directions
#define DIR_DOWN  0
#define DIR_UP    1
#define DIR_LEFT  2
#define DIR_RIGHT 3

// Tile types
#define T_EMPTY    0
#define T_WALL     1
#define T_WATER    2
#define T_TREE     3
#define T_DOOR     4
#define T_CHEST    5
#define T_POT      6
#define T_BUSH     7
#define T_STAIRS   8
#define T_FLOOR    9
#define T_FENCE   10

// Enemy types
#define EN_OCTOROK  0
#define EN_MOBLIN   1
#define EN_SLIME    2
#define EN_BAT      3

// Item types
#define ITEM_HEART    0
#define ITEM_RUPEE    1
#define ITEM_KEY      2
#define ITEM_TRIFORCE 3

typedef struct {
    Vector2 pos;
    Vector2 vel;
    int dir;
    int health;
    int maxHealth;
    int rupees;
    int keys;
    bool hasSword;
    bool swinging;
    float swingTimer;
    float swingCooldown;
    float knockbackTimer;
    Vector2 knockbackDir;
    float iframeTimer;
    float animTimer;
    int animFrame;
    bool triforce;
    int roomX, roomY;      // current room
} Player;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    int type;
    int health;
    int dir;
    bool active;
    float moveTimer;
    float shootTimer;
    float knockbackTimer;
    Vector2 knockbackDir;
    float animTimer;
    int homeRoomX, homeRoomY;
} Enemy;

typedef struct {
    Vector2 pos;
    int type;
    bool active;
    int roomX, roomY;
    float animTimer;
} Item;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float life;
    float maxLife;
    Color color;
    float size;
} Particle;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    bool active;
    float life;
    bool fromPlayer;
} Projectile;

static int worldMap[MAP_H][MAP_W];
static Player player;
static Enemy enemies[MAX_ENEMIES];
static Item items[MAX_ITEMS];
static Particle particles[MAX_PARTICLES];
static Projectile projectiles[MAX_PROJECTILES];
static int particleIdx = 0;
static int numEnemies = 0;
static int numItems = 0;

// Room transition
static bool transitioning = false;
static float transTimer = 0.0f;
static float transDuration = 0.5f;
static Vector2 transOffset = { 0 };
static Vector2 transDir = { 0 };
static int newRoomX, newRoomY;

void SpawnParticle(Vector2 pos, Vector2 vel, Color col, float life, float size) {
    Particle *p = &particles[particleIdx];
    p->pos = pos;
    p->vel = vel;
    p->color = col;
    p->life = life;
    p->maxLife = life;
    p->size = size;
    particleIdx = (particleIdx + 1) % MAX_PARTICLES;
}

void SpawnBurst(Vector2 pos, Color col, int count) {
    for (int i = 0; i < count; i++) {
        float a = (float)GetRandomValue(0, 360) * DEG2RAD;
        float s = (float)GetRandomValue(20, 80);
        SpawnParticle(pos, (Vector2){ cosf(a)*s, sinf(a)*s }, col, 0.3f,
                      1.0f + (float)GetRandomValue(0, 20)/10.0f);
    }
}

// Fill a room rectangle with a tile
void FillRoom(int rx, int ry, int tile) {
    int ox = rx * ROOM_W, oy = ry * ROOM_H;
    for (int y = 0; y < ROOM_H; y++)
        for (int x = 0; x < ROOM_W; x++)
            worldMap[oy + y][ox + x] = tile;
}

// Set room borders (walls) with openings
void RoomBorders(int rx, int ry, bool openN, bool openS, bool openE, bool openW) {
    int ox = rx * ROOM_W, oy = ry * ROOM_H;
    for (int x = 0; x < ROOM_W; x++) {
        bool isOpening = (x >= ROOM_W/2 - 1 && x <= ROOM_W/2);
        if (!(openN && isOpening)) worldMap[oy][ox + x] = T_WALL;
        if (!(openS && isOpening)) worldMap[oy + ROOM_H - 1][ox + x] = T_WALL;
    }
    for (int y = 0; y < ROOM_H; y++) {
        bool isOpening = (y >= ROOM_H/2 - 1 && y <= ROOM_H/2);
        if (!(openW && isOpening)) worldMap[oy + y][ox] = T_WALL;
        if (!(openE && isOpening)) worldMap[oy + y][ox + ROOM_W - 1] = T_WALL;
    }
}

void SetTile(int rx, int ry, int lx, int ly, int tile) {
    worldMap[ry * ROOM_H + ly][rx * ROOM_W + lx] = tile;
}

void AddEnemy(int rx, int ry, int lx, int ly, int type) {
    if (numEnemies >= MAX_ENEMIES) return;
    Enemy *e = &enemies[numEnemies++];
    e->pos = (Vector2){ (rx * ROOM_W + lx) * TILE_SIZE + 4, (ry * ROOM_H + ly) * TILE_SIZE + 4 };
    e->vel = (Vector2){ 0 };
    e->type = type;
    e->health = (type == EN_MOBLIN) ? 3 : (type == EN_OCTOROK) ? 2 : 1;
    e->dir = DIR_DOWN;
    e->active = true;
    e->moveTimer = (float)GetRandomValue(0, 100) / 100.0f;
    e->shootTimer = 2.0f;
    e->knockbackTimer = 0;
    e->animTimer = 0;
    e->homeRoomX = rx;
    e->homeRoomY = ry;
}

void AddItem(int rx, int ry, int lx, int ly, int type) {
    if (numItems >= MAX_ITEMS) return;
    Item *it = &items[numItems++];
    it->pos = (Vector2){ (rx * ROOM_W + lx) * TILE_SIZE + 4, (ry * ROOM_H + ly) * TILE_SIZE + 4 };
    it->type = type;
    it->active = true;
    it->roomX = rx;
    it->roomY = ry;
    it->animTimer = 0;
}

void BuildWorld(void) {
    memset(worldMap, 0, sizeof(worldMap));

    // --- Room (0,3): Starting village (bottom-left) ---
    FillRoom(0, 3, T_FLOOR);
    RoomBorders(0, 3, true, false, true, false);
    // Houses (wall blocks)
    for (int x = 2; x <= 4; x++) { SetTile(0,3, x, 2, T_WALL); SetTile(0,3, x, 3, T_WALL); }
    SetTile(0,3, 3, 3, T_DOOR);
    for (int x = 10; x <= 12; x++) { SetTile(0,3, x, 2, T_WALL); SetTile(0,3, x, 3, T_WALL); }
    SetTile(0,3, 11, 3, T_DOOR);
    // Pots
    SetTile(0,3, 6, 5, T_POT);
    SetTile(0,3, 7, 5, T_POT);
    // Bushes
    SetTile(0,3, 5, 8, T_BUSH);
    SetTile(0,3, 6, 9, T_BUSH);

    // --- Room (1,3): Field with enemies ---
    FillRoom(1, 3, T_EMPTY);
    RoomBorders(1, 3, true, false, true, false);
    // Grass patches / bushes
    SetTile(1,3, 3, 3, T_BUSH); SetTile(1,3, 4, 4, T_BUSH);
    SetTile(1,3, 10, 7, T_BUSH); SetTile(1,3, 12, 3, T_BUSH);
    // Trees
    SetTile(1,3, 2, 8, T_TREE); SetTile(1,3, 13, 2, T_TREE);
    // Enemies
    AddEnemy(1, 3, 6, 4, EN_OCTOROK);
    AddEnemy(1, 3, 10, 8, EN_SLIME);
    AddEnemy(1, 3, 8, 6, EN_SLIME);

    // --- Room (2,3): Water crossing ---
    FillRoom(2, 3, T_EMPTY);
    RoomBorders(2, 3, true, false, true, false);
    // Water river running vertically
    for (int y = 1; y < ROOM_H - 1; y++) {
        SetTile(2,3, 6, y, T_WATER);
        SetTile(2,3, 7, y, T_WATER);
        SetTile(2,3, 8, y, T_WATER);
    }
    // Bridge
    SetTile(2,3, 6, 5, T_FLOOR); SetTile(2,3, 7, 5, T_FLOOR); SetTile(2,3, 8, 5, T_FLOOR);
    SetTile(2,3, 6, 6, T_FLOOR); SetTile(2,3, 7, 6, T_FLOOR); SetTile(2,3, 8, 6, T_FLOOR);
    // Enemies on far side
    AddEnemy(2, 3, 11, 4, EN_OCTOROK);
    AddEnemy(2, 3, 12, 8, EN_OCTOROK);
    // Rupee reward
    AddItem(2, 3, 13, 6, ITEM_RUPEE);

    // --- Room (3,3): Forest / dead end with chest ---
    FillRoom(3, 3, T_EMPTY);
    RoomBorders(3, 3, true, false, false, false);
    // Dense trees
    SetTile(3,3, 2, 2, T_TREE); SetTile(3,3, 4, 3, T_TREE); SetTile(3,3, 6, 2, T_TREE);
    SetTile(3,3, 3, 7, T_TREE); SetTile(3,3, 8, 8, T_TREE); SetTile(3,3, 12, 4, T_TREE);
    SetTile(3,3, 10, 2, T_TREE); SetTile(3,3, 13, 7, T_TREE);
    // Chest with key
    SetTile(3,3, 7, 5, T_CHEST);
    AddItem(3, 3, 7, 5, ITEM_KEY);
    // Enemies
    AddEnemy(3, 3, 5, 5, EN_MOBLIN);
    AddEnemy(3, 3, 10, 6, EN_BAT);

    // --- Room (0,2): Sword cave ---
    FillRoom(0, 2, T_FLOOR);
    RoomBorders(0, 2, false, true, true, false);
    // Cave interior walls
    for (int x = 3; x <= 12; x++) { SetTile(0,2, x, 2, T_WALL); SetTile(0,2, x, 9, T_WALL); }
    for (int y = 2; y <= 9; y++) { SetTile(0,2, 3, y, T_WALL); SetTile(0,2, 12, y, T_WALL); }
    // Opening in inner walls
    SetTile(0,2, 7, 9, T_FLOOR); SetTile(0,2, 8, 9, T_FLOOR);
    // Sword pedestal
    SetTile(0,2, 7, 5, T_CHEST);
    // Pots flanking
    SetTile(0,2, 5, 5, T_POT); SetTile(0,2, 10, 5, T_POT);

    // --- Room (1,2): Graveyard ---
    FillRoom(1, 2, T_EMPTY);
    RoomBorders(1, 2, true, true, true, false);
    // Gravestones (walls)
    for (int i = 0; i < 3; i++) {
        SetTile(1,2, 3 + i*4, 3, T_WALL);
        SetTile(1,2, 3 + i*4, 7, T_WALL);
    }
    // Enemies
    AddEnemy(1, 2, 5, 5, EN_BAT);
    AddEnemy(1, 2, 9, 5, EN_BAT);
    AddEnemy(1, 2, 7, 8, EN_SLIME);

    // --- Room (2,2): Open field ---
    FillRoom(2, 2, T_EMPTY);
    RoomBorders(2, 2, true, true, true, false);
    SetTile(2,2, 4, 4, T_BUSH); SetTile(2,2, 11, 3, T_BUSH);
    SetTile(2,2, 3, 8, T_TREE); SetTile(2,2, 12, 8, T_TREE);
    // Fence line
    for (int x = 5; x <= 10; x++) SetTile(2,2, x, 6, T_FENCE);
    SetTile(2,2, 7, 6, T_EMPTY); SetTile(2,2, 8, 6, T_EMPTY); // gap
    AddEnemy(2, 2, 7, 3, EN_MOBLIN);
    AddEnemy(2, 2, 8, 9, EN_OCTOROK);
    AddItem(2, 2, 13, 2, ITEM_RUPEE);

    // --- Room (3,2): Dungeon entrance ---
    FillRoom(3, 2, T_EMPTY);
    RoomBorders(3, 2, false, true, false, false);
    // Dungeon facade
    for (int x = 4; x <= 11; x++) SetTile(3,2, x, 3, T_WALL);
    for (int x = 4; x <= 11; x++) SetTile(3,2, x, 4, T_WALL);
    SetTile(3,2, 7, 4, T_DOOR); SetTile(3,2, 8, 4, T_DOOR);
    // Stairs inside
    SetTile(3,2, 7, 3, T_STAIRS); SetTile(3,2, 8, 3, T_STAIRS);
    // Guard enemies
    AddEnemy(3, 2, 5, 7, EN_MOBLIN);
    AddEnemy(3, 2, 10, 7, EN_MOBLIN);
    AddItem(3, 2, 7, 9, ITEM_HEART);

    // --- Room (1,1): Upper graveyard path ---
    FillRoom(1, 1, T_EMPTY);
    RoomBorders(1, 1, true, true, true, false);
    SetTile(1,1, 3, 4, T_TREE); SetTile(1,1, 12, 4, T_TREE);
    SetTile(1,1, 3, 8, T_TREE); SetTile(1,1, 12, 8, T_TREE);
    AddEnemy(1, 1, 7, 5, EN_BAT);
    AddEnemy(1, 1, 8, 8, EN_BAT);
    AddEnemy(1, 1, 5, 3, EN_SLIME);
    AddItem(1, 1, 7, 6, ITEM_RUPEE);

    // --- Room (2,1): Pre-boss area ---
    FillRoom(2, 1, T_EMPTY);
    RoomBorders(2, 1, true, true, false, false);
    for (int x = 2; x <= 13; x++) SetTile(2,1, x, 4, T_FENCE);
    SetTile(2,1, 7, 4, T_EMPTY); SetTile(2,1, 8, 4, T_EMPTY);
    AddEnemy(2, 1, 4, 2, EN_MOBLIN);
    AddEnemy(2, 1, 11, 2, EN_MOBLIN);
    AddEnemy(2, 1, 7, 8, EN_OCTOROK);
    AddItem(2, 1, 3, 9, ITEM_HEART);

    // --- Room (1,0): Secret area ---
    FillRoom(1, 0, T_EMPTY);
    RoomBorders(1, 0, false, true, false, false);
    // Hidden garden
    SetTile(1,0, 3, 3, T_TREE); SetTile(1,0, 12, 3, T_TREE);
    SetTile(1,0, 3, 8, T_TREE); SetTile(1,0, 12, 8, T_TREE);
    SetTile(1,0, 7, 3, T_TREE); SetTile(1,0, 8, 3, T_TREE);
    for (int x = 5; x <= 10; x++) SetTile(1,0, x, 8, T_BUSH);
    AddItem(1, 0, 7, 6, ITEM_RUPEE);
    AddItem(1, 0, 8, 6, ITEM_RUPEE);
    AddItem(1, 0, 7, 5, ITEM_HEART);

    // --- Room (2,0): Triforce room ---
    FillRoom(2, 0, T_FLOOR);
    RoomBorders(2, 0, false, true, false, false);
    for (int x = 4; x <= 11; x++) { SetTile(2,0, x, 3, T_WALL); SetTile(2,0, x, 8, T_WALL); }
    for (int y = 3; y <= 8; y++) { SetTile(2,0, 4, y, T_WALL); SetTile(2,0, 11, y, T_WALL); }
    SetTile(2,0, 7, 8, T_FLOOR); SetTile(2,0, 8, 8, T_FLOOR);
    // Triforce
    AddItem(2, 0, 7, 5, ITEM_TRIFORCE);
    // Boss guards
    AddEnemy(2, 0, 6, 6, EN_MOBLIN);
    AddEnemy(2, 0, 9, 6, EN_MOBLIN);

    // Player start
    player.pos = (Vector2){ (0 * ROOM_W + 7) * TILE_SIZE + 4, (3 * ROOM_H + 6) * TILE_SIZE + 4 };
    player.vel = (Vector2){ 0 };
    player.dir = DIR_DOWN;
    player.health = 6;  // 3 hearts
    player.maxHealth = 6;
    player.rupees = 0;
    player.keys = 0;
    player.hasSword = false;
    player.swinging = false;
    player.swingTimer = 0;
    player.swingCooldown = 0;
    player.knockbackTimer = 0;
    player.iframeTimer = 0;
    player.animTimer = 0;
    player.animFrame = 0;
    player.triforce = false;
    player.roomX = 0;
    player.roomY = 3;
}

bool TileSolid(int tx, int ty) {
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return true;
    int t = worldMap[ty][tx];
    return t == T_WALL || t == T_TREE || t == T_WATER || t == T_FENCE ||
           t == T_CHEST || t == T_POT;
}

bool TileIsDoor(int tx, int ty) {
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return false;
    return worldMap[ty][tx] == T_DOOR;
}

bool RectCollides(float x, float y, float w, float h) {
    int x1 = (int)(x / TILE_SIZE);
    int y1 = (int)(y / TILE_SIZE);
    int x2 = (int)((x + w - 0.01f) / TILE_SIZE);
    int y2 = (int)((y + h - 0.01f) / TILE_SIZE);
    for (int ty = y1; ty <= y2; ty++)
        for (int tx = x1; tx <= x2; tx++)
            if (TileSolid(tx, ty)) return true;
    return false;
}

// Get sword hitbox based on direction
Rectangle SwordHitbox(Player *p) {
    float cx = p->pos.x + 4, cy = p->pos.y + 4;
    switch (p->dir) {
        case DIR_UP:    return (Rectangle){ cx - SWORD_WIDTH/2, cy - SWORD_REACH, SWORD_WIDTH, SWORD_REACH };
        case DIR_DOWN:  return (Rectangle){ cx - SWORD_WIDTH/2, cy + 8, SWORD_WIDTH, SWORD_REACH };
        case DIR_LEFT:  return (Rectangle){ cx - SWORD_REACH, cy - SWORD_WIDTH/2 + 4, SWORD_REACH, SWORD_WIDTH };
        case DIR_RIGHT: return (Rectangle){ cx + 8, cy - SWORD_WIDTH/2 + 4, SWORD_REACH, SWORD_WIDTH };
    }
    return (Rectangle){ 0 };
}

void DrawTile(int tx, int ty) {
    int t = worldMap[ty][tx];
    float x = tx * TILE_SIZE, y = ty * TILE_SIZE;

    switch (t) {
        case T_EMPTY:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, (Color){ 100, 180, 80, 255 });
            // Subtle grass detail
            if ((tx + ty) % 3 == 0) {
                DrawRectangle(x + 4, y + 6, 2, 3, (Color){ 80, 160, 60, 255 });
                DrawRectangle(x + 10, y + 10, 2, 3, (Color){ 80, 160, 60, 255 });
            }
            break;
        case T_FLOOR:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, (Color){ 180, 160, 130, 255 });
            if ((tx + ty) % 2 == 0)
                DrawRectangle(x + 1, y + 1, TILE_SIZE - 2, TILE_SIZE - 2, (Color){ 170, 150, 120, 255 });
            break;
        case T_WALL:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, (Color){ 80, 80, 100, 255 });
            DrawRectangleLines(x, y, TILE_SIZE, TILE_SIZE, (Color){ 60, 60, 80, 255 });
            // Brick pattern
            DrawLine(x, y + 8, x + TILE_SIZE, y + 8, (Color){ 60, 60, 80, 255 });
            DrawLine(x + 8, y, x + 8, y + 8, (Color){ 60, 60, 80, 255 });
            DrawLine(x + 4, y + 8, x + 4, y + 16, (Color){ 60, 60, 80, 255 });
            break;
        case T_WATER: {
            float wave = sinf((float)GetTime() * 2.0f + tx * 0.5f) * 0.3f + 0.5f;
            Color wc = (Color){ 40, (unsigned char)(100 + wave * 40), (unsigned char)(200 + wave * 30), 255 };
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, wc);
            // Wave lines
            float wo = sinf((float)GetTime() * 3.0f + tx) * 2.0f;
            DrawLine(x + 2, y + 5 + (int)wo, x + 14, y + 5 + (int)wo, (Color){ 80, 150, 230, 255 });
            DrawLine(x + 4, y + 11 - (int)wo, x + 12, y + 11 - (int)wo, (Color){ 80, 150, 230, 255 });
            break;
        }
        case T_TREE:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, (Color){ 100, 180, 80, 255 });
            // Trunk
            DrawRectangle(x + 6, y + 10, 4, 6, (Color){ 120, 80, 40, 255 });
            // Canopy
            DrawCircle(x + 8, y + 7, 7, (Color){ 30, 120, 30, 255 });
            DrawCircle(x + 8, y + 6, 5, (Color){ 40, 140, 40, 255 });
            break;
        case T_DOOR:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, (Color){ 120, 80, 40, 255 });
            DrawRectangleLines(x + 1, y + 1, TILE_SIZE - 2, TILE_SIZE - 2, (Color){ 90, 60, 30, 255 });
            DrawRectangle(x + 11, y + 7, 2, 2, GOLD);
            break;
        case T_CHEST:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE,
                worldMap[ty][tx] == T_FLOOR ? (Color){180,160,130,255} : (Color){100,180,80,255});
            DrawRectangle(x + 2, y + 4, 12, 10, (Color){ 160, 120, 40, 255 });
            DrawRectangle(x + 2, y + 4, 12, 4, (Color){ 180, 140, 50, 255 });
            DrawRectangle(x + 6, y + 6, 4, 4, GOLD);
            break;
        case T_POT:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE,
                (Color){ 180, 160, 130, 255 });
            DrawRectangle(x + 3, y + 4, 10, 10, (Color){ 160, 140, 120, 255 });
            DrawRectangle(x + 4, y + 3, 8, 3, (Color){ 180, 160, 140, 255 });
            break;
        case T_BUSH:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, (Color){ 100, 180, 80, 255 });
            DrawRectangle(x + 2, y + 4, 12, 10, (Color){ 40, 130, 40, 255 });
            DrawRectangle(x + 3, y + 3, 10, 4, (Color){ 50, 150, 50, 255 });
            break;
        case T_STAIRS:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, (Color){ 80, 80, 100, 255 });
            for (int i = 0; i < 4; i++) {
                int sy = y + 4 * i;
                DrawRectangle(x + i*2, sy, TILE_SIZE - i*4, 4, (Color){ 100 + i*15, 100 + i*15, 120 + i*10, 255 });
            }
            break;
        case T_FENCE:
            DrawRectangle(x, y, TILE_SIZE, TILE_SIZE, (Color){ 100, 180, 80, 255 });
            DrawRectangle(x, y + 4, TILE_SIZE, 2, (Color){ 140, 100, 50, 255 });
            DrawRectangle(x, y + 10, TILE_SIZE, 2, (Color){ 140, 100, 50, 255 });
            DrawRectangle(x + 3, y + 2, 2, 12, (Color){ 160, 120, 60, 255 });
            DrawRectangle(x + 11, y + 2, 2, 12, (Color){ 160, 120, 60, 255 });
            break;
    }
}

void DrawPlayer(Player *p) {
    if (p->iframeTimer > 0 && ((int)(p->iframeTimer * 10) % 2 == 0)) return;

    float x = p->pos.x, y = p->pos.y;

    // Body
    Color tunic = (Color){ 80, 180, 80, 255 };
    DrawRectangle(x, y + 3, 8, 10, tunic);

    // Head / hair
    DrawRectangle(x + 1, y, 6, 5, (Color){ 240, 200, 160, 255 });
    Color hair = (Color){ 200, 170, 60, 255 };  // blonde like Link
    switch (p->dir) {
        case DIR_DOWN:
            DrawRectangle(x, y - 1, 8, 2, hair);
            DrawRectangle(x + 2, y + 2, 1, 1, BLACK);
            DrawRectangle(x + 5, y + 2, 1, 1, BLACK);
            break;
        case DIR_UP:
            DrawRectangle(x, y - 1, 8, 3, hair);
            break;
        case DIR_LEFT:
            DrawRectangle(x - 1, y - 1, 5, 3, hair);
            DrawRectangle(x + 1, y + 2, 1, 1, BLACK);
            break;
        case DIR_RIGHT:
            DrawRectangle(x + 4, y - 1, 5, 3, hair);
            DrawRectangle(x + 6, y + 2, 1, 1, BLACK);
            break;
    }

    // Hat point
    switch (p->dir) {
        case DIR_LEFT:  DrawRectangle(x - 3, y - 2, 4, 2, hair); break;
        case DIR_RIGHT: DrawRectangle(x + 7, y - 2, 4, 2, hair); break;
        default: DrawRectangle(x + 5, y - 3, 4, 3, hair); break;
    }

    // Sword swing
    if (p->swinging && p->hasSword) {
        Rectangle sb = SwordHitbox(p);
        Color swordCol = (Color){ 200, 220, 255, 255 };
        DrawRectangleRec(sb, swordCol);
        DrawRectangleLinesEx(sb, 1, (Color){ 150, 170, 200, 255 });
    }
}

void DrawEnemySprite(Enemy *e) {
    float x = e->pos.x, y = e->pos.y;

    switch (e->type) {
        case EN_OCTOROK:
            DrawRectangle(x, y + 2, 8, 8, (Color){ 200, 60, 60, 255 });
            DrawRectangle(x + 1, y, 6, 4, (Color){ 220, 80, 80, 255 });
            // Eyes
            DrawRectangle(x + 1, y + 3, 2, 2, WHITE);
            DrawRectangle(x + 5, y + 3, 2, 2, WHITE);
            // Mouth
            DrawRectangle(x + 3, y + 7, 2, 2, (Color){ 100, 30, 30, 255 });
            break;
        case EN_MOBLIN:
            DrawRectangle(x, y + 1, 8, 12, (Color){ 160, 100, 40, 255 });
            DrawRectangle(x + 1, y, 6, 4, (Color){ 140, 80, 30, 255 });
            DrawRectangle(x + 1, y + 2, 2, 2, RED);
            DrawRectangle(x + 5, y + 2, 2, 2, RED);
            // Spear
            if (e->dir == DIR_DOWN) DrawRectangle(x + 3, y + 12, 2, 6, GRAY);
            else if (e->dir == DIR_UP) DrawRectangle(x + 3, y - 6, 2, 6, GRAY);
            else if (e->dir == DIR_LEFT) DrawRectangle(x - 6, y + 5, 6, 2, GRAY);
            else DrawRectangle(x + 8, y + 5, 6, 2, GRAY);
            break;
        case EN_SLIME:
            DrawEllipse(x + 4, y + 8, 5, 4, (Color){ 60, 200, 60, 200 });
            DrawEllipse(x + 4, y + 6, 4, 3, (Color){ 80, 230, 80, 180 });
            DrawRectangle(x + 2, y + 5, 1, 1, WHITE);
            DrawRectangle(x + 5, y + 5, 1, 1, WHITE);
            break;
        case EN_BAT: {
            float flap = sinf(e->animTimer * 10.0f);
            int wingW = 3 + (int)(flap * 2);
            DrawRectangle(x + 2, y + 4, 4, 4, (Color){ 60, 40, 80, 255 });
            DrawRectangle(x - wingW + 2, y + 3, wingW, 3, (Color){ 80, 50, 100, 255 });
            DrawRectangle(x + 6, y + 3, wingW, 3, (Color){ 80, 50, 100, 255 });
            DrawRectangle(x + 2, y + 4, 1, 1, RED);
            DrawRectangle(x + 5, y + 4, 1, 1, RED);
            break;
        }
    }
}

void DrawItemSprite(Item *it) {
    float x = it->pos.x, y = it->pos.y;
    float bob = sinf(it->animTimer * 3.0f) * 2.0f;

    switch (it->type) {
        case ITEM_HEART:
            DrawRectangle(x + 1, y + bob + 2, 2, 3, RED);
            DrawRectangle(x + 5, y + bob + 2, 2, 3, RED);
            DrawRectangle(x + 0, y + bob + 3, 8, 3, RED);
            DrawRectangle(x + 1, y + bob + 6, 6, 1, RED);
            DrawRectangle(x + 2, y + bob + 7, 4, 1, RED);
            DrawRectangle(x + 3, y + bob + 8, 2, 1, RED);
            break;
        case ITEM_RUPEE:
            DrawTriangle(
                (Vector2){ x + 4, y + bob + 1 },
                (Vector2){ x + 1, y + bob + 5 },
                (Vector2){ x + 7, y + bob + 5 }, GREEN);
            DrawTriangle(
                (Vector2){ x + 1, y + bob + 5 },
                (Vector2){ x + 4, y + bob + 9 },
                (Vector2){ x + 7, y + bob + 5 }, (Color){ 0, 180, 0, 255 });
            break;
        case ITEM_KEY:
            DrawRectangle(x + 3, y + bob + 1, 3, 3, GOLD);
            DrawRectangle(x + 3, y + bob + 4, 2, 5, GOLD);
            DrawRectangle(x + 5, y + bob + 7, 2, 1, GOLD);
            break;
        case ITEM_TRIFORCE:
            DrawTriangle(
                (Vector2){ x + 4, y + bob },
                (Vector2){ x - 1, y + bob + 10 },
                (Vector2){ x + 9, y + bob + 10 }, GOLD);
            DrawTriangle(
                (Vector2){ x + 4, y + bob + 1 },
                (Vector2){ x + 1, y + bob + 9 },
                (Vector2){ x + 7, y + bob + 9 }, YELLOW);
            break;
    }
}

void DrawHearts(int health, int maxHealth) {
    for (int i = 0; i < maxHealth / 2; i++) {
        float x = 10 + i * 14;
        float y = 10;
        int hp = health - i * 2;
        Color c = (hp >= 2) ? RED : (hp == 1) ? (Color){200,80,80,255} : (Color){60,60,60,255};
        // Heart shape
        DrawRectangle(x, y + 1, 3, 4, c);
        DrawRectangle(x + 5, y + 1, 3, 4, c);
        DrawRectangle(x - 1, y + 3, 10, 3, c);
        DrawRectangle(x, y + 6, 8, 1, c);
        DrawRectangle(x + 1, y + 7, 6, 1, c);
        DrawRectangle(x + 2, y + 8, 4, 1, c);
        DrawRectangle(x + 3, y + 9, 2, 1, c);
    }
}

int main(void) {
    InitWindow(SCREEN_W, SCREEN_H, "Zelda - Link's Awakening Style");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    BuildWorld();

    Camera2D camera = { 0 };
    camera.zoom = ZOOM;

    bool gameOver = false;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;

        float screenW = (float)GetScreenWidth();
        float screenH = (float)GetScreenHeight();

        // --- Room transition ---
        if (transitioning) {
            transTimer += dt;
            float t = transTimer / transDuration;
            if (t >= 1.0f) {
                transitioning = false;
                player.roomX = newRoomX;
                player.roomY = newRoomY;
                transOffset = (Vector2){ 0 };
            } else {
                transOffset = (Vector2){
                    transDir.x * (1.0f - t) * ROOM_W * TILE_SIZE,
                    transDir.y * (1.0f - t) * ROOM_H * TILE_SIZE
                };
            }
            goto draw;
        }

        if (gameOver) goto draw;
        if (player.triforce) goto draw;

        // --- Player update ---
        player.animTimer += dt;
        player.swingCooldown -= dt;

        // Knockback
        if (player.knockbackTimer > 0) {
            player.knockbackTimer -= dt;
            float kx = player.knockbackDir.x * KNOCKBACK_SPEED * dt;
            float ky = player.knockbackDir.y * KNOCKBACK_SPEED * dt;
            if (!RectCollides(player.pos.x + kx, player.pos.y, 8, 12))
                player.pos.x += kx;
            if (!RectCollides(player.pos.x, player.pos.y + ky, 8, 12))
                player.pos.y += ky;
        } else if (!player.swinging) {
            // Movement
            Vector2 move = { 0 };
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  { move.x = -1; player.dir = DIR_LEFT; }
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) { move.x =  1; player.dir = DIR_RIGHT; }
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    { move.y = -1; player.dir = DIR_UP; }
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))   { move.y =  1; player.dir = DIR_DOWN; }

            if (move.x != 0 || move.y != 0) {
                if (move.x != 0 && move.y != 0) {
                    move.x *= 0.707f;
                    move.y *= 0.707f;
                }
                float nx = player.pos.x + move.x * PLAYER_SPEED * dt;
                float ny = player.pos.y + move.y * PLAYER_SPEED * dt;
                if (!RectCollides(nx, player.pos.y, 8, 12)) player.pos.x = nx;
                if (!RectCollides(player.pos.x, ny, 8, 12)) player.pos.y = ny;

                player.animFrame = ((int)(player.animTimer * 6) % 2);
            }
        }

        // Sword swing
        if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z)) &&
            player.hasSword && !player.swinging && player.swingCooldown <= 0) {
            player.swinging = true;
            player.swingTimer = SWORD_DURATION;
        }
        if (player.swinging) {
            player.swingTimer -= dt;
            if (player.swingTimer <= 0) {
                player.swinging = false;
                player.swingCooldown = SWORD_COOLDOWN;
            }
        }

        // I-frames
        if (player.iframeTimer > 0) player.iframeTimer -= dt;

        // --- Interact with tiles ---
        {
            // Check for chest / pot in front of player
            if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_X)) {
                int fx = (int)((player.pos.x + 4) / TILE_SIZE);
                int fy = (int)((player.pos.y + 6) / TILE_SIZE);
                switch (player.dir) {
                    case DIR_UP:    fy--; break;
                    case DIR_DOWN:  fy++; break;
                    case DIR_LEFT:  fx--; break;
                    case DIR_RIGHT: fx++; break;
                }
                if (fx >= 0 && fx < MAP_W && fy >= 0 && fy < MAP_H) {
                    if (worldMap[fy][fx] == T_CHEST) {
                        worldMap[fy][fx] = T_FLOOR;
                        SpawnBurst((Vector2){ fx * TILE_SIZE + 8, fy * TILE_SIZE + 8 }, GOLD, 12);
                        // Check if this is the sword chest (room 0,2)
                        if (fx < ROOM_W && fy >= ROOM_H * 2 && fy < ROOM_H * 3) {
                            player.hasSword = true;
                        }
                    } else if (worldMap[fy][fx] == T_POT) {
                        worldMap[fy][fx] = T_FLOOR;
                        SpawnBurst((Vector2){ fx * TILE_SIZE + 8, fy * TILE_SIZE + 8 }, (Color){160,140,120,255}, 8);
                        // Random drop
                        if (GetRandomValue(0, 2) == 0 && numItems < MAX_ITEMS) {
                            int rx = fx / ROOM_W, ry = fy / ROOM_H;
                            AddItem(rx, ry, fx % ROOM_W, fy % ROOM_H,
                                    GetRandomValue(0,1) == 0 ? ITEM_HEART : ITEM_RUPEE);
                        }
                    } else if (worldMap[fy][fx] == T_BUSH) {
                        worldMap[fy][fx] = T_EMPTY;
                        SpawnBurst((Vector2){ fx * TILE_SIZE + 8, fy * TILE_SIZE + 8 }, (Color){40,130,40,255}, 6);
                    } else if (worldMap[fy][fx] == T_DOOR) {
                        if (player.keys > 0) {
                            player.keys--;
                            worldMap[fy][fx] = T_FLOOR;
                            SpawnBurst((Vector2){ fx * TILE_SIZE + 8, fy * TILE_SIZE + 8 }, GOLD, 10);
                        }
                    }
                }
            }
        }

        // Bush slashing with sword
        if (player.swinging && player.swingTimer > SWORD_DURATION * 0.8f) {
            Rectangle sb = SwordHitbox(&player);
            int x1 = (int)(sb.x / TILE_SIZE);
            int y1 = (int)(sb.y / TILE_SIZE);
            int x2 = (int)((sb.x + sb.width) / TILE_SIZE);
            int y2 = (int)((sb.y + sb.height) / TILE_SIZE);
            for (int ty = y1; ty <= y2; ty++) {
                for (int tx = x1; tx <= x2; tx++) {
                    if (tx >= 0 && tx < MAP_W && ty >= 0 && ty < MAP_H) {
                        if (worldMap[ty][tx] == T_BUSH) {
                            worldMap[ty][tx] = T_EMPTY;
                            SpawnBurst((Vector2){ tx*TILE_SIZE+8, ty*TILE_SIZE+8 }, (Color){40,130,40,255}, 6);
                        } else if (worldMap[ty][tx] == T_POT) {
                            worldMap[ty][tx] = T_FLOOR;
                            SpawnBurst((Vector2){ tx*TILE_SIZE+8, ty*TILE_SIZE+8 }, (Color){160,140,120,255}, 6);
                        }
                    }
                }
            }
        }

        // --- Item pickup ---
        for (int i = 0; i < numItems; i++) {
            if (!items[i].active) continue;
            if (items[i].roomX != player.roomX || items[i].roomY != player.roomY) continue;
            items[i].animTimer += dt;

            Rectangle pr = { player.pos.x, player.pos.y, 8, 12 };
            Rectangle ir = { items[i].pos.x, items[i].pos.y, 8, 8 };
            if (CheckCollisionRecs(pr, ir)) {
                switch (items[i].type) {
                    case ITEM_HEART:
                        player.health += 2;
                        if (player.health > player.maxHealth) player.health = player.maxHealth;
                        break;
                    case ITEM_RUPEE:
                        player.rupees++;
                        break;
                    case ITEM_KEY:
                        player.keys++;
                        break;
                    case ITEM_TRIFORCE:
                        player.triforce = true;
                        break;
                }
                items[i].active = false;
                SpawnBurst(items[i].pos, YELLOW, 10);
            }
        }

        // --- Enemy update ---
        for (int i = 0; i < numEnemies; i++) {
            Enemy *e = &enemies[i];
            if (!e->active) continue;
            if (e->homeRoomX != player.roomX || e->homeRoomY != player.roomY) continue;

            e->animTimer += dt;

            // Knockback
            if (e->knockbackTimer > 0) {
                e->knockbackTimer -= dt;
                float kx = e->knockbackDir.x * KNOCKBACK_SPEED * 1.5f * dt;
                float ky = e->knockbackDir.y * KNOCKBACK_SPEED * 1.5f * dt;
                if (!RectCollides(e->pos.x + kx, e->pos.y, 8, 8)) e->pos.x += kx;
                if (!RectCollides(e->pos.x, e->pos.y + ky, 8, 8)) e->pos.y += ky;
                continue;  // skip AI while knocked back
            }

            // AI movement
            e->moveTimer -= dt;
            if (e->moveTimer <= 0) {
                e->moveTimer = 0.5f + (float)GetRandomValue(0, 100) / 100.0f;
                float speed = (e->type == EN_BAT) ? 50.0f : 30.0f;

                if (e->type == EN_BAT) {
                    // Bats move toward player erratically
                    Vector2 toPlayer = Vector2Subtract(player.pos, e->pos);
                    if (Vector2Length(toPlayer) > 1.0f) toPlayer = Vector2Normalize(toPlayer);
                    toPlayer.x += (float)GetRandomValue(-50, 50) / 100.0f;
                    toPlayer.y += (float)GetRandomValue(-50, 50) / 100.0f;
                    e->vel = Vector2Scale(toPlayer, speed);
                } else {
                    // Random cardinal direction, sometimes toward player
                    if (GetRandomValue(0, 2) == 0) {
                        Vector2 toP = Vector2Subtract(player.pos, e->pos);
                        if (fabsf(toP.x) > fabsf(toP.y)) {
                            e->vel = (Vector2){ (toP.x > 0 ? 1 : -1) * speed, 0 };
                            e->dir = (toP.x > 0) ? DIR_RIGHT : DIR_LEFT;
                        } else {
                            e->vel = (Vector2){ 0, (toP.y > 0 ? 1 : -1) * speed };
                            e->dir = (toP.y > 0) ? DIR_DOWN : DIR_UP;
                        }
                    } else {
                        int d = GetRandomValue(0, 3);
                        e->dir = d;
                        float dirs[][2] = {{0,1},{0,-1},{-1,0},{1,0}};
                        e->vel = (Vector2){ dirs[d][0] * speed, dirs[d][1] * speed };
                    }
                }
            }

            // Move with collision
            float nx = e->pos.x + e->vel.x * dt;
            float ny = e->pos.y + e->vel.y * dt;
            if (!RectCollides(nx, e->pos.y, 8, 8)) e->pos.x = nx; else e->vel.x = 0;
            if (!RectCollides(e->pos.x, ny, 8, 8)) e->pos.y = ny; else e->vel.y = 0;

            // Octorok shooting
            if (e->type == EN_OCTOROK) {
                e->shootTimer -= dt;
                if (e->shootTimer <= 0 && Vector2Distance(e->pos, player.pos) < 80.0f) {
                    e->shootTimer = 2.0f + (float)GetRandomValue(0, 100) / 100.0f;
                    for (int p = 0; p < MAX_PROJECTILES; p++) {
                        if (!projectiles[p].active) {
                            projectiles[p].pos = (Vector2){ e->pos.x + 4, e->pos.y + 4 };
                            Vector2 dir = Vector2Normalize(Vector2Subtract(player.pos, e->pos));
                            projectiles[p].vel = Vector2Scale(dir, PROJ_SPEED);
                            projectiles[p].active = true;
                            projectiles[p].life = 2.0f;
                            projectiles[p].fromPlayer = false;
                            break;
                        }
                    }
                }
            }

            // Sword hit detection
            if (player.swinging && player.swingTimer > 0) {
                Rectangle sb = SwordHitbox(&player);
                Rectangle eb = { e->pos.x, e->pos.y, 8, 8 };
                if (CheckCollisionRecs(sb, eb)) {
                    e->health--;
                    Vector2 kbDir = Vector2Normalize(Vector2Subtract(e->pos, player.pos));
                    e->knockbackDir = kbDir;
                    e->knockbackTimer = KNOCKBACK_TIME;
                    if (e->health <= 0) {
                        e->active = false;
                        SpawnBurst(e->pos, (Color){200,60,60,255}, 12);
                        // Drop
                        if (GetRandomValue(0, 2) == 0 && numItems < MAX_ITEMS) {
                            AddItem(e->homeRoomX, e->homeRoomY,
                                    (int)(e->pos.x / TILE_SIZE) % ROOM_W,
                                    (int)(e->pos.y / TILE_SIZE) % ROOM_H,
                                    GetRandomValue(0,1) == 0 ? ITEM_HEART : ITEM_RUPEE);
                        }
                    } else {
                        SpawnBurst(e->pos, WHITE, 6);
                    }
                }
            }

            // Enemy touch damage
            if (player.iframeTimer <= 0 && player.knockbackTimer <= 0) {
                Rectangle pr = { player.pos.x, player.pos.y, 8, 12 };
                Rectangle er = { e->pos.x, e->pos.y, 8, 8 };
                if (CheckCollisionRecs(pr, er)) {
                    player.health -= (e->type == EN_MOBLIN) ? 2 : 1;
                    player.iframeTimer = IFRAME_TIME;
                    Vector2 kbDir = Vector2Normalize(Vector2Subtract(player.pos, e->pos));
                    player.knockbackDir = kbDir;
                    player.knockbackTimer = KNOCKBACK_TIME;
                    if (player.health <= 0) {
                        player.health = 0;
                        gameOver = true;
                        SpawnBurst(player.pos, RED, 20);
                    }
                }
            }
        }

        // --- Projectile update ---
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (!projectiles[i].active) continue;
            projectiles[i].pos = Vector2Add(projectiles[i].pos,
                Vector2Scale(projectiles[i].vel, dt));
            projectiles[i].life -= dt;
            if (projectiles[i].life <= 0) { projectiles[i].active = false; continue; }

            // Tile collision
            int tx = (int)(projectiles[i].pos.x / TILE_SIZE);
            int ty = (int)(projectiles[i].pos.y / TILE_SIZE);
            if (TileSolid(tx, ty)) { projectiles[i].active = false; continue; }

            // Hit player
            if (!projectiles[i].fromPlayer && player.iframeTimer <= 0) {
                Rectangle pr = { player.pos.x, player.pos.y, 8, 12 };
                if (CheckCollisionPointRec(projectiles[i].pos, pr)) {
                    player.health--;
                    player.iframeTimer = IFRAME_TIME;
                    Vector2 kbDir = Vector2Normalize(projectiles[i].vel);
                    player.knockbackDir = kbDir;
                    player.knockbackTimer = KNOCKBACK_TIME;
                    projectiles[i].active = false;
                    if (player.health <= 0) { player.health = 0; gameOver = true; }
                }
            }

            // Sword deflect
            if (!projectiles[i].fromPlayer && player.swinging) {
                Rectangle sb = SwordHitbox(&player);
                if (CheckCollisionPointRec(projectiles[i].pos, sb)) {
                    projectiles[i].vel = Vector2Scale(projectiles[i].vel, -1);
                    projectiles[i].fromPlayer = true;
                    SpawnBurst(projectiles[i].pos, WHITE, 4);
                }
            }
        }

        // --- Room transition check ---
        {
            float roomPxW = ROOM_W * TILE_SIZE;
            float roomPxH = ROOM_H * TILE_SIZE;
            float px = player.pos.x, py = player.pos.y;
            float roomLeft = player.roomX * roomPxW;
            float roomTop = player.roomY * roomPxH;

            if (px < roomLeft && player.roomX > 0) {
                newRoomX = player.roomX - 1; newRoomY = player.roomY;
                transDir = (Vector2){ 1, 0 };
                transitioning = true; transTimer = 0;
                player.pos.x = (newRoomX + 1) * roomPxW - 12;
            } else if (px + 8 > roomLeft + roomPxW && player.roomX < WORLD_W - 1) {
                newRoomX = player.roomX + 1; newRoomY = player.roomY;
                transDir = (Vector2){ -1, 0 };
                transitioning = true; transTimer = 0;
                player.pos.x = newRoomX * roomPxW + 4;
            } else if (py < roomTop && player.roomY > 0) {
                newRoomX = player.roomX; newRoomY = player.roomY - 1;
                transDir = (Vector2){ 0, 1 };
                transitioning = true; transTimer = 0;
                player.pos.y = (newRoomY + 1) * roomPxH - 16;
            } else if (py + 12 > roomTop + roomPxH && player.roomY < WORLD_H - 1) {
                newRoomX = player.roomX; newRoomY = player.roomY + 1;
                transDir = (Vector2){ 0, -1 };
                transitioning = true; transTimer = 0;
                player.pos.y = newRoomY * roomPxH + 4;
            }
        }

        // --- Restart ---
        if (gameOver && IsKeyPressed(KEY_R)) {
            numEnemies = 0;
            numItems = 0;
            memset(particles, 0, sizeof(particles));
            memset(projectiles, 0, sizeof(projectiles));
            BuildWorld();
            gameOver = false;
        }

draw:
        // --- Particles update ---
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particles[i].life <= 0) continue;
            particles[i].pos = Vector2Add(particles[i].pos, Vector2Scale(particles[i].vel, dt));
            particles[i].vel = Vector2Scale(particles[i].vel, 0.95f);
            particles[i].life -= dt;
        }

        // --- Camera: lock to current room ---
        {
            int rx = transitioning ? newRoomX : player.roomX;
            int ry = transitioning ? newRoomY : player.roomY;
            float roomCx = rx * ROOM_W * TILE_SIZE + (ROOM_W * TILE_SIZE) / 2.0f;
            float roomCy = ry * ROOM_H * TILE_SIZE + (ROOM_H * TILE_SIZE) / 2.0f;
            camera.target = (Vector2){ roomCx + transOffset.x, roomCy + transOffset.y };
            camera.offset = (Vector2){ screenW / 2.0f, screenH / 2.0f };
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(camera);
            // Draw visible tiles (current room + neighbors for transition)
            int drawRx0 = (transitioning ? (newRoomX < player.roomX ? newRoomX : player.roomX) : player.roomX);
            int drawRy0 = (transitioning ? (newRoomY < player.roomY ? newRoomY : player.roomY) : player.roomY);
            int drawRx1 = (transitioning ? (newRoomX > player.roomX ? newRoomX : player.roomX) : player.roomX);
            int drawRy1 = (transitioning ? (newRoomY > player.roomY ? newRoomY : player.roomY) : player.roomY);

            for (int ry = drawRy0; ry <= drawRy1; ry++) {
                for (int rx = drawRx0; rx <= drawRx1; rx++) {
                    for (int y = 0; y < ROOM_H; y++) {
                        for (int x = 0; x < ROOM_W; x++) {
                            DrawTile(rx * ROOM_W + x, ry * ROOM_H + y);
                        }
                    }
                }
            }

            // Draw items
            for (int i = 0; i < numItems; i++) {
                if (!items[i].active) continue;
                if (items[i].roomX < drawRx0 || items[i].roomX > drawRx1) continue;
                if (items[i].roomY < drawRy0 || items[i].roomY > drawRy1) continue;
                DrawItemSprite(&items[i]);
            }

            // Draw enemies
            for (int i = 0; i < numEnemies; i++) {
                if (!enemies[i].active) continue;
                if (enemies[i].homeRoomX < drawRx0 || enemies[i].homeRoomX > drawRx1) continue;
                if (enemies[i].homeRoomY < drawRy0 || enemies[i].homeRoomY > drawRy1) continue;
                DrawEnemySprite(&enemies[i]);
            }

            // Draw projectiles
            for (int i = 0; i < MAX_PROJECTILES; i++) {
                if (!projectiles[i].active) continue;
                DrawCircleV(projectiles[i].pos, 3, projectiles[i].fromPlayer ? SKYBLUE : MAROON);
            }

            // Draw player
            if (!gameOver) DrawPlayer(&player);

            // Draw particles
            for (int i = 0; i < MAX_PARTICLES; i++) {
                if (particles[i].life <= 0) continue;
                float a = particles[i].life / particles[i].maxLife;
                Color c = particles[i].color;
                c.a = (unsigned char)(a * 255);
                float s = particles[i].size * a;
                DrawRectangle(particles[i].pos.x - s/2, particles[i].pos.y - s/2, s, s, c);
            }

        EndMode2D();

        // --- HUD ---
        DrawHearts(player.health, player.maxHealth);

        // Rupees
        DrawRectangle(10, 24, 6, 10, GREEN);
        DrawText(TextFormat("x%d", player.rupees), 20, 24, 16, WHITE);

        // Keys
        if (player.keys > 0) {
            DrawRectangle(70, 26, 4, 8, GOLD);
            DrawText(TextFormat("x%d", player.keys), 78, 24, 16, WHITE);
        }

        // Sword indicator
        if (!player.hasSword) {
            DrawText("Find the sword!", 10, GetScreenHeight() - 22, 14, (Color){150,150,170,255});
        }

        // Controls
        DrawText("Arrows/WASD: Move  Z/Space: Sword  E/X: Interact", 10, GetScreenHeight() - 40, 12, (Color){120,120,140,255});

        // Room indicator (mini-map)
        {
            int mmx = GetScreenWidth() - 70, mmy = 10;
            for (int ry = 0; ry < WORLD_H; ry++) {
                for (int rx = 0; rx < WORLD_W; rx++) {
                    // Check if room has content (any non-empty border means it exists)
                    bool hasRoom = false;
                    int ox = rx * ROOM_W, oy = ry * ROOM_H;
                    for (int y = 0; y < ROOM_H && !hasRoom; y++)
                        for (int x = 0; x < ROOM_W && !hasRoom; x++)
                            if (worldMap[oy+y][ox+x] != T_EMPTY || worldMap[oy][ox] == T_WALL)
                                hasRoom = true;
                    if (!hasRoom) continue;

                    Color rc = (rx == player.roomX && ry == player.roomY) ? GREEN : (Color){60,60,80,255};
                    DrawRectangle(mmx + rx * 14, mmy + ry * 11, 12, 9, rc);
                    DrawRectangleLines(mmx + rx * 14, mmy + ry * 11, 12, 9, (Color){80,80,100,255});
                }
            }
        }

        // Game over
        if (gameOver) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){0,0,0,150});
            const char *goText = "GAME OVER";
            int gw = MeasureText(goText, 50);
            DrawText(goText, GetScreenWidth()/2 - gw/2, GetScreenHeight()/2 - 40, 50, RED);
            DrawText("Press R to restart", GetScreenWidth()/2 - 80, GetScreenHeight()/2 + 20, 20, WHITE);
        }

        // Triforce win
        if (player.triforce) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){0,0,0,120});
            const char *winText = "YOU GOT THE TRIFORCE!";
            int ww = MeasureText(winText, 40);
            DrawText(winText, GetScreenWidth()/2 - ww/2, GetScreenHeight()/2 - 30, 40, GOLD);
            DrawText(TextFormat("Rupees: %d", player.rupees), GetScreenWidth()/2 - 50, GetScreenHeight()/2 + 20, 20, WHITE);
        }

        DrawFPS(10, GetScreenHeight() - 60);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
