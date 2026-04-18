#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

// WipEout-style: futuristic anti-gravity racing on a track with
// speed pads, weapons, shields, and AI opponents

#define TRACK_SEGS     80
#define TRACK_WIDTH     8.0f
#define HOVER_HEIGHT    0.6f
#define NUM_SHIPS       4
#define TOTAL_LAPS      3

// Ship physics
#define SHIP_ACCEL     40.0f
#define SHIP_BRAKE     25.0f
#define SHIP_MAX_SPEED 90.0f
#define SHIP_TURN       2.2f
#define SHIP_DRAG       0.992f
#define SHIP_AIR_DRAG   0.985f
#define SHIP_WALL_BOUNCE 0.4f

// Pickups
#define MAX_PICKUPS    16
#define MAX_PROJECTILES 8
#define BOOST_PAD_MULT  1.4f
#define BOOST_TIME      2.0f
#define SHIELD_TIME     4.0f
#define ROCKET_SPEED  120.0f
#define MINE_LIFETIME  20.0f
#define QUAKE_DURATION  2.0f

typedef enum {
    WPN_NONE = 0,
    WPN_ROCKETS,
    WPN_MINES,
    WPN_SHIELD,
    WPN_TURBO,
    WPN_QUAKE,
    WPN_COUNT
} WeaponType;

static const char *wpnNames[] = { "", "ROCKETS", "MINES", "SHIELD", "TURBO", "QUAKE" };

typedef struct {
    Vector3 pos;
    float rotation;     // yaw
    float pitch;        // track pitch (up/down hills)
    float speed;
    float boostTimer;
    float shieldTimer;
    float quakeTimer;   // affected by quake
    float energy;       // 0-100, ship health
    WeaponType weapon;
    int lap;
    int nextWP;
    bool isPlayer;
    bool finished;
    bool eliminated;
    Color color;
    float aiNoise;
} Ship;

typedef struct {
    Vector3 pos;
    bool active;
    float respawn;
    int segIdx;
} Pickup;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    bool active;
    float life;
    int fromShip;
    bool isMine;
} Projectile;

// Track
static Vector3 trackPts[TRACK_SEGS];
static Vector3 trackDirs[TRACK_SEGS];   // forward direction
static Vector3 trackNormals[TRACK_SEGS]; // side normal (horizontal)
static float trackHeights[TRACK_SEGS];   // elevation

// Tweakable camera params (adjustable via in-game sliders)
static float camDistRest = 8.0f;
static float camDistSpeed = 4.0f;    // subtracted at max speed
static float camHeightRest = 4.0f;
static float camHeightSpeed = 1.5f;  // subtracted at max speed
static float fovRest = 70.0f;
static float fovSpeed = -20.0f;      // added at max speed (negative = narrows)
static float camLerp = 5.0f;
static bool showTweaks = false;

static Pickup pickups[MAX_PICKUPS];
static int numPickups = 0;
static Projectile projectiles[MAX_PROJECTILES];

void GenerateTrack(void) {
    // Create a varied circuit with elevation changes
    for (int i = 0; i < TRACK_SEGS; i++) {
        float t = (float)i / TRACK_SEGS * 2.0f * PI;
        // Base shape: rounded rectangle with kinks
        float rx = 120.0f + sinf(t * 2.0f) * 30.0f + cosf(t * 3.0f) * 15.0f;
        float ry = 80.0f + cosf(t * 2.0f) * 20.0f + sinf(t * 5.0f) * 10.0f;
        float x = cosf(t) * rx;
        float z = sinf(t) * ry;

        // Elevation: hills and dips
        float elev = sinf(t * 3.0f) * 8.0f + cosf(t * 5.0f) * 4.0f;
        // Flatten start/finish area
        if (i < 5 || i > TRACK_SEGS - 5) elev *= 0.2f;

        trackPts[i] = (Vector3){ x, elev, z };
        trackHeights[i] = elev;
    }

    // Smooth
    for (int pass = 0; pass < 4; pass++) {
        Vector3 temp[TRACK_SEGS];
        for (int i = 0; i < TRACK_SEGS; i++) {
            int prev = (i - 1 + TRACK_SEGS) % TRACK_SEGS;
            int next = (i + 1) % TRACK_SEGS;
            temp[i] = (Vector3){
                (trackPts[prev].x + trackPts[i].x * 2 + trackPts[next].x) / 4.0f,
                (trackPts[prev].y + trackPts[i].y * 2 + trackPts[next].y) / 4.0f,
                (trackPts[prev].z + trackPts[i].z * 2 + trackPts[next].z) / 4.0f
            };
        }
        for (int i = 0; i < TRACK_SEGS; i++) trackPts[i] = temp[i];
    }

    // Compute directions and normals
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        trackDirs[i] = Vector3Normalize(Vector3Subtract(trackPts[next], trackPts[i]));
        // Horizontal normal (perpendicular in xz plane)
        trackNormals[i] = (Vector3){ -trackDirs[i].z, 0, trackDirs[i].x };
    }

    // Place pickups along track
    numPickups = 0;
    for (int i = 5; i < TRACK_SEGS; i += 5) {
        if (numPickups >= MAX_PICKUPS) break;
        pickups[numPickups].pos = trackPts[i];
        pickups[numPickups].pos.y += 1.5f;
        pickups[numPickups].active = true;
        pickups[numPickups].respawn = 0;
        pickups[numPickups].segIdx = i;
        numPickups++;
    }
}

int NearestSeg(Vector3 pos) {
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < TRACK_SEGS; i++) {
        float d = Vector3Distance(pos, trackPts[i]);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

float DistFromTrackCenter(Vector3 pos) {
    int seg = NearestSeg(pos);
    int next = (seg + 1) % TRACK_SEGS;
    Vector3 segDir = Vector3Subtract(trackPts[next], trackPts[seg]);
    float segLen = Vector3Length(segDir);
    if (segLen < 0.001f) return Vector3Distance(pos, trackPts[seg]);
    segDir = Vector3Scale(segDir, 1.0f / segLen);
    Vector3 toPos = Vector3Subtract(pos, trackPts[seg]);
    float dot = Vector3DotProduct(toPos, segDir);
    Vector3 closest = Vector3Add(trackPts[seg], Vector3Scale(segDir, Clamp(dot, 0, segLen)));
    return Vector3Distance(pos, closest);
}

float TrackHeightAt(Vector3 pos) {
    int seg = NearestSeg(pos);
    int next = (seg + 1) % TRACK_SEGS;
    Vector3 segDir = Vector3Subtract(trackPts[next], trackPts[seg]);
    float segLen = Vector3Length(segDir);
    if (segLen < 0.001f) return trackPts[seg].y;
    Vector3 toPos = Vector3Subtract(pos, trackPts[seg]);
    float t = Clamp(Vector3DotProduct(toPos, Vector3Normalize(segDir)) / segLen, 0, 1);
    return Lerp(trackPts[seg].y, trackPts[next].y, t);
}

float ShipProgress(Ship *s) { return (float)s->lap * TRACK_SEGS + (float)s->nextWP; }

void DrawTrack(void) {
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        Vector3 p0 = trackPts[i], p1 = trackPts[next];
        Vector3 n0 = trackNormals[i], n1 = trackNormals[next];
        float hw = TRACK_WIDTH / 2.0f;

        Vector3 inner0 = Vector3Add(p0, Vector3Scale(n0, -hw));
        Vector3 outer0 = Vector3Add(p0, Vector3Scale(n0, hw));
        Vector3 inner1 = Vector3Add(p1, Vector3Scale(n1, -hw));
        Vector3 outer1 = Vector3Add(p1, Vector3Scale(n1, hw));

        // Track surface
        Color trackCol = (i % 4 < 2) ? (Color){45, 45, 55, 255} : (Color){40, 40, 50, 255};
        DrawTriangle3D(inner0, outer0, inner1, trackCol);
        DrawTriangle3D(outer0, outer1, inner1, trackCol);

        // Side barriers (glowing edges)
        Color barrierCol = (i % 8 < 4) ? (Color){0, 150, 255, 255} : (Color){0, 100, 200, 255};
        Vector3 innerUp0 = inner0; innerUp0.y += 0.5f;
        Vector3 innerUp1 = inner1; innerUp1.y += 0.5f;
        DrawTriangle3D(inner0, innerUp0, inner1, barrierCol);
        DrawTriangle3D(innerUp0, innerUp1, inner1, barrierCol);

        Vector3 outerUp0 = outer0; outerUp0.y += 0.5f;
        Vector3 outerUp1 = outer1; outerUp1.y += 0.5f;
        Color barrierCol2 = (i % 8 < 4) ? (Color){255, 50, 0, 255} : (Color){200, 30, 0, 255};
        DrawTriangle3D(outer0, outer1, outerUp0, barrierCol2);
        DrawTriangle3D(outerUp0, outer1, outerUp1, barrierCol2);

        // Center line (speed stripe)
        if (i % 3 == 0) {
            Vector3 cl0 = Vector3Lerp(inner0, outer0, 0.5f);
            Vector3 cl1 = Vector3Lerp(inner1, outer1, 0.5f);
            cl0.y += 0.02f; cl1.y += 0.02f;
            DrawLine3D(cl0, cl1, (Color){100, 100, 120, 200});
        }
    }

    // Start/finish line
    Vector3 p = trackPts[0];
    Vector3 n = trackNormals[0];
    float hw = TRACK_WIDTH / 2.0f;
    Vector3 left = Vector3Add(p, Vector3Scale(n, -hw));
    Vector3 right = Vector3Add(p, Vector3Scale(n, hw));
    left.y += 0.03f; right.y += 0.03f;
    DrawLine3D(left, right, WHITE);
    // Checkered
    for (int c = 0; c < 8; c++) {
        float t0 = (float)c / 8, t1 = (float)(c+1) / 8;
        Vector3 a = Vector3Lerp(left, right, t0);
        Vector3 b = Vector3Lerp(left, right, t1);
        int next = 1;
        Vector3 fwd = Vector3Scale(trackDirs[0], 1.0f);
        Vector3 a2 = Vector3Add(a, fwd);
        Vector3 b2 = Vector3Add(b, fwd);
        a.y += 0.03f; b.y += 0.03f; a2.y += 0.03f; b2.y += 0.03f;
        Color chk = (c % 2 == 0) ? WHITE : BLACK;
        DrawTriangle3D(a, b, b2, chk);
        DrawTriangle3D(a, b2, a2, chk);
    }
}

void DrawShip(Ship *s) {
    if (s->eliminated) return;

    float cs = cosf(s->rotation), sn = sinf(s->rotation);
    Vector3 p = s->pos;

    // Ship body — sleek wedge shape
    float len = 1.8f, wid = 0.7f, ht = 0.3f;

    // Front point
    Vector3 nose = { p.x + cs * len, p.y + ht * 0.3f, p.z + sn * len };
    // Rear corners
    Vector3 rearL = { p.x - cs * len * 0.5f - sn * wid, p.y + ht, p.z - sn * len * 0.5f + cs * wid };
    Vector3 rearR = { p.x - cs * len * 0.5f + sn * wid, p.y + ht, p.z - sn * len * 0.5f - cs * wid };
    // Bottom rear
    Vector3 rearLB = { rearL.x, p.y - ht * 0.5f, rearL.z };
    Vector3 rearRB = { rearR.x, p.y - ht * 0.5f, rearR.z };
    Vector3 noseB = { nose.x, p.y - ht * 0.5f, nose.z };

    Color shipCol = s->color;
    Color shipDark = { shipCol.r * 3/4, shipCol.g * 3/4, shipCol.b * 3/4, 255 };

    // Top surface
    DrawTriangle3D(nose, rearL, rearR, shipCol);
    // Bottom
    DrawTriangle3D(noseB, rearRB, rearLB, shipDark);
    // Left side
    DrawTriangle3D(nose, noseB, rearL, shipDark);
    DrawTriangle3D(noseB, rearLB, rearL, shipDark);
    // Right side
    DrawTriangle3D(nose, rearR, noseB, shipDark);
    DrawTriangle3D(noseB, rearR, rearRB, shipDark);
    // Rear
    DrawTriangle3D(rearL, rearLB, rearR, (Color){20,20,25,255});
    DrawTriangle3D(rearLB, rearRB, rearR, (Color){20,20,25,255});

    // Engine glow
    Vector3 engineL = Vector3Lerp(rearL, rearLB, 0.5f);
    Vector3 engineR = Vector3Lerp(rearR, rearRB, 0.5f);
    Color glowCol = (s->boostTimer > 0) ? (Color){255,150,0,255} : (Color){0,150,255,200};
    DrawSphere(engineL, 0.15f, glowCol);
    DrawSphere(engineR, 0.15f, glowCol);

    // Shield bubble
    if (s->shieldTimer > 0) {
        Color sc = (Color){0, 200, 255, 80};
        DrawSphereWires(p, 2.0f, 6, 6, sc);
    }

    // Quake wobble effect
    if (s->quakeTimer > 0) {
        DrawSphereWires(p, 1.5f, 4, 4, (Color){255, 255, 0, 60});
    }

    // Shadow on track
    Vector3 shadow = p;
    shadow.y = TrackHeightAt(p) + 0.05f;
    DrawCircle3D(shadow, 1.2f, (Vector3){1,0,0}, 90, (Color){0,0,0,40});
}

void DrawPickups(void) {
    for (int i = 0; i < numPickups; i++) {
        if (!pickups[i].active) continue;
        float bob = sinf((float)GetTime() * 3.0f + i * 1.5f) * 0.3f;
        float spin = (float)GetTime() * 120.0f + i * 45.0f;
        Vector3 p = pickups[i].pos;
        p.y += bob;

        // Weapon pad: rotating diamond
        Color padCol = (Color){255, 200, 0, 220};
        DrawCube(p, 0.8f, 0.8f, 0.8f, padCol);
        DrawCubeWires(p, 0.8f, 0.8f, 0.8f, (Color){255, 150, 0, 255});

        // Arrow pointing up
        DrawLine3D(p, (Vector3){p.x, p.y + 0.8f, p.z}, (Color){255,255,255,150});
    }
}

int main(void) {
    InitWindow(800, 600, "WipEout - Anti-Gravity Racing");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    GenerateTrack();

    // Init ships
    Ship ships[NUM_SHIPS];
    Color shipColors[] = { (Color){0,120,255,255}, (Color){255,30,30,255},
                           (Color){30,200,30,255}, (Color){200,200,0,255} };
    for (int i = 0; i < NUM_SHIPS; i++) {
        float offset = (float)(i - NUM_SHIPS/2) * 2.5f;
        ships[i].pos = Vector3Add(trackPts[0], Vector3Scale(trackNormals[0], offset));
        ships[i].pos.y += HOVER_HEIGHT;
        ships[i].rotation = atan2f(trackDirs[0].z, trackDirs[0].x);
        ships[i].pitch = 0;
        ships[i].speed = 0;
        ships[i].boostTimer = 0;
        ships[i].shieldTimer = 0;
        ships[i].quakeTimer = 0;
        ships[i].energy = 100;
        ships[i].weapon = WPN_NONE;
        ships[i].lap = 0;
        ships[i].nextWP = 1;
        ships[i].isPlayer = (i == 0);
        ships[i].finished = false;
        ships[i].eliminated = false;
        ships[i].color = shipColors[i];
        ships[i].aiNoise = (float)GetRandomValue(-20, 20) / 100.0f;
    }

    Camera3D camera = { 0 };
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 65;
    camera.projection = CAMERA_PERSPECTIVE;

    bool raceOver = false;
    int playerRank = 1;
    float countdown = 3.99f;
    bool raceStarted = false;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        // Countdown
        if (!raceStarted) {
            countdown -= dt;
            if (countdown <= 0) raceStarted = true;
        }

        // --- Update ships ---
        for (int s = 0; s < NUM_SHIPS; s++) {
            Ship *ship = &ships[s];
            if (ship->finished || ship->eliminated) continue;

            float maxSpd = SHIP_MAX_SPEED;
            if (ship->boostTimer > 0) { ship->boostTimer -= dt; maxSpd *= BOOST_PAD_MULT; }
            if (ship->shieldTimer > 0) ship->shieldTimer -= dt;

            // Quake effect: wobble and slow
            if (ship->quakeTimer > 0) {
                ship->quakeTimer -= dt;
                ship->speed *= 0.98f;
                ship->pos.x += sinf((float)GetTime() * 20 + s) * 0.1f;
                ship->pos.z += cosf((float)GetTime() * 20 + s) * 0.1f;
            }

            float accel = 0, steer = 0;

            if (!raceStarted) goto moveShip;

            if (ship->isPlayer) {
                if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    accel =  SHIP_ACCEL;
                if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))  accel = -SHIP_BRAKE;
                if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  steer = -SHIP_TURN;
                if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) steer =  SHIP_TURN;

                // Use weapon
                if (IsKeyPressed(KEY_SPACE) && ship->weapon != WPN_NONE) {
                    float cs = cosf(ship->rotation), sn = sinf(ship->rotation);
                    switch (ship->weapon) {
                        case WPN_ROCKETS:
                            for (int p = 0; p < MAX_PROJECTILES; p++) {
                                if (!projectiles[p].active) {
                                    projectiles[p].pos = ship->pos;
                                    projectiles[p].vel = (Vector3){cs*ROCKET_SPEED, 0, sn*ROCKET_SPEED};
                                    projectiles[p].active = true;
                                    projectiles[p].life = 3.0f;
                                    projectiles[p].fromShip = s;
                                    projectiles[p].isMine = false;
                                    break;
                                }
                            }
                            break;
                        case WPN_MINES:
                            for (int p = 0; p < MAX_PROJECTILES; p++) {
                                if (!projectiles[p].active) {
                                    projectiles[p].pos = (Vector3){
                                        ship->pos.x - cs*3, ship->pos.y, ship->pos.z - sn*3};
                                    projectiles[p].vel = (Vector3){0,0,0};
                                    projectiles[p].active = true;
                                    projectiles[p].life = MINE_LIFETIME;
                                    projectiles[p].fromShip = s;
                                    projectiles[p].isMine = true;
                                    break;
                                }
                            }
                            break;
                        case WPN_SHIELD:
                            ship->shieldTimer = SHIELD_TIME;
                            break;
                        case WPN_TURBO:
                            ship->boostTimer = BOOST_TIME;
                            ship->speed = fmaxf(ship->speed, maxSpd * 1.1f);
                            break;
                        case WPN_QUAKE:
                            // Affect all other ships
                            for (int o = 0; o < NUM_SHIPS; o++) {
                                if (o == s) continue;
                                ships[o].quakeTimer = QUAKE_DURATION;
                            }
                            break;
                        default: break;
                    }
                    ship->weapon = WPN_NONE;
                }

                // Airbrakes for tighter turns
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_LEFT_CONTROL)) {
                    steer *= 1.5f;
                    ship->speed *= 0.995f;
                }
            } else {
                // AI
                Vector3 target = trackPts[ship->nextWP];
                target = Vector3Add(target, Vector3Scale(trackNormals[ship->nextWP], ship->aiNoise * TRACK_WIDTH * 0.3f));
                Vector3 toTarget = Vector3Subtract(target, ship->pos);
                toTarget.y = 0;
                float targetAngle = atan2f(toTarget.z, toTarget.x);
                float angleDiff = targetAngle - ship->rotation;
                while (angleDiff > PI) angleDiff -= 2*PI;
                while (angleDiff < -PI) angleDiff += 2*PI;
                steer = Clamp(angleDiff * 3.0f, -SHIP_TURN, SHIP_TURN);
                accel = SHIP_ACCEL * 0.9f;

                // Rubber banding
                float pProg = ShipProgress(&ships[0]);
                float myProg = ShipProgress(ship);
                if (myProg > pProg + 5) accel *= 0.7f;
                else if (myProg < pProg - 5) accel *= 1.15f;

                // AI use weapons
                if (ship->weapon == WPN_TURBO) { ship->boostTimer = BOOST_TIME; ship->weapon = WPN_NONE; }
                else if (ship->weapon == WPN_SHIELD) { ship->shieldTimer = SHIELD_TIME; ship->weapon = WPN_NONE; }
                else if (ship->weapon == WPN_ROCKETS && GetRandomValue(0,60) == 0) {
                    float cs = cosf(ship->rotation), sn = sinf(ship->rotation);
                    for (int p = 0; p < MAX_PROJECTILES; p++) {
                        if (!projectiles[p].active) {
                            projectiles[p].pos = ship->pos;
                            projectiles[p].vel = (Vector3){cs*ROCKET_SPEED, 0, sn*ROCKET_SPEED};
                            projectiles[p].active = true;
                            projectiles[p].life = 3.0f;
                            projectiles[p].fromShip = s;
                            projectiles[p].isMine = false;
                            break;
                        }
                    }
                    ship->weapon = WPN_NONE;
                }
            }

            // Physics
            ship->speed += accel * dt;
            ship->speed *= SHIP_DRAG;
            ship->speed = Clamp(ship->speed, -15.0f, maxSpd);

            float turnFactor = Clamp(fabsf(ship->speed) / 30.0f, 0.3f, 1.0f);
            ship->rotation += steer * turnFactor * dt;

moveShip:;
            float cs = cosf(ship->rotation), sn = sinf(ship->rotation);
            ship->pos.x += cs * ship->speed * dt;
            ship->pos.z += sn * ship->speed * dt;

            // Hover over track surface
            float trackY = TrackHeightAt(ship->pos);
            float targetY = trackY + HOVER_HEIGHT;
            ship->pos.y += (targetY - ship->pos.y) * 8.0f * dt;

            // Track boundaries
            float trackDist = DistFromTrackCenter(ship->pos);
            float edgeDist = TRACK_WIDTH / 2.0f;
            int seg = NearestSeg(ship->pos);
            Vector3 toCenter = Vector3Subtract(trackPts[seg], ship->pos);
            toCenter.y = 0;
            float toCenterLen = Vector3Length(toCenter);
            if (toCenterLen > 0.01f) toCenter = Vector3Scale(toCenter, 1.0f / toCenterLen);

            if (trackDist > edgeDist) {
                // Hard clamp: push back onto track, one-time speed/energy hit
                ship->pos = Vector3Add(ship->pos, Vector3Scale(toCenter, trackDist - edgeDist + 0.2f));
                ship->speed *= 0.9f;
            } else if (trackDist > edgeDist - 1.5f) {
                // Soft push: gently steer back, no damage
                float pushStrength = (trackDist - (edgeDist - 1.5f)) / 1.5f;
                ship->pos = Vector3Add(ship->pos, Vector3Scale(toCenter, pushStrength * 8.0f * dt));
            }

            // Waypoint progression
            float wpDist = Vector3Distance(ship->pos, trackPts[ship->nextWP]);
            if (wpDist < 12.0f) {
                int prev = ship->nextWP;
                ship->nextWP = (ship->nextWP + 1) % TRACK_SEGS;
                if (ship->nextWP == 0 && prev == TRACK_SEGS - 1) {
                    ship->lap++;
                    if (ship->lap >= TOTAL_LAPS) {
                        ship->finished = true;
                        if (ship->isPlayer) raceOver = true;
                    }
                }
            }

            // Pickup collision
            for (int p = 0; p < numPickups; p++) {
                if (!pickups[p].active) continue;
                if (Vector3Distance(ship->pos, pickups[p].pos) < 3.0f && ship->weapon == WPN_NONE) {
                    ship->weapon = (WeaponType)(GetRandomValue(1, WPN_COUNT - 1));
                    pickups[p].active = false;
                    pickups[p].respawn = 8.0f;
                }
            }

            // Energy depletion = elimination
            if (ship->energy <= 0) {
                ship->eliminated = true;
                if (ship->isPlayer) raceOver = true;
            }
        }

        // Ship-ship collision
        for (int i = 0; i < NUM_SHIPS; i++) {
            if (ships[i].eliminated) continue;
            for (int j = i + 1; j < NUM_SHIPS; j++) {
                if (ships[j].eliminated) continue;
                Vector3 diff = Vector3Subtract(ships[i].pos, ships[j].pos);
                diff.y = 0;
                float dist = Vector3Length(diff);
                if (dist < 2.0f && dist > 0.01f) {
                    Vector3 push = Vector3Scale(Vector3Normalize(diff), (2.0f - dist) * 0.5f);
                    ships[i].pos = Vector3Add(ships[i].pos, push);
                    ships[j].pos = Vector3Subtract(ships[j].pos, push);
                }
            }
        }

        // Projectiles
        for (int p = 0; p < MAX_PROJECTILES; p++) {
            if (!projectiles[p].active) continue;
            if (!projectiles[p].isMine) {
                projectiles[p].pos = Vector3Add(projectiles[p].pos,
                    Vector3Scale(projectiles[p].vel, dt));
            }
            projectiles[p].life -= dt;
            if (projectiles[p].life <= 0) { projectiles[p].active = false; continue; }

            for (int s = 0; s < NUM_SHIPS; s++) {
                if (s == projectiles[p].fromShip && !projectiles[p].isMine) continue;
                if (ships[s].eliminated) continue;
                if (Vector3Distance(projectiles[p].pos, ships[s].pos) < 2.5f) {
                    if (ships[s].shieldTimer > 0) {
                        ships[s].shieldTimer = 0; // absorb hit
                    } else {
                        ships[s].energy -= 10;
                        ships[s].speed *= 0.5f;
                        ships[s].quakeTimer = 0.5f;
                    }
                    projectiles[p].active = false;
                    break;
                }
            }
        }

        // Respawn pickups
        for (int i = 0; i < numPickups; i++) {
            if (!pickups[i].active) {
                pickups[i].respawn -= dt;
                if (pickups[i].respawn <= 0) pickups[i].active = true;
            }
        }

        // Rankings
        int ranks[NUM_SHIPS] = {0,1,2,3};
        for (int i = 0; i < NUM_SHIPS - 1; i++)
            for (int j = 0; j < NUM_SHIPS - 1 - i; j++)
                if (ShipProgress(&ships[ranks[j]]) < ShipProgress(&ships[ranks[j+1]])) {
                    int tmp = ranks[j]; ranks[j] = ranks[j+1]; ranks[j+1] = tmp;
                }
        for (int i = 0; i < NUM_SHIPS; i++)
            if (ranks[i] == 0) { playerRank = i + 1; break; }

        // --- Camera: chase cam ---
        {
            Ship *p = &ships[0];
            float cs = cosf(p->rotation), sn = sinf(p->rotation);
            Vector3 camTarget = p->pos;
            camTarget.y += 1.0f;
            float speedPct = Clamp(fabsf(p->speed) / SHIP_MAX_SPEED, 0, 1.2f);
            float camDist = camDistRest - speedPct * camDistSpeed;
            float camHeight = camHeightRest - speedPct * camHeightSpeed;
            Vector3 camPos = {
                p->pos.x - cs * camDist,
                p->pos.y + camHeight,
                p->pos.z - sn * camDist
            };
            camera.position = Vector3Lerp(camera.position, camPos, camLerp * dt);
            camera.target = Vector3Lerp(camera.target, camTarget, 8.0f * dt);

            float targetFov = fovRest + speedPct * fovSpeed;
            camera.fovy += (targetFov - camera.fovy) * 4.0f * dt;
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){5, 5, 15, 255});

        BeginMode3D(camera);
            // Skybox substitute: ground plane far below
            DrawPlane((Vector3){0, -20, 0}, (Vector2){800, 800}, (Color){10, 10, 20, 255});

            DrawTrack();
            DrawPickups();

            // Projectiles
            for (int p = 0; p < MAX_PROJECTILES; p++) {
                if (!projectiles[p].active) continue;
                if (projectiles[p].isMine) {
                    DrawSphere(projectiles[p].pos, 0.4f, RED);
                    DrawSphereWires(projectiles[p].pos, 0.5f, 4, 4, (Color){255,0,0,100});
                } else {
                    DrawSphere(projectiles[p].pos, 0.2f, (Color){255,200,0,255});
                    // Trail
                    Vector3 trail = Vector3Subtract(projectiles[p].pos,
                        Vector3Scale(projectiles[p].vel, dt * 3));
                    DrawLine3D(trail, projectiles[p].pos, (Color){255,150,0,150});
                }
            }

            // Ships
            for (int s = 0; s < NUM_SHIPS; s++) DrawShip(&ships[s]);

        EndMode3D();

        // --- HUD ---
        // Speed
        float speedKmh = fabsf(ships[0].speed) * 3.6f;
        DrawRectangle(20, sh - 55, 140, 45, (Color){0,0,0,180});
        DrawText(TextFormat("%.0f km/h", speedKmh), 30, sh - 48, 28, (Color){0,200,255,255});

        // Position
        const char *ordinals[] = {"1st","2nd","3rd","4th"};
        DrawText(ordinals[playerRank-1], sw - 80, 15, 40, WHITE);

        // Lap
        int dispLap = ships[0].lap + 1;
        if (dispLap > TOTAL_LAPS) dispLap = TOTAL_LAPS;
        DrawText(TextFormat("LAP %d/%d", dispLap, TOTAL_LAPS), sw - 120, 58, 20, WHITE);

        // Energy bar
        DrawRectangle(20, sh - 80, 140, 16, (Color){0,0,0,180});
        float ePct = Clamp(ships[0].energy / 100.0f, 0, 1);
        Color eCol = ePct > 0.5f ? GREEN : (ePct > 0.25f ? YELLOW : RED);
        DrawRectangle(22, sh - 78, (int)(136 * ePct), 12, eCol);
        DrawText("ENERGY", 25, sh - 78, 10, WHITE);

        // Weapon
        if (ships[0].weapon != WPN_NONE) {
            DrawRectangle(sw/2 - 55, 12, 110, 30, (Color){0,0,0,180});
            DrawText(wpnNames[ships[0].weapon], sw/2 - 45, 18, 18, (Color){255,200,0,255});
        }

        // Shield indicator
        if (ships[0].shieldTimer > 0)
            DrawText("SHIELD", sw/2 - 30, 45, 18, (Color){0,200,255,255});
        if (ships[0].boostTimer > 0)
            DrawText("BOOST", sw/2 - 25, 45, 18, ORANGE);

        // Countdown
        if (!raceStarted) {
            int cd = (int)ceilf(countdown);
            if (cd > 0) {
                const char *cdText = TextFormat("%d", cd);
                int cdw = MeasureText(cdText, 80);
                DrawText(cdText, sw/2-cdw/2, sh/2-40, 80, (Color){0,200,255,255});
            }
        }
        if (raceStarted && countdown > -0.8f) {
            int gow = MeasureText("GO!", 80);
            DrawText("GO!", sw/2-gow/2, sh/2-40, 80, GREEN);
        }

        // Race over
        if (raceOver) {
            DrawRectangle(sw/2-160, sh/2-30, 320, 65, (Color){0,0,0,200});
            if (ships[0].eliminated) {
                DrawText("ELIMINATED", sw/2-80, sh/2-20, 36, RED);
            } else {
                const char *finText = TextFormat("FINISH  %s", ordinals[playerRank-1]);
                int fw = MeasureText(finText, 36);
                DrawText(finText, sw/2-fw/2, sh/2-20, 36, (Color){0,200,255,255});
            }
        }

        // Mini-map
        {
            int mmx = sw - 120, mmy = sh - 120;
            float mmScale = 0.4f;
            DrawRectangle(mmx - 5, mmy - 5, 115, 115, (Color){0,0,0,120});
            for (int i = 0; i < TRACK_SEGS; i++) {
                int next = (i + 1) % TRACK_SEGS;
                Vector2 a = {mmx + 52 + trackPts[i].x * mmScale, mmy + 52 + trackPts[i].z * mmScale};
                Vector2 b = {mmx + 52 + trackPts[next].x * mmScale, mmy + 52 + trackPts[next].z * mmScale};
                DrawLineV(a, b, (Color){60,60,80,200});
            }
            for (int s = 0; s < NUM_SHIPS; s++) {
                if (ships[s].eliminated) continue;
                float mx = mmx + 52 + ships[s].pos.x * mmScale;
                float mz = mmy + 52 + ships[s].pos.z * mmScale;
                DrawCircle(mx, mz, ships[s].isPlayer ? 3 : 2, ships[s].color);
            }
        }

        DrawText("WASD: Fly  Shift: Airbrake  SPACE: Weapon  TAB: Tweaks", 10, sh - 16, 11, (Color){80,80,100,180});

        // Toggle tweaks panel
        if (IsKeyPressed(KEY_TAB)) showTweaks = !showTweaks;

        // Slider helper: draw a slider, handle mouse drag, return new value
        if (showTweaks) {
            int px = 10, py = 30;
            int sliderW = 200, sliderH = 14, gap = 28;
            Vector2 mouse = GetMousePosition();
            bool mouseDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

            DrawRectangle(px - 5, py - 5, sliderW + 120, gap * 7 + 15, (Color){0,0,0,200});
            DrawText("CAMERA TWEAKS", px, py - 2, 12, GOLD);
            py += 18;

            // Macro-like slider drawing
            #define DRAW_SLIDER(label, var, lo, hi) do { \
                DrawText(label, px, py, 10, WHITE); \
                DrawText(TextFormat("%.1f", var), px + sliderW + 5, py, 10, (Color){180,180,180,255}); \
                Rectangle bar = {px, py + 12, sliderW, sliderH}; \
                DrawRectangleRec(bar, (Color){40,40,50,255}); \
                float pct = ((var) - (lo)) / ((hi) - (lo)); \
                int knobX = px + (int)(pct * sliderW); \
                DrawRectangle(px, py + 12, (int)(pct * sliderW), sliderH, (Color){0,150,255,200}); \
                DrawRectangle(knobX - 3, py + 10, 6, sliderH + 4, WHITE); \
                if (mouseDown && mouse.y >= py + 10 && mouse.y <= py + 12 + sliderH + 4 && \
                    mouse.x >= px && mouse.x <= px + sliderW) { \
                    float newPct = (mouse.x - px) / (float)sliderW; \
                    (var) = (lo) + newPct * ((hi) - (lo)); \
                } \
                py += gap; \
            } while(0)

            DRAW_SLIDER("Dist (rest)",   camDistRest,   1.0f, 15.0f);
            DRAW_SLIDER("Dist (speed)",  camDistSpeed,  0.0f, 14.0f);
            DRAW_SLIDER("Height (rest)", camHeightRest, 0.5f, 10.0f);
            DRAW_SLIDER("Height (spd)",  camHeightSpeed,0.0f, 8.0f);
            DRAW_SLIDER("FOV (rest)",    fovRest,       30.0f, 120.0f);
            DRAW_SLIDER("FOV (speed)",   fovSpeed,      -60.0f, 60.0f);
            DRAW_SLIDER("Cam lerp",      camLerp,       1.0f, 20.0f);

            #undef DRAW_SLIDER
        }

        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
