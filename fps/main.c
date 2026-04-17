#include "raylib.h"
#include "raymath.h"
#include "../common/objects3d.h"
#include "../common/util/math.h"
#include "../common/util/input.h"
#include "../common/util/pool.h"
#include "../common/util/camera.h"
#include "../common/util/collide.h"
#include "../common/util/hud.h"
#include <math.h>
#include <stdlib.h>

// FPS: arena shooter with waves, enemy AI, weapons, player health

#define MAX_BULLETS    128
#define MAX_ENEMIES     24
#define MAX_PARTICLES   64
#define MAX_PICKUPS      8
#define MAP_SIZE       60.0f
#define WALL_HEIGHT     4.0f

// Player
typedef struct {
    Vector3 pos;
    float yaw, pitch;
    float velY;
    bool grounded;
    float hp, maxHp;
    float armor;
    int ammo, maxAmmo;
    float fireTimer;
    float reloadTimer;
    bool reloading;
    int kills;
    int weapon;  // 0=pistol, 1=shotgun, 2=rifle
    float damageFlash;
} Player;

// Enemy types
typedef enum { EN_GRUNT, EN_FAST, EN_TANK } EnemyType;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float hp, maxHp;
    float speed;
    float attackTimer;
    float attackRange;
    float damage;
    EnemyType type;
    bool active;
    float hitFlash;
} Enemy;

typedef struct {
    Vector3 pos, vel;
    float life;
    float damage;
    bool active;
} Bullet;

typedef enum { PICKUP_HEALTH, PICKUP_AMMO, PICKUP_ARMOR } PickupType;

typedef struct {
    Vector3 pos;
    PickupType type;
    bool active;
    float respawnTimer;
} Pickup;

// Weapon stats
typedef struct {
    const char *name;
    int maxAmmo;
    float fireRate;
    float reloadTime;
    float damage;
    float spread;
    int pellets;     // >1 for shotgun
    float bulletSpeed;
} WeaponDef;

static WeaponDef weapons[] = {
    {"Pistol",   12, 0.25f, 1.2f, 15, 0.02f, 1, 40},
    {"Shotgun",   6, 0.6f,  1.8f, 8,  0.08f, 6, 35},
    {"Rifle",    30, 0.08f, 2.0f, 10, 0.01f, 1, 50},
};

// Enemy models
// Grunt — red soldier with helmet, rifle, boots
static Part gruntParts[] = {
    // Boots
    CUBE(-0.15f, 0.08f, 0, 0.18f, 0.16f, 0.25f, COL(40,35,30,255)),
    CUBE( 0.15f, 0.08f, 0, 0.18f, 0.16f, 0.25f, COL(40,35,30,255)),
    // Legs
    CUBE(-0.12f, 0.35f, 0, 0.14f, 0.4f, 0.14f, COL(60,50,45,255)),
    CUBE( 0.12f, 0.35f, 0, 0.14f, 0.4f, 0.14f, COL(60,50,45,255)),
    // Belt
    CUBE(0, 0.55f, 0, 0.5f, 0.08f, 0.35f, COL(50,40,30,255)),
    // Torso
    CUBE(0, 0.8f, 0, 0.5f, 0.5f, 0.3f, COL(160,50,50,255)),
    // Shoulder pads
    CUBE(-0.3f, 1.0f, 0, 0.14f, 0.1f, 0.2f, COL(140,40,40,255)),
    CUBE( 0.3f, 1.0f, 0, 0.14f, 0.1f, 0.2f, COL(140,40,40,255)),
    // Arms
    CUBE(-0.35f, 0.7f, 0, 0.1f, 0.4f, 0.1f, COL(180,155,130,255)),
    CUBE( 0.35f, 0.7f, 0, 0.1f, 0.4f, 0.1f, COL(180,155,130,255)),
    // Hands
    SPHERE(-0.35f, 0.48f, 0.08f, 0.06f, COL(180,155,130,255)),
    SPHERE( 0.35f, 0.48f, 0.08f, 0.06f, COL(180,155,130,255)),
    // Neck
    CYL(0, 1.05f, 0, 0.08f, 0.08f, COL(180,155,130,255)),
    // Head
    SPHERE(0, 1.25f, 0, 0.18f, COL(180,155,130,255)),
    // Helmet
    SPHERE(0, 1.32f, 0, 0.2f, COL(100,40,35,255)),
    // Eyes
    SPHERE(-0.06f, 1.22f, 0.16f, 0.03f, COL(220,40,40,255)),
    SPHERE( 0.06f, 1.22f, 0.16f, 0.03f, COL(220,40,40,255)),
    // Rifle
    CUBE(0.25f, 0.6f, 0.15f, 0.04f, 0.04f, 0.5f, COL(50,50,55,255)),
};

// Fast — lean green mutant, hunched, claws
static Part fastParts[] = {
    // Feet
    CUBE(-0.12f, 0.05f, 0.05f, 0.12f, 0.1f, 0.2f, COL(30,80,30,255)),
    CUBE( 0.12f, 0.05f, 0.05f, 0.12f, 0.1f, 0.2f, COL(30,80,30,255)),
    // Legs (thin, bent)
    CYL(-0.1f, 0.1f, 0, 0.05f, 0.35f, COL(40,130,40,255)),
    CYL( 0.1f, 0.1f, 0, 0.05f, 0.35f, COL(40,130,40,255)),
    // Torso (hunched forward)
    CUBE(0, 0.6f, 0.05f, 0.35f, 0.35f, 0.25f, COL(50,160,50,255)),
    // Spine ridges
    SPHERE(0, 0.8f, -0.1f, 0.06f, COL(60,180,60,255)),
    SPHERE(0, 0.7f, -0.12f, 0.05f, COL(60,180,60,255)),
    // Arms (long, dangling)
    CYL(-0.25f, 0.5f, 0.05f, 0.04f, 0.4f, COL(45,140,45,255)),
    CYL( 0.25f, 0.5f, 0.05f, 0.04f, 0.4f, COL(45,140,45,255)),
    // Claws
    CONE(-0.25f, 0.08f, 0.12f, 0.04f, 0.12f, 0.0f, COL(200,200,180,255)),
    CONE( 0.25f, 0.08f, 0.12f, 0.04f, 0.12f, 0.0f, COL(200,200,180,255)),
    // Head (angular)
    SPHERE(0, 0.9f, 0.1f, 0.15f, COL(50,160,50,255)),
    // Eyes (glowing yellow)
    SPHERE(-0.05f, 0.92f, 0.22f, 0.04f, COL(255,255,50,255)),
    SPHERE( 0.05f, 0.92f, 0.22f, 0.04f, COL(255,255,50,255)),
    // Jaw
    CUBE(0, 0.82f, 0.18f, 0.1f, 0.04f, 0.08f, COL(40,120,40,255)),
};

// Tank — heavy armored brute, massive, shoulder plates
static Part tankParts[] = {
    // Boots (heavy)
    CUBE(-0.2f, 0.1f, 0, 0.22f, 0.2f, 0.3f, COL(50,50,60,255)),
    CUBE( 0.2f, 0.1f, 0, 0.22f, 0.2f, 0.3f, COL(50,50,60,255)),
    // Legs (thick)
    CUBE(-0.18f, 0.4f, 0, 0.2f, 0.4f, 0.2f, COL(70,70,85,255)),
    CUBE( 0.18f, 0.4f, 0, 0.2f, 0.4f, 0.2f, COL(70,70,85,255)),
    // Waist plate
    CUBE(0, 0.6f, 0, 0.65f, 0.1f, 0.45f, COL(60,60,75,255)),
    // Torso (massive)
    CUBE(0, 0.95f, 0, 0.7f, 0.6f, 0.45f, COL(80,80,100,255)),
    // Chest plate
    CUBE(0, 0.9f, 0.2f, 0.5f, 0.4f, 0.06f, COL(100,100,120,255)),
    // Shoulder armor
    CUBE(-0.45f, 1.15f, 0, 0.2f, 0.15f, 0.25f, COL(90,90,110,255)),
    CUBE( 0.45f, 1.15f, 0, 0.2f, 0.15f, 0.25f, COL(90,90,110,255)),
    // Arms
    CUBE(-0.5f, 0.8f, 0, 0.14f, 0.45f, 0.14f, COL(80,80,95,255)),
    CUBE( 0.5f, 0.8f, 0, 0.14f, 0.45f, 0.14f, COL(80,80,95,255)),
    // Fists
    SPHERE(-0.5f, 0.55f, 0, 0.1f, COL(70,70,80,255)),
    SPHERE( 0.5f, 0.55f, 0, 0.1f, COL(70,70,80,255)),
    // Neck
    CYL(0, 1.25f, 0, 0.12f, 0.1f, COL(80,80,95,255)),
    // Head (small for body, armored)
    SPHERE(0, 1.45f, 0, 0.2f, COL(90,90,110,255)),
    // Visor
    CUBE(0, 1.42f, 0.18f, 0.22f, 0.06f, 0.04f, COL(200,50,50,255)),
    // Back exhaust
    CYL(-0.15f, 1.0f, -0.22f, 0.06f, 0.2f, COL(50,50,55,255)),
    CYL( 0.15f, 1.0f, -0.22f, 0.06f, 0.2f, COL(50,50,55,255)),
};

// Wall segments for the arena
typedef struct { Vector3 pos; float w, h, d; Color color; } Wall;

#define MAX_WALLS 20
static Wall walls[MAX_WALLS];
static int numWalls = 0;

// State
static Player player;
static Enemy enemies[MAX_ENEMIES];
static Bullet bullets[MAX_BULLETS];
static Particle3D particles[MAX_PARTICLES];
static Pickup pickups[MAX_PICKUPS];
static int wave = 0;
static int enemiesAlive = 0;
static float waveTimer = 0;
static float gameTime = 0;
static bool gameOver = false;
static ScreenShake shake;

// Bullet decals on walls
#define MAX_DECALS 64
typedef struct { Vector3 pos; Vector3 normal; float life; } Decal;
static Decal decals[MAX_DECALS];
static int decalIdx = 0;

void SpawnDecal(Vector3 pos, Vector3 normal) {
    decals[decalIdx % MAX_DECALS] = (Decal){pos, normal, 10.0f};
    decalIdx++;
}

// Props: barrels (destructible) and crates (solid cover)
#define MAX_PROPS 32
typedef enum { PROP_BARREL, PROP_CRATE } PropType;
typedef struct {
    Vector3 pos;
    PropType type;
    float hp;
    bool active;
} Prop;
static Prop props[MAX_PROPS];
static int numProps = 0;

// Barrel model
static Part barrelParts[] = {
    CYL(0, 0, 0, 0.45f, 1.2f, COL(120,70,35,255)),          // body
    CYL(0, 0, 0, 0.48f, 0.08f, COL(80,80,85,255)),           // bottom ring
    CYL(0, 0.5f, 0, 0.48f, 0.08f, COL(80,80,85,255)),        // middle ring
    CYL(0, 1.1f, 0, 0.48f, 0.08f, COL(80,80,85,255)),        // top ring
    CYL(0, 1.2f, 0, 0.43f, 0.04f, COL(100,60,30,255)),       // lid
};
// Crate model
static Part crateParts[] = {
    CUBE(0, 0.5f, 0, 1.0f, 1.0f, 1.0f, COL(160,130,80,255)), // body
    CUBE(0, 0.5f, 0, 1.04f, 0.08f, 1.04f, COL(120,90,50,255)), // bottom plank
    CUBE(0, 1.0f, 0, 1.04f, 0.08f, 1.04f, COL(120,90,50,255)), // top plank
    CUBE(-0.5f, 0.5f, 0, 0.08f, 1.0f, 1.04f, COL(130,100,55,255)), // left plank
    CUBE(0.5f, 0.5f, 0, 0.08f, 1.0f, 1.04f, COL(130,100,55,255)),  // right plank
    CUBE(0, 0.5f, 0, 0.08f, 0.08f, 1.0f, COL(110,80,40,255)), // cross brace
};

void SpawnProp(PropType type, Vector3 pos) {
    if (numProps >= MAX_PROPS) return;
    float hp = (type == PROP_BARREL) ? 15 : 50;
    props[numProps++] = (Prop){pos, type, hp, true};
}

bool PropCollision(Vector3 pos, float radius) {
    for (int i = 0; i < numProps; i++) {
        if (!props[i].active) continue;
        float r = (props[i].type == PROP_BARREL) ? 0.5f : 0.6f;
        Vector3 diff = Vector3Subtract(pos, props[i].pos);
        diff.y = 0;
        if (Vector3Length(diff) < r + radius) return true;
    }
    return false;
}

void BuildArena(void) {
    numProps = 0;
    numWalls = 0;
    Color wc = {120, 110, 100, 255};
    Color wc2 = {100, 90, 80, 255};
    float hs = MAP_SIZE / 2;
    // Outer walls
    walls[numWalls++] = (Wall){{0, WALL_HEIGHT/2, -hs}, MAP_SIZE, WALL_HEIGHT, 0.5f, wc};
    walls[numWalls++] = (Wall){{0, WALL_HEIGHT/2, hs}, MAP_SIZE, WALL_HEIGHT, 0.5f, wc};
    walls[numWalls++] = (Wall){{-hs, WALL_HEIGHT/2, 0}, 0.5f, WALL_HEIGHT, MAP_SIZE, wc};
    walls[numWalls++] = (Wall){{hs, WALL_HEIGHT/2, 0}, 0.5f, WALL_HEIGHT, MAP_SIZE, wc};
    // Interior cover
    walls[numWalls++] = (Wall){{-10, WALL_HEIGHT/2, -10}, 6, WALL_HEIGHT, 0.5f, wc2};
    walls[numWalls++] = (Wall){{10, WALL_HEIGHT/2, -10}, 0.5f, WALL_HEIGHT, 6, wc2};
    walls[numWalls++] = (Wall){{-10, WALL_HEIGHT/2, 10}, 0.5f, WALL_HEIGHT, 6, wc2};
    walls[numWalls++] = (Wall){{10, WALL_HEIGHT/2, 10}, 6, WALL_HEIGHT, 0.5f, wc2};
    walls[numWalls++] = (Wall){{0, 1.0f, 0}, 3, 2.0f, 3, wc2}; // center pillar
    walls[numWalls++] = (Wall){{-20, WALL_HEIGHT/2, 0}, 4, WALL_HEIGHT, 0.5f, wc2};
    walls[numWalls++] = (Wall){{20, WALL_HEIGHT/2, 0}, 4, WALL_HEIGHT, 0.5f, wc2};
    walls[numWalls++] = (Wall){{0, WALL_HEIGHT/2, -20}, 0.5f, WALL_HEIGHT, 4, wc2};
    walls[numWalls++] = (Wall){{0, WALL_HEIGHT/2, 20}, 0.5f, WALL_HEIGHT, 4, wc2};

    // Scatter barrels and crates for cover and loot
    SpawnProp(PROP_BARREL, (Vector3){-15, 0, -5});
    SpawnProp(PROP_BARREL, (Vector3){-14, 0, -5.5f});
    SpawnProp(PROP_BARREL, (Vector3){15, 0, 5});
    SpawnProp(PROP_BARREL, (Vector3){8, 0, -18});
    SpawnProp(PROP_BARREL, (Vector3){-8, 0, 18});
    SpawnProp(PROP_BARREL, (Vector3){22, 0, -15});
    SpawnProp(PROP_BARREL, (Vector3){-22, 0, 15});
    SpawnProp(PROP_BARREL, (Vector3){5, 0, -12});
    SpawnProp(PROP_CRATE, (Vector3){-6, 0, -15});
    SpawnProp(PROP_CRATE, (Vector3){6, 0, 15});
    SpawnProp(PROP_CRATE, (Vector3){-18, 0, -18});
    SpawnProp(PROP_CRATE, (Vector3){18, 0, 18});
    SpawnProp(PROP_CRATE, (Vector3){-5, 0, 8});
    SpawnProp(PROP_CRATE, (Vector3){12, 0, -8});
    SpawnProp(PROP_CRATE, (Vector3){-12, 0, 3});
    SpawnProp(PROP_CRATE, (Vector3){0, 0, -8});
}

bool WallCollision(Vector3 pos, float radius) {
    for (int i = 0; i < numWalls; i++) {
        Wall *w = &walls[i];
        float hw = w->w/2 + radius, hd = w->d/2 + radius;
        if (fabsf(pos.x - w->pos.x) < hw && fabsf(pos.z - w->pos.z) < hd && pos.y < w->h)
            return true;
    }
    return false;
}

static bool FpsBlocked(Vector3 pos, float radius, void *user) {
    (void)user;
    return WallCollision(pos, radius) || PropCollision(pos, radius);
}

void SpawnPickup(Vector3 pos, PickupType type) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (pickups[i].active) continue;
        pickups[i] = (Pickup){pos, type, true, 0};
        break;
    }
}

void SpawnWave(void) {
    wave++;
    int count = 3 + wave * 2;
    if (count > MAX_ENEMIES) count = MAX_ENEMIES;
    float hs = MAP_SIZE / 2 - 3;
    for (int i = 0; i < count; i++) {
        if (enemies[i].active) continue;
        // Pick type based on wave
        EnemyType t = EN_GRUNT;
        if (wave >= 3 && GetRandomValue(0, 3) == 0) t = EN_FAST;
        if (wave >= 5 && GetRandomValue(0, 5) == 0) t = EN_TANK;
        float hp = (t == EN_TANK) ? 80 : (t == EN_FAST) ? 20 : 40;
        float spd = (t == EN_FAST) ? 8 : (t == EN_TANK) ? 3 : 5;
        float dmg = (t == EN_TANK) ? 20 : 10;
        hp += wave * 5;
        // Spawn at edge of map
        float angle = (float)GetRandomValue(0, 628) / 100.0f;
        enemies[i] = (Enemy){
            .pos = {cosf(angle) * hs, 0, sinf(angle) * hs},
            .hp = hp, .maxHp = hp, .speed = spd,
            .attackTimer = 0, .attackRange = (t == EN_GRUNT) ? 2.5f : 1.8f,
            .damage = dmg, .type = t, .active = true, .hitFlash = 0
        };
        enemiesAlive++;
    }
    // Spawn pickups
    SpawnPickup((Vector3){(float)GetRandomValue(-200,200)/10.0f, 0.5f, (float)GetRandomValue(-200,200)/10.0f}, PICKUP_HEALTH);
    SpawnPickup((Vector3){(float)GetRandomValue(-200,200)/10.0f, 0.5f, (float)GetRandomValue(-200,200)/10.0f}, PICKUP_AMMO);
}

void InitGame(void) {
    player = (Player){
        .pos = {5, 0, 5}, .yaw = 0, .pitch = 0,
        .hp = 100, .maxHp = 100, .armor = 0,
        .ammo = weapons[0].maxAmmo, .maxAmmo = weapons[0].maxAmmo,
        .weapon = 0, .kills = 0
    };
    memset(enemies, 0, sizeof(enemies));
    memset(bullets, 0, sizeof(bullets));
    memset(particles, 0, sizeof(particles));
    memset(pickups, 0, sizeof(pickups));
    wave = 0; enemiesAlive = 0; waveTimer = 3.0f;
    gameOver = false; gameTime = 0;
    shake = (ScreenShake){0};
    BuildArena();
}

void FireWeapon(Player *p, Vector3 forward, Vector3 right) {
    WeaponDef *w = &weapons[p->weapon];
    if (p->ammo <= 0 || p->reloading) return;
    p->ammo--;
    p->fireTimer = w->fireRate;
    ShakeTrigger(&shake, 0.1f + (p->weapon == 1 ? 0.2f : 0));

    for (int pellet = 0; pellet < w->pellets; pellet++) {
        int i = POOL_SPAWN(bullets, MAX_BULLETS);
        if (i >= 0) {
            Vector3 dir = forward;
            dir.x += (float)GetRandomValue(-100, 100) / 100.0f * w->spread;
            dir.y += (float)GetRandomValue(-100, 100) / 100.0f * w->spread;
            dir.z += (float)GetRandomValue(-100, 100) / 100.0f * w->spread;
            dir = Vector3Normalize(dir);
            bullets[i] = (Bullet){
                .pos = Vector3Add(p->pos, (Vector3){0, 1.5f, 0}),
                .vel = Vector3Scale(dir, w->bulletSpeed),
                .life = 2.0f, .damage = w->damage, .active = true
            };
        }
    }
    // Auto-reload
    if (p->ammo <= 0) { p->reloading = true; p->reloadTimer = w->reloadTime; }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "FPS Arena");
    MaximizeWindow();
    SetTargetFPS(60);
    DisableCursor();

    InitGame();

    CamFPS camRig = CamFPSInit((Vector3){0, 1.5f, 0});

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        gameTime += dt;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        if (gameOver) {
            if (IsKeyPressed(KEY_ENTER)) { InitGame(); DisableCursor(); }
        } else {
            // --- Mouse look ---
            InputLookMouse(&player.yaw, &player.pitch, 0.002f, 1.55f);

            // --- Movement ---
            float speed = 10.0f * dt;
            Vector3 move = Vector3Scale(InputMoveDir3Flat(player.yaw), speed);

            // Try move with wall collision
            SlideXZ(&player.pos, move, 0.4f, FpsBlocked, NULL);

            // Clamp to arena
            float hs = MAP_SIZE/2 - 0.5f;
            player.pos.x = Clamp(player.pos.x, -hs, hs);
            player.pos.z = Clamp(player.pos.z, -hs, hs);

            // Jump
            if (IsKeyPressed(KEY_SPACE) && player.grounded) {
                player.velY = 8.0f;
                player.grounded = false;
            }
            GroundSnap(&player.pos, &player.velY, dt, 20.0f, 0.0f, &player.grounded);

            // --- Weapon switch ---
            if (IsKeyPressed(KEY_ONE)) { player.weapon = 0; player.ammo = weapons[0].maxAmmo; player.reloading = false; }
            if (IsKeyPressed(KEY_TWO)) { player.weapon = 1; player.ammo = weapons[1].maxAmmo; player.reloading = false; }
            if (IsKeyPressed(KEY_THREE)) { player.weapon = 2; player.ammo = weapons[2].maxAmmo; player.reloading = false; }

            // Reload
            if (IsKeyPressed(KEY_R) && !player.reloading && player.ammo < weapons[player.weapon].maxAmmo) {
                player.reloading = true;
                player.reloadTimer = weapons[player.weapon].reloadTime;
            }
            if (player.reloading) {
                player.reloadTimer -= dt;
                if (player.reloadTimer <= 0) {
                    player.ammo = weapons[player.weapon].maxAmmo;
                    player.reloading = false;
                }
            }

            // Shoot
            player.fireTimer -= dt;
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && player.fireTimer <= 0 && !player.reloading) {
                // Derive aim vectors from current yaw/pitch
                float cy = cosf(player.yaw), sy = sinf(player.yaw);
                float cp = cosf(player.pitch), sp = sinf(player.pitch);
                Vector3 aimFwd = (Vector3){ sy * cp, sp, -cy * cp };
                Vector3 aimRight = Vector3Normalize(Vector3CrossProduct(aimFwd, (Vector3){0,1,0}));
                FireWeapon(&player, aimFwd, aimRight);
            }

            // --- Update bullets ---
            POOL_FOREACH(bullets, MAX_BULLETS, b) {
                if (!b->active) continue;
                b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
                b->life -= dt;
                if (b->life <= 0 || WallCollision(b->pos, 0.05f)) {
                    SpawnParticleBurst(particles, MAX_PARTICLES, b->pos, 3, 1, 3, 0.1f, 0.3f, 0.02f, 0.08f);
                    if (WallCollision(b->pos, 0.05f)) {
                        Vector3 n = Vector3Normalize(Vector3Negate(b->vel));
                        SpawnDecal(b->pos, n);
                    }
                    b->active = false;
                    continue;
                }
                // Hit enemies
                POOL_FOREACH(enemies, MAX_ENEMIES, e) {
                    if (!e->active) continue;
                    if (Vector3Distance(b->pos, e->pos) < 0.8f) {
                        e->hp -= b->damage;
                        e->hitFlash = 0.1f;
                        SpawnParticleBurst(particles, MAX_PARTICLES, b->pos, 4, 2, 5, 0.2f, 0.5f, 0.03f, 0.1f);
                        b->active = false;
                        if (e->hp <= 0) {
                            e->active = false;
                            enemiesAlive--;
                            player.kills++;
                            SpawnParticleBurst(particles, MAX_PARTICLES, e->pos, 12, 2, 8, 0.3f, 1.0f, 0.05f, 0.2f);
                            ShakeTrigger(&shake, 0.15f);
                        }
                        break;
                    }
                }
                // Hit props
                if (b->active) {
                    for (int p = 0; p < numProps; p++) {
                        if (!props[p].active) continue;
                        float hitR = (props[p].type == PROP_BARREL) ? 0.5f : 0.6f;
                        if (Vector3Distance(b->pos, (Vector3){props[p].pos.x, props[p].pos.y + 0.5f, props[p].pos.z}) < hitR) {
                            props[p].hp -= b->damage;
                            SpawnParticleBurst(particles, MAX_PARTICLES, b->pos, 3, 1, 4, 0.1f, 0.4f, 0.02f, 0.08f);
                            b->active = false;
                            if (props[p].hp <= 0) {
                                props[p].active = false;
                                SpawnParticleBurst(particles, MAX_PARTICLES, props[p].pos, 10, 3, 8, 0.3f, 0.8f, 0.05f, 0.2f);
                                ShakeTrigger(&shake, 0.1f);
                                if (props[p].type == PROP_BARREL) {
                                    PickupType pt = (PickupType)GetRandomValue(0, 2);
                                    SpawnPickup((Vector3){props[p].pos.x, 0.5f, props[p].pos.z}, pt);
                                }
                            }
                            break;
                        }
                    }
                }
            }

            // --- Update enemies ---
            for (int e = 0; e < MAX_ENEMIES; e++) {
                if (!enemies[e].active) continue;
                enemies[e].hitFlash -= dt;
                // Move toward player
                Vector3 toPlayer = Vector3Subtract(player.pos, enemies[e].pos);
                toPlayer.y = 0;
                float dist = Vector3Length(toPlayer);
                if (dist > enemies[e].attackRange) {
                    Vector3 dir = Vector3Normalize(toPlayer);
                    Vector3 newP = Vector3Add(enemies[e].pos, Vector3Scale(dir, enemies[e].speed * dt));
                    if (!WallCollision(newP, 0.5f) && !PropCollision(newP, 0.5f)) enemies[e].pos = newP;
                }
                // Attack
                if (dist < enemies[e].attackRange) {
                    enemies[e].attackTimer -= dt;
                    if (enemies[e].attackTimer <= 0) {
                        enemies[e].attackTimer = 1.0f;
                        float dmg = enemies[e].damage;
                        if (player.armor > 0) { player.armor -= dmg * 0.5f; dmg *= 0.5f; if (player.armor < 0) player.armor = 0; }
                        player.hp -= dmg;
                        player.damageFlash = 0.2f;
                        ShakeTrigger(&shake, 0.2f);
                        if (player.hp <= 0) { player.hp = 0; gameOver = true; EnableCursor(); }
                    }
                }
                // Push apart from other enemies
                for (int j = e + 1; j < MAX_ENEMIES; j++) {
                    if (!enemies[j].active) continue;
                    Vector3 diff = Vector3Subtract(enemies[e].pos, enemies[j].pos);
                    diff.y = 0;
                    float d = Vector3Length(diff);
                    if (d < 1.2f && d > 0.01f) {
                        Vector3 push = Vector3Scale(Vector3Normalize(diff), (1.2f - d) * 0.5f);
                        enemies[e].pos = Vector3Add(enemies[e].pos, push);
                        enemies[j].pos = Vector3Subtract(enemies[j].pos, push);
                    }
                }
            }

            // --- Pickups ---
            for (int i = 0; i < MAX_PICKUPS; i++) {
                if (!pickups[i].active) continue;
                if (Vector3Distance(player.pos, pickups[i].pos) < 1.5f) {
                    switch (pickups[i].type) {
                        case PICKUP_HEALTH: player.hp = fminf(player.hp + 25, player.maxHp); break;
                        case PICKUP_AMMO: player.ammo = weapons[player.weapon].maxAmmo; player.reloading = false; break;
                        case PICKUP_ARMOR: player.armor = fminf(player.armor + 50, 100); break;
                    }
                    pickups[i].active = false;
                }
            }

            // --- Wave spawning ---
            if (enemiesAlive <= 0) {
                waveTimer -= dt;
                if (waveTimer <= 0) { SpawnWave(); waveTimer = 5.0f; }
            }

            player.damageFlash -= dt;
        }

        // Update particles and shake
        UpdateParticles3D(particles, MAX_PARTICLES, dt, 10.0f);
        ShakeUpdate(&shake, dt);
        Vector2 shakeOff = ShakeOffset(&shake);

        // --- Camera ---
        camRig.pos   = (Vector3){ player.pos.x, player.pos.y + 1.5f, player.pos.z };
        camRig.yaw   = player.yaw;
        camRig.pitch = player.pitch;
        Camera3D camera = CamFPSToCamera3D(&camRig);
        camera.position = Vector3Add(camera.position, (Vector3){ shakeOff.x, shakeOff.y, 0 });
        camera.target   = Vector3Add(camera.target,   (Vector3){ shakeOff.x, shakeOff.y, 0 });
        // Derive forward/right vectors for gun rendering
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){40, 50, 60, 255});
        BeginMode3D(camera);

        // Floor
        DrawPlane((Vector3){0,0,0}, (Vector2){MAP_SIZE, MAP_SIZE}, (Color){50, 55, 50, 255});
        // Floor grid
        for (float g = -MAP_SIZE/2; g <= MAP_SIZE/2; g += 5) {
            DrawLine3D((Vector3){g, 0.01f, -MAP_SIZE/2}, (Vector3){g, 0.01f, MAP_SIZE/2}, (Color){45, 50, 45, 255});
            DrawLine3D((Vector3){-MAP_SIZE/2, 0.01f, g}, (Vector3){MAP_SIZE/2, 0.01f, g}, (Color){45, 50, 45, 255});
        }

        // Walls
        for (int i = 0; i < numWalls; i++) {
            Wall *wl = &walls[i];
            // Main wall body
            DrawCube(wl->pos, wl->w, wl->h, wl->d, wl->color);
            // Darker base strip
            Vector3 basePos = {wl->pos.x, 0.1f, wl->pos.z};
            float bw = wl->w > 1 ? wl->w : wl->w + 0.04f;
            float bd = wl->d > 1 ? wl->d : wl->d + 0.04f;
            DrawCube(basePos, bw, 0.2f, bd, (Color){wl->color.r-30, wl->color.g-30, wl->color.b-30, 255});
            // Top cap (slightly lighter)
            Vector3 topPos = {wl->pos.x, wl->h - 0.05f, wl->pos.z};
            DrawCube(topPos, bw, 0.1f, bd, (Color){wl->color.r+15, wl->color.g+15, wl->color.b+15, 255});
            // Horizontal mortar lines
            for (float ly = 0.5f; ly < wl->h; ly += 0.8f) {
                Vector3 linePos = {wl->pos.x, ly, wl->pos.z};
                float lw = wl->w > 1 ? wl->w + 0.01f : wl->w + 0.02f;
                float ld = wl->d > 1 ? wl->d + 0.01f : wl->d + 0.02f;
                DrawCube(linePos, lw, 0.02f, ld, (Color){wl->color.r-15, wl->color.g-15, wl->color.b-15, 200});
            }
            // Edge wireframe
            DrawCubeWires(wl->pos, wl->w, wl->h, wl->d, (Color){wl->color.r-25, wl->color.g-25, wl->color.b-25, 255});
        }

        // Enemies
        POOL_FOREACH(enemies, MAX_ENEMIES, en) {
            if (!en->active) continue;
            Part *ep; int ec;
            switch (en->type) {
                case EN_GRUNT: ep = gruntParts; ec = sizeof(gruntParts)/sizeof(Part); break;
                case EN_FAST:  ep = fastParts;  ec = sizeof(fastParts)/sizeof(Part); break;
                case EN_TANK:  ep = tankParts;  ec = sizeof(tankParts)/sizeof(Part); break;
            }
            float rotY = atan2f(player.pos.x - en->pos.x, player.pos.z - en->pos.z);
            if (en->hitFlash > 0) {
                DrawCube(en->pos, 0.8f, 1.2f, 0.5f, WHITE);
            } else {
                Object3D obj = {ep, ec, en->pos, rotY};
                DrawObject3D(&obj);
            }
            // HP bar
            if (en->hp < en->maxHp) {
                Vector2 sp = GetWorldToScreen((Vector3){en->pos.x, 2.0f, en->pos.z}, camera);
                if (sp.y > 0 && sp.y < sh) {
                    float pct = en->hp / en->maxHp;
                    DrawRectangle(sp.x-15, sp.y, 30, 4, (Color){40,40,40,200});
                    Color hc = pct > 0.5f ? (Color){50,200,80,255} : (Color){220,40,40,255};
                    DrawRectangle(sp.x-15, sp.y, (int)(30*pct), 4, hc);
                }
            }
        }

        // Bullets
        POOL_FOREACH(bullets, MAX_BULLETS, b) {
            if (!b->active) continue;
            DrawSphere(b->pos, 0.06f, YELLOW);
        }

        // Pickups
        for (int i = 0; i < MAX_PICKUPS; i++) {
            if (!pickups[i].active) continue;
            float bob = sinf(gameTime * 3.0f + i) * 0.15f;
            float spin = gameTime * 2.0f + i * 1.5f;
            Vector3 pp = pickups[i].pos;
            pp.y += 0.5f + bob;

            // Ground glow ring
            Color glowCol;
            switch (pickups[i].type) {
                case PICKUP_HEALTH: glowCol = (Color){50,200,50,80}; break;
                case PICKUP_AMMO:   glowCol = (Color){200,200,50,80}; break;
                case PICKUP_ARMOR:  glowCol = (Color){50,100,200,80}; break;
            }
            DrawCircle3D(pickups[i].pos, 0.8f + sinf(gameTime * 2 + i) * 0.1f,
                (Vector3){1,0,0}, 90, glowCol);

            if (pickups[i].type == PICKUP_HEALTH) {
                // Health: green cross
                float cs = cosf(spin), sn = sinf(spin);
                // Vertical bar of cross
                DrawCube(pp, 0.15f, 0.4f, 0.15f, (Color){40,180,40,255});
                // Horizontal bar of cross (rotates)
                Vector3 h1 = {pp.x + cs*0.2f, pp.y, pp.z + sn*0.2f};
                Vector3 h2 = {pp.x - cs*0.2f, pp.y, pp.z - sn*0.2f};
                DrawCylinderEx(h1, h2, 0.08f, 0.08f, 4, (Color){40,180,40,255});
                // White highlight
                DrawCube((Vector3){pp.x, pp.y + 0.05f, pp.z}, 0.1f, 0.35f, 0.1f, (Color){100,230,100,200});
                // Sparkle
                float sparkle = sinf(gameTime * 8 + i * 3) * 0.5f + 0.5f;
                DrawSphere((Vector3){pp.x, pp.y + 0.25f, pp.z}, 0.04f * sparkle, (Color){200,255,200,(unsigned char)(sparkle*200)});
            } else if (pickups[i].type == PICKUP_AMMO) {
                // Ammo: golden bullet shape (spinning)
                float cs = cosf(spin), sn = sinf(spin);
                Vector3 base = {pp.x - sn*0.15f, pp.y - 0.1f, pp.z - cs*0.15f};
                Vector3 tip  = {pp.x + sn*0.15f, pp.y + 0.15f, pp.z + cs*0.15f};
                // Shell casing
                DrawCylinderEx(base, pp, 0.08f, 0.08f, 6, (Color){180,150,40,255});
                // Bullet tip
                DrawCylinderEx(pp, tip, 0.08f, 0.0f, 6, (Color){200,170,50,255});
                // Second bullet behind
                Vector3 base2 = {pp.x + cs*0.12f, pp.y - 0.15f, pp.z - sn*0.12f};
                DrawCylinderEx(base2, (Vector3){base2.x, pp.y, base2.z}, 0.06f, 0.06f, 6, (Color){160,130,30,255});
                DrawCylinderEx((Vector3){base2.x, pp.y, base2.z}, (Vector3){base2.x, pp.y + 0.1f, base2.z}, 0.06f, 0.0f, 6, (Color){180,150,40,255});
            } else {
                // Armor: blue shield shape
                float cs = cosf(spin), sn = sinf(spin);
                // Shield body (flat rotated cube)
                Vector3 shieldFwd = {sn * 0.02f, 0, cs * 0.02f};
                DrawCube(pp, 0.35f, 0.4f, 0.08f, (Color){40,80,200,255});
                DrawCube((Vector3){pp.x, pp.y, pp.z}, 0.28f, 0.33f, 0.09f, (Color){60,110,220,255});
                // Shield emblem (white stripe)
                DrawCube((Vector3){pp.x, pp.y + 0.05f, pp.z}, 0.06f, 0.25f, 0.1f, (Color){180,200,255,220});
                DrawCube((Vector3){pp.x, pp.y + 0.05f, pp.z}, 0.2f, 0.06f, 0.1f, (Color){180,200,255,220});
                // Rim glow
                DrawSphere((Vector3){pp.x, pp.y + 0.22f, pp.z}, 0.04f, (Color){150,200,255,200});
            }
        }

        // Particles
        DrawParticles3D(particles, MAX_PARTICLES);

        // Props (barrels and crates)
        for (int i = 0; i < numProps; i++) {
            if (!props[i].active) continue;
            Part *pp2; int pc2;
            if (props[i].type == PROP_BARREL) {
                pp2 = barrelParts; pc2 = sizeof(barrelParts)/sizeof(Part);
            } else {
                pp2 = crateParts; pc2 = sizeof(crateParts)/sizeof(Part);
            }
            Object3D propObj = {pp2, pc2, props[i].pos, 0};
            DrawObject3D(&propObj);
        }

        // Decals on walls
        for (int i = 0; i < MAX_DECALS; i++) {
            decals[i].life -= dt;
            if (decals[i].life <= 0) continue;
            float alpha = fminf(decals[i].life / 2.0f, 1.0f);
            if (alpha < 0) alpha = 0;
            Vector3 dp = Vector3Add(decals[i].pos, Vector3Scale(decals[i].normal, 0.02f));
            DrawSphere(dp, 0.06f, (Color){30, 30, 30, (unsigned char)(alpha * 200)});
        }

        // Gun model (first person) — oriented along camera axes
        {
            Vector3 fwd = forward;
            Vector3 rgt = Vector3Normalize(Vector3CrossProduct(fwd, (Vector3){0,1,0}));
            Vector3 up2 = Vector3Normalize(Vector3CrossProduct(rgt, fwd));
            float sway = (IsKeyDown(KEY_W)||IsKeyDown(KEY_S)||IsKeyDown(KEY_A)||IsKeyDown(KEY_D))
                ? sinf(gameTime * 8) * 0.02f : 0;
            float recoil = (player.fireTimer > 0) ? player.fireTimer * 0.3f : 0;

            // Gun base position in camera space: forward + right + down
            Vector3 gunBase = camera.position;
            gunBase = Vector3Add(gunBase, Vector3Scale(fwd, 0.4f - recoil));
            gunBase = Vector3Add(gunBase, Vector3Scale(rgt, 0.22f));
            gunBase = Vector3Add(gunBase, Vector3Scale(up2, -0.28f - sway));

            // Helper: offset a point along camera axes
            #define GUN_PT(f,r,u) Vector3Add(Vector3Add(Vector3Add(gunBase, \
                Vector3Scale(fwd,f)), Vector3Scale(rgt,r)), Vector3Scale(up2,u))

            bool firing = player.fireTimer > weapons[player.weapon].fireRate - 0.05f;

            if (player.weapon == 0) {
                // Pistol
                Vector3 muzzle = GUN_PT(0.25f, 0, 0);
                DrawCylinderEx(gunBase, muzzle, 0.018f, 0.016f, 6, (Color){50,50,55,255});
                DrawCylinderEx(GUN_PT(-0.02f,0,-0.01f), GUN_PT(-0.02f,0,-0.1f), 0.02f, 0.018f, 4, (Color){40,35,30,255}); // grip
                DrawCylinderEx(GUN_PT(-0.05f,0,0.01f), GUN_PT(0.08f,0,0.01f), 0.025f, 0.022f, 4, (Color){55,55,60,255}); // slide
                if (firing) {
                    DrawSphere(muzzle, 0.06f, (Color){255,220,50,220});
                    DrawSphere(muzzle, 0.04f, (Color){255,255,200,255});
                }
            } else if (player.weapon == 1) {
                // Shotgun
                Vector3 muzzle = GUN_PT(0.35f, 0, 0);
                DrawCylinderEx(gunBase, muzzle, 0.03f, 0.028f, 8, (Color){50,50,55,255});
                DrawCylinderEx(GUN_PT(0.08f,0,-0.03f), GUN_PT(0.18f,0,-0.03f), 0.022f, 0.02f, 4, (Color){100,75,40,255}); // pump
                DrawCylinderEx(GUN_PT(-0.08f,0,0), GUN_PT(-0.2f,0,0.02f), 0.025f, 0.02f, 4, (Color){90,65,35,255}); // stock
                if (firing) {
                    DrawSphere(muzzle, 0.1f, (Color){255,200,50,200});
                    DrawSphere(muzzle, 0.06f, (Color){255,255,200,255});
                }
            } else {
                // Rifle
                Vector3 muzzle = GUN_PT(0.4f, 0, 0);
                DrawCylinderEx(gunBase, muzzle, 0.016f, 0.014f, 6, (Color){45,45,50,255});
                DrawCylinderEx(GUN_PT(-0.05f,0,0.01f), GUN_PT(0.1f,0,0.01f), 0.023f, 0.02f, 4, (Color){55,55,60,255}); // receiver
                DrawCylinderEx(GUN_PT(0,0,-0.03f), GUN_PT(0,0,-0.1f), 0.016f, 0.014f, 4, (Color){40,40,45,255}); // magazine
                DrawCylinderEx(GUN_PT(-0.02f,0,0.025f), GUN_PT(0.06f,0,0.025f), 0.008f, 0.006f, 4, (Color){60,60,65,255}); // rail
                DrawCylinderEx(GUN_PT(-0.06f,0,0), GUN_PT(-0.18f,0,0.01f), 0.02f, 0.018f, 4, (Color){70,55,35,255}); // stock
                if (firing) {
                    DrawSphere(muzzle, 0.05f, (Color){255,220,50,230});
                }
            }
            #undef GUN_PT
        }

        EndMode3D();

        // --- HUD ---
        // Crosshair
        int crossSize = 8 + (player.weapon == 1 ? 4 : 0);
        HudCrosshair(sw / 2, sh / 2, crossSize, 4, 0, WHITE);

        // HP bar
        float hpPct = player.hp / player.maxHp;
        Color hpCol = hpPct > 0.5f ? (Color){50,200,80,255} : hpPct > 0.25f ? (Color){240,200,40,255} : (Color){220,40,40,255};
        HudHealthBar(20, sh - 50, 200, 20, hpPct, hpCol, (Color){30,30,30,200}, (Color){80,80,80,255});
        DrawText(TextFormat("HP: %.0f", player.hp), 25, sh-48, 16, WHITE);

        // Armor bar
        if (player.armor > 0) {
            DrawRectangle(20, sh-75, (int)(200*(player.armor/100.0f)), 14, (Color){50,100,200,200});
            DrawRectangleLines(20, sh-75, 200, 14, (Color){80,80,80,255});
            DrawText(TextFormat("Armor: %.0f", player.armor), 25, sh-73, 12, WHITE);
        }

        // Ammo
        DrawText(TextFormat("%s", weapons[player.weapon].name), sw-200, sh-50, 18, WHITE);
        DrawText(TextFormat("%d / %d", player.ammo, weapons[player.weapon].maxAmmo), sw-200, sh-28, 16,
            player.ammo > 0 ? WHITE : RED);
        if (player.reloading) DrawText("RELOADING...", sw-200, sh-70, 14, YELLOW);

        // Wave + kills
        DrawText(TextFormat("Wave %d", wave), 20, 20, 24, WHITE);
        DrawText(TextFormat("Kills: %d  Enemies: %d", player.kills, enemiesAlive), 20, 50, 16, (Color){180,180,180,255});
        if (enemiesAlive <= 0 && !gameOver)
            DrawText(TextFormat("Next wave in %.0f...", waveTimer), sw/2-80, 80, 20, GOLD);

        // Weapon select hint
        DrawText("[1] Pistol  [2] Shotgun  [3] Rifle  [R] Reload", 20, sh-20, 12, (Color){100,100,100,255});

        // Damage flash
        HudDamageVignette(player.damageFlash * (400.0f / 255.0f), (Color){200, 30, 30, 255});

        // Game over
        if (gameOver) {
            DrawRectangle(0, 0, sw, sh, (Color){0,0,0,150});
            int goW = MeasureText("YOU DIED", 48);
            DrawText("YOU DIED", sw/2-goW/2, sh/2-50, 48, RED);
            DrawText(TextFormat("Waves survived: %d  Kills: %d", wave, player.kills), sw/2-120, sh/2+10, 20, WHITE);
            DrawText("Press ENTER to restart", sw/2-100, sh/2+40, 16, (Color){180,180,180,255});
        }

        DrawFPS(sw-80, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
