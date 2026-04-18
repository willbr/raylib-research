#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// RE1-style 3D: fixed camera angles, tank controls, 3D rooms

#define MAX_ROOMS       10
#define MAX_ENEMIES      8
#define MAX_ITEMS       16
#define MAX_DOORS       12
#define MAX_INV          8
#define MAX_BULLETS     10
#define MAX_WALLS       12

// Physics
#define PLAYER_SPEED    3.0f
#define PLAYER_RUN      5.0f
#define TURN_SPEED      3.0f
#define SHOOT_COOLDOWN  0.5f
#define BULLET_SPEED   20.0f
#define KNIFE_RANGE     1.8f
#define KNIFE_ARC       0.7f
#define IFRAME_TIME     1.5f
#define KNOCKBACK_TIME  0.2f
#define KNOCKBACK_SPEED 6.0f
#define CAM_LERP        4.0f

// Zombie
#define ZOMBIE_SPEED    1.2f
#define ZOMBIE_CHASE    8.0f
#define ZOMBIE_ATTACK   1.2f
#define ZOMBIE_HP       6

// Item types
typedef enum {
    IT_NONE = 0, IT_HANDGUN_AMMO, IT_HEALTH_SPRAY, IT_KEY_SWORD,
    IT_KEY_ARMOR, IT_KEY_SHIELD, IT_HANDGUN, IT_FILE,
} ItemType;

static const char *itemNames[] = {
    "", "Handgun Ammo", "First Aid Spray", "Sword Key", "Armor Key",
    "Shield Key", "Handgun", "File"
};

typedef enum { LOCK_NONE = 0, LOCK_SWORD, LOCK_ARMOR, LOCK_SHIELD } LockType;

// Wall segment (axis-aligned box)
typedef struct {
    Vector3 pos;    // center
    Vector3 size;   // half-extents
    Color color;
} Wall;

typedef struct {
    Vector3 floorMin, floorMax;     // walkable rectangle
    float ceilHeight;
    Color floorColor, wallColor, ceilColor;
    Wall walls[MAX_WALLS];
    int numWalls;
    // Fixed camera
    Vector3 camPos;
    Vector3 camTarget;
    float camFov;
    bool isDark;
} Room;

typedef struct {
    Vector3 pos;
    float rotation;
    int health, maxHealth;
    int ammo, maxAmmo;
    bool hasGun, aiming, running, dead;
    float shootTimer, iframeTimer, knockbackTimer, deathTimer, walkTimer;
    Vector3 knockbackDir;
    int currentRoom;
    ItemType inventory[MAX_INV];
    int invCount;
} Player;

typedef struct {
    Vector3 pos;
    float rotation;
    int health;
    bool active, alerted;
    float attackTimer, groanTimer, stunTimer, animTimer;
    int room;
} Zombie;

typedef struct {
    Vector3 pos;
    ItemType type;
    bool active;
    int room;
    float bobTimer;
} PickupItem;

typedef struct {
    Vector3 pos, targetPos;
    int fromRoom, toRoom;
    LockType lock;
    bool locked;
} Door;

typedef struct {
    Vector3 pos, vel;
    bool active;
    float life;
} Bullet;

static Room rooms[MAX_ROOMS];
static int numRooms = 0;
static Player player;
static Zombie zombies[MAX_ENEMIES];
static int numZombies = 0;
static PickupItem pickups[MAX_ITEMS];
static int numPickups = 0;
static Door doors[MAX_DOORS];
static int numDoors = 0;
static Bullet bullets[MAX_BULLETS];

static bool showInventory = false;
static int invCursor = 0;
static bool showMessage = false;
static const char *messageText = "";
static float messageTimer = 0;
static bool doorTransition = false;
static float doorTransTimer = 0;
static int doorTargetRoom = 0;
static Vector3 doorTargetPos;

void ShowMsg(const char *t, float d) { messageText = t; messageTimer = d; showMessage = true; }

bool HasItem(ItemType t) {
    for (int i = 0; i < player.invCount; i++) if (player.inventory[i] == t) return true;
    return false;
}
bool RemoveItem(ItemType t) {
    for (int i = 0; i < player.invCount; i++)
        if (player.inventory[i] == t) {
            for (int j = i; j < player.invCount - 1; j++) player.inventory[j] = player.inventory[j+1];
            player.invCount--; return true;
        }
    return false;
}
bool AddItem(ItemType t) {
    if (player.invCount >= MAX_INV) { ShowMsg("Inventory full.", 2); return false; }
    player.inventory[player.invCount++] = t; return true;
}

int AddRoom(Vector3 fmin, Vector3 fmax, float ceil, Color floor, Color wall, Color ceilC,
            Vector3 camP, Vector3 camT, float fov, bool dark) {
    int id = numRooms++;
    rooms[id].floorMin = fmin;
    rooms[id].floorMax = fmax;
    rooms[id].ceilHeight = ceil;
    rooms[id].floorColor = floor;
    rooms[id].wallColor = wall;
    rooms[id].ceilColor = ceilC;
    rooms[id].numWalls = 0;
    rooms[id].camPos = camP;
    rooms[id].camTarget = camT;
    rooms[id].camFov = fov;
    rooms[id].isDark = dark;
    return id;
}

void AddWall(int r, Vector3 pos, Vector3 size, Color c) {
    if (rooms[r].numWalls >= MAX_WALLS) return;
    Wall *w = &rooms[r].walls[rooms[r].numWalls++];
    w->pos = pos; w->size = size; w->color = c;
}

void AddDoorPair(int from, Vector3 fromP, int to, Vector3 toP, LockType lock) {
    if (numDoors + 2 > MAX_DOORS) return;
    doors[numDoors++] = (Door){ fromP, toP, from, to, lock, lock != LOCK_NONE };
    doors[numDoors++] = (Door){ toP, fromP, to, from, lock, lock != LOCK_NONE };
}

void AddZombie(int room, float x, float z) {
    if (numZombies >= MAX_ENEMIES) return;
    zombies[numZombies++] = (Zombie){
        .pos = {x, 0, z}, .rotation = (float)GetRandomValue(0,628)/100.0f,
        .health = ZOMBIE_HP, .active = true, .room = room,
        .groanTimer = (float)GetRandomValue(200,500)/100.0f
    };
}

void AddPickup(int room, float x, float z, ItemType type) {
    if (numPickups >= MAX_ITEMS) return;
    pickups[numPickups++] = (PickupItem){ .pos = {x, 0.5f, z}, .type = type, .active = true, .room = room };
}

// Collision: check if point is outside room bounds or inside a wall
bool Collides(Vector3 pos, float radius, int room) {
    Room *r = &rooms[room];
    if (pos.x - radius < r->floorMin.x || pos.x + radius > r->floorMax.x ||
        pos.z - radius < r->floorMin.z || pos.z + radius > r->floorMax.z)
        return true;
    for (int i = 0; i < r->numWalls; i++) {
        Wall *w = &r->walls[i];
        if (pos.x + radius > w->pos.x - w->size.x && pos.x - radius < w->pos.x + w->size.x &&
            pos.z + radius > w->pos.z - w->size.z && pos.z - radius < w->pos.z + w->size.z)
            return true;
    }
    return false;
}

void BuildMansion(void) {
    Color darkWood = {50,30,18,255};
    Color carpet   = {70,25,25,255};
    Color tile     = {90,90,80,255};
    Color stone    = {60,60,65,255};
    Color wallBrown= {65,45,30,255};
    Color wallGrey = {55,55,60,255};
    Color ceilDark = {35,25,18,255};
    Color ceilGrey = {50,50,55,255};

    // Room 0: Main Hall — large, grand
    int r0 = AddRoom((Vector3){-6,0,-8}, (Vector3){6,0,8}, 4.0f,
        carpet, wallBrown, ceilDark,
        (Vector3){0, 8, -12}, (Vector3){0, 0, 0}, 50, false);
    // Staircase block
    AddWall(r0, (Vector3){0,1.5f,-5}, (Vector3){1.5f,1.5f,1.5f}, (Color){80,55,35,255});
    // Pillars
    AddWall(r0, (Vector3){-4,1.5f,0}, (Vector3){0.3f,1.5f,0.3f}, wallBrown);
    AddWall(r0, (Vector3){ 4,1.5f,0}, (Vector3){0.3f,1.5f,0.3f}, wallBrown);

    // Room 1: Dining Room
    int r1 = AddRoom((Vector3){-5,0,-4}, (Vector3){5,0,4}, 3.5f,
        darkWood, wallBrown, ceilDark,
        (Vector3){6, 6, -6}, (Vector3){0, 0, 0}, 55, false);
    // Dining table
    AddWall(r1, (Vector3){0,0.45f,0}, (Vector3){2.5f,0.45f,0.8f}, (Color){90,60,35,255});

    // Room 2: West Corridor — long, dark
    int r2 = AddRoom((Vector3){-1,0,-8}, (Vector3){1,0,8}, 3.0f,
        darkWood, wallBrown, ceilDark,
        (Vector3){4, 5, 0}, (Vector3){0, 0, 0}, 50, true);

    // Room 3: East Corridor
    int r3 = AddRoom((Vector3){-1,0,-8}, (Vector3){1,0,8}, 3.0f,
        darkWood, wallBrown, ceilDark,
        (Vector3){-4, 5, 0}, (Vector3){0, 0, 0}, 50, true);

    // Room 4: Library
    int r4 = AddRoom((Vector3){-4,0,-4}, (Vector3){4,0,4}, 3.5f,
        darkWood, wallBrown, ceilDark,
        (Vector3){0, 7, -7}, (Vector3){0, 0, 0}, 50, false);
    // Bookshelves
    AddWall(r4, (Vector3){-3.5f,1.5f,0}, (Vector3){0.3f,1.5f,2.5f}, (Color){70,45,25,255});
    AddWall(r4, (Vector3){ 3.5f,1.5f,0}, (Vector3){0.3f,1.5f,2.5f}, (Color){70,45,25,255});
    // Desk
    AddWall(r4, (Vector3){0,0.4f,1}, (Vector3){1.5f,0.4f,0.5f}, (Color){85,60,35,255});

    // Room 5: Medical Room
    int r5 = AddRoom((Vector3){-3,0,-3}, (Vector3){3,0,3}, 3.0f,
        tile, wallGrey, ceilGrey,
        (Vector3){5, 5, -5}, (Vector3){0, 0, 0}, 55, false);
    // Gurney
    AddWall(r5, (Vector3){-1.5f,0.4f,-1}, (Vector3){1.0f,0.4f,0.3f}, (Color){180,180,180,255});

    // Room 6: Storage
    int r6 = AddRoom((Vector3){-3,0,-2.5f}, (Vector3){3,0,2.5f}, 3.0f,
        stone, wallGrey, ceilGrey,
        (Vector3){0, 6, -5}, (Vector3){0, 0, 0}, 50, true);
    // Shelves
    AddWall(r6, (Vector3){-1.5f,1,0}, (Vector3){0.2f,1,1.8f}, (Color){80,70,60,255});
    AddWall(r6, (Vector3){ 1.5f,1,0}, (Vector3){0.2f,1,1.8f}, (Color){80,70,60,255});

    // Room 7: Art Gallery
    int r7 = AddRoom((Vector3){-5,0,-3}, (Vector3){5,0,3}, 4.0f,
        carpet, wallBrown, ceilDark,
        (Vector3){0, 6, -8}, (Vector3){0, 0, 0}, 50, false);
    // Display pedestal
    AddWall(r7, (Vector3){0,0.6f,0}, (Vector3){0.5f,0.6f,0.5f}, (Color){120,100,80,255});

    // Room 8: Basement
    int r8 = AddRoom((Vector3){-2.5f,0,-4}, (Vector3){2.5f,0,4}, 2.5f,
        stone, wallGrey, ceilGrey,
        (Vector3){4, 4, -4}, (Vector3){0, 0, 0}, 55, true);
    // Stairwell block
    AddWall(r8, (Vector3){0.5f,1,1.5f}, (Vector3){0.8f,1,1.2f}, stone);

    // Room 9: Lab
    int r9 = AddRoom((Vector3){-4,0,-4}, (Vector3){4,0,4}, 3.0f,
        tile, wallGrey, ceilGrey,
        (Vector3){-5, 7, -7}, (Vector3){0, 0, 0}, 50, false);
    AddWall(r9, (Vector3){0,0.5f,-3}, (Vector3){1.5f,0.5f,0.3f}, (Color){100,100,110,255});
    AddWall(r9, (Vector3){-2.5f,0.5f,1.5f}, (Vector3){0.8f,0.5f,0.3f}, (Color){100,100,110,255});

    // --- Doors ---
    AddDoorPair(0, (Vector3){5.5f,0,2}, 1, (Vector3){-4.5f,0,0}, LOCK_NONE);
    AddDoorPair(0, (Vector3){-5.5f,0,3}, 2, (Vector3){0,0,-7}, LOCK_NONE);
    AddDoorPair(0, (Vector3){5.5f,0,-3}, 3, (Vector3){0,0,-7}, LOCK_NONE);
    AddDoorPair(2, (Vector3){0,0,0}, 4, (Vector3){0,0,3.5f}, LOCK_NONE);
    AddDoorPair(2, (Vector3){0,0,7}, 6, (Vector3){0,0,-2}, LOCK_SWORD);
    AddDoorPair(3, (Vector3){0,0,0}, 5, (Vector3){0,0,2.5f}, LOCK_NONE);
    AddDoorPair(3, (Vector3){0,0,7}, 7, (Vector3){0,0,-2.5f}, LOCK_ARMOR);
    AddDoorPair(1, (Vector3){0,0,3.5f}, 8, (Vector3){0,0,-3.5f}, LOCK_NONE);
    AddDoorPair(8, (Vector3){0,0,3.5f}, 9, (Vector3){0,0,-3.5f}, LOCK_SHIELD);

    // --- Zombies ---
    AddZombie(1, 2, 0);
    AddZombie(2, 0, -2);
    AddZombie(2, 0, 4);
    AddZombie(3, 0, 3);
    AddZombie(4, 2, -1);
    AddZombie(7, 2, 1);
    AddZombie(8, -1, 2);
    AddZombie(9, 2, 1);

    // --- Items ---
    AddPickup(0, 3, 6, IT_HANDGUN);
    AddPickup(0, -3, -5, IT_HANDGUN_AMMO);
    AddPickup(1, -3, -2, IT_HEALTH_SPRAY);
    AddPickup(1, 3, -2, IT_FILE);
    AddPickup(4, 2, -3, IT_KEY_SWORD);
    AddPickup(4, -2, 2, IT_HANDGUN_AMMO);
    AddPickup(5, 1, -1, IT_HEALTH_SPRAY);
    AddPickup(5, -1, 1, IT_HANDGUN_AMMO);
    AddPickup(6, 0, 1, IT_KEY_ARMOR);
    AddPickup(7, 0, 0.8f, IT_KEY_SHIELD);
    AddPickup(7, 3, 1, IT_HANDGUN_AMMO);
    AddPickup(9, -2, 2, IT_HEALTH_SPRAY);

    // Player
    player = (Player){
        .pos = {0, 0, 5}, .rotation = -PI/2,
        .health = 8, .maxHealth = 8, .maxAmmo = 15, .currentRoom = 0
    };
}

// --- Drawing ---

void DrawRoom3D(int id, Vector3 camPos) {
    Room *r = &rooms[id];
    float x0 = r->floorMin.x, z0 = r->floorMin.z;
    float x1 = r->floorMax.x, z1 = r->floorMax.z;
    float h = r->ceilHeight;
    float cx = (x0+x1)/2, cz = (z0+z1)/2;

    // Floor
    DrawPlane((Vector3){cx, 0, cz}, (Vector2){x1-x0, z1-z0}, r->floorColor);

    // Floor detail lines
    for (float lz = z0; lz < z1; lz += 1.5f)
        DrawLine3D((Vector3){x0, 0.01f, lz}, (Vector3){x1, 0.01f, lz},
            (Color){(unsigned char)(r->floorColor.r+8), (unsigned char)(r->floorColor.g+8),
                    (unsigned char)(r->floorColor.b+6), 40});

    // Room boundary walls — cull walls between camera and room center
    float wallThick = 0.15f;
    Color wc = r->wallColor;
    Color wcD = {(unsigned char)(wc.r*0.8f),(unsigned char)(wc.g*0.8f),(unsigned char)(wc.b*0.8f),255};

    // North wall (z = z0): only draw if camera is south of center
    if (camPos.z >= cz)
        DrawCube((Vector3){cx, h/2, z0 - wallThick}, x1-x0 + wallThick*2, h, wallThick*2, wc);
    // South wall (z = z1): only draw if camera is north of center
    if (camPos.z <= cz)
        DrawCube((Vector3){cx, h/2, z1 + wallThick}, x1-x0 + wallThick*2, h, wallThick*2, wcD);
    // West wall (x = x0): only draw if camera is east of center
    if (camPos.x >= cx)
        DrawCube((Vector3){x0 - wallThick, h/2, cz}, wallThick*2, h, z1-z0, wcD);
    // East wall (x = x1): only draw if camera is west of center
    if (camPos.x <= cx)
        DrawCube((Vector3){x1 + wallThick, h/2, cz}, wallThick*2, h, z1-z0, wc);

    // Interior walls / furniture
    for (int i = 0; i < r->numWalls; i++) {
        Wall *w = &r->walls[i];
        DrawCube(w->pos, w->size.x*2, w->size.y*2, w->size.z*2, w->color);
        DrawCubeWires(w->pos, w->size.x*2, w->size.y*2, w->size.z*2,
            (Color){w->color.r/2, w->color.g/2, w->color.b/2, 255});
    }

    // Door markers — orient to nearest wall
    for (int i = 0; i < numDoors; i++) {
        if (doors[i].fromRoom != id) continue;
        Color dc = doors[i].locked ? RED : (Color){180,140,50,255};
        Vector3 dp = doors[i].pos;

        // Snap door to nearest wall edge and orient it
        float dN = fabsf(dp.z - z0), dS = fabsf(dp.z - z1);
        float dW = fabsf(dp.x - x0), dE = fabsf(dp.x - x1);
        float dMin = fminf(fminf(dN, dS), fminf(dW, dE));

        float dw, dd;
        if (dMin == dN)      { dp.z = z0; dw = 0.8f; dd = 0.3f; }
        else if (dMin == dS) { dp.z = z1; dw = 0.8f; dd = 0.3f; }
        else if (dMin == dW) { dp.x = x0; dw = 0.3f; dd = 0.8f; }
        else                 { dp.x = x1; dw = 0.3f; dd = 0.8f; }

        dp.y = 1.0f;
        DrawCube(dp, dw, 2.0f, dd, dc);
        DrawCubeWires(dp, dw, 2.0f, dd, (Color){dc.r/2, dc.g/2, dc.b/2, 255});
    }
}

void DrawPlayer3D(Player *p) {
    if (p->iframeTimer > 0 && ((int)(p->iframeTimer * 8) % 2 == 0)) return;

    float cs = cosf(p->rotation), sn = sinf(p->rotation);
    Vector3 pos = p->pos;

    // Legs
    DrawCube((Vector3){pos.x, 0.3f, pos.z}, 0.4f, 0.6f, 0.3f, (Color){50,50,60,255});
    // Torso
    DrawCube((Vector3){pos.x, 0.9f, pos.z}, 0.5f, 0.6f, 0.3f, (Color){120,100,75,255});
    // Head
    DrawSphere((Vector3){pos.x + cs*0.15f, 1.4f, pos.z + sn*0.15f}, 0.2f, (Color){220,190,150,255});
    // Hair
    DrawSphere((Vector3){pos.x, 1.5f, pos.z}, 0.18f, (Color){130,90,40,255});

    // Gun / aim indicator
    if (p->aiming && p->hasGun) {
        Vector3 gunEnd = {pos.x + cs*1.0f, 0.85f, pos.z + sn*1.0f};
        DrawCylinderEx((Vector3){pos.x + cs*0.3f, 0.85f, pos.z + sn*0.3f},
                       gunEnd, 0.04f, 0.03f, 6, DARKGRAY);
    } else {
        // Direction line on ground
        DrawLine3D((Vector3){pos.x, 0.05f, pos.z},
                   (Vector3){pos.x + cs*0.6f, 0.05f, pos.z + sn*0.6f},
                   (Color){150,130,100,150});
    }
}

void DrawZombie3D(Zombie *z) {
    if (!z->active) return;
    float cs = cosf(z->rotation), sn = sinf(z->rotation);
    float sway = sinf(z->animTimer * 3.0f) * 0.1f;
    Vector3 p = z->pos;

    Color bodyCol = z->stunTimer > 0 ? (Color){180,180,180,255} : (Color){90,110,70,255};
    Color headCol = {120,130,90,255};

    // Body
    DrawCube((Vector3){p.x + sway, 0.6f, p.z}, 0.5f, 1.0f, 0.35f, bodyCol);
    // Head
    DrawSphere((Vector3){p.x + cs*0.2f + sway, 1.3f, p.z + sn*0.2f}, 0.2f, headCol);
    // Eyes
    float ey = 1.35f;
    DrawSphere((Vector3){p.x + cs*0.35f - sn*0.08f, ey, p.z + sn*0.35f + cs*0.08f}, 0.04f, RED);
    DrawSphere((Vector3){p.x + cs*0.35f + sn*0.08f, ey, p.z + sn*0.35f - cs*0.08f}, 0.04f, RED);
    // Arms reaching
    DrawCylinderEx(
        (Vector3){p.x - sn*0.25f, 0.8f, p.z + cs*0.25f},
        (Vector3){p.x + cs*0.7f - sn*0.25f + sway, 0.7f, p.z + sn*0.7f + cs*0.25f},
        0.06f, 0.05f, 4, bodyCol);
    DrawCylinderEx(
        (Vector3){p.x + sn*0.25f, 0.8f, p.z - cs*0.25f},
        (Vector3){p.x + cs*0.7f + sn*0.25f - sway, 0.7f, p.z + sn*0.7f - cs*0.25f},
        0.06f, 0.05f, 4, bodyCol);

    // Blood splatters
    int splats = ZOMBIE_HP - z->health;
    for (int s = 0; s < splats && s < 4; s++) {
        float sy = 0.3f + s * 0.2f;
        DrawSphere((Vector3){p.x + sway * (s+1) * 0.5f, sy, p.z}, 0.05f, (Color){150,20,20,200});
    }
}

void DrawPickup3D(PickupItem *item) {
    if (!item->active) return;
    float bob = sinf(item->bobTimer * 3.0f) * 0.15f;
    float spin = item->bobTimer * 90.0f;
    Vector3 p = {item->pos.x, item->pos.y + bob, item->pos.z};

    // Glow ring on floor
    DrawCircle3D((Vector3){p.x, 0.02f, p.z}, 0.5f, (Vector3){1,0,0}, 90,
        (Color){255, 200, 80, (unsigned char)(140 + sinf(item->bobTimer*4)*60)});

    // Item cube
    Color ic = GOLD;
    switch (item->type) {
        case IT_HANDGUN_AMMO: ic = (Color){180,160,60,255}; break;
        case IT_HEALTH_SPRAY: ic = GREEN; break;
        case IT_KEY_SWORD: case IT_KEY_ARMOR: case IT_KEY_SHIELD: ic = GOLD; break;
        case IT_HANDGUN: ic = DARKGRAY; break;
        case IT_FILE: ic = WHITE; break;
        default: break;
    }
    DrawCube(p, 0.3f, 0.3f, 0.3f, ic);
    DrawCubeWires(p, 0.3f, 0.3f, 0.3f, (Color){ic.r/2, ic.g/2, ic.b/2, 255});
}

void DrawHUD(void) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    // Health condition
    DrawRectangle(10, sh - 50, 130, 40, (Color){0,0,0,180});
    float hpPct = (float)player.health / player.maxHealth;
    const char *cond; Color cc;
    if (hpPct > 0.6f)      { cond = "FINE";    cc = GREEN; }
    else if (hpPct > 0.3f) { cond = "CAUTION"; cc = YELLOW; }
    else if (hpPct > 0)    { cond = "DANGER";  cc = RED; }
    else                   { cond = "DEAD";    cc = DARKGRAY; }
    DrawText(cond, 20, sh - 44, 18, cc);
    DrawRectangle(20, sh - 22, (int)(110 * hpPct), 10, cc);
    DrawRectangleLines(20, sh - 22, 110, 10, (Color){80,80,80,255});

    // Ammo
    if (player.hasGun) {
        DrawRectangle(sw - 100, sh - 45, 90, 35, (Color){0,0,0,180});
        DrawText(TextFormat("AMMO %d", player.ammo), sw - 93, sh - 38, 18, WHITE);
    }
}

void DrawInventoryScreen(void) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, (Color){0,0,0,200});
    DrawText("INVENTORY", sw/2 - 65, 30, 26, WHITE);

    int startX = sw/2 - 190, startY = 80;
    for (int i = 0; i < MAX_INV; i++) {
        int col = i % 4, row = i / 4;
        int x = startX + col * 100, y = startY + row * 75;
        Color bg = (i == invCursor) ? (Color){80,60,40,255} : (Color){40,35,30,255};
        DrawRectangle(x, y, 90, 65, bg);
        DrawRectangleLinesEx((Rectangle){(float)x,(float)y,90,65}, 1,
            (i == invCursor) ? GOLD : (Color){80,70,60,255});
        if (i < player.invCount)
            DrawText(itemNames[player.inventory[i]], x + 5, y + 5, 10, WHITE);
    }
    if (invCursor < player.invCount) {
        ItemType sel = player.inventory[invCursor];
        if (sel == IT_HANDGUN_AMMO) DrawText("[ENTER] Reload", sw/2-55, sh-60, 16, GOLD);
        else if (sel == IT_HEALTH_SPRAY) DrawText("[ENTER] Use", sw/2-40, sh-60, 16, GOLD);
    }
    DrawText("[TAB] Close   [ENTER] Use", sw/2-105, sh-30, 14, (Color){150,150,150,255});
}

int main(void) {
    InitWindow(800, 600, "Resident Evil - 3D Mansion");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    BuildMansion();

    Camera3D camera = { 0 };
    camera.up = (Vector3){0,1,0};
    camera.projection = CAMERA_PERSPECTIVE;
    camera.fovy = 50;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        if (showMessage) { messageTimer -= dt; if (messageTimer <= 0) showMessage = false; }

        // Door transition
        if (doorTransition) {
            doorTransTimer += dt;
            if (doorTransTimer >= 1.5f) {
                doorTransition = false;
                player.currentRoom = doorTargetRoom;
                player.pos = doorTargetPos;
            }
            goto draw;
        }

        if (player.dead) { player.deathTimer += dt; goto draw; }

        // Inventory
        if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_I)) { showInventory = !showInventory; invCursor = 0; }
        if (showInventory) {
            if (IsKeyPressed(KEY_RIGHT)) invCursor = (invCursor+1) % MAX_INV;
            if (IsKeyPressed(KEY_LEFT))  invCursor = (invCursor-1+MAX_INV) % MAX_INV;
            if (IsKeyPressed(KEY_DOWN))  invCursor = (invCursor+4) % MAX_INV;
            if (IsKeyPressed(KEY_UP))    invCursor = (invCursor-4+MAX_INV) % MAX_INV;
            if (IsKeyPressed(KEY_ENTER) && invCursor < player.invCount) {
                ItemType u = player.inventory[invCursor];
                if (u == IT_HANDGUN_AMMO && player.ammo < player.maxAmmo) {
                    player.ammo += 15; if (player.ammo > player.maxAmmo) player.ammo = player.maxAmmo;
                    RemoveItem(IT_HANDGUN_AMMO); ShowMsg("Reloaded.", 1.5f);
                } else if (u == IT_HEALTH_SPRAY) {
                    player.health = player.maxHealth; RemoveItem(IT_HEALTH_SPRAY); ShowMsg("Healed.", 1.5f);
                } else if (u == IT_FILE) { ShowMsg("'The keys are scattered throughout the mansion...'", 3); }
                else if (u == IT_HANDGUN) { player.hasGun = true; ShowMsg("Equipped Handgun.", 1.5f); }
            }
            goto draw;
        }

        // --- Player ---
        player.shootTimer -= dt;
        if (player.iframeTimer > 0) player.iframeTimer -= dt;

        if (player.knockbackTimer > 0) {
            player.knockbackTimer -= dt;
            Vector3 kb = Vector3Scale(player.knockbackDir, KNOCKBACK_SPEED * dt);
            Vector3 np = Vector3Add(player.pos, kb);
            np.y = 0;
            if (!Collides(np, 0.3f, player.currentRoom)) player.pos = np;
            goto skip_input;
        }

        player.aiming = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_LEFT_CONTROL);
        player.running = IsKeyDown(KEY_LEFT_SHIFT) && !player.aiming;

        // Tank turn
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  player.rotation -= TURN_SPEED * dt;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) player.rotation += TURN_SPEED * dt;

        // Tank move
        if (!player.aiming) {
            float spd = player.running ? PLAYER_RUN : PLAYER_SPEED;
            float moveDir = 0;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) moveDir = 1;
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) moveDir = -1;
            if (moveDir != 0) {
                float cs = cosf(player.rotation), sn = sinf(player.rotation);
                Vector3 np = {player.pos.x + cs*spd*moveDir*dt, 0, player.pos.z + sn*spd*moveDir*dt};
                if (!Collides(np, 0.3f, player.currentRoom)) player.pos = np;
                player.walkTimer += dt;
            }
        }

        // Shoot
        if (player.aiming && player.hasGun && player.shootTimer <= 0 &&
            (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_ENTER))) {
            if (player.ammo > 0) {
                player.ammo--; player.shootTimer = SHOOT_COOLDOWN;
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!bullets[i].active) {
                        float cs = cosf(player.rotation), sn = sinf(player.rotation);
                        bullets[i].pos = (Vector3){player.pos.x+cs*0.5f, 0.85f, player.pos.z+sn*0.5f};
                        bullets[i].vel = (Vector3){cs*BULLET_SPEED, 0, sn*BULLET_SPEED};
                        bullets[i].active = true; bullets[i].life = 0.8f;
                        break;
                    }
                }
            } else ShowMsg("No ammo!", 1);
        }

        // Knife
        if (player.aiming && IsKeyPressed(KEY_ENTER) && (!player.hasGun || player.ammo <= 0)) {
            float cs = cosf(player.rotation), sn = sinf(player.rotation);
            for (int i = 0; i < numZombies; i++) {
                if (!zombies[i].active || zombies[i].room != player.currentRoom) continue;
                Vector3 toZ = Vector3Subtract(zombies[i].pos, player.pos);
                toZ.y = 0;
                float dist = Vector3Length(toZ);
                if (dist < KNIFE_RANGE) {
                    float dot = (toZ.x*cs + toZ.z*sn) / dist;
                    if (dot > cosf(KNIFE_ARC)) {
                        zombies[i].health--; zombies[i].stunTimer = 0.3f;
                        if (zombies[i].health <= 0) zombies[i].active = false;
                    }
                }
            }
        }

skip_input:
        // Bullets
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].active) continue;
            bullets[i].pos = Vector3Add(bullets[i].pos, Vector3Scale(bullets[i].vel, dt));
            bullets[i].life -= dt;
            if (bullets[i].life <= 0) { bullets[i].active = false; continue; }
            for (int z = 0; z < numZombies; z++) {
                if (!zombies[z].active || zombies[z].room != player.currentRoom) continue;
                Vector3 diff = Vector3Subtract(bullets[i].pos, zombies[z].pos);
                diff.y = 0;
                if (Vector3Length(diff) < 0.6f) {
                    zombies[z].health -= 2; zombies[z].stunTimer = 0.5f;
                    Vector3 kb = Vector3Normalize(bullets[i].vel);
                    zombies[z].pos = Vector3Add(zombies[z].pos, Vector3Scale(kb, 0.3f));
                    bullets[i].active = false;
                    if (zombies[z].health <= 0) zombies[z].active = false;
                    break;
                }
            }
            if (Collides(bullets[i].pos, 0.05f, player.currentRoom)) bullets[i].active = false;
        }

        // Zombie AI
        for (int i = 0; i < numZombies; i++) {
            Zombie *z = &zombies[i];
            if (!z->active || z->room != player.currentRoom) continue;
            z->animTimer += dt;
            if (z->stunTimer > 0) { z->stunTimer -= dt; continue; }

            Vector3 toP = Vector3Subtract(player.pos, z->pos); toP.y = 0;
            float dist = Vector3Length(toP);
            if (dist < ZOMBIE_CHASE) z->alerted = true;

            if (z->alerted && dist > 0.1f) {
                Vector3 dir = Vector3Normalize(toP);
                z->rotation = atan2f(dir.z, dir.x);
                Vector3 np = Vector3Add(z->pos, Vector3Scale(dir, ZOMBIE_SPEED * dt));
                np.y = 0;
                if (!Collides(np, 0.3f, z->room)) z->pos = np;

                if (dist < ZOMBIE_ATTACK) {
                    z->attackTimer -= dt;
                    if (z->attackTimer <= 0 && player.iframeTimer <= 0) {
                        player.health--;
                        player.iframeTimer = IFRAME_TIME;
                        player.knockbackDir = Vector3Normalize(Vector3Subtract(player.pos, z->pos));
                        player.knockbackDir.y = 0;
                        player.knockbackTimer = KNOCKBACK_TIME;
                        z->attackTimer = 1.0f;
                        if (player.health <= 0) { player.health = 0; player.dead = true; player.deathTimer = 0; }
                    }
                }
            } else if (!z->alerted) {
                z->groanTimer -= dt;
                if (z->groanTimer <= 0) {
                    z->rotation += (float)GetRandomValue(-100,100)/100.0f;
                    z->groanTimer = (float)GetRandomValue(200,500)/100.0f;
                }
                float zcs = cosf(z->rotation), zsn = sinf(z->rotation);
                Vector3 np = {z->pos.x + zcs*ZOMBIE_SPEED*0.3f*dt, 0, z->pos.z + zsn*ZOMBIE_SPEED*0.3f*dt};
                if (!Collides(np, 0.3f, z->room)) z->pos = np;
            }
        }

        // Item pickup
        for (int i = 0; i < numPickups; i++) {
            PickupItem *pi = &pickups[i];
            if (!pi->active || pi->room != player.currentRoom) continue;
            pi->bobTimer += dt;
            Vector3 diff = Vector3Subtract(player.pos, pi->pos); diff.y = 0;
            if (Vector3Length(diff) < 1.0f && IsKeyPressed(KEY_E)) {
                if (pi->type == IT_HANDGUN) {
                    player.hasGun = true; pi->active = false;
                    AddItem(IT_HANDGUN); ShowMsg("Acquired: Handgun", 2);
                } else if (AddItem(pi->type)) {
                    pi->active = false;
                    ShowMsg(TextFormat("Acquired: %s", itemNames[pi->type]), 2);
                }
            }
        }

        // Door interaction
        for (int i = 0; i < numDoors; i++) {
            if (doors[i].fromRoom != player.currentRoom) continue;
            Vector3 diff = Vector3Subtract(player.pos, doors[i].pos); diff.y = 0;
            if (Vector3Length(diff) < 1.2f && IsKeyPressed(KEY_E)) {
                if (doors[i].locked) {
                    ItemType key = IT_NONE;
                    if (doors[i].lock == LOCK_SWORD) key = IT_KEY_SWORD;
                    if (doors[i].lock == LOCK_ARMOR) key = IT_KEY_ARMOR;
                    if (doors[i].lock == LOCK_SHIELD) key = IT_KEY_SHIELD;
                    if (HasItem(key)) {
                        RemoveItem(key); doors[i].locked = false;
                        for (int j = 0; j < numDoors; j++)
                            if (j != i && doors[j].fromRoom == doors[i].toRoom && doors[j].toRoom == doors[i].fromRoom)
                                doors[j].locked = false;
                        ShowMsg("Used key. Door unlocked.", 2);
                    } else ShowMsg("It's locked. I need a special key.", 2);
                } else {
                    doorTransition = true; doorTransTimer = 0;
                    doorTargetRoom = doors[i].toRoom;
                    doorTargetPos = doors[i].targetPos;
                }
            }
        }

draw:
        // Camera: lerp to room's fixed camera
        {
            Room *r = &rooms[player.currentRoom];
            camera.position = Vector3Lerp(camera.position, r->camPos, CAM_LERP * dt);
            camera.target = Vector3Lerp(camera.target, r->camTarget, CAM_LERP * dt);
            camera.fovy += (r->camFov - camera.fovy) * CAM_LERP * dt;
        }

        BeginDrawing();
        ClearBackground(BLACK);

        if (doorTransition) {
            float t = doorTransTimer / 1.5f;
            if (t < 0.3f) {
                DrawRectangle(0, 0, sw, sh, (Color){0,0,0,(unsigned char)(t/0.3f*255)});
            } else if (t < 0.7f) {
                float open = (t - 0.3f) / 0.4f;
                int dw = (int)(sw * 0.25f * open);
                DrawRectangle(sw/2-dw, 0, dw*2, sh, (Color){25,18,12,255});
                DrawRectangleLinesEx((Rectangle){(float)(sw/2-dw),0,(float)(dw*2),(float)sh}, 3, (Color){60,45,30,255});
            } else {
                BeginMode3D(camera);
                    DrawRoom3D(doorTargetRoom, camera.position);
                EndMode3D();
                float fade = (t - 0.7f) / 0.3f;
                DrawRectangle(0, 0, sw, sh, (Color){0,0,0,(unsigned char)((1-fade)*255)});
            }
            EndDrawing();
            continue;
        }

        BeginMode3D(camera);
            DrawRoom3D(player.currentRoom, camera.position);

            for (int i = 0; i < numPickups; i++)
                if (pickups[i].room == player.currentRoom) DrawPickup3D(&pickups[i]);

            for (int i = 0; i < numZombies; i++)
                if (zombies[i].room == player.currentRoom) DrawZombie3D(&zombies[i]);

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) continue;
                DrawSphere(bullets[i].pos, 0.06f, YELLOW);
                Vector3 trail = Vector3Subtract(bullets[i].pos, Vector3Scale(bullets[i].vel, dt*2));
                DrawLine3D(trail, bullets[i].pos, (Color){255,200,100,180});
            }

            if (!player.dead) DrawPlayer3D(&player);

            // Aim crosshair in 3D
            if (player.aiming && player.hasGun) {
                float cs = cosf(player.rotation), sn = sinf(player.rotation);
                Vector3 cross = {player.pos.x+cs*2.0f, 0.85f, player.pos.z+sn*2.0f};
                DrawSphere(cross, 0.05f, RED);
            }
        EndMode3D();

        // Darkness vignette for dark rooms
        if (rooms[player.currentRoom].isDark) {
            DrawRectangle(0, 0, sw, sh, (Color){0,0,0,140});
            // Central light spot
            DrawCircleGradient(sw/2, sh/2, (float)(sw < sh ? sw : sh) * 0.35f,
                (Color){0,0,0,0}, (Color){0,0,0,0});
        }

        DrawHUD();

        // Pickup prompt
        for (int i = 0; i < numPickups; i++) {
            if (!pickups[i].active || pickups[i].room != player.currentRoom) continue;
            Vector3 d = Vector3Subtract(player.pos, pickups[i].pos); d.y = 0;
            if (Vector3Length(d) < 1.0f) { DrawText("[E] Take", sw/2-28, sh-80, 18, GOLD); break; }
        }
        for (int i = 0; i < numDoors; i++) {
            if (doors[i].fromRoom != player.currentRoom) continue;
            Vector3 d = Vector3Subtract(player.pos, doors[i].pos); d.y = 0;
            if (Vector3Length(d) < 1.2f) { DrawText("[E] Door", sw/2-28, sh-80, 18, GOLD); break; }
        }

        if (showMessage) {
            int mw = MeasureText(messageText, 16);
            DrawRectangle(sw/2-mw/2-10, 20, mw+20, 30, (Color){0,0,0,200});
            DrawText(messageText, sw/2-mw/2, 26, 16, WHITE);
        }

        if (showInventory) DrawInventoryScreen();

        if (player.dead) {
            float fade = Clamp(player.deathTimer / 2, 0, 1);
            DrawRectangle(0, 0, sw, sh, (Color){100,0,0,(unsigned char)(fade*200)});
            if (player.deathTimer > 1.5f) {
                int dw = MeasureText("YOU DIED", 60);
                DrawText("YOU DIED", sw/2-dw/2, sh/2-30, 60, (Color){200,0,0,255});
                DrawText("Press R to restart", sw/2-80, sh/2+40, 18, (Color){180,180,180,255});
            }
        }
        if (player.dead && IsKeyPressed(KEY_R)) {
            numZombies=0; numPickups=0; numDoors=0; numRooms=0;
            memset(bullets, 0, sizeof(bullets));
            BuildMansion();
        }

        DrawText("WASD: Tank Move  Space: Aim  E: Interact  TAB: Inventory", 10, 10, 11, (Color){120,120,120,180});
        DrawFPS(sw-80, sh-20);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
