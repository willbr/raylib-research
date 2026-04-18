#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Golden Sun GBA-style battle — fully 2D
// Fake camera rotation by moving/scaling sprites based on orbit angle
// Mode 7-style floor drawn as scanlines

#define MAX_PARTY   4
#define MAX_ENEMIES 3
#define MAX_EFFECTS 15
#define MAX_POPUPS  8
#define MAX_PARTICLES 100

#define CAM_ORBIT_SPEED  0.12f

typedef enum { CMD_NONE, CMD_ATTACK, CMD_FIRE, CMD_HEAL, CMD_QUAKE, CMD_SUMMON } CmdType;
typedef enum { EFF_NONE, EFF_SLASH, EFF_FIRE, EFF_HEAL, EFF_QUAKE, EFF_SUMMON } EffType;
typedef enum { PHASE_SELECT, PHASE_ANIMATE, PHASE_ENEMY, PHASE_WON, PHASE_LOST } Phase;

typedef struct {
    char name[16];
    int hp, maxHp, pp, maxPp;
    int atk, def;
    int djinn;
    bool alive;
    float angle;    // position on battle circle (radians)
    float radius;
    Color color;
    bool isEnemy;
    int storedCmd;
    // Attack animation
    float animTimer;
    float animDuration;
    float targetAngle;
    float targetRadius;
    bool animating;
    // Hurt animation
    float hurtTimer;
    float hurtDuration;
    // Pending damage (applied at impact frame)
    int pendingDmg;
    bool pendingCrit;
    bool hasPending;
} Fighter;

typedef struct {
    EffType type;
    float x, y;     // screen position
    float timer, duration;
    bool active;
} Effect;

typedef struct {
    float x, y;
    int value;
    float timer;
    Color color;
    bool active;
} Popup;

// State
static Fighter party[MAX_PARTY];
static Fighter enemies[MAX_ENEMIES];
static int numEnemies = 0;
static Effect effects[MAX_EFFECTS];
static Popup popups[MAX_POPUPS];

typedef struct {
    float x, y;
    float vx, vy;
    float life, maxLife;
    float size;
    Color color;
    bool active;
} Particle;
static Particle particles[MAX_PARTICLES];

void SpawnDustCloud(float x, float y, int count, float spread) {
    for (int n = 0; n < count; n++) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particles[i].active) continue;
            particles[i].x = x + (float)GetRandomValue(-10, 10) * spread * 0.1f;
            particles[i].y = y + (float)GetRandomValue(-5, 5) * spread * 0.1f;
            particles[i].vx = (float)GetRandomValue(-30, 30) * spread;
            particles[i].vy = (float)GetRandomValue(-20, -5) * spread;
            particles[i].life = 0.3f + (float)GetRandomValue(0, 20) / 100.0f;
            particles[i].maxLife = particles[i].life;
            particles[i].size = 2.0f + (float)GetRandomValue(0, 30) / 10.0f;
            int shade = 150 + GetRandomValue(-30, 30);
            particles[i].color = (Color){ shade, shade - 10, shade - 20, 200 };
            particles[i].active = true;
            break;
        }
    }
}

static float camAngle = 0;        // current orbit angle
static float camTargetAngle = 0;  // target for spin
static bool camSpinning = false;
static float camSpinSpeed = 0;
static float shakeTimer = 0, shakeIntensity = 0;
static float flashTimer = 0;

static Phase phase = PHASE_SELECT;
static int currentChar = 0;
static int menuCursor = 0;
static int targetCursor = 0;
static bool selectingTarget = false;
static CmdType selectedCmd = CMD_NONE;
static float turnDelay = 0;
static int animAction = 0;

// Project fighter to screen — GBA style: fixed rows, just slide left/right
// Party is always on the bottom row, enemies on the upper row
// Camera angle shifts their X positions to fake rotation
Vector2 ProjectToScreen(float angle, float radius, float sw, float sh) {
    float relAngle = angle - camAngle;
    // Horizontal sliding
    float slideX = sinf(relAngle) * sw * 0.3f;
    // Y row based on depth relative to camera: cos > 0 = far side (upper), cos < 0 = near side (lower)
    float depth = cosf(relAngle);
    float baseY = sh * 0.50f - depth * sh * 0.10f;  // far = higher, near = lower
    return (Vector2){ sw / 2.0f + slideX, baseY };
}

float ProjectScale(float angle) {
    // Slight scale variation as they slide — bigger when centered, smaller at edges
    float relAngle = angle - camAngle;
    float center = fabsf(sinf(relAngle));
    return 1.0f - center * 0.15f;
}

float ProjectDepth(float angle) {
    // For draw ordering — characters closer to camera angle center draw last (on top)
    return fabsf(sinf(angle - camAngle));
}

void SpawnPopup(float x, float y, int value, bool heal) {
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (popups[i].active) continue;
        popups[i] = (Popup){ x, y, value, 1.5f, heal ? GREEN : WHITE, true };
        break;
    }
}

void SpawnEffect(EffType type, float x, float y, float dur) {
    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i].active) continue;
        effects[i] = (Effect){ type, x, y, dur, dur, true };
        break;
    }
}

void CamSpinTo(float target, float speed) {
    camTargetAngle = target;
    camSpinSpeed = speed;
    camSpinning = true;
}

void CamShake(float dur, float intensity) {
    shakeTimer = dur;
    shakeIntensity = intensity;
}

void InitBattle(void) {
    const char *names[] = {"Isaac", "Garet", "Ivan", "Mia"};
    Color colors[] = {{220,180,50,255}, {200,60,40,255}, {140,100,200,255}, {80,150,220,255}};
    int hps[] = {280, 320, 200, 220};
    int pps[] = {60, 40, 80, 90};
    int atks[] = {30, 35, 20, 18};
    int defs[] = {20, 25, 12, 15};
    for (int i = 0; i < MAX_PARTY; i++) {
        strncpy(party[i].name, names[i], 15);
        party[i].hp = hps[i]; party[i].maxHp = hps[i];
        party[i].pp = pps[i]; party[i].maxPp = pps[i];
        party[i].atk = atks[i]; party[i].def = defs[i];
        party[i].djinn = 1; party[i].alive = true; party[i].isEnemy = false;
        party[i].angle = PI - 0.6f - 0.3f * i;  // right side when camera at 0
        party[i].radius = 5.0f;
        party[i].color = colors[i];
    }
    numEnemies = 2;
    enemies[0] = (Fighter){ .name = "Lizard Man", .hp = 200, .maxHp = 200,
        .atk = 28, .def = 15, .alive = true, .isEnemy = true,
        .angle = -0.6f, .radius = 4.5f, .color = {60,120,60,255} };
    enemies[1] = (Fighter){ .name = "Skeleton", .hp = 160, .maxHp = 160,
        .atk = 22, .def = 10, .alive = true, .isEnemy = true,
        .angle = -0.9f, .radius = 4.5f, .color = {180,170,150,255} };

    camAngle = 0;  // start facing party (ready to select commands)
    camSpinning = false;
    phase = PHASE_SELECT;
    currentChar = 0; menuCursor = 0;
    selectingTarget = false;
    memset(effects, 0, sizeof(effects));
    memset(popups, 0, sizeof(popups));
    flashTimer = 0;
}

void TriggerHurt(Fighter *f) {
    f->hurtTimer = 0;
    f->hurtDuration = 0.5f;
}

void ExecuteAction(CmdType cmd, int actorIsEnemy, int actorIdx, int targetIdx, int sw, int sh) {
    Fighter *actor = actorIsEnemy ? &enemies[actorIdx] : &party[actorIdx];
    Fighter *target;
    Vector2 tgtScreen;

    switch (cmd) {
        case CMD_ATTACK: {
            target = actorIsEnemy ? &party[targetIdx] : &enemies[targetIdx];
            int dmg = actor->atk * 2 - target->def + GetRandomValue(-5, 5);
            bool crit = GetRandomValue(0, 100) < 12;
            if (crit) dmg = (int)(dmg * 1.8f);
            if (dmg < 1) dmg = 1;
            // Store damage — applied at impact frame
            target->pendingDmg = dmg;
            target->pendingCrit = crit;
            target->hasPending = true;
            // Start jump animation toward target
            actor->animating = true;
            actor->animTimer = 0;
            actor->animDuration = 0.6f;
            actor->targetAngle = target->angle;
            actor->targetRadius = target->radius;
            break;
        }
        case CMD_FIRE: {
            if (actor->pp < 6) break;
            actor->pp -= 6;
            target = actorIsEnemy ? &party[targetIdx] : &enemies[targetIdx];
            tgtScreen = ProjectToScreen(target->angle, target->radius, sw, sh);
            int dmg = actor->atk * 2 + 20 - target->def;
            if (dmg < 1) dmg = 1;
            target->hp -= dmg; if (target->hp < 0) target->hp = 0;
            if (target->hp <= 0) target->alive = false;
            TriggerHurt(target);
            SpawnPopup(tgtScreen.x, tgtScreen.y - 30, dmg, false);
            SpawnEffect(EFF_FIRE, tgtScreen.x, tgtScreen.y, 0.8f);
            CamShake(0.3f, 4);
            break;
        }
        case CMD_HEAL: {
            if (actor->pp < 4) break;
            actor->pp -= 4;
            target = &party[targetIdx];
            tgtScreen = ProjectToScreen(target->angle, target->radius, sw, sh);
            int heal = 60 + GetRandomValue(0, 20);
            target->hp += heal; if (target->hp > target->maxHp) target->hp = target->maxHp;
            SpawnPopup(tgtScreen.x, tgtScreen.y - 30, heal, true);
            SpawnEffect(EFF_HEAL, tgtScreen.x, tgtScreen.y, 0.8f);
            break;
        }
        case CMD_QUAKE: {
            if (actor->pp < 10) break;
            actor->pp -= 10;
            for (int e = 0; e < numEnemies; e++) {
                if (!enemies[e].alive) continue;
                int dmg = actor->atk * 2 + 30 - enemies[e].def;
                if (dmg < 1) dmg = 1;
                enemies[e].hp -= dmg; if (enemies[e].hp < 0) enemies[e].hp = 0;
                if (enemies[e].hp <= 0) enemies[e].alive = false;
                TriggerHurt(&enemies[e]);
                Vector2 sp = ProjectToScreen(enemies[e].angle, enemies[e].radius, sw, sh);
                SpawnPopup(sp.x, sp.y - 30, dmg, false);
                SpawnEffect(EFF_QUAKE, sp.x, sp.y, 1.0f);
            }
            CamShake(0.8f, 10);
            CamSpinTo(camAngle + PI * 2, 6.0f);
            break;
        }
        case CMD_SUMMON: {
            if (actor->djinn < 1) break;
            actor->djinn--;
            for (int e = 0; e < numEnemies; e++) {
                if (!enemies[e].alive) continue;
                int dmg = 80 + actor->atk * 3;
                enemies[e].hp -= dmg; if (enemies[e].hp < 0) enemies[e].hp = 0;
                if (enemies[e].hp <= 0) enemies[e].alive = false;
                TriggerHurt(&enemies[e]);
                Vector2 sp = ProjectToScreen(enemies[e].angle, enemies[e].radius, sw, sh);
                SpawnPopup(sp.x, sp.y - 30, dmg, false);
                SpawnEffect(EFF_SUMMON, sp.x, sp.y, 1.5f);
            }
            CamShake(1.2f, 14);
            CamSpinTo(camAngle + PI * 4, 8.0f);
            flashTimer = 0.5f;
            break;
        }
        default: break;
    }
}

// Draw Mode 7-style floor — GBA affine background
void DrawMode7Floor(int sw, int sh) {
    int horizon = (int)(sh * 0.40f);
    int floorEnd = sh - 100;
    float camCos = cosf(camAngle), camSin = sinf(camAngle);

    for (int y = horizon; y < floorEnd; y++) {
        // Distance from camera: closer at bottom of screen, further at horizon
        // This is the key Mode 7 formula: distance = constant / (y - horizon)
        float screenRow = (float)(y - horizon);
        if (screenRow < 1) continue;
        float distance = 200.0f / screenRow;

        // Tile scale at this row
        float rowScale = distance * 0.15f;

        for (int x = 0; x < sw; x += 2) {
            // Screen X offset from center, scaled by distance
            float sx = (x - sw / 2.0f) * rowScale;
            float sz = distance;

            // Rotate by camera angle
            float wx = sx * camCos - sz * camSin;
            float wz = sx * camSin + sz * camCos;

            // Checkerboard pattern
            int tx = ((int)floorf(wx * 0.5f)) & 1;
            int tz = ((int)floorf(wz * 0.5f)) & 1;
            int shade = ((tx ^ tz) == 0) ? 60 : 45;

            // Fog: further rows (near horizon) are darker
            float fog = Clamp(1.0f - screenRow / (floorEnd - horizon), 0, 0.7f);
            int cr = (int)(shade * (1 - fog) + 15 * fog);
            int cg = (int)((shade + 25) * (1 - fog) + 12 * fog);
            int cb = (int)((shade + 5) * (1 - fog) + 25 * fog);

            DrawRectangle(x, y, 2, 1, (Color){cr, cg, cb, 255});
        }
    }
}

// Draw a 2D sprite character
// Window scale factor (relative to 800x600 reference)
float WinScale(int sw, int sh) {
    float sx = sw / 800.0f, sy = sh / 600.0f;
    return (sx < sy) ? sx : sy;
}

void DrawFighter2D(Fighter *f, int sw, int sh, bool selected) {
    if (!f->alive && !f->animating) return;
    float ws = WinScale(sw, sh);

    // If animating, move sprite in screen space toward target with arc
    Vector2 pos = ProjectToScreen(f->angle, f->radius, sw, sh);
    if (f->animating && f->animDuration > 0) {
        Vector2 targetPos = ProjectToScreen(f->targetAngle, f->targetRadius, sw, sh);
        float t = f->animTimer / f->animDuration;
        if (t < 0.4f) {
            // Leap toward target
            float p = t / 0.4f;
            pos.x += (targetPos.x - pos.x) * p;
            pos.y += (targetPos.y - pos.y) * p;
            pos.y -= sinf(p * PI) * 80 * ws;  // big arc up
        } else if (t < 0.6f) {
            // Hold at target (impact moment)
            pos = targetPos;
        } else {
            // Leap back home
            float p = (t - 0.6f) / 0.4f;
            Vector2 homePos = ProjectToScreen(f->angle, f->radius, sw, sh);
            pos.x = targetPos.x + (homePos.x - targetPos.x) * p;
            pos.y = targetPos.y + (homePos.y - targetPos.y) * p;
            pos.y -= sinf(p * PI) * 50 * ws;  // smaller return arc
        }
    }
    float scale = ProjectScale(f->angle) * ws;
    float depth = ProjectDepth(f->angle);

    // Hurt: step back and flash
    bool isHurt = (f->hurtDuration > 0 && f->hurtTimer < f->hurtDuration);
    if (isHurt) {
        float ht = f->hurtTimer / f->hurtDuration;
        // Step back (away from attacker = toward their own side)
        float knockback = sinf(ht * PI) * 15 * ws;
        float camNorm2 = fmodf(camAngle, 2*PI);
        if (camNorm2 < 0) camNorm2 += 2*PI;
        // Push right if enemies (left side), push left if party (right side)
        pos.x += f->isEnemy ? -knockback : knockback;
        // Flash white every few frames
        if (((int)(f->hurtTimer * 16)) % 2 == 0) return;  // skip drawing = flash
    }

    int sprW = (int)(40 * scale), sprH = (int)(60 * scale);
    int x = (int)pos.x - sprW/2, y = (int)pos.y - sprH;

    Color col = f->color;
    Color dark = {col.r*3/4, col.g*3/4, col.b*3/4, 255};

    // Is this fighter facing the camera?
    // cosf(relAngle) > 0 means they're on the far side of the circle (facing away)
    // cosf(relAngle) < 0 means they're on the near side (facing toward camera)
    float relAngle = f->angle - camAngle;
    float cosRel = cosf(relAngle);
    // Party members face toward enemies (away from their circle position)
    // Enemies face toward party (away from their circle position)
    // So: if on the far side (cosRel > 0), we see their front (they face toward us)
    //     if on the near side (cosRel < 0), we see their back
    bool facingCam = (cosRel > 0);

    // Shadow
    DrawEllipse(pos.x, pos.y + 2*ws, sprW * 0.4f, sprW * 0.15f, (Color){0,0,0,40});

    if (f->isEnemy) {
        if (facingCam) {
            // Front view: body with eyes and mouth
            DrawRectangle(x, y, sprW, sprH, col);
            DrawRectangle(x + (int)(2*ws), y + (int)(2*ws), sprW - (int)(4*ws), sprH - (int)(4*ws), dark);
            int eyeY = y + sprH / 4;
            DrawCircle(x + sprW/3, eyeY, 3 * scale, RED);
            DrawCircle(x + sprW*2/3, eyeY, 3 * scale, RED);
            // Mouth
            DrawRectangle(x + sprW/3, y + sprH*2/5, sprW/3, (int)(3*scale), (Color){40,20,20,255});
        } else {
            // Back view: plain body, no face, darker shade
            Color backCol = {col.r*2/3, col.g*2/3, col.b*2/3, 255};
            DrawRectangle(x, y, sprW, sprH, backCol);
            DrawRectangle(x + (int)(2*ws), y + (int)(2*ws), sprW - (int)(4*ws), sprH - (int)(4*ws),
                (Color){backCol.r*3/4, backCol.g*3/4, backCol.b*3/4, 255});
            // Spine/back detail line
            DrawLine(x + sprW/2, y + (int)(5*ws), x + sprW/2, y + sprH - (int)(5*ws),
                (Color){col.r/2, col.g/2, col.b/2, 100});
        }
        DrawRectangleLines(x, y, sprW, sprH, BLACK);
    } else {
        int headR = (int)(8 * scale);
        int headX = x + sprW/2, headY = y + sprH/6;
        // Legs + body (same for both views)
        DrawRectangle(x + sprW/4, y + sprH*2/3, sprW/2, sprH/3, dark);
        DrawRectangle(x + sprW/6, y + sprH/4, sprW*2/3, sprH/2, col);
        DrawRectangleLines(x + sprW/6, y + sprH/4, sprW*2/3, sprH/2, (Color){0,0,0,100});

        if (facingCam) {
            // Front: skin face, small hair fringe on top, eyes, mouth
            DrawCircle(headX, headY, headR, (Color){230,200,160,255});
            // Hair fringe (just the top arc)
            DrawRectangle(headX - headR, headY - headR, headR*2, headR*2/3, col);
            // Spiky hair tips
            for (int h = 0; h < 3; h++) {
                int hx = headX - headR + headR*2/3 * h + headR/3;
                DrawTriangle(
                    (Vector2){hx - (int)(3*scale), headY - headR/2},
                    (Vector2){hx, headY - headR - (int)(4*scale)},
                    (Vector2){hx + (int)(3*scale), headY - headR/2}, col);
            }
            // Eyes
            DrawCircle(headX - (int)(3*scale), headY + (int)(1*scale), (int)(1.5f*scale), (Color){40,40,60,255});
            DrawCircle(headX + (int)(3*scale), headY + (int)(1*scale), (int)(1.5f*scale), (Color){40,40,60,255});
            // Mouth
            DrawRectangle(headX - (int)(2*scale), headY + (int)(3*scale), (int)(4*scale), (int)(1*scale), (Color){180,120,100,255});
        } else {
            // Back: full hair covering head, no face
            DrawCircle(headX, headY, headR, col);
            Color hairDark = {col.r*3/4, col.g*3/4, col.b*3/4, 255};
            // Hair texture lines
            for (int h = 0; h < 3; h++) {
                int hy = headY - headR/2 + h * headR/2;
                DrawLine(headX - headR/2, hy, headX + headR/2, hy, hairDark);
            }
            // Spiky tips on back of head
            for (int h = 0; h < 3; h++) {
                int hx = headX - headR + headR*2/3 * h + headR/3;
                DrawTriangle(
                    (Vector2){hx - (int)(3*scale), headY - headR/2},
                    (Vector2){hx, headY - headR - (int)(3*scale)},
                    (Vector2){hx + (int)(3*scale), headY - headR/2}, hairDark);
            }
        }
    }

    // Weapons (party members, visible from both sides)
    if (!f->isEnemy) {
        // Mirror weapon to opposite side when seen from back
        // Weapon always points toward enemies
        // Enemies are to the left of the party, so default is -1 (left)
        // When camera rotates 180 for enemy turn, enemies are on the right, so flip
        float camNorm = fmodf(camAngle, 2*PI);
        if (camNorm < 0) camNorm += 2*PI;
        int wpnSide = (camNorm < PI) ? -1 : 1;
        // Weapon swing animation during attack
        float swingAngle = 0;
        if (f->animating && f->animDuration > 0) {
            float t = f->animTimer / f->animDuration;
            if (t > 0.1f && t < 0.45f) {
                // Wind up and swing during the jump approach
                float st = (t - 0.1f) / 0.35f;
                swingAngle = sinf(st * PI) * 200;
            }
        }

        // Weapon origin: on the side facing enemies
        int wpnX = x + sprW/2 + wpnSide * sprW/3;
        int wpnY = y + sprH/3;
        float rad = swingAngle * PI / 180.0f;

        // Weapon type by character index
        int charIdx = 0;
        for (int c = 0; c < MAX_PARTY; c++) if (&party[c] == f) { charIdx = c; break; }

        // All weapons extend in wpnSide direction
        if (charIdx == 0) {
            // Isaac: sword
            int swordLen = (int)(35 * scale);
            int tipX = wpnX + wpnSide * (int)(cosf(rad) * swordLen);
            int tipY = wpnY - (int)(sinf(rad) * swordLen) - swordLen/2;
            DrawLineEx((Vector2){wpnX, wpnY}, (Vector2){tipX, tipY}, 3*scale, (Color){200,200,220,255});
            DrawLineEx((Vector2){wpnX, wpnY}, (Vector2){tipX, tipY}, 1.5f*scale, (Color){240,240,255,255});
            DrawCircle(wpnX, wpnY, 2*scale, (Color){160,140,60,255});
        } else if (charIdx == 1) {
            // Garet: axe
            int axeLen = (int)(30 * scale);
            int tipX = wpnX + wpnSide * (int)(cosf(rad) * axeLen);
            int tipY = wpnY - (int)(sinf(rad) * axeLen) - axeLen/2;
            DrawLineEx((Vector2){wpnX, wpnY}, (Vector2){tipX, tipY}, 2*scale, (Color){120,80,40,255});
            DrawTriangle(
                (Vector2){tipX, tipY - (int)(5*scale)},
                (Vector2){tipX + wpnSide * (int)(8*scale), tipY},
                (Vector2){tipX, tipY + (int)(5*scale)}, (Color){180,180,190,255});
        } else if (charIdx == 2) {
            // Ivan: staff with orb
            int staffLen = (int)(32 * scale);
            int tipX = wpnX + wpnSide * (int)(cosf(rad) * staffLen);
            int tipY = wpnY - (int)(sinf(rad) * staffLen) - staffLen/2;
            DrawLineEx((Vector2){wpnX, wpnY}, (Vector2){tipX, tipY}, 2*scale, (Color){120,80,40,255});
            DrawCircle(tipX, tipY, 3*scale, (Color){140,100,200,255});
        } else {
            // Mia: crystal staff
            int staffLen = (int)(32 * scale);
            int tipX = wpnX + wpnSide * (int)(cosf(rad) * staffLen);
            int tipY = wpnY - (int)(sinf(rad) * staffLen) - staffLen/2;
            DrawLineEx((Vector2){wpnX, wpnY}, (Vector2){tipX, tipY}, 2*scale, (Color){100,80,50,255});
            DrawTriangle(
                (Vector2){tipX, tipY - (int)(4*scale)},
                (Vector2){tipX - (int)(3*scale), tipY},
                (Vector2){tipX + (int)(3*scale), tipY}, (Color){80,180,255,255});
            DrawTriangle(
                (Vector2){tipX, tipY + (int)(4*scale)},
                (Vector2){tipX - (int)(3*scale), tipY},
                (Vector2){tipX + (int)(3*scale), tipY}, (Color){60,140,220,255});
        }
    }

    if (selected) {
        float bounce = sinf((float)GetTime() * 6) * 3 * ws;
        int arrowY = y - (int)(12*ws) + (int)bounce;
        DrawTriangle(
            (Vector2){pos.x, arrowY + 10},
            (Vector2){pos.x - 6, arrowY},
            (Vector2){pos.x + 6, arrowY}, YELLOW);
    }

    // Name above when targeted
    if (selected) {
        int fontSize = (int)(14 * ws);
        int tw = MeasureText(f->name, fontSize);
        DrawText(f->name, (int)pos.x - tw/2, y - (int)(24*ws), fontSize, WHITE);
    }
}

void DrawEffect2D(Effect *e) {
    if (!e->active) return;
    float t = 1.0f - (e->timer / e->duration);
    float alpha = 1.0f - t;

    switch (e->type) {
        case EFF_SLASH: {
            float spread = t * 30;
            Color c = {255,255,200, (unsigned char)(alpha*255)};
            DrawLineEx((Vector2){e->x - spread, e->y - spread},
                       (Vector2){e->x + spread, e->y + spread}, 3, c);
            DrawLineEx((Vector2){e->x + spread, e->y - spread},
                       (Vector2){e->x - spread, e->y + spread}, 3, c);
            if (t < 0.3f) {
                DrawCircle(e->x, e->y, (0.3f-t)*40, (Color){255,255,220,(unsigned char)((0.3f-t)/0.3f*200)});
            }
            break;
        }
        case EFF_FIRE: {
            for (int i = 0; i < 8; i++) {
                float a = i * PI/4 + t * 5;
                float r = t * 25;
                float fy = e->y - t * 40 + sinf(a + t*8) * 8;
                Color fc = {255, (unsigned char)(100+i*20), 0, (unsigned char)(alpha*220)};
                DrawCircle(e->x + cosf(a)*r, fy, 4 * alpha + 1, fc);
            }
            break;
        }
        case EFF_HEAL: {
            for (int i = 0; i < 10; i++) {
                float a = i * PI/5 + t * 3;
                float fy = e->y - t * 50;
                DrawCircle(e->x + cosf(a)*12, fy + sinf(i+t*6)*5, 3, (Color){100,255,150,(unsigned char)(alpha*255)});
            }
            break;
        }
        case EFF_QUAKE: {
            for (int i = 0; i < 6; i++) {
                float a = i * PI/3;
                float r = t * 40;
                DrawLineEx((Vector2){e->x, e->y}, (Vector2){e->x + cosf(a)*r, e->y + sinf(a)*r*0.3f},
                    2, (Color){200,180,100,(unsigned char)(alpha*200)});
            }
            for (int i = 0; i < 4; i++) {
                float bx = e->x + cosf(i*1.5f + t*4) * t * 30;
                float by = e->y - t * 20 + t*t * 40;
                DrawRectangle(bx-3, by-3, 6, 6, (Color){120,100,60,(unsigned char)(alpha*200)});
            }
            break;
        }
        case EFF_SUMMON: {
            // Pillar of light
            float pw = 20 + t * 10;
            float ph = 200 * alpha;
            DrawRectangle(e->x - pw/2, e->y - ph, pw, ph, (Color){255,255,200,(unsigned char)(alpha*100)});
            DrawRectangle(e->x - pw/4, e->y - ph, pw/2, ph, (Color){255,255,230,(unsigned char)(alpha*60)});
            // Orbiting sparkles
            for (int i = 0; i < 8; i++) {
                float a = i * PI/4 + t * 10;
                float r = 15 + t * 5;
                float sy = e->y - t * ph * 0.8f;
                DrawCircle(e->x + cosf(a)*r, sy + sinf(a)*5, 3, (Color){255,240,150,(unsigned char)(alpha*255)});
            }
            break;
        }
        default: break;
    }
}

int main(void) {
    InitWindow(800, 600, "Golden Sun Battle");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();
    InitBattle();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        // Camera orbit
        if (camSpinning) {
            float diff = camTargetAngle - camAngle;
            camAngle += diff * camSpinSpeed * dt;
            if (fabsf(diff) < 0.05f) { camAngle = camTargetAngle; camSpinning = false; }
        }
        // No idle orbit — camera only moves on attacks/events
        if (shakeTimer > 0) shakeTimer -= dt;
        if (flashTimer > 0) flashTimer -= dt;

        // Update attack animations + spawn dust at key moments
        float ws = WinScale(sw, sh);
        for (int fi = 0; fi < MAX_PARTY + numEnemies; fi++) {
            Fighter *f = (fi < MAX_PARTY) ? &party[fi] : &enemies[fi - MAX_PARTY];
            if (!f->animating) continue;
            float prevT = f->animTimer / f->animDuration;
            f->animTimer += dt;
            float newT = f->animTimer / f->animDuration;

            // Spawn dust at takeoff (t crosses 0.02)
            if (prevT < 0.02f && newT >= 0.02f) {
                Vector2 homePos = ProjectToScreen(f->angle, f->radius, sw, sh);
                SpawnDustCloud(homePos.x, homePos.y, 8, ws);
            }
            // Impact frame (t crosses 0.4): apply pending damage + dust
            if (prevT < 0.4f && newT >= 0.4f) {
                Vector2 tgtPos = ProjectToScreen(f->targetAngle, f->targetRadius, sw, sh);
                SpawnDustCloud(tgtPos.x, tgtPos.y, 12, ws * 1.5f);

                // Find the target fighter and apply pending damage
                for (int ti = 0; ti < MAX_PARTY + numEnemies; ti++) {
                    Fighter *tgt = (ti < MAX_PARTY) ? &party[ti] : &enemies[ti - MAX_PARTY];
                    if (!tgt->hasPending) continue;
                    // Check if this target matches the attacker's target
                    if (fabsf(tgt->angle - f->targetAngle) < 0.01f) {
                        tgt->hp -= tgt->pendingDmg;
                        if (tgt->hp < 0) tgt->hp = 0;
                        if (tgt->hp <= 0) tgt->alive = false;
                        TriggerHurt(tgt);
                        SpawnPopup(tgtPos.x, tgtPos.y - 30, tgt->pendingDmg, false);
                        SpawnEffect(EFF_SLASH, tgtPos.x, tgtPos.y, 0.4f);
                        CamShake(0.2f, tgt->pendingCrit ? 8 : 3);
                        tgt->hasPending = false;
                        break;
                    }
                }
            }
            // Spawn dust at return landing (t crosses 0.95)
            if (prevT < 0.95f && newT >= 0.95f) {
                Vector2 homePos = ProjectToScreen(f->angle, f->radius, sw, sh);
                SpawnDustCloud(homePos.x, homePos.y, 6, ws * 0.8f);
            }

            if (f->animTimer >= f->animDuration) {
                f->animating = false;
                f->animTimer = 0;
            }
        }

        // Update hurt timers
        for (int fi = 0; fi < MAX_PARTY + numEnemies; fi++) {
            Fighter *f = (fi < MAX_PARTY) ? &party[fi] : &enemies[fi - MAX_PARTY];
            if (f->hurtDuration > 0) {
                f->hurtTimer += dt;
                if (f->hurtTimer >= f->hurtDuration) f->hurtDuration = 0;
            }
        }

        // Update particles
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            particles[i].x += particles[i].vx * dt;
            particles[i].y += particles[i].vy * dt;
            particles[i].vy += 60.0f * dt;  // gravity
            particles[i].life -= dt;
            if (particles[i].life <= 0) particles[i].active = false;
        }

        // Effects/popups
        for (int i = 0; i < MAX_EFFECTS; i++) {
            if (effects[i].active) { effects[i].timer -= dt; if (effects[i].timer <= 0) effects[i].active = false; }
        }
        for (int i = 0; i < MAX_POPUPS; i++) {
            if (popups[i].active) { popups[i].timer -= dt; popups[i].y -= 30*dt; if (popups[i].timer <= 0) popups[i].active = false; }
        }

        // --- Battle logic (same as before) ---
        if (phase == PHASE_SELECT) {
            while (currentChar < MAX_PARTY && !party[currentChar].alive) currentChar++;
            if (currentChar >= MAX_PARTY) {
                phase = PHASE_ANIMATE; animAction = 0; turnDelay = 0.5f;
            } else if (!selectingTarget) {
                if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) menuCursor = (menuCursor - 1 + 5) % 5;
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) menuCursor = (menuCursor + 1) % 5;
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                    selectedCmd = (CmdType)(menuCursor + 1);
                    selectingTarget = true;
                    if (selectedCmd == CMD_HEAL) targetCursor = currentChar;
                    else { targetCursor = 0; while (targetCursor < numEnemies && !enemies[targetCursor].alive) targetCursor++; }
                }
            } else {
                if (selectedCmd == CMD_HEAL) {
                    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) targetCursor = (targetCursor-1+MAX_PARTY)%MAX_PARTY;
                    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) targetCursor = (targetCursor+1)%MAX_PARTY;
                } else if (selectedCmd != CMD_QUAKE && selectedCmd != CMD_SUMMON) {
                    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) {
                        int t2 = 0; do { targetCursor = (targetCursor-1+numEnemies)%numEnemies; t2++; }
                        while (!enemies[targetCursor].alive && t2 < numEnemies);
                    }
                    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) {
                        int t2 = 0; do { targetCursor = (targetCursor+1)%numEnemies; t2++; }
                        while (!enemies[targetCursor].alive && t2 < numEnemies);
                    }
                }
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                    party[currentChar].storedCmd = selectedCmd * 100 + targetCursor;
                    selectingTarget = false; currentChar++; menuCursor = 0;
                }
                if (IsKeyPressed(KEY_ESCAPE)) selectingTarget = false;
            }
        } else if (phase == PHASE_ANIMATE) {
            if (camSpinning) goto skip_battle;
            {
                bool anyAnim = false;
                for (int i = 0; i < MAX_PARTY; i++) if (party[i].animating) anyAnim = true;
                for (int i = 0; i < numEnemies; i++) if (enemies[i].animating) anyAnim = true;
                if (anyAnim) goto skip_battle;
            }
            turnDelay -= dt;
            if (turnDelay <= 0) {
                while (animAction < MAX_PARTY && !party[animAction].alive) animAction++;
                if (animAction < MAX_PARTY) {
                    int s = party[animAction].storedCmd;
                    CmdType cmd = (CmdType)(s / 100);
                    int tgt = s % 100;
                    // Retarget if target enemy is dead (for single-target attacks)
                    if (cmd == CMD_ATTACK || cmd == CMD_FIRE) {
                        if (tgt >= numEnemies || !enemies[tgt].alive) {
                            // Find next alive enemy
                            tgt = -1;
                            for (int e = 0; e < numEnemies; e++) {
                                if (enemies[e].alive) { tgt = e; break; }
                            }
                        }
                        if (tgt < 0) { animAction++; continue; }  // no enemies left, skip
                    }
                    ExecuteAction(cmd, 0, animAction, tgt, sw, sh);
                    animAction++; turnDelay = 0.1f;
                } else {
                    phase = PHASE_ENEMY; animAction = 0; turnDelay = 0.5f;
                    // Spin camera to face party before enemies attack
                    CamSpinTo(camAngle + PI, 3.5f);
                }
            }
        } else if (phase == PHASE_ENEMY) {
            // Wait for camera spin and any attack animations to finish
            if (camSpinning) goto skip_battle;
            bool anyAnimating = false;
            for (int i = 0; i < numEnemies; i++) if (enemies[i].animating) anyAnimating = true;
            for (int i = 0; i < MAX_PARTY; i++) if (party[i].animating) anyAnimating = true;
            if (anyAnimating) goto skip_battle;
            turnDelay -= dt;
            if (turnDelay <= 0) {
                while (animAction < numEnemies && !enemies[animAction].alive) animAction++;
                if (animAction < numEnemies) {
                    int tgt = 0; int t2 = 0;
                    do { tgt = GetRandomValue(0, MAX_PARTY-1); t2++; } while (!party[tgt].alive && t2 < 10);
                    if (party[tgt].alive) ExecuteAction(CMD_ATTACK, 1, animAction, tgt, sw, sh);
                    animAction++; turnDelay = 0.1f;
                } else {
                    bool ae = true, ap = true;
                    for (int i = 0; i < numEnemies; i++) if (enemies[i].alive) ae = false;
                    for (int i = 0; i < MAX_PARTY; i++) if (party[i].alive) ap = false;
                    if (ae) phase = PHASE_WON; else if (ap) phase = PHASE_LOST;
                    else {
                        phase = PHASE_SELECT; currentChar = 0; menuCursor = 0;
                        // Spin back to face party (camera near 0 to see party fronts)
                        float targetCam = roundf(camAngle / (2*PI)) * 2*PI;  // nearest multiple of 2PI (= 0)
                        CamSpinTo(targetCam, 3.0f);
                    }
                }
            }
        }

skip_battle:
        // --- Draw ---
        BeginDrawing();

        // Sky gradient (GBA-style dark to lighter blue)
        DrawRectangleGradientV(0, 0, sw, sh/2, (Color){15,10,35,255}, (Color){40,30,60,255});
        // Ground area
        DrawRectangle(0, sh/2, sw, sh/2, (Color){25,20,40,255});

        // Screen shake offset
        int shakeX = 0, shakeY = 0;
        if (shakeTimer > 0) {
            shakeX = (int)(sinf(shakeTimer * 40) * shakeIntensity * shakeTimer);
            shakeY = (int)(cosf(shakeTimer * 50) * shakeIntensity * shakeTimer * 0.5f);
        }

        // Mode 7 floor with shake
        DrawMode7Floor(sw, sh);

        // Depth-sort all fighters and draw back to front
        typedef struct { int type; int idx; float depth; } DrawOrder;
        DrawOrder order[MAX_PARTY + MAX_ENEMIES];
        int orderCount = 0;
        for (int i = 0; i < MAX_PARTY; i++) {
            if (!party[i].alive) continue;
            float d = party[i].animating ? -999.0f : ProjectDepth(party[i].angle);
            order[orderCount++] = (DrawOrder){0, i, d};
        }
        for (int i = 0; i < numEnemies; i++) {
            if (!enemies[i].alive) continue;
            float d = enemies[i].animating ? -999.0f : ProjectDepth(enemies[i].angle);
            order[orderCount++] = (DrawOrder){1, i, d};
        }
        // Sort by depth (furthest first)
        for (int i = 0; i < orderCount - 1; i++)
            for (int j = 0; j < orderCount - 1 - i; j++)
                if (order[j].depth < order[j+1].depth) {
                    DrawOrder tmp = order[j]; order[j] = order[j+1]; order[j+1] = tmp;
                }

        // Draw sorted
        for (int i = 0; i < orderCount; i++) {
            if (order[i].type == 0) {
                bool sel = (phase == PHASE_SELECT && order[i].idx == currentChar && !selectingTarget);
                DrawFighter2D(&party[order[i].idx], sw + shakeX, sh + shakeY, sel);
            } else {
                bool sel = (selectingTarget && selectedCmd != CMD_HEAL &&
                           selectedCmd != CMD_QUAKE && selectedCmd != CMD_SUMMON &&
                           order[i].idx == targetCursor);
                DrawFighter2D(&enemies[order[i].idx], sw + shakeX, sh + shakeY, sel);
            }
        }

        // Effects
        for (int i = 0; i < MAX_EFFECTS; i++) DrawEffect2D(&effects[i]);

        // Flash overlay
        if (flashTimer > 0)
            DrawRectangle(0, 0, sw, sh, (Color){255,255,220,(unsigned char)(flashTimer/0.5f * 120)});

        // Particles
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            float alpha = particles[i].life / particles[i].maxLife;
            Color pc = particles[i].color;
            pc.a = (unsigned char)(alpha * pc.a);
            float sz = particles[i].size * (0.5f + alpha * 0.5f);
            DrawCircle((int)particles[i].x, (int)particles[i].y, sz * ws, pc);
        }

        // Popups
        for (int i = 0; i < MAX_POPUPS; i++) {
            if (!popups[i].active) continue;
            float a = Clamp(popups[i].timer, 0, 1);
            Color c = popups[i].color; c.a = (unsigned char)(a * 255);
            DrawText(TextFormat("%d", popups[i].value), popups[i].x, popups[i].y, 24, c);
        }

        // --- HUD (scaled) --- (ws already declared above)

        int panelH = (int)(100*ws), panelY = sh - panelH;
        DrawRectangle(0, panelY, sw, panelH, (Color){20,15,40,235});
        DrawRectangle(0, panelY, sw, (int)(2*ws), (Color){100,80,50,255});

        int colW = sw / MAX_PARTY;
        for (int i = 0; i < MAX_PARTY; i++) {
            int cx = i * colW + (int)(10*ws), cy = panelY + (int)(8*ws);
            Color nc = !party[i].alive ? (Color){80,80,80,255} :
                (phase == PHASE_SELECT && i == currentChar) ? YELLOW : WHITE;
            DrawText(party[i].name, cx, cy, (int)(18*ws), nc);
            float hp = (float)party[i].hp / party[i].maxHp;
            DrawText(TextFormat("HP%d", party[i].hp), cx, cy + (int)(22*ws), (int)(14*ws), WHITE);
            int barW = colW - (int)(25*ws);
            DrawRectangle(cx, cy + (int)(38*ws), barW, (int)(8*ws), (Color){30,25,50,255});
            DrawRectangle(cx, cy + (int)(38*ws), (int)(barW*hp), (int)(8*ws), hp>0.5f ? GREEN : hp>0.25f ? YELLOW : RED);
            DrawText(TextFormat("PP%d", party[i].pp), cx, cy + (int)(50*ws), (int)(14*ws), (Color){100,180,255,255});
            float pp = party[i].maxPp > 0 ? (float)party[i].pp / party[i].maxPp : 0;
            DrawRectangle(cx, cy + (int)(66*ws), barW, (int)(6*ws), (Color){30,25,50,255});
            DrawRectangle(cx, cy + (int)(66*ws), (int)(barW*pp), (int)(6*ws), (Color){80,140,255,255});
            if (party[i].djinn > 0) DrawCircle(cx+colW-(int)(20*ws), cy+(int)(12*ws), (int)(5*ws), (Color){255,200,50,255});
        }

        // Command menu
        if (phase == PHASE_SELECT && currentChar < MAX_PARTY && !selectingTarget) {
            int mw = (int)(145*ws), mh = (int)(140*ws);
            int mx = (int)(15*ws), my = panelY - mh - (int)(5*ws);
            int lineH = (int)(22*ws);
            DrawRectangle(mx, my, mw, mh, (Color){20,15,40,235});
            DrawRectangleLinesEx((Rectangle){mx, my, mw, mh}, (int)(1*ws), (Color){100,80,50,255});
            DrawText(party[currentChar].name, mx+(int)(8*ws), my+(int)(5*ws), (int)(14*ws), YELLOW);
            const char *opts[] = {"Attack","Fire","Heal","Quake","Summon"};
            for (int i = 0; i < 5; i++) {
                int oy = my + (int)(22*ws) + i * lineH;
                if (i == menuCursor) DrawRectangle(mx+(int)(3*ws), oy, mw-(int)(6*ws), lineH, (Color){50,40,80,200});
                DrawText(opts[i], mx+(int)(12*ws), oy+(int)(2*ws), (int)(16*ws), i==menuCursor ? YELLOW : WHITE);
            }
        }

        if (selectingTarget) {
            const char *tt = (selectedCmd == CMD_QUAKE) ? "All Enemies" :
                (selectedCmd == CMD_SUMMON) ? "Summon!" :
                (selectedCmd == CMD_HEAL) ? TextFormat("-> %s", party[targetCursor].name) :
                TextFormat("-> %s", enemies[targetCursor].name);
            int tw = (int)(160*ws);
            DrawRectangle(sw/2-tw/2, panelY-(int)(30*ws), tw, (int)(25*ws), (Color){20,15,40,220});
            DrawText(tt, sw/2-tw/2+(int)(10*ws), panelY-(int)(26*ws), (int)(18*ws), YELLOW);
        }

        for (int i = 0; i < numEnemies; i++) {
            if (!enemies[i].alive) continue;
            int ew = (int)(175*ws), eh = (int)(40*ws);
            int ex = sw-ew-(int)(5*ws), ey = (int)(15*ws)+i*(int)(45*ws);
            DrawRectangle(ex-(int)(5*ws), ey-(int)(2*ws), ew+(int)(10*ws), eh, (Color){20,15,40,200});
            DrawText(enemies[i].name, ex, ey, (int)(16*ws), WHITE);
            float ehp = (float)enemies[i].hp / enemies[i].maxHp;
            int ebw = (int)(160*ws);
            DrawRectangle(ex, ey+(int)(20*ws), ebw, (int)(8*ws), (Color){30,25,50,255});
            DrawRectangle(ex, ey+(int)(20*ws), (int)(ebw*ehp), (int)(8*ws), ehp>0.5f ? GREEN : ehp>0.25f ? YELLOW : RED);
        }

        if (phase == PHASE_WON) {
            int bw = (int)(300*ws), bh = (int)(100*ws);
            DrawRectangle(sw/2-bw/2, sh/2-bh/2, bw, bh, (Color){20,15,40,235});
            DrawRectangleLinesEx((Rectangle){sw/2-bw/2,sh/2-bh/2,bw,bh}, 2, GOLD);
            DrawText("VICTORY!", sw/2-(int)(60*ws), sh/2-(int)(35*ws), (int)(34*ws), GOLD);
            DrawText("EXP: 150  Coins: 80", sw/2-(int)(80*ws), sh/2+(int)(5*ws), (int)(18*ws), WHITE);
            if (IsKeyPressed(KEY_R)) InitBattle();
        }
        if (phase == PHASE_LOST) {
            int lw = (int)(200*ws), lh = (int)(65*ws);
            DrawRectangle(sw/2-lw/2, sh/2-lh/2, lw, lh, (Color){20,15,40,235});
            DrawText("GAME OVER", sw/2-(int)(72*ws), sh/2-(int)(20*ws), (int)(28*ws), RED);
            if (IsKeyPressed(KEY_R)) InitBattle();
        }

        DrawFPS(sw-80, 10);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
