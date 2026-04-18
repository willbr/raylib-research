#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// 1080 Snowboarding style: downhill racing on a procedural slope
// with jumps, rails, trees, trick system, and chase camera

#define SLOPE_LENGTH   400.0f
#define SLOPE_WIDTH     40.0f
#define NUM_TREES       60
#define NUM_JUMPS       10
#define NUM_RAILS        6
#define NUM_GATES       15
#define NUM_OPPONENTS    3

// Physics
#define GRAVITY         25.0f
#define MAX_SPEED       45.0f
#define ACCEL            8.0f
#define BRAKE_DECEL     15.0f
#define TURN_SPEED       2.5f
#define CARVE_TURN       3.5f
#define CARVE_DRAG       0.97f
#define AIR_TURN         1.5f
#define JUMP_FORCE      12.0f
#define GRIND_SPEED     30.0f

// Trick scoring
#define TRICK_POINTS_GRAB   200
#define TRICK_POINTS_SPIN   300
#define TRICK_POINTS_FLIP   500
#define TRICK_POINTS_1080  1080
#define TRICK_POINTS_GRIND  150

typedef enum {
    TRICK_NONE = 0,
    TRICK_GRAB_INDY,
    TRICK_GRAB_METHOD,
    TRICK_SPIN_360,
    TRICK_SPIN_720,
    TRICK_SPIN_1080,
    TRICK_BACKFLIP,
    TRICK_GRIND,
} TrickType;

static const char *trickNames[] = {
    "", "Indy Grab", "Method Air", "360 Spin", "720 Spin",
    "1080!", "Backflip", "Rail Grind"
};
static const int trickScores[] = {
    0, 200, 250, 300, 500, 1080, 500, 150
};

// Firework particles
#define MAX_FIREWORKS 200
typedef struct {
    Vector3 pos;
    Vector3 vel;
    Color color;
    float life;
    float maxLife;
    float size;
    bool active;
} Firework;
static Firework fireworks[MAX_FIREWORKS];
static float fireworkTimer = 0;

void SpawnFireworkBurst(Vector3 origin) {
    Color colors[] = { RED, YELLOW, GREEN, BLUE, ORANGE, PURPLE, WHITE, SKYBLUE, PINK, GOLD };
    Color burstCol = colors[GetRandomValue(0, 9)];
    for (int i = 0; i < 30; i++) {
        for (int j = 0; j < MAX_FIREWORKS; j++) {
            if (fireworks[j].active) continue;
            float a1 = (float)GetRandomValue(0, 628) / 100.0f;
            float a2 = (float)GetRandomValue(-314, 314) / 200.0f;
            float spd = (float)GetRandomValue(3, 12);
            fireworks[j].pos = origin;
            fireworks[j].vel = (Vector3){
                cosf(a1)*cosf(a2)*spd,
                sinf(a2)*spd + 3.0f,
                sinf(a1)*cosf(a2)*spd
            };
            fireworks[j].color = burstCol;
            fireworks[j].color.r += GetRandomValue(-30, 30);
            fireworks[j].color.g += GetRandomValue(-30, 30);
            fireworks[j].life = (float)GetRandomValue(50, 150) / 100.0f;
            fireworks[j].maxLife = fireworks[j].life;
            fireworks[j].size = (float)GetRandomValue(5, 15) / 100.0f;
            fireworks[j].active = true;
            break;
        }
    }
}

typedef struct {
    Vector3 pos;
    float rotation;     // yaw
    float speed;
    float velY;         // vertical velocity
    bool airborne;
    bool grinding;
    float grindDir;
    bool carving;       // holding carve for sharper turns
    int score;
    int comboMult;
    float comboTimer;
    int comboScore;
    TrickType lastTrick;
    float trickSpin;    // accumulated spin in air
    float trickFlip;    // accumulated flip in air
    bool trickGrab;     // holding grab
    float displayTimer;
    char displayText[48];
    bool finished;
    bool isPlayer;
    float aiTimer;
    float aiTurnTarget;
    Color color;
} Rider;

typedef struct { Vector3 pos; float width; float height; } Jump;
typedef struct { Vector3 start; Vector3 end; } Rail;
typedef struct { Vector3 pos; float radius; } TreeObj;
typedef struct { Vector3 pos; float width; bool passed; } Gate;

static Jump jumps[NUM_JUMPS];
static Rail rails[NUM_RAILS];
static TreeObj trees[NUM_TREES];
static Gate gates[NUM_GATES];

// Slope height: gentle downhill with undulations
float SlopeHeight(float x, float z) {
    // Main downhill gradient — rider goes in -Z, height decreases
    float h = z * 0.6f;
    // Undulations
    h += sinf(z * 0.05f) * 3.0f;
    h += cosf(z * 0.08f + x * 0.1f) * 1.5f;
    // Banked edges
    float edge = fabsf(x) / (SLOPE_WIDTH / 2.0f);
    if (edge > 0.7f) h += (edge - 0.7f) * 8.0f;
    return h;
}

Vector3 SlopeNormal(float x, float z) {
    float eps = 0.5f;
    float hc = SlopeHeight(x, z);
    float hx = SlopeHeight(x + eps, z);
    float hz = SlopeHeight(x, z + eps);
    Vector3 dx = { eps, hx - hc, 0 };
    Vector3 dz = { 0, hz - hc, eps };
    return Vector3Normalize(Vector3CrossProduct(dz, dx));
}

void GenerateCourse(void) {
    // Jumps along the slope
    for (int i = 0; i < NUM_JUMPS; i++) {
        float z = -30.0f - i * (SLOPE_LENGTH / NUM_JUMPS);
        float x = (float)GetRandomValue(-15, 15);
        jumps[i].pos = (Vector3){ x, SlopeHeight(x, z), z };
        jumps[i].width = (float)GetRandomValue(4, 8);
        jumps[i].height = (float)GetRandomValue(15, 30) / 10.0f;
    }

    // Rails
    for (int i = 0; i < NUM_RAILS; i++) {
        float z = -50.0f - i * (SLOPE_LENGTH / NUM_RAILS);
        float x = (float)GetRandomValue(-12, 12);
        float len = (float)GetRandomValue(8, 16);
        rails[i].start = (Vector3){ x, SlopeHeight(x, z) + 0.5f, z };
        rails[i].end = (Vector3){ x + (float)GetRandomValue(-3, 3),
                                  SlopeHeight(x, z - len) + 0.5f, z - len };
    }

    // Trees along edges
    for (int i = 0; i < NUM_TREES; i++) {
        float z = -(float)GetRandomValue(10, (int)SLOPE_LENGTH);
        float side = (GetRandomValue(0, 1) == 0) ? -1.0f : 1.0f;
        float x = side * ((SLOPE_WIDTH / 2.0f) + (float)GetRandomValue(1, 8));
        trees[i].pos = (Vector3){ x, SlopeHeight(x, z), z };
        trees[i].radius = (float)GetRandomValue(8, 15) / 10.0f;
    }

    // Gates (slalom)
    for (int i = 0; i < NUM_GATES; i++) {
        float z = -20.0f - i * (SLOPE_LENGTH / NUM_GATES);
        float x = sinf(i * 1.3f) * 10.0f;
        gates[i].pos = (Vector3){ x, SlopeHeight(x, z) + 1.5f, z };
        gates[i].width = 6.0f;
        gates[i].passed = false;
    }
}

void DrawSlope(Vector3 camPos) {
    // Draw slope as a grid of quads
    float step = 4.0f;
    float viewDist = 120.0f;
    // Snap to grid so tiles don't slide with camera
    float zStart = floorf((camPos.z + 20) / step) * step;
    float zEnd = camPos.z - viewDist;

    for (float z = zStart; z > zEnd; z -= step) {
        for (float x = -SLOPE_WIDTH / 2; x < SLOPE_WIDTH / 2; x += step) {
            Vector3 p00 = { x, SlopeHeight(x, z), z };
            Vector3 p10 = { x + step, SlopeHeight(x + step, z), z };
            Vector3 p01 = { x, SlopeHeight(x, z - step), z - step };
            Vector3 p11 = { x + step, SlopeHeight(x + step, z - step), z - step };

            // Color based on position (snow with tracks)
            int shade = 200 + (int)(sinf(x * 0.5f + z * 0.3f) * 20);
            if (shade > 240) shade = 240;
            if (shade < 180) shade = 180;
            Color snowCol = { shade, shade, shade + 10, 255 };

            // Darker at edges
            float edgeFade = fabsf(x) / (SLOPE_WIDTH / 2);
            if (edgeFade > 0.7f) {
                int dark = (int)((edgeFade - 0.7f) * 200);
                snowCol.r -= dark > snowCol.r ? snowCol.r : dark;
                snowCol.g -= dark > snowCol.g ? snowCol.g : dark;
            }

            DrawTriangle3D(p00, p10, p01, snowCol);
            DrawTriangle3D(p10, p11, p01, snowCol);
            DrawTriangle3D(p00, p01, p10, snowCol);
            DrawTriangle3D(p10, p01, p11, snowCol);
        }
    }
}

void DrawTree(TreeObj *t) {
    // Trunk
    DrawCylinderEx(t->pos,
        (Vector3){t->pos.x, t->pos.y + t->radius * 3, t->pos.z},
        t->radius * 0.2f, t->radius * 0.15f, 6, (Color){100, 70, 40, 255});
    // Canopy layers
    for (int i = 0; i < 3; i++) {
        float y = t->pos.y + t->radius * (1.5f + i * 0.8f);
        float r = t->radius * (1.2f - i * 0.3f);
        DrawCylinderEx((Vector3){t->pos.x, y, t->pos.z},
            (Vector3){t->pos.x, y + r * 0.8f, t->pos.z},
            r, 0.05f, 8, (Color){30, 80 + i * 20, 30, 255});
    }
}

void DrawJump(Jump *j) {
    float hw = j->width / 2;
    Color rampCol = { 220, 220, 230, 255 };
    // Ramp surface
    Vector3 bl = { j->pos.x - hw, j->pos.y, j->pos.z + 1 };
    Vector3 br = { j->pos.x + hw, j->pos.y, j->pos.z + 1 };
    Vector3 tl = { j->pos.x - hw, j->pos.y + j->height, j->pos.z - 1 };
    Vector3 tr = { j->pos.x + hw, j->pos.y + j->height, j->pos.z - 1 };
    DrawTriangle3D(bl, br, tr, rampCol);
    DrawTriangle3D(bl, tr, tl, rampCol);
    DrawTriangle3D(bl, tr, br, rampCol);
    DrawTriangle3D(bl, tl, tr, rampCol);
    // Lip
    DrawLine3D(tl, tr, (Color){180, 180, 200, 255});
}

void DrawRail(Rail *r) {
    // Posts
    Vector3 postBot1 = r->start; postBot1.y -= 0.5f;
    Vector3 postBot2 = r->end; postBot2.y -= 0.5f;
    DrawCylinderEx(postBot1, r->start, 0.08f, 0.08f, 4, DARKGRAY);
    DrawCylinderEx(postBot2, r->end, 0.08f, 0.08f, 4, DARKGRAY);
    // Rail bar
    DrawCylinderEx(r->start, r->end, 0.06f, 0.06f, 6, (Color){180, 180, 190, 255});
}

void DrawGate(Gate *g) {
    float hw = g->width / 2;
    Color poleCol = g->passed ? GREEN : RED;
    // Two poles
    DrawCylinderEx((Vector3){g->pos.x - hw, g->pos.y - 1.5f, g->pos.z},
                   (Vector3){g->pos.x - hw, g->pos.y + 0.5f, g->pos.z},
                   0.06f, 0.06f, 4, poleCol);
    DrawCylinderEx((Vector3){g->pos.x + hw, g->pos.y - 1.5f, g->pos.z},
                   (Vector3){g->pos.x + hw, g->pos.y + 0.5f, g->pos.z},
                   0.06f, 0.06f, 4, poleCol);
    // Banner
    if (!g->passed) {
        DrawLine3D((Vector3){g->pos.x - hw, g->pos.y, g->pos.z},
                   (Vector3){g->pos.x + hw, g->pos.y, g->pos.z}, poleCol);
    }
}

void DrawRider(Rider *r) {
    if (r->finished && !r->isPlayer) return;
    // Visual rotation includes spin when airborne
    float visualRot = r->rotation + (r->airborne ? r->trickSpin : 0);
    float cs = cosf(visualRot), sn = sinf(visualRot);
    Vector3 p = r->pos;

    // Flip rotation: rotate points around the rider's side axis
    float flipAngle = r->airborne ? r->trickFlip : 0;
    float flipCos = cosf(flipAngle), flipSin = sinf(flipAngle);
    // Forward axis: (sn, 0, -cs), side axis: (cs, 0, sn)
    // Rotate a local offset (fwd, up) around the side axis by flipAngle
    #define FLIP_PT(fwd, up) (Vector3){ \
        p.x + sn * (fwd * flipCos - up * flipSin), \
        p.y + (fwd * flipSin + up * flipCos), \
        p.z - cs * (fwd * flipCos - up * flipSin) }

    // Shadow
    float shadowY = SlopeHeight(p.x, p.z) + 0.05f;
    DrawCircle3D((Vector3){p.x, shadowY, p.z}, 0.8f, (Vector3){1,0,0}, 90, (Color){0,0,0,40});

    // Board
    float boardLen = 0.9f;
    Vector3 boardF = FLIP_PT(boardLen, -0.1f);
    Vector3 boardB = FLIP_PT(-boardLen, -0.1f);
    DrawCylinderEx(boardB, boardF, 0.12f, 0.08f, 4, (Color){60, 60, 70, 255});

    // Body
    Color jacketCol = r->color;
    Vector3 bodyPos = FLIP_PT(0, 0);
    DrawCube(bodyPos, 0.4f, 0.7f, 0.3f, jacketCol);
    // Legs
    Vector3 legPos = FLIP_PT(0, -0.4f);
    DrawCube(legPos, 0.35f, 0.3f, 0.25f, (Color){40, 40, 50, 255});
    // Head + goggles
    Vector3 headPos = FLIP_PT(0.15f, 0.55f);
    DrawSphere(headPos, 0.18f, (Color){220, 190, 150, 255});
    Vector3 gogglePos = FLIP_PT(0.22f, 0.55f);
    DrawSphere(gogglePos, 0.1f, (Color){40, 40, 50, 255});
    #undef FLIP_PT

    // Grab visual
    if (r->trickGrab && r->airborne) {
        DrawLine3D(p, (Vector3){p.x, p.y - 0.3f, p.z}, jacketCol);
    }

    // Grind sparks
    if (r->grinding) {
        for (int s = 0; s < 3; s++) {
            float sx = p.x + (float)GetRandomValue(-3, 3) / 10.0f;
            float sz = p.z + (float)GetRandomValue(-3, 3) / 10.0f;
            DrawSphere((Vector3){sx, p.y - 0.2f, sz}, 0.04f,
                (Color){255, 200 + GetRandomValue(0, 55), 0, 220});
        }
    }
}

int main(void) {
    InitWindow(800, 600, "1080 Snowboarding");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    GenerateCourse();

    // Init riders
    Rider riders[NUM_OPPONENTS + 1];
    Color riderCols[] = { (Color){0,100,220,255}, (Color){220,40,40,255},
                          (Color){40,180,40,255}, (Color){220,180,0,255} };
    for (int i = 0; i <= NUM_OPPONENTS; i++) {
        float x = (float)(i - 1) * 3.0f;
        riders[i] = (Rider){
            .pos = {x, SlopeHeight(x, 0) + 0.3f, 0},
            .rotation = 0,
            .speed = 0,
            .isPlayer = (i == 0),
            .color = riderCols[i],
        };
    }

    Camera3D camera = { 0 };
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 60;
    camera.projection = CAMERA_PERSPECTIVE;

    float raceTimer = 0;
    bool raceStarted = false;
    float countdown = 3.99f;
    int playerRank = 1;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        // Countdown
        countdown -= dt;
        if (!raceStarted && countdown <= 0) raceStarted = true;
        if (raceStarted && !riders[0].finished) raceTimer += dt;

        // --- Update riders ---
        for (int ri = 0; ri <= NUM_OPPONENTS; ri++) {
            Rider *r = &riders[ri];
            if (r->finished) continue;

            float accel = 0, steer = 0;

            if (!raceStarted) goto move_rider;

            if (r->isPlayer) {
                // Air tricks (checked first so inputs don't also accelerate)
                if (r->airborne) {
                    r->trickGrab = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_E);

                    // Spin
                    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) r->trickSpin -= 6.0f * dt;
                    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) r->trickSpin += 6.0f * dt;

                    // Flip (grab + W/S)
                    if (r->trickGrab && (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)))
                        r->trickFlip -= 8.0f * dt;
                    if (r->trickGrab && (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)))
                        r->trickFlip += 8.0f * dt;
                } else {
                    // Ground controls only
                    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) accel = ACCEL;
                    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) accel = -BRAKE_DECEL;
                    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) steer = -TURN_SPEED;
                    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) steer = TURN_SPEED;

                    r->carving = IsKeyDown(KEY_LEFT_SHIFT);
                    if (r->carving && fabsf(steer) > 0.1f)
                        steer = (steer > 0 ? CARVE_TURN : -CARVE_TURN);
                }

                // Jump
                if (IsKeyPressed(KEY_SPACE) && !r->airborne) {
                    r->velY = JUMP_FORCE;
                    r->airborne = true;
                }
            } else {
                // AI: go downhill, slight random steering, auto-jump on ramps
                accel = ACCEL * 0.85f;
                r->aiTimer -= dt;
                if (r->aiTimer <= 0) {
                    r->aiTurnTarget = (float)GetRandomValue(-10, 10) / 10.0f;
                    r->aiTimer = (float)GetRandomValue(50, 200) / 100.0f;
                }
                steer = r->aiTurnTarget;

                // Stay on slope
                if (r->pos.x < -SLOPE_WIDTH/2 + 5) steer = -1.5f;
                if (r->pos.x > SLOPE_WIDTH/2 - 5) steer = 1.5f;

                // Rubber banding
                float playerZ = riders[0].pos.z;
                if (r->pos.z < playerZ - 20) accel *= 0.6f;
                else if (r->pos.z > playerZ + 20) accel *= 1.2f;
            }

            // Gravity along slope
            Vector3 normal = SlopeNormal(r->pos.x, r->pos.z);
            float slopeAccel = -normal.z * GRAVITY * 0.5f;  // steeper = faster
            r->speed += (slopeAccel + accel) * dt;

            // Drag
            float drag = r->carving ? CARVE_DRAG : 0.995f;
            r->speed *= drag;
            r->speed = Clamp(r->speed, -5.0f, MAX_SPEED);

            // Steering
            if (!r->airborne) {
                float turnMult = Clamp(r->speed / 15.0f, 0.2f, 1.0f);
                r->rotation += steer * turnMult * dt;
            }
            // Air steering only affects trickSpin (handled above), not rotation/velocity

move_rider:;
            float cs = cosf(r->rotation), sn = sinf(r->rotation);
            // Move downhill (negative Z) plus steering
            r->pos.x += sn * r->speed * dt;
            r->pos.z -= cs * r->speed * dt;

            // Vertical
            if (r->airborne) {
                r->velY -= GRAVITY * dt;
                r->pos.y += r->velY * dt;

                float groundH = SlopeHeight(r->pos.x, r->pos.z);
                if (r->pos.y <= groundH + 0.3f) {
                    r->pos.y = groundH + 0.3f;
                    r->velY = 0;
                    r->airborne = false;

                    // Land tricks
                    if (r->isPlayer) {
                        float totalSpin = fabsf(r->trickSpin);
                        if (totalSpin > 5.5f) {
                            r->lastTrick = TRICK_SPIN_1080;
                            r->comboScore += trickScores[TRICK_SPIN_1080];
                            r->comboMult++;
                            snprintf(r->displayText, sizeof(r->displayText), "1080! x%d", r->comboMult);
                            r->displayTimer = 2.0f;
                        } else if (totalSpin > 3.5f) {
                            r->lastTrick = TRICK_SPIN_720;
                            r->comboScore += trickScores[TRICK_SPIN_720];
                            r->comboMult++;
                            snprintf(r->displayText, sizeof(r->displayText), "720 Spin x%d", r->comboMult);
                            r->displayTimer = 1.5f;
                        } else if (totalSpin > 1.5f) {
                            r->lastTrick = TRICK_SPIN_360;
                            r->comboScore += trickScores[TRICK_SPIN_360];
                            r->comboMult++;
                            snprintf(r->displayText, sizeof(r->displayText), "360 x%d", r->comboMult);
                            r->displayTimer = 1.5f;
                        }
                        if (fabsf(r->trickFlip) > 2.5f) {
                            r->lastTrick = TRICK_BACKFLIP;
                            r->comboScore += trickScores[TRICK_BACKFLIP];
                            r->comboMult++;
                            const char *flipName = (r->trickFlip > 0) ? "Backflip!" : "Frontflip!";
                            snprintf(r->displayText, sizeof(r->displayText), "%s x%d", flipName, r->comboMult);
                            r->displayTimer = 1.5f;
                        }
                        if (r->trickGrab && (totalSpin > 0.5f || fabsf(r->trickFlip) > 0.5f)) {
                            r->comboScore += TRICK_POINTS_GRAB;
                            r->comboMult++;
                        }
                        // Land combo
                        r->score += r->comboScore * r->comboMult;
                        r->comboScore = 0;
                        r->comboMult = 0;
                    }
                    r->trickSpin = 0;
                    r->trickFlip = 0;
                    r->trickGrab = false;
                    r->grinding = false;
                }
            } else {
                r->pos.y = SlopeHeight(r->pos.x, r->pos.z) + 0.3f;
            }

            // Jump ramps
            if (!r->airborne) {
                for (int j = 0; j < NUM_JUMPS; j++) {
                    float dz = r->pos.z - jumps[j].pos.z;
                    float dx = fabsf(r->pos.x - jumps[j].pos.x);
                    if (dz > -1.5f && dz < 0.5f && dx < jumps[j].width / 2 && r->speed > 5) {
                        r->velY = JUMP_FORCE + r->speed * 0.15f;
                        r->airborne = true;
                        break;
                    }
                }
            }

            // Rail grinding
            if (!r->grinding && r->airborne) {
                for (int j = 0; j < NUM_RAILS; j++) {
                    Vector3 closest = Vector3Lerp(rails[j].start, rails[j].end, 0.5f);
                    float d = Vector3Distance(r->pos, closest);
                    float railLen = Vector3Distance(rails[j].start, rails[j].end);
                    if (d < railLen / 2 + 1.0f && fabsf(r->pos.y - closest.y) < 1.0f) {
                        r->grinding = true;
                        r->airborne = false;
                        r->velY = 0;
                        Vector3 railDir = Vector3Normalize(Vector3Subtract(rails[j].end, rails[j].start));
                        r->grindDir = atan2f(railDir.x, -railDir.z);
                        r->pos.y = closest.y + 0.3f;
                        if (r->isPlayer) {
                            r->comboScore += TRICK_POINTS_GRIND;
                            r->comboMult++;
                            r->lastTrick = TRICK_GRIND;
                            snprintf(r->displayText, sizeof(r->displayText), "Rail Grind! x%d", r->comboMult);
                            r->displayTimer = 1.5f;
                        }
                        break;
                    }
                }
            }
            if (r->grinding) {
                r->rotation = r->grindDir;
                // Check if still near any rail
                bool onRail = false;
                for (int j = 0; j < NUM_RAILS; j++) {
                    Vector3 closest = Vector3Lerp(rails[j].start, rails[j].end, 0.5f);
                    float railLen = Vector3Distance(rails[j].start, rails[j].end);
                    if (Vector3Distance(r->pos, closest) < railLen / 2 + 0.5f) {
                        onRail = true;
                        break;
                    }
                }
                if (!onRail) {
                    r->grinding = false;
                    r->airborne = true;
                    r->velY = 3.0f;
                }
            }

            // Gate check
            if (r->isPlayer) {
                for (int g = 0; g < NUM_GATES; g++) {
                    if (gates[g].passed) continue;
                    float dz = r->pos.z - gates[g].pos.z;
                    if (dz < 0 && dz > -2.0f && fabsf(r->pos.x - gates[g].pos.x) < gates[g].width / 2) {
                        gates[g].passed = true;
                        r->score += 50;
                    }
                }
            }

            // Slope boundaries
            r->pos.x = Clamp(r->pos.x, -SLOPE_WIDTH / 2 - 5, SLOPE_WIDTH / 2 + 5);

            // Tree collision
            for (int t = 0; t < NUM_TREES; t++) {
                Vector3 diff = Vector3Subtract(r->pos, trees[t].pos);
                diff.y = 0;
                if (Vector3Length(diff) < trees[t].radius + 0.3f) {
                    r->speed *= 0.3f;
                    Vector3 push = Vector3Normalize(diff);
                    r->pos = Vector3Add(r->pos, Vector3Scale(push, 0.5f));
                }
            }

            // Finish
            if (r->pos.z < -SLOPE_LENGTH && !r->airborne) r->finished = true;

            // Display timer
            if (r->displayTimer > 0) r->displayTimer -= dt;
        }

        // Firework particles update
        for (int i = 0; i < MAX_FIREWORKS; i++) {
            if (!fireworks[i].active) continue;
            fireworks[i].vel.y -= 8.0f * dt;  // gravity
            fireworks[i].pos = Vector3Add(fireworks[i].pos, Vector3Scale(fireworks[i].vel, dt));
            fireworks[i].life -= dt;
            if (fireworks[i].life <= 0) fireworks[i].active = false;
        }

        // Spawn fireworks at finish line when player finishes
        if (riders[0].finished) {
            fireworkTimer -= dt;
            if (fireworkTimer <= 0) {
                float fz = -SLOPE_LENGTH;
                float fx = (float)GetRandomValue(-15, 15);
                float fh = SlopeHeight(fx, fz) + (float)GetRandomValue(5, 15);
                SpawnFireworkBurst((Vector3){fx, fh, fz});
                fireworkTimer = 0.3f + (float)GetRandomValue(0, 30) / 100.0f;
            }
        }

        // Rankings (by Z position, most negative = furthest)
        playerRank = 1;
        for (int i = 1; i <= NUM_OPPONENTS; i++)
            if (riders[i].pos.z < riders[0].pos.z) playerRank++;

        // --- Camera ---
        {
            Rider *p = &riders[0];
            float cs = cosf(p->rotation), sn = sinf(p->rotation);
            // Look ahead down the slope
            Vector3 camTarget = {
                p->pos.x + sn * 5.0f,
                p->pos.y - 2.0f,
                p->pos.z - cs * 5.0f
            };
            float camDist = 8.0f;
            float camH = 4.0f;
            Vector3 camPos = {
                p->pos.x - sn * camDist,
                p->pos.y + camH,
                p->pos.z + cs * camDist
            };
            // Keep camera above terrain
            float groundAtCam = SlopeHeight(camPos.x, camPos.z);
            if (camPos.y < groundAtCam + 2.0f) camPos.y = groundAtCam + 2.0f;
            camera.position = Vector3Lerp(camera.position, camPos, 5.0f * dt);
            camera.target = Vector3Lerp(camera.target, camTarget, 8.0f * dt);
        }

        // --- Draw ---
        BeginDrawing();
        // Sky gradient
        ClearBackground((Color){140, 180, 220, 255});

        BeginMode3D(camera);
            // Distant mountains
            for (int m = 0; m < 8; m++) {
                float mx = -200 + m * 60;
                float mh = 30 + sinf(m * 1.7f) * 15;
                float mz = -SLOPE_LENGTH - 50;
                DrawTriangle3D(
                    (Vector3){mx - 30, SlopeHeight(0, mz), mz},
                    (Vector3){mx, SlopeHeight(0, mz) + mh, mz},
                    (Vector3){mx + 30, SlopeHeight(0, mz), mz},
                    (Color){180, 190, 200, 255});
                DrawTriangle3D(
                    (Vector3){mx - 30, SlopeHeight(0, mz), mz},
                    (Vector3){mx + 30, SlopeHeight(0, mz), mz},
                    (Vector3){mx, SlopeHeight(0, mz) + mh, mz},
                    (Color){180, 190, 200, 255});
            }

            DrawSlope(camera.position);

            // Course objects
            for (int i = 0; i < NUM_JUMPS; i++) DrawJump(&jumps[i]);
            for (int i = 0; i < NUM_RAILS; i++) DrawRail(&rails[i]);
            for (int i = 0; i < NUM_TREES; i++) DrawTree(&trees[i]);
            for (int i = 0; i < NUM_GATES; i++) DrawGate(&gates[i]);

            // Finish line
            {
                float fz = -SLOPE_LENGTH;
                float fw = SLOPE_WIDTH / 2;
                float fh = SlopeHeight(0, fz);
                // Checkered line on ground
                for (int c = 0; c < 16; c++) {
                    float x0 = -fw + c * (SLOPE_WIDTH / 16);
                    float x1 = x0 + (SLOPE_WIDTH / 16);
                    Color chk = (c % 2 == 0) ? WHITE : BLACK;
                    Vector3 a = {x0, SlopeHeight(x0, fz) + 0.05f, fz};
                    Vector3 b = {x1, SlopeHeight(x1, fz) + 0.05f, fz};
                    Vector3 c1 = {x0, SlopeHeight(x0, fz + 1.5f) + 0.05f, fz + 1.5f};
                    Vector3 d = {x1, SlopeHeight(x1, fz + 1.5f) + 0.05f, fz + 1.5f};
                    DrawTriangle3D(a, b, d, chk);
                    DrawTriangle3D(a, d, c1, chk);
                    DrawTriangle3D(a, d, b, chk);
                    DrawTriangle3D(a, c1, d, chk);
                }
                // Banner poles
                float poleH = 5.0f;
                DrawCylinderEx((Vector3){-fw, fh, fz}, (Vector3){-fw, fh + poleH, fz},
                    0.1f, 0.1f, 6, RED);
                DrawCylinderEx((Vector3){fw, fh, fz}, (Vector3){fw, fh + poleH, fz},
                    0.1f, 0.1f, 6, RED);
                // Banner
                DrawTriangle3D(
                    (Vector3){-fw, fh + poleH, fz}, (Vector3){fw, fh + poleH, fz},
                    (Vector3){fw, fh + poleH - 1.5f, fz}, RED);
                DrawTriangle3D(
                    (Vector3){-fw, fh + poleH, fz}, (Vector3){fw, fh + poleH - 1.5f, fz},
                    (Vector3){-fw, fh + poleH - 1.5f, fz}, RED);
                DrawTriangle3D(
                    (Vector3){fw, fh + poleH, fz}, (Vector3){-fw, fh + poleH, fz},
                    (Vector3){fw, fh + poleH - 1.5f, fz}, RED);
                DrawTriangle3D(
                    (Vector3){fw, fh + poleH - 1.5f, fz}, (Vector3){-fw, fh + poleH, fz},
                    (Vector3){-fw, fh + poleH - 1.5f, fz}, RED);
            }

            // Fireworks
            for (int i = 0; i < MAX_FIREWORKS; i++) {
                if (!fireworks[i].active) continue;
                float alpha = fireworks[i].life / fireworks[i].maxLife;
                Color fc = fireworks[i].color;
                fc.a = (unsigned char)(alpha * 255);
                DrawSphere(fireworks[i].pos, fireworks[i].size * alpha + 0.02f, fc);
            }

            // Riders
            for (int i = NUM_OPPONENTS; i >= 0; i--) DrawRider(&riders[i]);

        EndMode3D();

        // --- HUD ---
        // Speed
        DrawRectangle(20, sh - 50, 130, 40, (Color){0,0,0,150});
        DrawText(TextFormat("%.0f km/h", fabsf(riders[0].speed) * 3.6f), 30, sh - 42, 24,
            (Color){0,200,255,255});

        // Score
        DrawRectangle(20, 15, 140, 35, (Color){0,0,0,150});
        DrawText(TextFormat("SCORE: %d", riders[0].score), 30, 22, 20, WHITE);

        // Position
        const char *ordinals[] = {"1st","2nd","3rd","4th"};
        DrawText(ordinals[playerRank - 1], sw - 80, 15, 36, WHITE);

        // Timer
        int mins = (int)raceTimer / 60;
        int secs = (int)raceTimer % 60;
        int ms = (int)(fmodf(raceTimer, 1.0f) * 100);
        DrawText(TextFormat("%d:%02d.%02d", mins, secs, ms), sw - 120, 55, 20, WHITE);

        // Trick display
        if (riders[0].displayTimer > 0) {
            int tw = MeasureText(riders[0].displayText, 28);
            DrawRectangle(sw/2 - tw/2 - 10, 60, tw + 20, 35, (Color){0,0,0,180});
            DrawText(riders[0].displayText, sw/2 - tw/2, 65, 28, YELLOW);
        }

        // Air indicator
        if (riders[0].airborne) {
            DrawText("AIR", sw/2 - 15, sh - 80, 20, (Color){100,200,255,200});
            if (riders[0].trickGrab) DrawText("GRAB!", sw/2 - 22, sh - 60, 18, GREEN);
            float spinDeg = fabsf(riders[0].trickSpin) * RAD2DEG;
            if (spinDeg > 90) DrawText(TextFormat("%.0f°", spinDeg), sw/2 - 20, sh - 100, 16, YELLOW);
        }
        if (riders[0].grinding) DrawText("GRIND!", sw/2 - 25, sh - 80, 20, ORANGE);
        if (riders[0].carving) DrawText("CARVE", sw/2 - 22, sh - 80, 16, (Color){150,200,255,200});

        // Gates passed
        int gatesPassed = 0;
        for (int i = 0; i < NUM_GATES; i++) if (gates[i].passed) gatesPassed++;
        DrawText(TextFormat("Gates: %d/%d", gatesPassed, NUM_GATES), 20, 55, 16, WHITE);

        // Countdown
        if (!raceStarted) {
            int cd = (int)ceilf(countdown);
            if (cd > 0) {
                const char *cdText = TextFormat("%d", cd);
                int cdw = MeasureText(cdText, 80);
                DrawText(cdText, sw/2 - cdw/2, sh/2 - 40, 80, WHITE);
            }
        }
        if (raceStarted && countdown > -0.8f) {
            int gow = MeasureText("GO!", 80);
            DrawText("GO!", sw/2 - gow/2, sh/2 - 40, 80, GREEN);
        }

        // Finish
        if (riders[0].finished) {
            DrawRectangle(sw/2 - 150, sh/2 - 40, 300, 90, (Color){0,0,0,200});
            DrawText(TextFormat("FINISH  %s", ordinals[playerRank-1]), sw/2 - 100, sh/2 - 30, 32, WHITE);
            DrawText(TextFormat("Time: %d:%02d.%02d  Score: %d", mins, secs, ms, riders[0].score),
                sw/2 - 120, sh/2 + 10, 18, (Color){200,200,200,255});
        }

        // Controls
        DrawText("WASD: Ride  Shift: Carve  Space: Jump  Ctrl/E: Grab  Steer in air = spin", 10, sh - 16, 11,
            (Color){80,80,100,200});

        DrawFPS(sw - 80, sh - 25);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
