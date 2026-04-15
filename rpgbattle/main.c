#include "raylib.h"
#include "raymath.h"
#include "../common/objects3d.h"
#include "../common/map3d.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// FF7-style ATB battle system
// - 3 party members vs 2-3 enemies
// - Active Time Battle: bars fill, act when full
// - Attack, Magic, Item commands
// - 3D scene with side view camera

#define MAX_PARTY    3
#define MAX_ENEMIES  3
#define MAX_EFFECTS 20

// ATB
#define ATB_MAX     100.0f
#define ATB_SPEED    30.0f   // base fill rate per second

// Damage formulas
#define BASE_PHYS    20
#define BASE_MAG     25

// --- Character models (using objects3d.h Part arrays) ---

// Cloud: blue outfit, yellow spiky hair, big sword
static Part cloudParts[] = {
    CUBE(0, 0.25f, 0,   0.3f, 0.5f, 0.25f, COL(40,40,50,255)),     // legs
    CUBE(0, 0.7f, 0,    0.4f, 0.5f, 0.25f, COL(80,100,200,255)),    // torso
    SPHERE(0.08f, 1.15f, 0,  0.18f, COL(220,190,150,255)),           // head
    SPHERE(-0.12f, 1.25f, 0, 0.15f, COL(220,200,50,255)),            // hair
    CUBE(0.1f, 0.65f, -0.25f, 0.12f,0.4f,0.12f, COL(80,100,200,255)), // arm L
    CUBE(0.1f, 0.65f, 0.25f,  0.12f,0.4f,0.12f, COL(80,100,200,255)), // arm R
    CUBE(0.3f, 0.9f, -0.3f,   0.06f,0.8f,0.04f, COL(180,180,190,255)), // sword
};

// Tifa: dark outfit, dark hair
static Part tifaParts[] = {
    CUBE(0, 0.25f, 0,   0.3f, 0.5f, 0.25f, COL(40,40,50,255)),
    CUBE(0, 0.7f, 0,    0.4f, 0.5f, 0.25f, COL(180,60,60,255)),
    SPHERE(0.08f, 1.15f, 0,  0.18f, COL(220,190,150,255)),
    SPHERE(-0.12f, 1.25f, 0, 0.15f, COL(50,30,20,255)),
    CUBE(0.1f, 0.65f, -0.25f, 0.12f,0.4f,0.12f, COL(180,60,60,255)),
    CUBE(0.1f, 0.65f, 0.25f,  0.12f,0.4f,0.12f, COL(180,60,60,255)),
    SPHERE(0.25f, 0.5f, -0.25f, 0.08f, COL(160,140,100,255)),        // glove L
    SPHERE(0.25f, 0.5f, 0.25f,  0.08f, COL(160,140,100,255)),        // glove R
};

// Aerith: pink outfit, brown hair, staff
static Part aerithParts[] = {
    CUBE(0, 0.25f, 0,   0.3f, 0.5f, 0.25f, COL(40,40,50,255)),
    CUBE(0, 0.7f, 0,    0.4f, 0.5f, 0.25f, COL(200,120,160,255)),
    SPHERE(0.08f, 1.15f, 0,  0.18f, COL(220,190,150,255)),
    SPHERE(-0.12f, 1.25f, 0, 0.15f, COL(140,90,60,255)),
    CUBE(0.1f, 0.65f, -0.25f, 0.12f,0.4f,0.12f, COL(200,120,160,255)),
    CUBE(0.1f, 0.65f, 0.25f,  0.12f,0.4f,0.12f, COL(200,120,160,255)),
    CYL(0.2f, 0.3f, -0.3f,   0.03f, 1.2f, COL(120,80,40,255)),       // staff
};

// Brute: big muscular man with huge arms
static Part houndParts[] = {
    CUBE(0, 0.35f, 0,    0.5f, 0.7f, 0.4f, COL(60,50,40,255)),       // legs
    CUBE(0, 0.95f, 0,    0.7f, 0.7f, 0.5f, COL(140,80,50,255)),      // torso (wide)
    CUBE(0, 1.5f, 0,     0.6f, 0.4f, 0.45f, COL(150,90,55,255)),     // shoulders
    SPHERE(-0.15f, 1.9f, 0, 0.25f, COL(200,160,120,255)),             // head
    SPHERE(-0.15f, 2.05f, 0, 0.18f, COL(40,30,20,255)),               // hair
    SPHERE(-0.3f, 1.95f, -0.08f, 0.04f, COL(200,30,30,255)),          // eye L
    SPHERE(-0.3f, 1.95f, 0.08f,  0.04f, COL(200,30,30,255)),          // eye R
    CYL(-0.15f, 1.0f, -0.45f, 0.15f, 0.7f, COL(200,160,120,255)),    // arm L (thick)
    CYL(-0.15f, 1.0f, 0.45f,  0.15f, 0.7f, COL(200,160,120,255)),    // arm R (thick)
    SPHERE(-0.15f, 0.9f, -0.45f, 0.18f, COL(200,160,120,255)),        // fist L
    SPHERE(-0.15f, 0.9f, 0.45f,  0.18f, COL(200,160,120,255)),        // fist R
};

// Sweeper: big mech robot
static Part sweeperParts[] = {
    CUBE(0, 0.6f, 0,     1.0f, 0.8f, 0.7f, COL(80,80,90,255)),       // body
    CUBE(0, 1.3f, 0,     0.7f, 0.5f, 0.5f, COL(70,70,80,255)),       // upper body
    CUBE(0, 1.7f, 0,     0.4f, 0.3f, 0.4f, COL(60,60,70,255)),       // head
    SPHERE(-0.25f, 1.75f, -0.1f, 0.06f, COL(255,50,50,255)),          // eye L
    SPHERE(-0.25f, 1.75f, 0.1f,  0.06f, COL(255,50,50,255)),          // eye R
    CUBE(-0.6f, 1.0f, 0,  0.5f, 0.15f, 0.2f, COL(90,90,100,255)),    // arm/cannon
    CUBE(0, 0.15f, -0.35f, 0.4f, 0.3f, 0.2f, COL(60,60,65,255)),     // track L
    CUBE(0, 0.15f, 0.35f,  0.4f, 0.3f, 0.2f, COL(60,60,65,255)),     // track R
};

// Part arrays + counts for lookup
static Part *charModels[] = { cloudParts, tifaParts, aerithParts };
static int charModelCounts[] = {
    sizeof(cloudParts)/sizeof(Part),
    sizeof(tifaParts)/sizeof(Part),
    sizeof(aerithParts)/sizeof(Part),
};
static Part *enemyModels[] = { houndParts, sweeperParts };
static int enemyModelCounts[] = {
    sizeof(houndParts)/sizeof(Part),
    sizeof(sweeperParts)/sizeof(Part),
};

// --- Battle arena map ---
static Map3D battleMap;

static TileDef battleTileDefs[] = {
    TILEDEF_EMPTY,                                         // 0
    TILEDEF_FLOOR(((Color){45,50,40,255})),                // 1: dark ground
    TILEDEF_WALL(2.0f, ((Color){35,40,45,255})),           // 2: wall
    TILEDEF_FLOOR(((Color){55,55,50,255})),                // 3: lighter ground
    TILEDEF_PLATFORM(0.3f, ((Color){50,50,45,255})),       // 4: rubble
};

static void InitBattleMap(void) {
    const char *layout =
        "2222222222"
        "2111111112"
        "2113311112"
        "2111111112"
        "2114411112"
        "2111111112"
        "2222222222";
    Map3DLoad(&battleMap, layout, 10, 7, 2.0f, battleTileDefs, 5);
}

typedef enum { CMD_NONE, CMD_ATTACK, CMD_FIRE, CMD_ICE, CMD_CURE, CMD_ITEM } CmdType;
typedef enum { EFF_NONE, EFF_SLASH, EFF_FIRE, EFF_ICE, EFF_CURE, EFF_HIT } EffectType;

static const char *cmdNames[] = { "", "Attack", "Fire", "Ice", "Cure", "Potion" };

typedef struct {
    char name[16];
    int hp, maxHp;
    int mp, maxMp;
    int atk, def, mag;
    float atb;          // 0 to ATB_MAX
    bool alive;
    bool isEnemy;
    Vector3 pos;        // 3D position in battle scene
    float animTimer;
    float hurtTimer;    // flash when hit
    float actionTimer;  // step forward when acting
    CmdType pendingCmd;
    int pendingTarget;
    Color color;
} Battler;

typedef struct {
    EffectType type;
    Vector3 pos;
    float timer;
    float duration;
    Color color;
    bool active;
} Effect;

static Battler party[MAX_PARTY];
static Battler enemies[MAX_ENEMIES];
static int numEnemies = 0;
static Effect effects[MAX_EFFECTS];
static int potions = 3;

// Menu state
typedef enum { MENU_NONE, MENU_MAIN, MENU_MAGIC, MENU_TARGET_ENEMY, MENU_TARGET_ALLY } MenuState;
static MenuState menuState = MENU_NONE;
static int menuCursor = 0;
static int activePartyMember = -1;  // who's choosing a command
static CmdType selectedCmd = CMD_NONE;

// Battle state
static bool battleWon = false;
static bool battleLost = false;
static float battleEndTimer = 0;
static int xpGained = 0;
static int gilGained = 0;

// Damage popup
#define MAX_POPUPS 10
typedef struct {
    Vector3 worldPos;
    int value;
    float timer;
    Color color;
    bool active;
    bool isHeal;
} DmgPopup;
static DmgPopup popups[MAX_POPUPS];

// Battle log
#define MAX_LOG 5
static char battleLog[MAX_LOG][64];
static float logTimers[MAX_LOG];
static int logCount = 0;

void AddLog(const char *fmt, ...) {
    // Shift old entries
    for (int i = MAX_LOG - 1; i > 0; i--) {
        strcpy(battleLog[i], battleLog[i-1]);
        logTimers[i] = logTimers[i-1];
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(battleLog[0], 64, fmt, args);
    va_end(args);
    logTimers[0] = 4.0f;
    if (logCount < MAX_LOG) logCount++;
}

void SpawnPopup(Vector3 pos, int value, bool isHeal) {
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (popups[i].active) continue;
        popups[i].worldPos = pos;
        popups[i].value = value;
        popups[i].timer = 1.5f;
        popups[i].color = isHeal ? GREEN : WHITE;
        popups[i].active = true;
        popups[i].isHeal = isHeal;
        break;
    }
}

void SpawnEffect(EffectType type, Vector3 pos, float duration, Color col) {
    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i].active) continue;
        effects[i].type = type;
        effects[i].pos = pos;
        effects[i].timer = duration;
        effects[i].duration = duration;
        effects[i].color = col;
        effects[i].active = true;
        break;
    }
}

void InitBattle(void) {
    InitBattleMap();

    // Party members (left side of arena)
    party[0] = (Battler){
        .name = "Cloud", .hp = 350, .maxHp = 350, .mp = 40, .maxMp = 40,
        .atk = 30, .def = 15, .mag = 20, .atb = 50, .alive = true,
        .pos = {5, 0, 5}, .color = {80, 100, 200, 255}
    };
    party[1] = (Battler){
        .name = "Tifa", .hp = 280, .maxHp = 280, .mp = 30, .maxMp = 30,
        .atk = 35, .def = 12, .mag = 15, .atb = 20, .alive = true,
        .pos = {5, 0, 7}, .color = {180, 60, 60, 255}
    };
    party[2] = (Battler){
        .name = "Aerith", .hp = 220, .maxHp = 220, .mp = 80, .maxMp = 80,
        .atk = 15, .def = 10, .mag = 40, .atb = 0, .alive = true,
        .pos = {5, 0, 9}, .color = {200, 120, 160, 255}
    };

    // Enemies (right side of arena)
    numEnemies = 2;
    enemies[0] = (Battler){
        .name = "Brute", .hp = 250, .maxHp = 250, .mp = 0, .maxMp = 0,
        .atk = 32, .def = 12, .mag = 5, .atb = 30, .alive = true, .isEnemy = true,
        .pos = {15, 0, 5.5f}, .color = {100, 80, 60, 255}
    };
    enemies[1] = (Battler){
        .name = "Sweeper", .hp = 300, .maxHp = 300, .mp = 0, .maxMp = 0,
        .atk = 28, .def = 20, .mag = 10, .atb = 10, .alive = true, .isEnemy = true,
        .pos = {16, 0, 8.5f}, .color = {80, 80, 90, 255}
    };

    potions = 3;
    battleWon = false;
    battleLost = false;
    battleEndTimer = 0;
    menuState = MENU_NONE;
    activePartyMember = -1;
    memset(effects, 0, sizeof(effects));
    memset(popups, 0, sizeof(popups));
}

int CalcPhysDmg(Battler *attacker, Battler *defender) {
    int dmg = attacker->atk * 2 - defender->def;
    dmg += GetRandomValue(-5, 5);
    if (dmg < 1) dmg = 1;
    return dmg;
}

int CalcMagDmg(Battler *caster, Battler *target) {
    int dmg = caster->mag * 3 + BASE_MAG - target->def / 2;
    dmg += GetRandomValue(-8, 8);
    if (dmg < 1) dmg = 1;
    return dmg;
}

void ExecuteCommand(Battler *actor, CmdType cmd, Battler *target) {
    switch (cmd) {
        case CMD_ATTACK: {
            int dmg = CalcPhysDmg(actor, target);
            bool crit = (GetRandomValue(0, 100) < 15);
            if (crit) dmg = (int)(dmg * 1.8f);
            target->hp -= dmg;
            if (target->hp < 0) target->hp = 0;
            target->hurtTimer = crit ? 0.6f : 0.4f;
            SpawnPopup(target->pos, dmg, false);
            if (crit) SpawnEffect(EFF_HIT, target->pos, 0.3f, YELLOW);
            SpawnEffect(EFF_SLASH, target->pos, 0.4f, WHITE);
            AddLog("%s attacks %s for %d%s", actor->name, target->name, dmg, crit ? " CRITICAL!" : "");
            if (target->hp <= 0) { target->alive = false; AddLog("%s defeated!", target->name); }
            break;
        }
        case CMD_FIRE: {
            if (actor->mp < 8) { AddLog("Not enough MP!"); break; }
            actor->mp -= 8;
            int dmg = CalcMagDmg(actor, target);
            target->hp -= dmg;
            if (target->hp < 0) target->hp = 0;
            target->hurtTimer = 0.5f;
            SpawnPopup(target->pos, dmg, false);
            SpawnEffect(EFF_FIRE, target->pos, 0.8f, (Color){255, 100, 0, 255});
            AddLog("%s casts Fire on %s for %d", actor->name, target->name, dmg);
            if (target->hp <= 0) { target->alive = false; AddLog("%s defeated!", target->name); }
            break;
        }
        case CMD_ICE: {
            if (actor->mp < 8) { AddLog("Not enough MP!"); break; }
            actor->mp -= 8;
            int dmg = CalcMagDmg(actor, target);
            target->hp -= dmg;
            if (target->hp < 0) target->hp = 0;
            target->hurtTimer = 0.5f;
            SpawnPopup(target->pos, dmg, false);
            SpawnEffect(EFF_ICE, target->pos, 0.8f, (Color){100, 200, 255, 255});
            AddLog("%s casts Ice on %s for %d", actor->name, target->name, dmg);
            if (target->hp <= 0) { target->alive = false; AddLog("%s defeated!", target->name); }
            break;
        }
        case CMD_CURE: {
            if (actor->mp < 6) { AddLog("Not enough MP!"); break; }
            actor->mp -= 6;
            int heal = actor->mag * 3 + 30;
            target->hp += heal;
            if (target->hp > target->maxHp) target->hp = target->maxHp;
            SpawnPopup(target->pos, heal, true);
            SpawnEffect(EFF_CURE, target->pos, 0.8f, GREEN);
            AddLog("%s casts Cure on %s for %d", actor->name, target->name, heal);
            break;
        }
        case CMD_ITEM: {
            if (potions <= 0) { AddLog("No potions left!"); break; }
            potions--;
            int heal = 100;
            target->hp += heal;
            if (target->hp > target->maxHp) target->hp = target->maxHp;
            SpawnPopup(target->pos, heal, true);
            SpawnEffect(EFF_CURE, target->pos, 0.6f, (Color){100, 255, 100, 255});
            AddLog("%s uses Potion on %s", actor->name, target->name);
            break;
        }
        default: break;
    }
    actor->atb = 0;
    actor->actionTimer = 0.5f;
}

// --- Drawing ---

void DrawBattler3D(Battler *b, int index) {
    if (!b->alive) return;

    Vector3 p = b->pos;

    // Step forward animation when acting
    if (b->actionTimer > 0) {
        float step = sinf(b->actionTimer * PI * 2) * 0.8f;
        if (b->isEnemy) p.x -= step;
        else p.x += step;
    }

    // Idle bob
    float bob = sinf(b->animTimer * 2.0f) * 0.05f;
    p.y += bob;

    // Hurt flash: skip drawing every other frame
    if (b->hurtTimer > 0 && ((int)(b->hurtTimer * 12) % 2 == 0)) return;

    // Draw using Part arrays from common library
    Part *parts;
    int partCount;
    if (b->isEnemy) {
        parts = enemyModels[index];
        partCount = enemyModelCounts[index];
    } else {
        parts = charModels[index];
        partCount = charModelCounts[index];
    }

    // Enemies face left (-x), party faces right (+x) — use rotation 0 since parts are pre-oriented
    for (int i = 0; i < partCount; i++) {
        DrawPart(&parts[i], p, 0);
    }

    // Shadow
    DrawCircle3D((Vector3){p.x, 0.01f, p.z}, 0.5f, (Vector3){1,0,0}, 90, (Color){0,0,0,40});
}

void DrawEffect3D(Effect *e) {
    if (!e->active) return;
    float t = 1.0f - (e->timer / e->duration);
    float alpha = (1.0f - t);

    switch (e->type) {
        case EFF_SLASH: {
            // Diagonal slash lines
            float spread = t * 1.5f;
            Color c = e->color; c.a = (unsigned char)(alpha * 255);
            DrawLine3D(
                (Vector3){e->pos.x - spread, e->pos.y + 1.5f - spread, e->pos.z},
                (Vector3){e->pos.x + spread, e->pos.y + 0.5f + spread, e->pos.z}, c);
            DrawLine3D(
                (Vector3){e->pos.x + spread, e->pos.y + 1.5f - spread, e->pos.z},
                (Vector3){e->pos.x - spread, e->pos.y + 0.5f + spread, e->pos.z}, c);
            break;
        }
        case EFF_FIRE: {
            // Rising flame spheres
            for (int i = 0; i < 5; i++) {
                float fy = e->pos.y + t * 2.0f + sinf(t * 10 + i) * 0.3f;
                float fx = e->pos.x + cosf(i * 1.5f + t * 5) * 0.4f;
                float fz = e->pos.z + sinf(i * 2.0f + t * 5) * 0.4f;
                float r = 0.15f * alpha + 0.05f;
                Color fc = {255, (unsigned char)(100 + i * 30), 0, (unsigned char)(alpha * 200)};
                DrawSphere((Vector3){fx, fy, fz}, r, fc);
            }
            break;
        }
        case EFF_ICE: {
            // Ice crystals
            for (int i = 0; i < 4; i++) {
                float angle = i * PI / 2.0f + t * 3.0f;
                float ix = e->pos.x + cosf(angle) * (0.5f + t * 0.5f);
                float iz = e->pos.z + sinf(angle) * (0.5f + t * 0.5f);
                float iy = e->pos.y + 0.5f + i * 0.3f;
                float sz = 0.15f * alpha;
                DrawCube((Vector3){ix, iy, iz}, sz, sz * 2, sz, (Color){150, 220, 255, (unsigned char)(alpha * 230)});
            }
            break;
        }
        case EFF_CURE: {
            // Rising sparkles
            for (int i = 0; i < 8; i++) {
                float angle = i * PI / 4.0f + t * 4.0f;
                float cx = e->pos.x + cosf(angle) * 0.5f;
                float cz = e->pos.z + sinf(angle) * 0.5f;
                float cy = e->pos.y + 0.5f + t * 2.0f + sinf(i + t * 8) * 0.2f;
                DrawSphere((Vector3){cx, cy, cz}, 0.06f, (Color){100, 255, 100, (unsigned char)(alpha * 255)});
            }
            break;
        }
        case EFF_HIT: {
            // Impact burst
            float r = t * 0.8f;
            DrawSphereWires(e->pos, r, 4, 4, (Color){255, 255, 255, (unsigned char)(alpha * 200)});
            break;
        }
        default: break;
    }
}

void DrawBattleScene(void) {
    Map3DDrawAll(&battleMap);
}

void DrawATBBar(int x, int y, int w, int h, float value, float max, Color fillCol) {
    DrawRectangle(x, y, w, h, (Color){20, 20, 25, 255});
    int fw = (int)((value / max) * w);
    DrawRectangle(x, y, fw, h, fillCol);
    DrawRectangleLines(x, y, w, h, (Color){80, 80, 90, 255});
}

void DrawHPBar(int x, int y, int w, int h, int hp, int maxHp) {
    float pct = (float)hp / maxHp;
    Color col = pct > 0.5f ? GREEN : (pct > 0.25f ? YELLOW : RED);
    DrawRectangle(x, y, w, h, (Color){20, 20, 25, 255});
    DrawRectangle(x, y, (int)(w * pct), h, col);
    DrawRectangleLines(x, y, w, h, (Color){60, 60, 70, 255});
}

int main(void) {
    InitWindow(900, 650, "RPG Battle - FF7 Style");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    InitBattle();

    Camera3D camera = { 0 };
    camera.position = (Vector3){2, 5, 1};
    camera.target = (Vector3){10, 0.8f, 7};
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 45;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        // --- Update ---
        if (!battleWon && !battleLost) {
            // ATB fill for all alive battlers
            bool anyMenuOpen = (menuState != MENU_NONE);

            for (int i = 0; i < MAX_PARTY; i++) {
                if (!party[i].alive) continue;
                party[i].animTimer += dt;
                if (party[i].hurtTimer > 0) party[i].hurtTimer -= dt;
                if (party[i].actionTimer > 0) party[i].actionTimer -= dt;
                if (!anyMenuOpen) {
                    party[i].atb += ATB_SPEED * dt;
                    if (party[i].atb > ATB_MAX) party[i].atb = ATB_MAX;
                }
            }

            for (int i = 0; i < numEnemies; i++) {
                if (!enemies[i].alive) continue;
                enemies[i].animTimer += dt;
                if (enemies[i].hurtTimer > 0) enemies[i].hurtTimer -= dt;
                if (enemies[i].actionTimer > 0) enemies[i].actionTimer -= dt;
                enemies[i].atb += ATB_SPEED * 0.8f * dt;
                if (enemies[i].atb > ATB_MAX) enemies[i].atb = ATB_MAX;

                // Enemy AI: attack when ATB full
                if (enemies[i].atb >= ATB_MAX) {
                    // Pick random alive party member
                    int tries = 0;
                    int target;
                    do { target = GetRandomValue(0, MAX_PARTY - 1); tries++; }
                    while (!party[target].alive && tries < 10);
                    if (party[target].alive) {
                        ExecuteCommand(&enemies[i], CMD_ATTACK, &party[target]);
                    }
                }
            }

            // Check if a party member's ATB is full and no menu is open
            if (menuState == MENU_NONE) {
                for (int i = 0; i < MAX_PARTY; i++) {
                    if (party[i].alive && party[i].atb >= ATB_MAX) {
                        activePartyMember = i;
                        menuState = MENU_MAIN;
                        menuCursor = 0;
                        break;
                    }
                }
            }

            // --- Menu input ---
            if (menuState == MENU_MAIN) {
                int numOptions = 4; // Attack, Magic, Item, (skip for now)
                if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) menuCursor = (menuCursor - 1 + numOptions) % numOptions;
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) menuCursor = (menuCursor + 1) % numOptions;
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                    switch (menuCursor) {
                        case 0: selectedCmd = CMD_ATTACK; menuState = MENU_TARGET_ENEMY;
                            menuCursor = 0; while (menuCursor < numEnemies && !enemies[menuCursor].alive) menuCursor++;
                            break;
                        case 1: menuState = MENU_MAGIC; menuCursor = 0; break;
                        case 2: selectedCmd = CMD_ITEM; menuState = MENU_TARGET_ALLY; menuCursor = 0; break;
                        case 3: // Defend / skip
                            party[activePartyMember].atb = 0;
                            party[activePartyMember].def += 5; // temp boost
                            menuState = MENU_NONE;
                            activePartyMember = -1;
                            break;
                    }
                }
                if (IsKeyPressed(KEY_ESCAPE)) {
                    menuState = MENU_NONE;
                    activePartyMember = -1;
                }
            } else if (menuState == MENU_MAGIC) {
                int numSpells = 3; // Fire, Ice, Cure
                if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) menuCursor = (menuCursor - 1 + numSpells) % numSpells;
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) menuCursor = (menuCursor + 1) % numSpells;
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                    switch (menuCursor) {
                        case 0: selectedCmd = CMD_FIRE; menuState = MENU_TARGET_ENEMY;
                            menuCursor = 0; while (menuCursor < numEnemies && !enemies[menuCursor].alive) menuCursor++;
                            break;
                        case 1: selectedCmd = CMD_ICE; menuState = MENU_TARGET_ENEMY;
                            menuCursor = 0; while (menuCursor < numEnemies && !enemies[menuCursor].alive) menuCursor++;
                            break;
                        case 2: selectedCmd = CMD_CURE; menuState = MENU_TARGET_ALLY; menuCursor = 0; break;
                    }
                }
                if (IsKeyPressed(KEY_ESCAPE)) { menuState = MENU_MAIN; menuCursor = 0; }
            } else if (menuState == MENU_TARGET_ENEMY) {
                // Navigate alive enemies
                if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) {
                    int tries = 0;
                    do { menuCursor = (menuCursor - 1 + numEnemies) % numEnemies; tries++; }
                    while (!enemies[menuCursor].alive && tries < numEnemies);
                }
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) {
                    int tries = 0;
                    do { menuCursor = (menuCursor + 1) % numEnemies; tries++; }
                    while (!enemies[menuCursor].alive && tries < numEnemies);
                }
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                    if (enemies[menuCursor].alive) {
                        ExecuteCommand(&party[activePartyMember], selectedCmd, &enemies[menuCursor]);
                        menuState = MENU_NONE;
                        activePartyMember = -1;
                    }
                }
                if (IsKeyPressed(KEY_ESCAPE)) { menuState = MENU_MAIN; menuCursor = 0; }
            } else if (menuState == MENU_TARGET_ALLY) {
                if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) menuCursor = (menuCursor - 1 + MAX_PARTY) % MAX_PARTY;
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) menuCursor = (menuCursor + 1) % MAX_PARTY;
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                    if (party[menuCursor].alive) {
                        ExecuteCommand(&party[activePartyMember], selectedCmd, &party[menuCursor]);
                        menuState = MENU_NONE;
                        activePartyMember = -1;
                    }
                }
                if (IsKeyPressed(KEY_ESCAPE)) { menuState = MENU_MAIN; menuCursor = 0; }
            }

            // Update effects
            for (int i = 0; i < MAX_EFFECTS; i++) {
                if (!effects[i].active) continue;
                effects[i].timer -= dt;
                if (effects[i].timer <= 0) effects[i].active = false;
            }

            // Update popups
            for (int i = 0; i < MAX_POPUPS; i++) {
                if (!popups[i].active) continue;
                popups[i].timer -= dt;
                popups[i].worldPos.y += 1.5f * dt;
                if (popups[i].timer <= 0) popups[i].active = false;
            }

            // Win/lose check
            bool allEnemiesDead = true;
            for (int i = 0; i < numEnemies; i++) if (enemies[i].alive) allEnemiesDead = false;
            bool allPartyDead = true;
            for (int i = 0; i < MAX_PARTY; i++) if (party[i].alive) allPartyDead = false;

            if (allEnemiesDead) { battleWon = true; xpGained = 120; gilGained = 250; }
            if (allPartyDead) battleLost = true;
        } else {
            battleEndTimer += dt;
        }

        // Camera gentle sway
        float camSway = sinf((float)GetTime() * 0.3f) * 0.5f;
        camera.position = (Vector3){2 + camSway * 0.3f, 5 + camSway * 0.15f, 1};

        // --- Draw ---
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);
            DrawBattleScene();

            // Draw all battlers
            for (int i = 0; i < MAX_PARTY; i++) DrawBattler3D(&party[i], i);
            for (int i = 0; i < numEnemies; i++) DrawBattler3D(&enemies[i], i);

            // Effects
            for (int i = 0; i < MAX_EFFECTS; i++) DrawEffect3D(&effects[i]);

            // Target cursor (arrow pointing down, above model)
            {
                // Helper: find max Y extent of a part array
                #define MODEL_TOP(parts, count) ({ \
                    float _maxY = 0; \
                    for (int _i = 0; _i < (count); _i++) { \
                        float _top = (parts)[_i].offset.y + (parts)[_i].size.y; \
                        if ((parts)[_i].type == PART_SPHERE) _top = (parts)[_i].offset.y + (parts)[_i].size.x; \
                        if (_top > _maxY) _maxY = _top; \
                    } \
                    _maxY; })

                float bounce = sinf((float)GetTime() * 6) * 0.15f;

                if (menuState == MENU_TARGET_ENEMY && menuCursor < numEnemies) {
                    Vector3 tp = enemies[menuCursor].pos;
                    float top = MODEL_TOP(enemyModels[menuCursor], enemyModelCounts[menuCursor]);
                    float arrowY = tp.y + top + 0.5f + bounce;
                    // Point-down cone: wide at top, point at bottom
                    DrawCylinderEx((Vector3){tp.x, arrowY + 0.4f, tp.z},
                        (Vector3){tp.x, arrowY, tp.z}, 0.15f, 0, 4, YELLOW);
                }
                if (menuState == MENU_TARGET_ALLY && menuCursor < MAX_PARTY) {
                    Vector3 tp = party[menuCursor].pos;
                    float top = MODEL_TOP(charModels[menuCursor], charModelCounts[menuCursor]);
                    float arrowY = tp.y + top + 0.3f + bounce;
                    DrawCylinderEx((Vector3){tp.x, arrowY + 0.4f, tp.z},
                        (Vector3){tp.x, arrowY, tp.z}, 0.15f, 0, 4, YELLOW);
                }
                #undef MODEL_TOP
            }
        EndMode3D();

        // --- Damage popups (2D) ---
        for (int i = 0; i < MAX_POPUPS; i++) {
            if (!popups[i].active) continue;
            Vector2 sp = GetWorldToScreen(popups[i].worldPos, camera);
            float alpha = Clamp(popups[i].timer / 1.0f, 0, 1);
            Color c = popups[i].color;
            c.a = (unsigned char)(alpha * 255);
            const char *txt = TextFormat("%s%d", popups[i].isHeal ? "+" : "", popups[i].value);
            int tw = MeasureText(txt, 28);
            DrawText(txt, (int)sp.x - tw/2, (int)sp.y, 28, c);
        }

        // --- HUD: Party status panel (bottom) ---
        int panelH = 120;
        int panelY = sh - panelH;
        DrawRectangle(0, panelY, sw, panelH, (Color){10, 10, 20, 230});
        DrawLine(0, panelY, sw, panelY, (Color){60, 60, 80, 255});

        int colW = sw / 3;
        for (int i = 0; i < MAX_PARTY; i++) {
            int cx = i * colW + 15;
            int cy = panelY + 10;

            // Name
            Color nameCol = (i == activePartyMember) ? YELLOW : (party[i].alive ? WHITE : (Color){80,80,80,255});
            DrawText(party[i].name, cx, cy, 18, nameCol);

            // HP
            DrawText(TextFormat("HP %d/%d", party[i].hp, party[i].maxHp), cx, cy + 22, 14, WHITE);
            DrawHPBar(cx, cy + 38, colW - 40, 8, party[i].hp, party[i].maxHp);

            // MP
            DrawText(TextFormat("MP %d/%d", party[i].mp, party[i].maxMp), cx, cy + 50, 14, (Color){100,180,255,255});

            // ATB bar
            DrawText("ATB", cx, cy + 68, 10, (Color){150,150,150,255});
            Color atbCol = (party[i].atb >= ATB_MAX) ? YELLOW : (Color){0, 150, 255, 255};
            DrawATBBar(cx + 25, cy + 68, colW - 70, 10, party[i].atb, ATB_MAX, atbCol);

            // Ready indicator
            if (party[i].alive && party[i].atb >= ATB_MAX) {
                DrawText("READY", cx + colW - 70, cy + 2, 12, YELLOW);
            }
        }

        // --- Command menu ---
        if (menuState == MENU_MAIN && activePartyMember >= 0) {
            int mx = 20, my = panelY - 130;
            DrawRectangle(mx, my, 150, 120, (Color){10, 10, 30, 230});
            DrawRectangleLinesEx((Rectangle){mx, my, 150, 120}, 1, (Color){80, 80, 120, 255});
            DrawText(party[activePartyMember].name, mx + 10, my + 5, 14, YELLOW);

            const char *options[] = {"Attack", "Magic", "Item", "Defend"};
            for (int i = 0; i < 4; i++) {
                Color oc = (i == menuCursor) ? YELLOW : WHITE;
                if (i == menuCursor) DrawRectangle(mx + 5, my + 25 + i * 22, 140, 20, (Color){40,40,80,200});
                DrawText(options[i], mx + 15, my + 27 + i * 22, 16, oc);
            }
        }

        if (menuState == MENU_MAGIC && activePartyMember >= 0) {
            int mx = 180, my = panelY - 110;
            DrawRectangle(mx, my, 160, 100, (Color){10, 10, 30, 230});
            DrawRectangleLinesEx((Rectangle){mx, my, 160, 100}, 1, (Color){80, 80, 120, 255});
            DrawText("Magic", mx + 10, my + 5, 14, (Color){100,180,255,255});

            const char *spells[] = {"Fire     8 MP", "Ice      8 MP", "Cure     6 MP"};
            for (int i = 0; i < 3; i++) {
                Color oc = (i == menuCursor) ? YELLOW : WHITE;
                if (i == menuCursor) DrawRectangle(mx + 5, my + 25 + i * 22, 150, 20, (Color){40,40,80,200});
                DrawText(spells[i], mx + 15, my + 27 + i * 22, 14, oc);
            }
        }

        // Target name display
        if (menuState == MENU_TARGET_ENEMY && menuCursor < numEnemies) {
            DrawRectangle(sw/2 - 80, 15, 160, 30, (Color){10,10,30,220});
            DrawText(enemies[menuCursor].name, sw/2 - 70, 20, 18, WHITE);
            DrawText(TextFormat("HP: %d/%d", enemies[menuCursor].hp, enemies[menuCursor].maxHp),
                sw/2 - 70, 38, 12, (Color){200,200,200,255});
        }
        if (menuState == MENU_TARGET_ALLY && menuCursor < MAX_PARTY) {
            DrawRectangle(sw/2 - 80, 15, 160, 30, (Color){10,10,30,220});
            DrawText(party[menuCursor].name, sw/2 - 70, 20, 18, WHITE);
        }

        // Potions count
        DrawText(TextFormat("Potions: %d", potions), sw - 120, panelY + 10, 14, (Color){150,200,150,255});

        // Battle log (right side)
        for (int i = 0; i < logCount; i++) {
            if (logTimers[i] <= 0) continue;
            logTimers[i] -= dt;
            float alpha = Clamp(logTimers[i] / 1.0f, 0, 1);
            if (logTimers[i] > 3.0f) alpha = 1.0f;
            Color lc = {200, 200, 200, (unsigned char)(alpha * 220)};
            DrawText(battleLog[i], sw - 320, panelY - 20 - i * 16, 12, lc);
        }

        // Win/Lose
        if (battleWon) {
            DrawRectangle(sw/2 - 150, sh/2 - 60, 300, 130, (Color){0,0,0,220});
            DrawRectangleLinesEx((Rectangle){sw/2-150, sh/2-60, 300, 130}, 2, GOLD);
            DrawText("VICTORY!", sw/2 - 60, sh/2 - 45, 30, GOLD);
            DrawText(TextFormat("EXP: %d", xpGained), sw/2 - 40, sh/2, 20, WHITE);
            DrawText(TextFormat("Gil: %d", gilGained), sw/2 - 40, sh/2 + 25, 20, YELLOW);
            if (battleEndTimer > 2.0f)
                DrawText("Press R to replay", sw/2 - 70, sh/2 + 55, 14, (Color){150,150,150,255});
        }
        if (battleLost) {
            DrawRectangle(sw/2 - 100, sh/2 - 30, 200, 70, (Color){0,0,0,220});
            DrawText("GAME OVER", sw/2 - 75, sh/2 - 20, 28, RED);
            if (battleEndTimer > 2.0f)
                DrawText("Press R to retry", sw/2 - 65, sh/2 + 15, 14, (Color){150,150,150,255});
        }

        // Restart
        if ((battleWon || battleLost) && battleEndTimer > 2.0f && IsKeyPressed(KEY_R)) {
            InitBattle();
        }

        // Controls
        DrawText("Up/Down: Navigate  Enter/Space: Select  Esc: Back", 10, 10, 11, (Color){80,80,100,180});

        DrawFPS(sw - 80, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
