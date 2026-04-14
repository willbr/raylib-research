#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

// Track: oval circuit defined as a series of waypoints
#define TRACK_POINTS   32
#define TRACK_WIDTH    12.0f
#define NUM_KARTS      6
#define MAX_ITEMS      20
#define MAX_PROJECTILES 10
#define ITEM_RESPAWN   8.0f

// Kart physics
#define KART_ACCEL       25.0f
#define KART_BRAKE       30.0f
#define KART_MAX_SPEED   35.0f
#define KART_TURN_SPEED  2.5f
#define KART_FRICTION    0.98f
#define DRIFT_TURN_MULT  1.6f
#define DRIFT_FRICTION   0.985f
#define BOOST_SPEED      50.0f
#define BOOST_DURATION   1.5f

typedef enum {
    ITEM_NONE = 0,
    ITEM_SHELL,
    ITEM_BANANA,
    ITEM_BOOST,
    ITEM_TRIPLE_SHELL,
} ItemKind;

typedef struct {
    Vector3 pos;
    float rotation;    // yaw in radians
    float speed;
    float steerAngle;
    bool drifting;
    float driftTime;   // accumulated drift time for mini-turbo
    float boostTimer;
    ItemKind heldItem;
    int lap;
    int nextWaypoint;
    float lapProgress;  // for ranking
    bool isPlayer;
    bool finished;
    Color color;
} Kart;

typedef struct {
    Vector3 pos;
    ItemKind kind;
    float respawnTimer;
    bool active;
} ItemBox;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    bool active;
    float lifetime;
    int fromKart;  // who fired it
    bool isDropped; // banana on ground
} Projectile;

// Track waypoints (generated as an oval/figure-8 shape)
static Vector3 trackPoints[TRACK_POINTS];
static Vector3 trackNormals[TRACK_POINTS]; // perpendicular for track width

void GenerateTrack(void) {
    // Create a rounded rectangle / stadium shape track
    // Two straight sections connected by semicircles
    float straightLen = 80.0f;
    float radius = 40.0f;

    int idx = 0;
    int ptsPerSection = TRACK_POINTS / 4;

    // Bottom straight (going right)
    for (int i = 0; i < ptsPerSection; i++) {
        float t = (float)i / ptsPerSection;
        trackPoints[idx] = (Vector3){ -straightLen/2 + t * straightLen, 0.0f, -radius };
        idx++;
    }
    // Right semicircle
    for (int i = 0; i < ptsPerSection; i++) {
        float angle = -PI/2 + PI * (float)i / ptsPerSection;
        trackPoints[idx] = (Vector3){ straightLen/2 + cosf(angle) * radius, 0.0f, sinf(angle) * radius };
        idx++;
    }
    // Top straight (going left)
    for (int i = 0; i < ptsPerSection; i++) {
        float t = (float)i / ptsPerSection;
        trackPoints[idx] = (Vector3){ straightLen/2 - t * straightLen, 0.0f, radius };
        idx++;
    }
    // Left semicircle
    for (int i = 0; i < ptsPerSection; i++) {
        float angle = PI/2 + PI * (float)i / ptsPerSection;
        trackPoints[idx] = (Vector3){ -straightLen/2 + cosf(angle) * radius, 0.0f, sinf(angle) * radius };
        idx++;
    }

    // Compute normals (perpendicular to track direction)
    for (int i = 0; i < TRACK_POINTS; i++) {
        int next = (i + 1) % TRACK_POINTS;
        Vector3 dir = Vector3Subtract(trackPoints[next], trackPoints[i]);
        dir = Vector3Normalize(dir);
        trackNormals[i] = (Vector3){ -dir.z, 0.0f, dir.x };
    }
}

// Find nearest track segment and distance from center
int NearestTrackSegment(Vector3 pos, float *outDist) {
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < TRACK_POINTS; i++) {
        float d = Vector3Distance(pos, trackPoints[i]);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    // Calculate perpendicular distance from track center line
    int next = (best + 1) % TRACK_POINTS;
    Vector3 segDir = Vector3Subtract(trackPoints[next], trackPoints[best]);
    Vector3 toPos = Vector3Subtract(pos, trackPoints[best]);
    float segLen = Vector3Length(segDir);
    if (segLen > 0.001f) {
        segDir = Vector3Scale(segDir, 1.0f / segLen);
        float dot = Vector3DotProduct(toPos, segDir);
        Vector3 closest = Vector3Add(trackPoints[best], Vector3Scale(segDir, Clamp(dot, 0, segLen)));
        *outDist = Vector3Distance(pos, closest);
    } else {
        *outDist = bestD;
    }
    return best;
}

float KartLapProgress(Kart *k) {
    return (float)k->lap * TRACK_POINTS + (float)k->nextWaypoint;
}

int CompareKartRank(const void *a, const void *b) {
    float pa = KartLapProgress((Kart *)a);
    float pb = KartLapProgress((Kart *)b);
    return (pa < pb) - (pa > pb);  // descending
}

void DrawKartModel(Kart *k, int rank) {
    float angle = k->rotation * RAD2DEG;

    // Body
    DrawCube(k->pos, 1.2f, 0.5f, 2.0f, k->color);
    DrawCubeWires(k->pos, 1.2f, 0.5f, 2.0f, BLACK);

    // Cockpit
    Vector3 cockpitPos = { k->pos.x, k->pos.y + 0.4f, k->pos.z };
    DrawCube(cockpitPos, 0.8f, 0.4f, 0.8f, k->color);

    // Head
    float fwd_x = sinf(k->rotation);
    float fwd_z = cosf(k->rotation);
    Vector3 headPos = {
        k->pos.x + fwd_x * 0.1f,
        k->pos.y + 0.9f,
        k->pos.z + fwd_z * 0.1f
    };
    DrawSphere(headPos, 0.25f, (Color){ 240, 200, 160, 255 });

    // Wheels
    float sin_r = sinf(k->rotation);
    float cos_r = cosf(k->rotation);
    Vector3 wheelOffsets[] = {
        { -0.7f, -0.2f,  0.8f },
        {  0.7f, -0.2f,  0.8f },
        { -0.7f, -0.2f, -0.8f },
        {  0.7f, -0.2f, -0.8f },
    };
    for (int w = 0; w < 4; w++) {
        Vector3 wo = wheelOffsets[w];
        Vector3 wp = {
            k->pos.x + wo.x * cos_r - wo.z * sin_r,
            k->pos.y + wo.y,
            k->pos.z + wo.x * sin_r + wo.z * cos_r
        };
        DrawSphere(wp, 0.2f, DARKGRAY);
    }

    // Drift sparks
    if (k->drifting && k->driftTime > 0.5f) {
        Color sparkCol = (k->driftTime > 1.5f) ? ORANGE : YELLOW;
        for (int s = 0; s < 3; s++) {
            float sx = k->pos.x - fwd_x * 1.0f + (float)GetRandomValue(-10, 10) / 20.0f;
            float sz = k->pos.z - fwd_z * 1.0f + (float)GetRandomValue(-10, 10) / 20.0f;
            DrawSphere((Vector3){ sx, 0.1f, sz }, 0.08f, sparkCol);
        }
    }

    // Boost flame
    if (k->boostTimer > 0.0f) {
        Vector3 flamePos = {
            k->pos.x - fwd_x * 1.2f,
            k->pos.y + 0.1f,
            k->pos.z - fwd_z * 1.2f
        };
        DrawSphere(flamePos, 0.3f, ORANGE);
        DrawSphere(flamePos, 0.2f, YELLOW);
    }

    // Held item indicator
    if (k->heldItem != ITEM_NONE) {
        Vector3 itemPos = { k->pos.x, k->pos.y + 1.4f, k->pos.z };
        Color itemCol = WHITE;
        switch (k->heldItem) {
            case ITEM_SHELL: itemCol = GREEN; break;
            case ITEM_BANANA: itemCol = YELLOW; break;
            case ITEM_BOOST: itemCol = RED; break;
            case ITEM_TRIPLE_SHELL: itemCol = GREEN; break;
            default: break;
        }
        DrawSphere(itemPos, 0.15f, itemCol);
    }

    // Position number above kart
    if (!k->isPlayer) {
        Vector3 labelPos = { k->pos.x, k->pos.y + 1.8f, k->pos.z };
    }
}

void DrawTrack(void) {
    // Draw track surface as quads between waypoints
    for (int i = 0; i < TRACK_POINTS; i++) {
        int next = (i + 1) % TRACK_POINTS;
        Vector3 p0 = trackPoints[i];
        Vector3 p1 = trackPoints[next];
        Vector3 n0 = trackNormals[i];
        Vector3 n1 = trackNormals[next];

        // Inner and outer edges
        Vector3 inner0 = Vector3Add(p0, Vector3Scale(n0, -TRACK_WIDTH/2));
        Vector3 outer0 = Vector3Add(p0, Vector3Scale(n0,  TRACK_WIDTH/2));
        Vector3 inner1 = Vector3Add(p1, Vector3Scale(n1, -TRACK_WIDTH/2));
        Vector3 outer1 = Vector3Add(p1, Vector3Scale(n1,  TRACK_WIDTH/2));

        // Road surface (two triangles per quad)
        Color roadCol = (i % 4 < 2) ? DARKGRAY : (Color){ 50, 50, 50, 255 };
        DrawTriangle3D(inner0, outer0, inner1, roadCol);
        DrawTriangle3D(outer0, outer1, inner1, roadCol);

        // Curb stripes on edges
        Color curbCol = (i % 2 == 0) ? RED : WHITE;
        float curbW = 0.8f;
        Vector3 curbInner0 = Vector3Add(inner0, Vector3Scale(n0, -curbW));
        Vector3 curbInner1 = Vector3Add(inner1, Vector3Scale(n1, -curbW));
        DrawTriangle3D(curbInner0, inner0, curbInner1, curbCol);
        DrawTriangle3D(inner0, inner1, curbInner1, curbCol);

        Vector3 curbOuter0 = Vector3Add(outer0, Vector3Scale(n0, curbW));
        Vector3 curbOuter1 = Vector3Add(outer1, Vector3Scale(n1, curbW));
        DrawTriangle3D(outer0, curbOuter0, outer1, curbCol);
        DrawTriangle3D(curbOuter0, curbOuter1, outer1, curbCol);
    }

    // Start/finish line
    Vector3 p = trackPoints[0];
    Vector3 n = trackNormals[0];
    Vector3 left = Vector3Add(p, Vector3Scale(n, -TRACK_WIDTH/2));
    Vector3 right = Vector3Add(p, Vector3Scale(n, TRACK_WIDTH/2));
    left.y = right.y = 0.03f;
    DrawLine3D(left, right, WHITE);
}

int main(void) {
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Kart Racing");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    GenerateTrack();

    // Initialize karts
    Kart karts[NUM_KARTS];
    Color kartColors[] = { BLUE, RED, GREEN, YELLOW, PURPLE, ORANGE };
    for (int i = 0; i < NUM_KARTS; i++) {
        float offset = (float)(i - NUM_KARTS/2) * 3.0f;
        Vector3 startNorm = trackNormals[0];
        karts[i].pos = Vector3Add(trackPoints[0], Vector3Scale(startNorm, offset));
        karts[i].pos.y = 0.3f;
        // Face forward along track
        Vector3 dir = Vector3Subtract(trackPoints[1], trackPoints[0]);
        karts[i].rotation = atan2f(dir.x, dir.z);
        karts[i].speed = 0.0f;
        karts[i].steerAngle = 0.0f;
        karts[i].drifting = false;
        karts[i].driftTime = 0.0f;
        karts[i].boostTimer = 0.0f;
        karts[i].heldItem = ITEM_NONE;
        karts[i].lap = 0;
        karts[i].nextWaypoint = 1;
        karts[i].lapProgress = 0.0f;
        karts[i].isPlayer = (i == 0);
        karts[i].finished = false;
        karts[i].color = kartColors[i % 6];
    }

    // Item boxes along track
    ItemBox items[MAX_ITEMS];
    int numItems = 0;
    for (int i = 2; i < TRACK_POINTS; i += 3) {
        if (numItems >= MAX_ITEMS) break;
        // Place item box on track center
        items[numItems].pos = trackPoints[i];
        items[numItems].pos.y = 0.8f;
        items[numItems].kind = ITEM_NONE;
        items[numItems].respawnTimer = 0.0f;
        items[numItems].active = true;
        numItems++;
    }

    // Projectiles
    Projectile projectiles[MAX_PROJECTILES] = { 0 };

    // Camera
    Camera3D camera = { 0 };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    int totalLaps = 3;
    bool raceFinished = false;
    int playerRank = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // --- Update karts ---
        for (int k = 0; k < NUM_KARTS; k++) {
            Kart *kart = &karts[k];
            if (kart->finished) continue;

            float accel = 0.0f;
            float steer = 0.0f;
            float maxSpd = KART_MAX_SPEED;
            float friction = KART_FRICTION;

            if (kart->boostTimer > 0.0f) {
                kart->boostTimer -= dt;
                maxSpd = BOOST_SPEED;
            }

            if (kart->isPlayer) {
                // Player input
                if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    accel =  KART_ACCEL;
                if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  accel = -KART_BRAKE;
                if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  steer =  KART_TURN_SPEED;
                if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) steer = -KART_TURN_SPEED;

                // Drift
                bool driftInput = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_LEFT_CONTROL);
                if (driftInput && fabsf(steer) > 0.1f && kart->speed > 10.0f) {
                    kart->drifting = true;
                    kart->driftTime += dt;
                    steer *= DRIFT_TURN_MULT;
                    friction = DRIFT_FRICTION;
                } else {
                    if (kart->drifting && kart->driftTime > 1.0f) {
                        // Mini-turbo boost on drift release
                        kart->boostTimer = (kart->driftTime > 2.0f) ? 1.5f : 0.8f;
                    }
                    kart->drifting = false;
                    kart->driftTime = 0.0f;
                }

                // Use item
                if (IsKeyPressed(KEY_SPACE) && kart->heldItem != ITEM_NONE) {
                    float fwd_x = sinf(kart->rotation);
                    float fwd_z = cosf(kart->rotation);

                    switch (kart->heldItem) {
                        case ITEM_SHELL:
                        case ITEM_TRIPLE_SHELL:
                            // Fire shell forward
                            for (int p = 0; p < MAX_PROJECTILES; p++) {
                                if (!projectiles[p].active) {
                                    projectiles[p].pos = kart->pos;
                                    projectiles[p].vel = (Vector3){ fwd_x * 40.0f, 0.0f, fwd_z * 40.0f };
                                    projectiles[p].active = true;
                                    projectiles[p].lifetime = 4.0f;
                                    projectiles[p].fromKart = k;
                                    projectiles[p].isDropped = false;
                                    break;
                                }
                            }
                            if (kart->heldItem == ITEM_TRIPLE_SHELL) {
                                kart->heldItem = ITEM_SHELL;  // still have more
                            } else {
                                kart->heldItem = ITEM_NONE;
                            }
                            break;
                        case ITEM_BANANA:
                            // Drop banana behind
                            for (int p = 0; p < MAX_PROJECTILES; p++) {
                                if (!projectiles[p].active) {
                                    projectiles[p].pos = (Vector3){
                                        kart->pos.x - fwd_x * 2.0f,
                                        0.2f,
                                        kart->pos.z - fwd_z * 2.0f
                                    };
                                    projectiles[p].vel = (Vector3){ 0 };
                                    projectiles[p].active = true;
                                    projectiles[p].lifetime = 15.0f;
                                    projectiles[p].fromKart = k;
                                    projectiles[p].isDropped = true;
                                    break;
                                }
                            }
                            kart->heldItem = ITEM_NONE;
                            break;
                        case ITEM_BOOST:
                            kart->boostTimer = BOOST_DURATION;
                            kart->heldItem = ITEM_NONE;
                            break;
                        default: break;
                    }
                }
            } else {
                // AI: steer toward next waypoint
                Vector3 target = trackPoints[kart->nextWaypoint];
                Vector3 toTarget = Vector3Subtract(target, kart->pos);
                toTarget.y = 0;
                float targetAngle = atan2f(toTarget.x, toTarget.z);
                float angleDiff = targetAngle - kart->rotation;

                // Normalize angle
                while (angleDiff > PI)  angleDiff -= 2.0f * PI;
                while (angleDiff < -PI) angleDiff += 2.0f * PI;

                steer = Clamp(angleDiff * 3.0f, -KART_TURN_SPEED, KART_TURN_SPEED);
                accel = KART_ACCEL * 0.85f;

                // AI speed variation based on position (rubber banding)
                float progress = KartLapProgress(kart);
                float playerProgress = KartLapProgress(&karts[0]);
                if (progress > playerProgress + 3.0f) {
                    accel *= 0.7f;  // slow down if ahead
                } else if (progress < playerProgress - 3.0f) {
                    accel *= 1.1f;  // speed up if behind
                }

                // AI use items
                if (kart->heldItem == ITEM_BOOST) {
                    kart->boostTimer = BOOST_DURATION;
                    kart->heldItem = ITEM_NONE;
                } else if (kart->heldItem == ITEM_SHELL || kart->heldItem == ITEM_TRIPLE_SHELL) {
                    // Fire if another kart is ahead and close
                    float fwd_x = sinf(kart->rotation);
                    float fwd_z = cosf(kart->rotation);
                    for (int other = 0; other < NUM_KARTS; other++) {
                        if (other == k) continue;
                        Vector3 toOther = Vector3Subtract(karts[other].pos, kart->pos);
                        float dot = toOther.x * fwd_x + toOther.z * fwd_z;
                        float dist = Vector3Length(toOther);
                        if (dot > 0 && dist < 20.0f) {
                            for (int p = 0; p < MAX_PROJECTILES; p++) {
                                if (!projectiles[p].active) {
                                    projectiles[p].pos = kart->pos;
                                    projectiles[p].vel = (Vector3){ fwd_x * 40.0f, 0.0f, fwd_z * 40.0f };
                                    projectiles[p].active = true;
                                    projectiles[p].lifetime = 4.0f;
                                    projectiles[p].fromKart = k;
                                    projectiles[p].isDropped = false;
                                    break;
                                }
                            }
                            kart->heldItem = ITEM_NONE;
                            break;
                        }
                    }
                } else if (kart->heldItem == ITEM_BANANA) {
                    // Drop banana randomly
                    if (GetRandomValue(0, 100) < 2) {
                        float fwd_x = sinf(kart->rotation);
                        float fwd_z = cosf(kart->rotation);
                        for (int p = 0; p < MAX_PROJECTILES; p++) {
                            if (!projectiles[p].active) {
                                projectiles[p].pos = (Vector3){
                                    kart->pos.x - fwd_x * 2.0f, 0.2f,
                                    kart->pos.z - fwd_z * 2.0f
                                };
                                projectiles[p].vel = (Vector3){ 0 };
                                projectiles[p].active = true;
                                projectiles[p].lifetime = 15.0f;
                                projectiles[p].fromKart = k;
                                projectiles[p].isDropped = true;
                                break;
                            }
                        }
                        kart->heldItem = ITEM_NONE;
                    }
                }
            }

            // Apply physics
            kart->speed += accel * dt;
            kart->speed *= friction;
            kart->speed = Clamp(kart->speed, -10.0f, maxSpd);

            // Steering (speed-dependent)
            float turnFactor = Clamp(kart->speed / 15.0f, 0.3f, 1.0f);
            kart->rotation += steer * turnFactor * dt;

            // Move
            float fwd_x = sinf(kart->rotation);
            float fwd_z = cosf(kart->rotation);
            kart->pos.x += fwd_x * kart->speed * dt;
            kart->pos.z += fwd_z * kart->speed * dt;
            kart->pos.y = 0.3f;

            // Off-track penalty
            float trackDist;
            NearestTrackSegment(kart->pos, &trackDist);
            if (trackDist > TRACK_WIDTH / 2.0f + 2.0f) {
                kart->speed *= 0.95f;  // slow down off track
            }

            // Waypoint progression
            float wpDist = Vector3Distance(kart->pos, trackPoints[kart->nextWaypoint]);
            if (wpDist < 8.0f) {
                int prev = kart->nextWaypoint;
                kart->nextWaypoint = (kart->nextWaypoint + 1) % TRACK_POINTS;
                if (kart->nextWaypoint == 0 && prev == TRACK_POINTS - 1) {
                    kart->lap++;
                    if (kart->lap >= totalLaps) {
                        kart->finished = true;
                        if (kart->isPlayer) raceFinished = true;
                    }
                }
            }

            // Item box collision
            for (int i = 0; i < numItems; i++) {
                if (!items[i].active) continue;
                float d = Vector3Distance(kart->pos, items[i].pos);
                if (d < 2.0f && kart->heldItem == ITEM_NONE) {
                    // Random item (weighted by position)
                    int roll = GetRandomValue(0, 100);
                    if (roll < 30) kart->heldItem = ITEM_SHELL;
                    else if (roll < 55) kart->heldItem = ITEM_BANANA;
                    else if (roll < 80) kart->heldItem = ITEM_BOOST;
                    else kart->heldItem = ITEM_TRIPLE_SHELL;
                    items[i].active = false;
                    items[i].respawnTimer = ITEM_RESPAWN;
                }
            }
        }

        // --- Kart-kart collision ---
        for (int i = 0; i < NUM_KARTS; i++) {
            for (int j = i + 1; j < NUM_KARTS; j++) {
                Vector3 diff = Vector3Subtract(karts[i].pos, karts[j].pos);
                diff.y = 0;
                float dist = Vector3Length(diff);
                if (dist < 1.8f && dist > 0.001f) {
                    Vector3 push = Vector3Scale(Vector3Normalize(diff), (1.8f - dist) * 0.5f);
                    karts[i].pos = Vector3Add(karts[i].pos, push);
                    karts[j].pos = Vector3Subtract(karts[j].pos, push);
                    // Bump speed transfer
                    float avgSpd = (karts[i].speed + karts[j].speed) * 0.5f;
                    karts[i].speed = avgSpd;
                    karts[j].speed = avgSpd;
                }
            }
        }

        // --- Update projectiles ---
        for (int p = 0; p < MAX_PROJECTILES; p++) {
            if (!projectiles[p].active) continue;
            projectiles[p].pos = Vector3Add(projectiles[p].pos, Vector3Scale(projectiles[p].vel, dt));
            projectiles[p].lifetime -= dt;
            if (projectiles[p].lifetime <= 0.0f) {
                projectiles[p].active = false;
                continue;
            }

            // Hit karts
            for (int k = 0; k < NUM_KARTS; k++) {
                if (k == projectiles[p].fromKart && !projectiles[p].isDropped) continue;
                float d = Vector3Distance(projectiles[p].pos, karts[k].pos);
                if (d < 1.5f) {
                    // Hit! Spin out
                    karts[k].speed = -5.0f;
                    karts[k].boostTimer = 0.0f;
                    projectiles[p].active = false;
                    break;
                }
            }
        }

        // --- Respawn item boxes ---
        for (int i = 0; i < numItems; i++) {
            if (!items[i].active) {
                items[i].respawnTimer -= dt;
                if (items[i].respawnTimer <= 0.0f) {
                    items[i].active = true;
                }
            }
        }

        // --- Camera: chase cam behind player kart ---
        {
            Kart *player = &karts[0];
            float fwd_x = sinf(player->rotation);
            float fwd_z = cosf(player->rotation);
            Vector3 camTarget = player->pos;
            camTarget.y = 1.5f;
            Vector3 camPos = {
                player->pos.x - fwd_x * 8.0f,
                player->pos.y + 5.0f,
                player->pos.z - fwd_z * 8.0f
            };
            // Smooth camera
            camera.position = Vector3Lerp(camera.position, camPos, 5.0f * dt);
            camera.target = Vector3Lerp(camera.target, camTarget, 8.0f * dt);
        }

        // --- Compute rankings ---
        Kart ranked[NUM_KARTS];
        for (int i = 0; i < NUM_KARTS; i++) ranked[i] = karts[i];
        qsort(ranked, NUM_KARTS, sizeof(Kart), CompareKartRank);
        // Find player rank
        for (int i = 0; i < NUM_KARTS; i++) {
            if (ranked[i].isPlayer) { playerRank = i + 1; break; }
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){ 100, 180, 100, 255 }); // grass color

        BeginMode3D(camera);
            // Ground
            DrawPlane((Vector3){ 0, -0.01f, 0 }, (Vector2){ 300, 300 }, (Color){ 80, 160, 80, 255 });

            DrawTrack();

            // Item boxes
            for (int i = 0; i < numItems; i++) {
                if (!items[i].active) continue;
                float bob = sinf((float)GetTime() * 3.0f + i) * 0.3f;
                Vector3 boxPos = items[i].pos;
                boxPos.y += bob;
                // Spinning question mark box
                float spin = (float)GetTime() * 120.0f + i * 45.0f;
                DrawCube(boxPos, 1.0f, 1.0f, 1.0f, (Color){ 255, 200, 0, 255 });
                DrawCubeWires(boxPos, 1.0f, 1.0f, 1.0f, (Color){ 200, 100, 0, 255 });
            }

            // Projectiles
            for (int p = 0; p < MAX_PROJECTILES; p++) {
                if (!projectiles[p].active) continue;
                if (projectiles[p].isDropped) {
                    // Banana
                    DrawSphere(projectiles[p].pos, 0.3f, YELLOW);
                } else {
                    // Shell
                    DrawSphere(projectiles[p].pos, 0.35f, GREEN);
                    DrawSphereWires(projectiles[p].pos, 0.35f, 6, 6, DARKGREEN);
                }
            }

            // Karts
            for (int k = 0; k < NUM_KARTS; k++) {
                DrawKartModel(&karts[k], 0);
            }
        EndMode3D();

        // --- HUD ---
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();

        // Position
        const char *ordinals[] = { "1st", "2nd", "3rd", "4th", "5th", "6th" };
        const char *posText = ordinals[playerRank - 1];
        DrawText(posText, sw - 100, 20, 50, WHITE);

        // Lap counter
        int dispLap = karts[0].lap + 1;
        if (dispLap > totalLaps) dispLap = totalLaps;
        DrawText(TextFormat("Lap %d/%d", dispLap, totalLaps), sw - 150, 75, 24, WHITE);

        // Speed
        DrawText(TextFormat("%.0f km/h", fabsf(karts[0].speed) * 3.6f), 20, sh - 50, 24, WHITE);

        // Held item
        if (karts[0].heldItem != ITEM_NONE) {
            const char *itemNames[] = { "", "SHELL", "BANANA", "BOOST", "3x SHELL" };
            DrawRectangle(sw/2 - 50, 15, 100, 35, (Color){ 0, 0, 0, 150 });
            DrawText(itemNames[karts[0].heldItem], sw/2 - 40, 20, 20, WHITE);
        }

        // Mini turbo indicator
        if (karts[0].drifting) {
            Color driftCol = (karts[0].driftTime > 1.5f) ? ORANGE : YELLOW;
            DrawText("DRIFT!", sw/2 - 30, 55, 20, driftCol);
        }
        if (karts[0].boostTimer > 0.0f) {
            DrawText("BOOST!", sw/2 - 30, 55, 20, RED);
        }

        // Finish
        if (raceFinished) {
            const char *finishText = TextFormat("FINISH! You placed %s", ordinals[playerRank - 1]);
            int fw = MeasureText(finishText, 50);
            DrawText(finishText, sw/2 - fw/2, sh/2 - 25, 50, YELLOW);
        }

        // Controls help
        DrawText("WASD: Drive  Shift/Ctrl+Turn: Drift  SPACE: Use Item", 10, sh - 25, 14, WHITE);

        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
