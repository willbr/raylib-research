#include "raylib.h"
#include "raymath.h"
#include "../common/objects3d.h"
#include "../common/util/vehicle.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// Sega Rally style: winding track with surface types, AI, dust effects

#define TRACK_SEGS    64
#define TRACK_WIDTH   10.0f
#define NUM_CARS       4
#define TOTAL_LAPS     3
#define MAX_DUST      80

// Car physics
#define CAR_ACCEL      35.0f
#define CAR_BRAKE      40.0f
#define CAR_MAX_SPEED  45.0f
#define CAR_TURN        2.8f
#define CAR_DRAG_ROAD   0.992f
#define CAR_DRAG_GRAVEL 0.990f
#define CAR_DRAG_MUD    0.985f
#define CAR_DRIFT_MULT  1.6f
#define CAR_DRIFT_DRAG  0.988f

// Surface types per track segment
typedef enum { SURF_TARMAC, SURF_GRAVEL, SURF_MUD } SurfaceType;

typedef struct {
    Vector3 pos;
    float rotation;
    float speed;
    int lap;
    int nextWP;
    int currentSeg;  // cached nearest segment
    bool isPlayer;
    bool finished;
    bool drifting;
    float driftTime;
    float boostTimer;
    Color color;
    float aiNoise;
    float steerInput;
} Car;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float life;
    Color color;
    bool active;
} Dust;

// Car models using objects3d.h
static Part carBody[] = {
    CUBE(0, 0.25f, 0,   1.0f, 0.4f, 1.8f, COL(200,40,40,255)),     // body
    CUBE(0, 0.5f, -0.15f, 0.75f, 0.35f, 0.9f, COL(180,35,35,255)), // cabin
    CUBE(0, 0.5f, -0.15f, 0.55f, 0.3f, 0.7f, COL(140,200,230,255)),// windows
    SPHERE(-0.45f, 0.12f, 0.7f,  0.14f, COL(30,30,30,255)),         // wheel FL
    SPHERE( 0.45f, 0.12f, 0.7f,  0.14f, COL(30,30,30,255)),         // wheel FR
    SPHERE(-0.45f, 0.12f,-0.7f,  0.14f, COL(30,30,30,255)),         // wheel RL
    SPHERE( 0.45f, 0.12f,-0.7f,  0.14f, COL(30,30,30,255)),         // wheel RR
    CUBE(0, 0.05f, -0.95f, 0.4f, 0.08f, 0.1f, COL(60,60,60,255)),  // rear bumper
    SPHERE(-0.35f, 0.3f, 0.9f, 0.06f, COL(255,255,200,255)),        // headlight L
    SPHERE( 0.35f, 0.3f, 0.9f, 0.06f, COL(255,255,200,255)),        // headlight R
    SPHERE(-0.35f, 0.3f,-0.9f, 0.06f, COL(255,30,30,255)),          // taillight L
    SPHERE( 0.35f, 0.3f,-0.9f, 0.06f, COL(255,30,30,255)),          // taillight R
};
static int carBodyCount = sizeof(carBody)/sizeof(Part);

// Track data
static Vector3 trackPts[TRACK_SEGS];
static Vector3 trackDirs[TRACK_SEGS];
static Vector3 trackNormals[TRACK_SEGS];
static SurfaceType trackSurface[TRACK_SEGS];

// Dust particles
static Dust dust[MAX_DUST];
static int dustIdx = 0;

// Trees along track
#define MAX_TREES 80
static Vector3 treePosns[MAX_TREES];
static float treeSizes[MAX_TREES];
static int numTrees = 0;

void GenerateTrack(void) {
    // Winding rally course with varied curvature
    for (int i = 0; i < TRACK_SEGS; i++) {
        float t = (float)i / TRACK_SEGS * 2.0f * PI;
        // Irregular shape: deformed ellipse with tight corners
        float rx = 100.0f + sinf(t * 2.0f) * 35.0f + cosf(t * 3.0f) * 20.0f;
        float rz = 70.0f + cosf(t * 2.0f) * 25.0f + sinf(t * 5.0f) * 15.0f;
        trackPts[i] = (Vector3){ cosf(t) * rx, 0, sinf(t) * rz };

        // Surface assignment: tarmac default, gravel/mud in sections
        if (i % 16 < 6) trackSurface[i] = SURF_GRAVEL;
        else if (i % 16 < 8) trackSurface[i] = SURF_MUD;
        else trackSurface[i] = SURF_TARMAC;
    }

    // Smooth track
    for (int pass = 0; pass < 4; pass++) {
        Vector3 temp[TRACK_SEGS];
        for (int i = 0; i < TRACK_SEGS; i++) {
            int prev = (i - 1 + TRACK_SEGS) % TRACK_SEGS;
            int next = (i + 1) % TRACK_SEGS;
            temp[i] = (Vector3){
                (trackPts[prev].x + trackPts[i].x * 2 + trackPts[next].x) / 4.0f,
                0,
                (trackPts[prev].z + trackPts[i].z * 2 + trackPts[next].z) / 4.0f
            };
        }
        for (int i = 0; i < TRACK_SEGS; i++) trackPts[i] = temp[i];
    }

    // Compute directions and normals
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        trackDirs[i] = Vector3Normalize(Vector3Subtract(trackPts[next], trackPts[i]));
        trackNormals[i] = (Vector3){ -trackDirs[i].z, 0, trackDirs[i].x };
    }

    // Place trees along track edges
    numTrees = 0;
    for (int i = 0; i < TRACK_SEGS && numTrees < MAX_TREES; i += 1) {
        for (int side = -1; side <= 1; side += 2) {
            if (numTrees >= MAX_TREES) break;
            if (GetRandomValue(0, 3) != 0) continue; // sparse
            float offset = (TRACK_WIDTH / 2.0f) + (float)GetRandomValue(3, 12);
            treePosns[numTrees] = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], side * offset));
            treeSizes[numTrees] = 1.0f + (float)GetRandomValue(0, 10) / 10.0f;
            numTrees++;
        }
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

// Search only nearby segments to avoid jitter at corners
int NearestSegLocal(Vector3 pos, int hint) {
    int best = hint;
    float bestD = Vector3Distance(pos, trackPts[hint]);
    int range = 5;
    for (int offset = -range; offset <= range; offset++) {
        int idx = (hint + offset + TRACK_SEGS) % TRACK_SEGS;
        float d = Vector3Distance(pos, trackPts[idx]);
        if (d < bestD) { bestD = d; best = idx; }
    }
    return best;
}

// Get closest point on track segment for smooth distance
Vector3 ClosestPointOnSeg(Vector3 pos, int seg) {
    int next = (seg + 1) % TRACK_SEGS;
    Vector3 segDir = Vector3Subtract(trackPts[next], trackPts[seg]);
    float segLen = Vector3Length(segDir);
    if (segLen < 0.001f) return trackPts[seg];
    segDir = Vector3Scale(segDir, 1.0f / segLen);
    Vector3 toPos = Vector3Subtract(pos, trackPts[seg]);
    float dot = Clamp(Vector3DotProduct(toPos, segDir), 0, segLen);
    return Vector3Add(trackPts[seg], Vector3Scale(segDir, dot));
}

float DistFromTrack(Vector3 pos) {
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

SurfaceType GetSurface(Vector3 pos) {
    return trackSurface[NearestSeg(pos)];
}

void SpawnDust(Vector3 pos, Color col) {
    Dust *d = &dust[dustIdx];
    d->pos = pos;
    d->pos.y = 0.1f;
    d->vel = (Vector3){
        (float)GetRandomValue(-20, 20) / 10.0f,
        (float)GetRandomValue(5, 20) / 10.0f,
        (float)GetRandomValue(-20, 20) / 10.0f
    };
    d->life = 0.5f + (float)GetRandomValue(0, 30) / 100.0f;
    d->color = col;
    d->active = true;
    dustIdx = (dustIdx + 1) % MAX_DUST;
}

float CarProgress(Car *c) { return (float)c->lap * TRACK_SEGS + (float)c->nextWP; }

void DrawTrack(void) {
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        float hw = TRACK_WIDTH / 2.0f;
        Vector3 p0 = trackPts[i], p1 = trackPts[next];
        Vector3 n0 = trackNormals[i], n1 = trackNormals[next];

        Vector3 inner0 = Vector3Add(p0, Vector3Scale(n0, -hw));
        Vector3 outer0 = Vector3Add(p0, Vector3Scale(n0, hw));
        Vector3 inner1 = Vector3Add(p1, Vector3Scale(n1, -hw));
        Vector3 outer1 = Vector3Add(p1, Vector3Scale(n1, hw));

        // Surface color
        Color roadCol;
        switch (trackSurface[i]) {
            case SURF_TARMAC: roadCol = (i % 4 < 2) ? (Color){75,75,80,255} : (Color){70,70,75,255}; break;
            case SURF_GRAVEL: roadCol = (i % 4 < 2) ? (Color){140,120,90,255} : (Color){130,110,85,255}; break;
            case SURF_MUD:    roadCol = (i % 4 < 2) ? (Color){90,70,45,255} : (Color){85,65,40,255}; break;
        }

        // Road surface (both windings for visibility)
        DrawTriangle3D(inner0, outer0, inner1, roadCol);
        DrawTriangle3D(outer0, outer1, inner1, roadCol);
        DrawTriangle3D(inner0, inner1, outer0, roadCol);
        DrawTriangle3D(outer0, inner1, outer1, roadCol);

        // Edge lines
        if (i % 2 == 0) {
            Color edgeCol = (Color){220, 220, 220, 255};
            inner0.y += 0.02f; inner1.y += 0.02f;
            outer0.y += 0.02f; outer1.y += 0.02f;
            DrawLine3D(inner0, inner1, edgeCol);
            DrawLine3D(outer0, outer1, edgeCol);
        }

        // Center dashes (tarmac only)
        if (trackSurface[i] == SURF_TARMAC && i % 4 == 0) {
            Vector3 cl0 = Vector3Lerp(inner0, outer0, 0.5f);
            Vector3 cl1 = Vector3Lerp(inner1, outer1, 0.5f);
            cl0.y += 0.02f; cl1.y += 0.02f;
            DrawLine3D(cl0, cl1, YELLOW);
        }
    }

    // Start/finish line
    Vector3 p = trackPts[0], n = trackNormals[0];
    float hw = TRACK_WIDTH / 2.0f;
    for (int c = 0; c < 10; c++) {
        float t0 = (float)c / 10, t1 = (float)(c+1) / 10;
        Vector3 left = Vector3Add(p, Vector3Scale(n, -hw));
        Vector3 right = Vector3Add(p, Vector3Scale(n, hw));
        Vector3 a = Vector3Lerp(left, right, t0);
        Vector3 b = Vector3Lerp(left, right, t1);
        Vector3 fwd = Vector3Scale(trackDirs[0], 1.5f);
        Vector3 a2 = Vector3Add(a, fwd), b2 = Vector3Add(b, fwd);
        a.y = b.y = a2.y = b2.y = 0.03f;
        Color chk = (c % 2 == 0) ? WHITE : BLACK;
        DrawTriangle3D(a, b, b2, chk);
        DrawTriangle3D(a, b2, a2, chk);
        DrawTriangle3D(a, b2, b, chk);
        DrawTriangle3D(a, a2, b2, chk);
    }
}

void DrawTree(Vector3 pos, float size) {
    // Trunk
    DrawCylinderEx(pos, (Vector3){pos.x, pos.y + size * 2.5f, pos.z},
        size * 0.15f, size * 0.1f, 6, (Color){90, 65, 35, 255});
    // Canopy layers
    for (int i = 0; i < 3; i++) {
        float y = pos.y + size * (1.2f + i * 0.7f);
        float r = size * (1.0f - i * 0.25f);
        DrawCylinderEx((Vector3){pos.x, y, pos.z},
            (Vector3){pos.x, y + r * 0.7f, pos.z},
            r, 0.05f, 8, (Color){30, 80 + i * 20, 30, 255});
    }
}

void DrawCar(Car *car, int colorIdx) {
    // Copy parts and tint body color
    Part tinted[sizeof(carBody)/sizeof(Part)];
    for (int i = 0; i < carBodyCount; i++) tinted[i] = carBody[i];
    tinted[0].color = car->color;                    // body
    Color darker = { car->color.r*3/4, car->color.g*3/4, car->color.b*3/4, 255 };
    tinted[1].color = darker;                         // cabin

    for (int i = 0; i < carBodyCount; i++) {
        DrawPart(&tinted[i], car->pos, -car->rotation);
    }

    // Shadow
    DrawCircle3D((Vector3){car->pos.x, 0.01f, car->pos.z}, 1.0f,
        (Vector3){1,0,0}, 90, (Color){0,0,0,40});

    // Drift smoke
    if (car->drifting && car->speed > 10) {
        float cs = cosf(car->rotation), sn = sinf(car->rotation);
        Vector3 rearPos = { car->pos.x - sn * 0.8f, 0.1f, car->pos.z + cs * 0.8f };
        if (GetRandomValue(0, 2) == 0) SpawnDust(rearPos, (Color){200,200,200,150});
    }

    // Surface dust
    SurfaceType surf = GetSurface(car->pos);
    if (car->speed > 15 && surf != SURF_TARMAC) {
        float cs = cosf(car->rotation), sn = sinf(car->rotation);
        Vector3 rearPos = { car->pos.x - sn * 0.9f, 0.1f, car->pos.z + cs * 0.9f };
        Color dustCol = (surf == SURF_GRAVEL) ? (Color){180,160,120,120} : (Color){100,80,50,120};
        if (GetRandomValue(0, 1) == 0) SpawnDust(rearPos, dustCol);
    }

    // Boost flame
    if (car->boostTimer > 0) {
        float cs = cosf(car->rotation), sn = sinf(car->rotation);
        Vector3 exhaust = { car->pos.x - sn * 1.0f, car->pos.y + 0.2f, car->pos.z + cs * 1.0f };
        DrawSphere(exhaust, 0.12f, ORANGE);
    }
}

int main(void) {
    InitWindow(800, 600, "Rally Racing");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    GenerateTrack();

    // Init cars
    Car cars[NUM_CARS];
    Color carColors[] = { {200,40,40,255}, {40,40,200,255}, {40,180,40,255}, {220,200,40,255} };
    for (int i = 0; i < NUM_CARS; i++) {
        float offset = (float)(i - NUM_CARS/2) * 2.5f;
        cars[i].pos = Vector3Add(trackPts[0], Vector3Scale(trackNormals[0], offset));
        cars[i].pos.y = 0.2f;
        cars[i].rotation = atan2f(trackDirs[0].x, trackDirs[0].z);
        cars[i].speed = 0;
        cars[i].lap = 0;
        cars[i].nextWP = 1;
        cars[i].isPlayer = (i == 0);
        cars[i].finished = false;
        cars[i].drifting = false;
        cars[i].driftTime = 0;
        cars[i].boostTimer = 0;
        cars[i].color = carColors[i];
        cars[i].aiNoise = (float)GetRandomValue(-20, 20) / 100.0f;
        cars[i].steerInput = 0;
    }

    Camera3D camera = { 0 };
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 55;
    camera.projection = CAMERA_PERSPECTIVE;

    float countdown = 3.99f;
    bool raceStarted = false;
    int playerRank = 1;
    float bestLapTime = 999.0f;
    float lapTimer = 0;
    float raceTimer = 0;
    float splitTimes[TOTAL_LAPS] = {0};

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        countdown -= dt;
        if (!raceStarted && countdown <= 0) raceStarted = true;
        if (raceStarted && !cars[0].finished) { raceTimer += dt; lapTimer += dt; }

        // --- Update cars ---
        for (int ci = 0; ci < NUM_CARS; ci++) {
            Car *car = &cars[ci];
            if (car->finished) continue;

            float accel = 0, steer = 0;
            SurfaceType surf = GetSurface(car->pos);
            float maxSpd = CAR_MAX_SPEED;
            float drag;
            float surfTurnMult = 1.0f;  // extra oversteer on loose surfaces
            switch (surf) {
                case SURF_TARMAC: drag = CAR_DRAG_ROAD; surfTurnMult = 1.0f; break;
                case SURF_GRAVEL: drag = CAR_DRAG_GRAVEL; maxSpd *= 0.95f; surfTurnMult = 1.4f; break;
                case SURF_MUD:    drag = CAR_DRAG_MUD; maxSpd *= 0.9f; surfTurnMult = 1.8f; break;
            }

            if (car->boostTimer > 0) { car->boostTimer -= dt; maxSpd *= 1.3f; }

            if (!raceStarted) goto moveCar;

            if (car->isPlayer) {
                if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    accel = CAR_ACCEL;
                if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))  accel = -CAR_BRAKE;
                if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  steer = CAR_TURN;
                if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) steer = -CAR_TURN;

                // Drift
                bool driftInput = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_LEFT_CONTROL);
                if (driftInput && fabsf(steer) > 0.1f && car->speed > 15.0f) {
                    car->drifting = true;
                    car->driftTime += dt;
                    steer *= CAR_DRIFT_MULT;
                    drag = CAR_DRIFT_DRAG;
                } else {
                    // Mini-turbo on drift release
                    if (car->drifting && car->driftTime > 0.8f) {
                        car->boostTimer = (car->driftTime > 2.0f) ? 1.2f : 0.6f;
                    }
                    car->drifting = false;
                    car->driftTime = 0;
                }
            } else {
                // AI
                Vector3 target = trackPts[car->nextWP];
                target = Vector3Add(target, Vector3Scale(trackNormals[car->nextWP], car->aiNoise * TRACK_WIDTH * 0.3f));
                Vector3 toTarget = Vector3Subtract(target, car->pos);
                toTarget.y = 0;
                float targetAngle = atan2f(toTarget.x, toTarget.z);
                float angleDiff = targetAngle - car->rotation;
                while (angleDiff > PI)  angleDiff -= 2*PI;
                while (angleDiff < -PI) angleDiff += 2*PI;
                steer = Clamp(angleDiff * 3.0f, -CAR_TURN, CAR_TURN);
                accel = CAR_ACCEL * 0.88f;

                // Rubber banding
                float playerProg = CarProgress(&cars[0]);
                float myProg = CarProgress(car);
                if (myProg > playerProg + 5) accel *= 0.65f;
                else if (myProg < playerProg - 5) accel *= 1.15f;

                // Brake for sharp corners
                if (fabsf(angleDiff) > 0.6f) accel *= 0.5f;
            }

            // Physics: accel/drag/clamp/move via VehicleUpdate; bicycle steering stays inline.
            car->steerInput = steer;

moveCar:;
            // Bicycle model: rear-wheel drive, front-wheel steering.
            // Compute rotation change from bicycle geometry first, then let
            // VehicleUpdate handle accel/drag/clamp/XZ movement.
            // steer=0 in VehicleInput prevents VehicleUpdate adding a second rotation.
            float wheelbase = 2.5f;
            float maxSteerAngle = 0.2f * surfTurnMult;  // max front wheel angle (radians)
            float steerAngle = steer / CAR_TURN * maxSteerAngle;  // normalize steer input
            if (car->speed < 0) steerAngle = -steerAngle;

            if (fabsf(car->speed) > 0.5f) {
                float angularVel = car->speed * tanf(steerAngle) / wheelbase;
                car->rotation += angularVel * dt;
            }

            // Build Vehicle from Car state and run accel/drag/clamp/move.
            // Normalise `accel` (continuous, includes AI rubber-band + corner
            // multipliers) back to a −1..1 throttle. Player inputs degenerate
            // to ±1; AI inputs preserve their fractional magnitude.
            float throttle = Clamp(accel / CAR_ACCEL, -1.0f, 1.0f);
            Vehicle vh = { .pos = car->pos, .rotation = car->rotation, .speed = car->speed,
                           .accel = CAR_ACCEL, .brake = CAR_BRAKE, .maxSpeed = maxSpd,
                           .reverseMax = 10.0f, .turnRate = 0.0f, .drag = drag };
            VehicleUpdate(&vh, (VehicleInput){ .throttle = throttle, .steer = 0.0f }, dt);
            car->pos   = vh.pos;
            car->speed = vh.speed;
            // car->rotation was already written by the bicycle block above.

            car->pos.y = 0.2f;

            // Track boundary: use local segment search for smooth push
            car->currentSeg = NearestSegLocal(car->pos, car->currentSeg);
            Vector3 closest = ClosestPointOnSeg(car->pos, car->currentSeg);
            Vector3 toCenter = Vector3Subtract(closest, car->pos);
            toCenter.y = 0;
            float trackDist = Vector3Length(toCenter);
            float edgeDist = TRACK_WIDTH / 2.0f;
            if (trackDist > edgeDist - 3.0f && trackDist > 0.01f) {
                Vector3 pushDir = Vector3Scale(toCenter, 1.0f / trackDist);
                float overEdge = trackDist - (edgeDist - 3.0f);
                float pushStrength = overEdge * overEdge * 1.5f;
                car->pos = Vector3Add(car->pos, Vector3Scale(pushDir, pushStrength * dt));
                if (trackDist > edgeDist) car->speed *= (1.0f - 0.3f * dt);
            }

            // Tree collision
            for (int t = 0; t < numTrees; t++) {
                Vector3 diff = Vector3Subtract(car->pos, treePosns[t]);
                diff.y = 0;
                if (Vector3Length(diff) < treeSizes[t] * 0.5f + 0.8f) {
                    car->speed *= 0.3f;
                    Vector3 push = Vector3Normalize(diff);
                    car->pos = Vector3Add(car->pos, Vector3Scale(push, 0.5f));
                }
            }

            // Waypoint progression
            float wpDist = Vector3Distance(car->pos, trackPts[car->nextWP]);
            if (wpDist < 10.0f) {
                int prev = car->nextWP;
                car->nextWP = (car->nextWP + 1) % TRACK_SEGS;
                if (car->nextWP == 0 && prev == TRACK_SEGS - 1) {
                    car->lap++;
                    if (car->isPlayer) {
                        if (lapTimer < bestLapTime) bestLapTime = lapTimer;
                        if (car->lap <= TOTAL_LAPS) splitTimes[car->lap - 1] = lapTimer;
                        lapTimer = 0;
                    }
                    if (car->lap >= TOTAL_LAPS) car->finished = true;
                }
            }
        }

        // Car-car collision
        for (int i = 0; i < NUM_CARS; i++) {
            for (int j = i + 1; j < NUM_CARS; j++) {
                Vector3 diff = Vector3Subtract(cars[i].pos, cars[j].pos);
                diff.y = 0;
                float dist = Vector3Length(diff);
                if (dist < 2.0f && dist > 0.01f) {
                    Vector3 push = Vector3Scale(Vector3Normalize(diff), (2.0f - dist) * 0.5f);
                    cars[i].pos = Vector3Add(cars[i].pos, push);
                    cars[j].pos = Vector3Subtract(cars[j].pos, push);
                }
            }
        }

        // Update dust
        for (int i = 0; i < MAX_DUST; i++) {
            if (!dust[i].active) continue;
            dust[i].pos = Vector3Add(dust[i].pos, Vector3Scale(dust[i].vel, dt));
            dust[i].vel.y -= 3.0f * dt;
            dust[i].life -= dt;
            if (dust[i].life <= 0) dust[i].active = false;
        }

        // Rankings
        playerRank = 1;
        for (int i = 1; i < NUM_CARS; i++)
            if (CarProgress(&cars[i]) > CarProgress(&cars[0])) playerRank++;

        // --- Camera ---
        {
            Car *p = &cars[0];
            float cs = cosf(p->rotation), sn = sinf(p->rotation);
            float speedPct = Clamp(fabsf(p->speed) / CAR_MAX_SPEED, 0, 1.2f);
            float camDist = 7.0f - speedPct * 1.5f;
            float camH = 3.5f - speedPct * 0.5f;
            Vector3 camPos = {
                p->pos.x - sn * camDist,
                p->pos.y + camH,
                p->pos.z - cs * camDist  // fixed: was +cs, should be -cs to be behind
            };
            Vector3 camTarget = {
                p->pos.x + sn * 4.0f,
                p->pos.y + 0.5f,
                p->pos.z + cs * 4.0f
            };
            camera.position = Vector3Lerp(camera.position, camPos, 5.0f * dt);
            camera.target = Vector3Lerp(camera.target, camTarget, 7.0f * dt);

            float targetFov = 55.0f + speedPct * 15.0f;
            camera.fovy += (targetFov - camera.fovy) * 4.0f * dt;
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){130, 170, 210, 255});

        BeginMode3D(camera);
            // Ground beneath track
            DrawPlane((Vector3){0, -0.02f, 0}, (Vector2){500, 500}, (Color){60, 100, 40, 255});

            DrawTrack();

            // Trees
            for (int i = 0; i < numTrees; i++) DrawTree(treePosns[i], treeSizes[i]);

            // Dust particles
            for (int i = 0; i < MAX_DUST; i++) {
                if (!dust[i].active) continue;
                float alpha = dust[i].life / 0.6f;
                Color dc = dust[i].color;
                dc.a = (unsigned char)(alpha * dc.a);
                DrawSphere(dust[i].pos, 0.1f + (1.0f - alpha) * 0.2f, dc);
            }

            // Cars (sort by distance from camera for rough depth)
            for (int ci = 0; ci < NUM_CARS; ci++) DrawCar(&cars[ci], ci);

        EndMode3D();

        // --- HUD ---
        // Speed
        DrawRectangle(20, sh - 50, 140, 40, (Color){0,0,0,160});
        DrawText(TextFormat("%.0f km/h", fabsf(cars[0].speed) * 3.6f), 30, sh - 42, 26,
            (Color){0,200,255,255});

        // Position
        const char *ordinals[] = {"1st","2nd","3rd","4th"};
        DrawText(ordinals[playerRank - 1], sw - 80, 15, 40, WHITE);

        // Lap
        int dispLap = cars[0].lap + 1;
        if (dispLap > TOTAL_LAPS) dispLap = TOTAL_LAPS;
        DrawText(TextFormat("LAP %d/%d", dispLap, TOTAL_LAPS), sw - 130, 58, 22, WHITE);

        // Timers
        int mins = (int)raceTimer / 60, secs = (int)raceTimer % 60;
        int ms = (int)(fmodf(raceTimer, 1.0f) * 100);
        DrawText(TextFormat("%d:%02d.%02d", mins, secs, ms), sw - 130, 85, 18, WHITE);

        // Current lap time
        DrawText(TextFormat("Lap: %.2f", lapTimer), sw - 130, 108, 14, (Color){200,200,200,255});

        // Best lap
        if (bestLapTime < 999.0f)
            DrawText(TextFormat("Best: %.2f", bestLapTime), sw - 130, 125, 14, YELLOW);

        // Surface indicator
        SurfaceType surf = GetSurface(cars[0].pos);
        const char *surfName = (surf == SURF_TARMAC) ? "TARMAC" : (surf == SURF_GRAVEL) ? "GRAVEL" : "MUD";
        Color surfCol = (surf == SURF_TARMAC) ? (Color){150,150,160,255} :
                        (surf == SURF_GRAVEL) ? (Color){180,160,120,255} : (Color){120,90,50,255};
        DrawText(surfName, 30, sh - 70, 14, surfCol);

        // Drift/boost
        if (cars[0].drifting) {
            Color driftCol = (cars[0].driftTime > 1.5f) ? ORANGE : YELLOW;
            DrawText("DRIFT!", sw/2 - 28, 20, 22, driftCol);
        }
        if (cars[0].boostTimer > 0) DrawText("BOOST!", sw/2 - 30, 20, 22, RED);

        // Mini-map
        {
            int mmx = sw - 140, mmy = sh - 140;
            float mmScale = 0.45f;
            DrawRectangle(mmx - 5, mmy - 5, 135, 135, (Color){0,0,0,120});
            for (int i = 0; i < TRACK_SEGS; i++) {
                int next = (i + 1) % TRACK_SEGS;
                Vector2 a = {mmx + 62 + trackPts[i].x * mmScale, mmy + 62 + trackPts[i].z * mmScale};
                Vector2 b = {mmx + 62 + trackPts[next].x * mmScale, mmy + 62 + trackPts[next].z * mmScale};
                Color lineCol;
                switch (trackSurface[i]) {
                    case SURF_TARMAC: lineCol = (Color){100,100,110,200}; break;
                    case SURF_GRAVEL: lineCol = (Color){160,140,100,200}; break;
                    case SURF_MUD:    lineCol = (Color){100,80,50,200}; break;
                }
                DrawLineV(a, b, lineCol);
            }
            for (int ci = 0; ci < NUM_CARS; ci++) {
                float mx = mmx + 62 + cars[ci].pos.x * mmScale;
                float mz = mmy + 62 + cars[ci].pos.z * mmScale;
                DrawCircle(mx, mz, cars[ci].isPlayer ? 3 : 2, cars[ci].color);
            }
        }

        // Countdown
        if (countdown > 0 && !raceStarted) {
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
        if (cars[0].finished) {
            DrawRectangle(sw/2 - 160, sh/2 - 50, 320, 110, (Color){0,0,0,200});
            DrawText(TextFormat("FINISH  %s", ordinals[playerRank-1]), sw/2 - 110, sh/2 - 40, 34, GOLD);
            DrawText(TextFormat("Time: %d:%02d.%02d", mins, secs, ms), sw/2 - 70, sh/2, 20, WHITE);
            if (bestLapTime < 999.0f)
                DrawText(TextFormat("Best Lap: %.2f", bestLapTime), sw/2 - 70, sh/2 + 25, 18, YELLOW);
        }

        DrawText("WASD: Drive  Shift/Ctrl+Turn: Drift  Release drift for boost", 10, sh - 16, 11,
            (Color){80,80,100,200});
        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
