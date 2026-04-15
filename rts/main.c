#include "raylib.h"
#include "raymath.h"
#include "../common/objects3d.h"
#include <math.h>
#include <stdlib.h>

// RTS: two factions, unit types, buildings, combat, fog of war, minimap

#define GROUND_SIZE    80.0f
#define MAX_UNITS      60
#define MAX_BUILDINGS  16
#define MAX_PROJECTILES 32
#define MAX_PARTICLES  64

// Unit types
typedef enum { UNIT_SOLDIER, UNIT_ARCHER, UNIT_KNIGHT } UnitType;
// Factions
typedef enum { FACTION_PLAYER, FACTION_ENEMY } Faction;

typedef struct {
    Vector3 pos;
    Vector3 target;
    float hp, maxHp;
    float speed;
    float attackRange;
    float attackDamage;
    float attackCooldown;
    float attackTimer;
    int targetUnit;     // index of unit being attacked (-1 = none)
    UnitType type;
    Faction faction;
    bool selected;
    bool active;
} Unit;

typedef enum { BLDG_BASE, BLDG_BARRACKS, BLDG_TOWER } BuildingType;

typedef struct {
    Vector3 pos;
    float hp, maxHp;
    BuildingType type;
    Faction faction;
    float spawnTimer;
    bool active;
} Building;

typedef struct {
    Vector3 pos, vel;
    float life;
    int owner;      // unit index
    float damage;
    bool active;
} Projectile;

// Unit stats per type
typedef struct {
    float hp, speed, range, damage, cooldown;
    const char *name;
} UnitStats;

static UnitStats unitStats[] = {
    {30, 8.0f, 2.5f, 5, 0.8f, "Soldier"},
    {20, 6.0f, 15.0f, 8, 1.2f, "Archer"},
    {60, 5.0f, 2.0f, 12, 1.5f, "Knight"},
};

// Unit models (Part arrays)
static Part soldierParts[] = {
    CUBE(0, 0.5f, 0, 0.4f, 1.0f, 0.3f, COL(60,60,180,255)),
    SPHERE(0, 1.2f, 0, 0.25f, COL(220,185,145,255)),
    CUBE(0, 0.3f, 0, 0.5f, 0.1f, 0.5f, COL(100,100,110,255)), // shield
};
static Part archerParts[] = {
    CUBE(0, 0.5f, 0, 0.35f, 0.9f, 0.25f, COL(40,120,40,255)),
    SPHERE(0, 1.15f, 0, 0.22f, COL(220,185,145,255)),
    CYL(0.25f, 0.4f, 0, 0.02f, 0.8f, COL(120,80,30,255)), // bow
};
static Part knightParts[] = {
    CUBE(0, 0.6f, 0, 0.5f, 1.2f, 0.4f, COL(180,180,190,255)),
    SPHERE(0, 1.4f, 0, 0.3f, COL(180,180,190,255)),
    CUBE(0.35f, 0.7f, 0, 0.08f, 0.6f, 0.08f, COL(200,200,210,255)), // sword
};
static Part enemySoldierParts[] = {
    CUBE(0, 0.5f, 0, 0.4f, 1.0f, 0.3f, COL(180,50,50,255)),
    SPHERE(0, 1.2f, 0, 0.25f, COL(200,170,130,255)),
    CUBE(0, 0.3f, 0, 0.5f, 0.1f, 0.5f, COL(80,30,30,255)),
};
static Part enemyArcherParts[] = {
    CUBE(0, 0.5f, 0, 0.35f, 0.9f, 0.25f, COL(160,40,40,255)),
    SPHERE(0, 1.15f, 0, 0.22f, COL(200,170,130,255)),
    CYL(0.25f, 0.4f, 0, 0.02f, 0.8f, COL(100,60,20,255)),
};
static Part enemyKnightParts[] = {
    CUBE(0, 0.6f, 0, 0.5f, 1.2f, 0.4f, COL(120,30,30,255)),
    SPHERE(0, 1.4f, 0, 0.3f, COL(100,30,30,255)),
    CUBE(0.35f, 0.7f, 0, 0.08f, 0.6f, 0.08f, COL(140,40,40,255)),
};

// Building models
static Part baseParts[] = {
    CUBE(0, 1.0f, 0, 4.0f, 2.0f, 4.0f, COL(160,140,100,255)),
    CUBE(0, 2.2f, 0, 3.0f, 0.4f, 3.0f, COL(140,60,40,255)),
    CUBE(0, 0.5f, 1.8f, 1.0f, 1.0f, 0.2f, COL(100,60,30,255)), // door
};
static Part barracksParts[] = {
    CUBE(0, 0.8f, 0, 3.0f, 1.6f, 2.5f, COL(130,120,90,255)),
    CUBE(0, 1.8f, 0, 2.5f, 0.3f, 2.0f, COL(120,50,30,255)),
};
static Part towerParts[] = {
    CYL(0, 0, 0, 0.8f, 4.0f, COL(140,140,150,255)),
    CONE(0, 4.0f, 0, 1.2f, 1.5f, 0.0f, COL(140,50,35,255)),
};

// State
static Unit units[MAX_UNITS];
static Building buildings[MAX_BUILDINGS];
static Projectile projectiles[MAX_PROJECTILES];
static Particle3D particles[MAX_PARTICLES];
static int resources = 100;
static float gameTime = 0;
static float enemySpawnTimer = 0;

// Selection
static Vector2 selStart, selEnd;
static bool selecting = false;

// Camera
static float camScale = 35.0f;
static Vector3 camFocus = {0, 0, 0};

Vector3 ScreenToGround(Vector2 screenPos, Camera3D camera) {
    Ray ray = GetMouseRay(screenPos, camera);
    float t = -ray.position.y / ray.direction.y;
    if (t < 0) t = 0;
    return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
}

Part *GetUnitParts(UnitType type, Faction faction, int *count) {
    if (faction == FACTION_PLAYER) {
        switch (type) {
            case UNIT_SOLDIER: *count = 3; return soldierParts;
            case UNIT_ARCHER:  *count = 3; return archerParts;
            case UNIT_KNIGHT:  *count = 3; return knightParts;
        }
    } else {
        switch (type) {
            case UNIT_SOLDIER: *count = 3; return enemySoldierParts;
            case UNIT_ARCHER:  *count = 3; return enemyArcherParts;
            case UNIT_KNIGHT:  *count = 3; return enemyKnightParts;
        }
    }
    *count = 3; return soldierParts;
}

int SpawnUnit(UnitType type, Faction faction, Vector3 pos) {
    for (int i = 0; i < MAX_UNITS; i++) {
        if (units[i].active) continue;
        UnitStats *s = &unitStats[type];
        units[i] = (Unit){
            .pos = pos, .target = pos,
            .hp = s->hp, .maxHp = s->hp,
            .speed = s->speed, .attackRange = s->range,
            .attackDamage = s->damage, .attackCooldown = s->cooldown,
            .attackTimer = 0, .targetUnit = -1,
            .type = type, .faction = faction,
            .selected = false, .active = true
        };
        return i;
    }
    return -1;
}

void SpawnBuilding(BuildingType type, Faction faction, Vector3 pos) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (buildings[i].active) continue;
        float hp = (type == BLDG_BASE) ? 200 : (type == BLDG_TOWER) ? 80 : 120;
        buildings[i] = (Building){
            .pos = pos, .hp = hp, .maxHp = hp,
            .type = type, .faction = faction,
            .spawnTimer = 0, .active = true
        };
        break;
    }
}

void InitGame(void) {
    memset(units, 0, sizeof(units));
    memset(buildings, 0, sizeof(buildings));
    memset(projectiles, 0, sizeof(projectiles));
    memset(particles, 0, sizeof(particles));
    resources = 100;
    gameTime = 0;
    enemySpawnTimer = 5.0f;

    // Player base + starting units
    SpawnBuilding(BLDG_BASE, FACTION_PLAYER, (Vector3){-25, 0, -25});
    SpawnBuilding(BLDG_BARRACKS, FACTION_PLAYER, (Vector3){-20, 0, -22});
    for (int i = 0; i < 4; i++)
        SpawnUnit(UNIT_SOLDIER, FACTION_PLAYER, (Vector3){-22 + i*2, 0, -20});
    SpawnUnit(UNIT_ARCHER, FACTION_PLAYER, (Vector3){-24, 0, -18});
    SpawnUnit(UNIT_ARCHER, FACTION_PLAYER, (Vector3){-20, 0, -18});

    // Enemy base + starting units
    SpawnBuilding(BLDG_BASE, FACTION_ENEMY, (Vector3){25, 0, 25});
    SpawnBuilding(BLDG_BARRACKS, FACTION_ENEMY, (Vector3){20, 0, 22});
    SpawnBuilding(BLDG_TOWER, FACTION_ENEMY, (Vector3){18, 0, 18});
    for (int i = 0; i < 4; i++)
        SpawnUnit(UNIT_SOLDIER, FACTION_ENEMY, (Vector3){22 + i*2, 0, 20});
    SpawnUnit(UNIT_KNIGHT, FACTION_ENEMY, (Vector3){24, 0, 18});

    camFocus = (Vector3){-25, 0, -25};
}

int FindNearestEnemy(Unit *u) {
    float bestD = 1e9f;
    int best = -1;
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].active || units[i].faction == u->faction) continue;
        float d = Vector3Distance(u->pos, units[i].pos);
        if (d < bestD) { bestD = d; best = i; }
    }
    return (bestD < 30.0f) ? best : -1;
}

void ShootProjectile(int owner, Vector3 target) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].active) continue;
        Vector3 dir = Vector3Normalize(Vector3Subtract(target, units[owner].pos));
        projectiles[i] = (Projectile){
            .pos = (Vector3){units[owner].pos.x, 1.0f, units[owner].pos.z},
            .vel = Vector3Scale(dir, 25.0f),
            .life = 2.0f, .owner = owner,
            .damage = units[owner].attackDamage, .active = true
        };
        break;
    }
}

void UpdateUnit(Unit *u, int idx, float dt) {
    if (!u->active) return;

    // Find target enemy if none
    if (u->targetUnit < 0 || !units[u->targetUnit].active)
        u->targetUnit = FindNearestEnemy(u);

    // Attack if in range
    if (u->targetUnit >= 0 && units[u->targetUnit].active) {
        float dist = Vector3Distance(u->pos, units[u->targetUnit].pos);
        if (dist <= u->attackRange) {
            u->attackTimer -= dt;
            if (u->attackTimer <= 0) {
                u->attackTimer = u->attackCooldown;
                if (u->type == UNIT_ARCHER) {
                    ShootProjectile(idx, units[u->targetUnit].pos);
                } else {
                    units[u->targetUnit].hp -= u->attackDamage;
                    if (units[u->targetUnit].hp <= 0) {
                        units[u->targetUnit].active = false;
                        SpawnParticleBurst(particles, MAX_PARTICLES, units[u->targetUnit].pos, 8, 2, 6, 0.3f, 0.8f, 0.1f, 0.3f);
                        if (u->faction == FACTION_PLAYER) resources += 5;
                        u->targetUnit = -1;
                    }
                }
            }
            return; // Don't move while attacking in melee range
        } else {
            // Move toward enemy
            u->target = units[u->targetUnit].pos;
        }
    }

    // Move toward target
    Vector3 dir = Vector3Subtract(u->target, u->pos);
    dir.y = 0;
    float dist = Vector3Length(dir);
    if (dist > 0.5f) {
        dir = Vector3Normalize(dir);
        u->pos = Vector3Add(u->pos, Vector3Scale(dir, u->speed * dt));
    }

    // Clamp to bounds
    float half = GROUND_SIZE / 2.0f;
    if (u->pos.x < -half) u->pos.x = -half;
    if (u->pos.x > half) u->pos.x = half;
    if (u->pos.z < -half) u->pos.z = -half;
    if (u->pos.z > half) u->pos.z = half;

    // Push apart from other units
    for (int i = 0; i < MAX_UNITS; i++) {
        if (i == idx || !units[i].active) continue;
        Vector3 diff = Vector3Subtract(u->pos, units[i].pos);
        diff.y = 0;
        float d = Vector3Length(diff);
        if (d < 1.2f && d > 0.01f) {
            Vector3 push = Vector3Scale(Vector3Normalize(diff), (1.2f - d) * 0.5f);
            u->pos = Vector3Add(u->pos, push);
        }
    }
}

void EnemyAI(float dt) {
    // Periodically spawn enemy units from barracks
    enemySpawnTimer -= dt;
    if (enemySpawnTimer <= 0) {
        enemySpawnTimer = 8.0f + (float)GetRandomValue(0, 40) / 10.0f;
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            if (!buildings[i].active || buildings[i].faction != FACTION_ENEMY) continue;
            if (buildings[i].type == BLDG_BARRACKS) {
                UnitType t = (GetRandomValue(0, 3) == 0) ? UNIT_KNIGHT : UNIT_SOLDIER;
                Vector3 sp = buildings[i].pos;
                sp.x += (float)GetRandomValue(-30, 30) / 10.0f;
                sp.z += (float)GetRandomValue(-30, 30) / 10.0f;
                int idx = SpawnUnit(t, FACTION_ENEMY, sp);
                // Send toward player base
                if (idx >= 0) {
                    units[idx].target = (Vector3){-25 + (float)GetRandomValue(-50, 50)/10.0f, 0,
                                                  -25 + (float)GetRandomValue(-50, 50)/10.0f};
                }
            }
        }
    }

    // Tower attacks
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].active || buildings[i].faction != FACTION_ENEMY || buildings[i].type != BLDG_TOWER) continue;
        buildings[i].spawnTimer -= dt;
        if (buildings[i].spawnTimer <= 0) {
            // Find nearest player unit
            float bestD = 20.0f;
            int best = -1;
            for (int u = 0; u < MAX_UNITS; u++) {
                if (!units[u].active || units[u].faction != FACTION_PLAYER) continue;
                float d = Vector3Distance(buildings[i].pos, units[u].pos);
                if (d < bestD) { bestD = d; best = u; }
            }
            if (best >= 0) {
                // Tower shoots
                for (int p = 0; p < MAX_PROJECTILES; p++) {
                    if (projectiles[p].active) continue;
                    Vector3 dir = Vector3Normalize(Vector3Subtract(units[best].pos, buildings[i].pos));
                    projectiles[p] = (Projectile){
                        .pos = (Vector3){buildings[i].pos.x, 3.0f, buildings[i].pos.z},
                        .vel = Vector3Scale(dir, 20.0f),
                        .life = 2.0f, .owner = -1, .damage = 10, .active = true
                    };
                    break;
                }
                buildings[i].spawnTimer = 2.0f;
            }
        }
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "RTS");
    MaximizeWindow();
    SetTargetFPS(60);

    InitGame();

    Camera3D camera = {0};
    camera.projection = CAMERA_ORTHOGRAPHIC;
    camera.up = (Vector3){0, 1, 0};

    // Build mode
    int buildMode = -1; // -1=none, 0=barracks, 1=tower
    int trainType = -1; // -1=none, 0=soldier, 1=archer, 2=knight

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        gameTime += dt;
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        Vector2 mouse = GetMousePosition();

        // Camera — move relative to screen direction
        float camSpd = 30.0f * dt;
        // Camera forward on ground plane (projected from camera->target direction)
        Vector3 camDir = Vector3Subtract(camera.target, camera.position);
        camDir.y = 0;
        camDir = Vector3Normalize(camDir);
        // Camera right on ground plane
        Vector3 camRight = {-camDir.z, 0, camDir.x};
        if (IsKeyDown(KEY_W)) camFocus = Vector3Add(camFocus, Vector3Scale(camDir, camSpd));
        if (IsKeyDown(KEY_S)) camFocus = Vector3Add(camFocus, Vector3Scale(camDir, -camSpd));
        if (IsKeyDown(KEY_A)) camFocus = Vector3Add(camFocus, Vector3Scale(camRight, -camSpd));
        if (IsKeyDown(KEY_D)) camFocus = Vector3Add(camFocus, Vector3Scale(camRight, camSpd));
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            camScale -= wheel * 3.0f;
            if (camScale < 10) camScale = 10;
            if (camScale > 80) camScale = 80;
        }
        camera.fovy = camScale;
        camera.position = Vector3Add(camFocus, (Vector3){20, 20, 20});
        camera.target = camFocus;

        Vector3 groundHit = ScreenToGround(mouse, camera);
        bool overUI = (mouse.x < 200 || mouse.y > sh - 80);

        // Resource income
        if ((int)(gameTime * 10) % 50 == 0) resources++;

        // --- Input ---
        // Build mode keys
        if (IsKeyPressed(KEY_B)) buildMode = 0; // barracks
        if (IsKeyPressed(KEY_T)) buildMode = 1; // tower
        if (IsKeyPressed(KEY_ESCAPE)) { buildMode = -1; trainType = -1; }

        // Place building
        if (buildMode >= 0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI) {
            int cost = (buildMode == 0) ? 30 : 20;
            if (resources >= cost) {
                resources -= cost;
                SpawnBuilding(buildMode == 0 ? BLDG_BARRACKS : BLDG_TOWER, FACTION_PLAYER, groundHit);
            }
            buildMode = -1;
        }

        // Train units from barracks
        if (IsKeyPressed(KEY_ONE)) trainType = 0;
        if (IsKeyPressed(KEY_TWO)) trainType = 1;
        if (IsKeyPressed(KEY_THREE)) trainType = 2;
        if (trainType >= 0) {
            int cost = (trainType == 2) ? 20 : 10;
            if (resources >= cost) {
                // Find player barracks
                for (int i = 0; i < MAX_BUILDINGS; i++) {
                    if (!buildings[i].active || buildings[i].faction != FACTION_PLAYER || buildings[i].type != BLDG_BARRACKS) continue;
                    Vector3 sp = buildings[i].pos;
                    sp.x += (float)GetRandomValue(-20, 20) / 10.0f;
                    sp.z += 3.0f;
                    SpawnUnit((UnitType)trainType, FACTION_PLAYER, sp);
                    resources -= cost;
                    break;
                }
            }
            trainType = -1;
        }

        // Selection
        if (buildMode < 0) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI) {
                selStart = mouse;
                selecting = true;
                // Click select
                bool hitUnit = false;
                for (int i = 0; i < MAX_UNITS; i++) {
                    if (!units[i].active || units[i].faction != FACTION_PLAYER) continue;
                    Vector2 sp = GetWorldToScreen(units[i].pos, camera);
                    if (Vector2Distance(mouse, sp) < 15) {
                        if (!IsKeyDown(KEY_LEFT_SHIFT))
                            for (int j = 0; j < MAX_UNITS; j++) units[j].selected = false;
                        units[i].selected = !units[i].selected;
                        hitUnit = true;
                        selecting = false;
                        break;
                    }
                }
                if (!hitUnit && !IsKeyDown(KEY_LEFT_SHIFT))
                    for (int i = 0; i < MAX_UNITS; i++) units[i].selected = false;
            }
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && selecting) selEnd = mouse;
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && selecting) {
                selecting = false;
                float x0 = fminf(selStart.x, selEnd.x), y0 = fminf(selStart.y, selEnd.y);
                float x1 = fmaxf(selStart.x, selEnd.x), y1 = fmaxf(selStart.y, selEnd.y);
                if (x1 - x0 > 5 || y1 - y0 > 5) {
                    if (!IsKeyDown(KEY_LEFT_SHIFT))
                        for (int i = 0; i < MAX_UNITS; i++) units[i].selected = false;
                    for (int i = 0; i < MAX_UNITS; i++) {
                        if (!units[i].active || units[i].faction != FACTION_PLAYER) continue;
                        Vector2 sp = GetWorldToScreen(units[i].pos, camera);
                        if (sp.x >= x0 && sp.x <= x1 && sp.y >= y0 && sp.y <= y1)
                            units[i].selected = true;
                    }
                }
            }

            // Right click: move/attack
            if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && !overUI) {
                // Formation spread
                int selCount = 0;
                for (int i = 0; i < MAX_UNITS; i++) if (units[i].selected) selCount++;
                int placed = 0;
                int cols = (int)ceilf(sqrtf((float)selCount));
                for (int i = 0; i < MAX_UNITS; i++) {
                    if (!units[i].selected) continue;
                    int row = placed / cols, col = placed % cols;
                    units[i].target = (Vector3){
                        groundHit.x + (col - cols/2) * 1.5f,
                        0,
                        groundHit.z + row * 1.5f
                    };
                    units[i].targetUnit = -1; // Clear attack target, let AI re-acquire
                    placed++;
                }
            }
        }

        // Update
        for (int i = 0; i < MAX_UNITS; i++) UpdateUnit(&units[i], i, dt);
        EnemyAI(dt);

        // Update projectiles
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (!projectiles[i].active) continue;
            projectiles[i].pos = Vector3Add(projectiles[i].pos, Vector3Scale(projectiles[i].vel, dt));
            projectiles[i].life -= dt;
            if (projectiles[i].life <= 0) { projectiles[i].active = false; continue; }
            // Hit check
            for (int u = 0; u < MAX_UNITS; u++) {
                if (!units[u].active) continue;
                if (projectiles[i].owner >= 0 && units[u].faction == units[projectiles[i].owner].faction) continue;
                if (projectiles[i].owner < 0 && units[u].faction == FACTION_ENEMY) continue; // tower shots hit player
                if (Vector3Distance(projectiles[i].pos, units[u].pos) < 1.0f) {
                    units[u].hp -= projectiles[i].damage;
                    SpawnParticleBurst(particles, MAX_PARTICLES, units[u].pos, 4, 1, 4, 0.2f, 0.5f, 0.05f, 0.15f);
                    if (units[u].hp <= 0) {
                        units[u].active = false;
                        SpawnParticleBurst(particles, MAX_PARTICLES, units[u].pos, 10, 2, 8, 0.3f, 1.0f, 0.1f, 0.3f);
                        if (units[u].faction == FACTION_ENEMY) resources += 5;
                    }
                    projectiles[i].active = false;
                    break;
                }
            }
        }

        // Update particles
        UpdateParticles3D(particles, MAX_PARTICLES, dt, 10.0f);

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){30, 40, 30, 255});
        BeginMode3D(camera);

        // Ground
        DrawPlane((Vector3){0, 0, 0}, (Vector2){GROUND_SIZE, GROUND_SIZE}, (Color){60, 100, 60, 255});
        // Grid
        for (float g = -GROUND_SIZE/2; g <= GROUND_SIZE/2; g += 5)  {
            DrawLine3D((Vector3){g, 0.01f, -GROUND_SIZE/2}, (Vector3){g, 0.01f, GROUND_SIZE/2}, (Color){50, 85, 50, 255});
            DrawLine3D((Vector3){-GROUND_SIZE/2, 0.01f, g}, (Vector3){GROUND_SIZE/2, 0.01f, g}, (Color){50, 85, 50, 255});
        }

        // Buildings
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            if (!buildings[i].active) continue;
            Part *bp; int bc;
            switch (buildings[i].type) {
                case BLDG_BASE: bp = baseParts; bc = 3; break;
                case BLDG_BARRACKS: bp = barracksParts; bc = 2; break;
                case BLDG_TOWER: bp = towerParts; bc = 2; break;
            }
            // Tint enemy buildings red
            DrawObject3DAt(&(Object3D){bp, bc, buildings[i].pos, 0}, buildings[i].pos, 0);
            // HP bar above building
            Vector2 bsp = GetWorldToScreen((Vector3){buildings[i].pos.x, 3.5f, buildings[i].pos.z}, camera);
            if (bsp.y > 0 && bsp.y < sh) {
                DrawRectangle(bsp.x - 20, bsp.y - 5, 40, 6, (Color){40,40,40,200});
                float pct = buildings[i].hp / buildings[i].maxHp;
                Color hc = buildings[i].faction == FACTION_PLAYER ? (Color){50,200,80,255} : (Color){200,50,50,255};
                DrawRectangle(bsp.x - 20, bsp.y - 5, (int)(40 * pct), 6, hc);
            }
        }

        // Units
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!units[i].active) continue;
            int pc; Part *pp = GetUnitParts(units[i].type, units[i].faction, &pc);
            float rotY = 0;
            // Face movement direction
            Vector3 dir = Vector3Subtract(units[i].target, units[i].pos);
            if (Vector3Length(dir) > 0.1f) rotY = atan2f(dir.x, dir.z);
            DrawObject3DAt(&(Object3D){pp, pc, units[i].pos, rotY}, units[i].pos, rotY);

            // Selection ring
            if (units[i].selected)
                DrawCircle3D(units[i].pos, 0.8f, (Vector3){1,0,0}, 90, GREEN);

            // HP bar
            if (units[i].hp < units[i].maxHp) {
                Vector2 usp = GetWorldToScreen((Vector3){units[i].pos.x, 2.0f, units[i].pos.z}, camera);
                if (usp.y > 0 && usp.y < sh) {
                    float pct = units[i].hp / units[i].maxHp;
                    DrawRectangle(usp.x - 12, usp.y, 24, 3, (Color){40,40,40,200});
                    Color hc = pct > 0.5f ? (Color){50,200,80,255} : pct > 0.25f ? (Color){240,200,40,255} : (Color){220,40,40,255};
                    DrawRectangle(usp.x - 12, usp.y, (int)(24 * pct), 3, hc);
                }
            }
        }

        // Projectiles
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (!projectiles[i].active) continue;
            DrawSphere(projectiles[i].pos, 0.15f, YELLOW);
        }

        // Particles
        DrawParticles3D(particles, MAX_PARTICLES);

        // Build preview
        if (buildMode >= 0) {
            Part *bp; int bc;
            if (buildMode == 0) { bp = barracksParts; bc = 2; }
            else { bp = towerParts; bc = 2; }
            Object3D preview = {bp, bc, groundHit, 0};
            DrawObject3D(&preview);
            DrawCircle3D(groundHit, 2.0f, (Vector3){1,0,0}, 90, (Color){255,255,0,150});
        }

        EndMode3D();

        // Selection rectangle
        if (selecting) {
            float x0 = fminf(selStart.x, selEnd.x), y0 = fminf(selStart.y, selEnd.y);
            float w = fabsf(selEnd.x - selStart.x), h = fabsf(selEnd.y - selStart.y);
            DrawRectangle(x0, y0, w, h, (Color){0, 200, 0, 40});
            DrawRectangleLines(x0, y0, w, h, GREEN);
        }

        // --- HUD ---
        // Resource panel
        DrawRectangle(0, sh - 80, sw, 80, (Color){20, 25, 20, 230});
        DrawRectangleLines(0, sh - 80, sw, 1, (Color){80, 100, 80, 255});
        DrawText(TextFormat("Gold: %d", resources), 20, sh - 70, 20, GOLD);

        // Unit counts
        int playerUnits = 0, enemyUnits = 0;
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!units[i].active) continue;
            if (units[i].faction == FACTION_PLAYER) playerUnits++;
            else enemyUnits++;
        }
        DrawText(TextFormat("Units: %d", playerUnits), 20, sh - 45, 14, (Color){150,200,150,255});
        DrawText(TextFormat("Enemy: %d", enemyUnits), 20, sh - 28, 14, (Color){200,150,150,255});

        // Build buttons
        DrawText("[1] Soldier (10g)  [2] Archer (10g)  [3] Knight (20g)", 200, sh - 70, 14, WHITE);
        DrawText("[B] Barracks (30g)  [T] Tower (20g)", 200, sh - 50, 14, WHITE);
        DrawText("WASD: Pan  Scroll: Zoom  LClick: Select  RClick: Move", 200, sh - 30, 12, (Color){120,140,120,255});

        // Minimap
        int mmSize = 120, mmX = sw - mmSize - 10, mmY = 10;
        DrawRectangle(mmX, mmY, mmSize, mmSize, (Color){30, 50, 30, 200});
        DrawRectangleLines(mmX, mmY, mmSize, mmSize, (Color){80, 100, 80, 255});
        float mmScale = (float)mmSize / GROUND_SIZE;
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!units[i].active) continue;
            int mx = mmX + mmSize/2 + (int)(units[i].pos.x * mmScale);
            int my = mmY + mmSize/2 + (int)(units[i].pos.z * mmScale);
            Color mc = units[i].faction == FACTION_PLAYER ? (Color){50,150,255,255} : (Color){255,80,80,255};
            DrawCircle(mx, my, 2, mc);
        }
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            if (!buildings[i].active) continue;
            int mx = mmX + mmSize/2 + (int)(buildings[i].pos.x * mmScale);
            int my = mmY + mmSize/2 + (int)(buildings[i].pos.z * mmScale);
            Color mc = buildings[i].faction == FACTION_PLAYER ? (Color){80,180,255,255} : (Color){255,100,100,255};
            DrawRectangle(mx - 3, my - 3, 6, 6, mc);
        }
        // Camera view on minimap
        int cvx = mmX + mmSize/2 + (int)(camFocus.x * mmScale);
        int cvy = mmY + mmSize/2 + (int)(camFocus.z * mmScale);
        DrawRectangleLines(cvx - 8, cvy - 8, 16, 16, (Color){255,255,0,200});

        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
