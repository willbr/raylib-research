#include "raylib.h"
#include "raymath.h"
#include "../common/objects3d.h"
#include <math.h>
#include <stdlib.h>

// Wave Race / Hydro Thunder style powerboat racing on a looping water circuit

#define TRACK_SEGS     80
#define TRACK_WIDTH    14.0f
#define NUM_BOATS       4
#define TOTAL_LAPS      3
#define MAX_SPRAY      80
#define MAX_BUOYS      200
#define MAX_ISLANDS    8
#define MAX_PALMS      24
#define WAKE_POINTS    16    // points per boat wake trail
#define MAX_CLOUDS     20
#define MAX_BIRDS      8

// Boat physics
#define BOAT_ACCEL      30.0f
#define BOAT_BRAKE      20.0f
#define BOAT_MAX_SPEED  40.0f
#define BOAT_TURN        2.2f
#define BOAT_DRAG        0.993f
#define BOAT_BOUNCE      0.4f
#define BASE_WAVE_HEIGHT 0.6f
#define WAVE_SPEED       2.0f
#define GRAVITY         25.0f
#define JUMP_THRESHOLD   0.7f    // wave slope needed to launch
#define DRAFT_DIST      10.0f    // distance to get drafting bonus
#define DRAFT_ANGLE      0.4f    // half-angle cone behind boat (radians)
#define DRAFT_BONUS      1.15f   // speed multiplier when drafting
#define DRIFT_TURN_MULT  1.8f
#define DRIFT_DRAG       0.988f
#define DRIFT_MIN_SPEED  15.0f

static float waveIntensity = 1.0f;  // increases each lap

typedef struct {
    Vector3 pos;
    float rotation;
    float speed;
    float pitch;        // nose tilt from waves
    float roll;         // lean into turns
    float velY;         // vertical velocity for jumps
    bool airborne;
    bool drifting;
    float driftTimer;
    int lap;
    int nextWP;
    int currentSeg;
    bool isPlayer;
    bool finished;
    float boostTimer;
    float boostFuel;    // 0 to 1
    Color color;
    float aiNoise;
    float steerInput;
    float wakeTimer;
    bool drafting;      // currently in another boat's slipstream
} Boat;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float life;
    float size;
    Color color;
    bool active;
} Spray;

typedef struct {
    Vector3 pos;
    float radius;
    int side;           // -1 = left (red), 1 = right (green)
    bool collected;     // for boost buoys
    bool isBoost;
} Buoy;

typedef struct {
    Vector3 pos;
    float radius;       // island land radius
    float height;       // peak height
} Island;

typedef struct {
    Vector3 pos;
    float height;
    float lean;         // trunk lean angle
    float leanDir;      // lean direction (radians)
} Palm;

typedef struct {
    Vector3 pos;
    float scaleX, scaleY, scaleZ;  // ellipsoid radii
} CloudObj;

typedef struct {
    Vector3 pos;
    float angle;         // circle around a center point
    float radius;        // orbit radius
    float speed;         // angular speed
    float flapPhase;     // wing flap offset
    float centerX, centerZ;
} Bird;

static CloudObj clouds[MAX_CLOUDS];
static Bird birds[MAX_BIRDS];

// Wake trail: ring buffer of recent positions per boat
typedef struct {
    Vector3 points[WAKE_POINTS];
    int head;
    int count;
} Wake;

// Screen shake state
static float shakeAmount = 0;
static float shakeDecay = 8.0f;

static Island islands[MAX_ISLANDS];
static int numIslands = 0;
static Palm palms[MAX_PALMS];
static int numPalms = 0;
static Wake wakes[NUM_BOATS];

// Boat model
static Part boatBody[] = {
    // Hull
    CONE(0, -0.1f, 0.6f,     0.4f, 1.0f, 0.0f, COL(220,220,230,255)),  // bow
    CUBE(0, -0.05f, -0.2f,   0.9f, 0.3f, 1.4f, COL(200,40,40,255)),    // main hull
    CUBE(0, 0.05f, -0.7f,    0.7f, 0.15f, 0.4f, COL(180,35,35,255)),   // stern deck
    // Cockpit
    CUBE(0, 0.2f, 0.0f,      0.4f, 0.2f, 0.5f, COL(60,60,70,255)),     // windshield base
    CUBE(0, 0.35f, -0.05f,   0.35f, 0.15f, 0.35f, COL(120,180,220,180)), // canopy
    // Engine housing
    CUBE(0, 0.05f, -0.95f,   0.3f, 0.2f, 0.2f, COL(50,50,55,255)),
    // Trim stripes
    CUBE(0, 0.1f, 0.1f,      0.92f, 0.02f, 1.2f, COL(255,255,255,255)),
    // Sponsons (side pontoons)
    CUBE(-0.5f, -0.15f, -0.1f, 0.12f, 0.12f, 0.8f, COL(180,35,35,255)),
    CUBE( 0.5f, -0.15f, -0.1f, 0.12f, 0.12f, 0.8f, COL(180,35,35,255)),
};
static int boatBodyCount = sizeof(boatBody)/sizeof(Part);

// Track
static Vector3 trackPts[TRACK_SEGS];
static Vector3 trackDirs[TRACK_SEGS];
static Vector3 trackNormals[TRACK_SEGS];

// Water spray particles
static Spray spray[MAX_SPRAY];
static int sprayIdx = 0;

// Buoys
static Buoy buoys[MAX_BUOYS];
static int numBuoys = 0;

// Boats
static Boat boats[NUM_BOATS];
static int playerIdx = 0;

// Race state
static float raceTime = 0;
static bool raceStarted = false;
static float countdownTimer = 4.0f;
static float gameTime = 0;

// Water wave height at a world position
float WaterHeight(float x, float z, float t) {
    float wh = BASE_WAVE_HEIGHT * waveIntensity;
    return sinf(x * 0.15f + t * WAVE_SPEED) * wh +
           cosf(z * 0.12f + t * WAVE_SPEED * 0.7f) * wh * 0.6f +
           sinf((x + z) * 0.08f + t * 1.5f) * wh * 0.3f;
}

void GenerateTrack(void) {
    for (int i = 0; i < TRACK_SEGS; i++) {
        float t = (float)i / TRACK_SEGS * 2.0f * PI;
        // Irregular water course shape
        float rx = 120.0f + sinf(t * 2.0f) * 40.0f + cosf(t * 3.0f) * 25.0f;
        float rz = 90.0f + cosf(t * 2.0f) * 30.0f + sinf(t * 5.0f) * 18.0f;
        trackPts[i] = (Vector3){ cosf(t) * rx, 0, sinf(t) * rz };
    }
    // Compute directions and normals
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        Vector3 dir = Vector3Subtract(trackPts[next], trackPts[i]);
        dir.y = 0;
        dir = Vector3Normalize(dir);
        trackDirs[i] = dir;
        trackNormals[i] = (Vector3){-dir.z, 0, dir.x};
    }

    // Place buoys along track edges
    numBuoys = 0;
    for (int i = 0; i < TRACK_SEGS && numBuoys < MAX_BUOYS - 2; i += 2) {
        float hw = TRACK_WIDTH * 0.5f;
        // Left buoy (red)
        buoys[numBuoys].pos = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], -hw));
        buoys[numBuoys].pos.y = 0.3f;
        buoys[numBuoys].radius = 0.5f;
        buoys[numBuoys].side = -1;
        buoys[numBuoys].isBoost = false;
        buoys[numBuoys].collected = false;
        numBuoys++;
        // Right buoy (green)
        buoys[numBuoys].pos = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], hw));
        buoys[numBuoys].pos.y = 0.3f;
        buoys[numBuoys].radius = 0.5f;
        buoys[numBuoys].side = 1;
        buoys[numBuoys].isBoost = false;
        buoys[numBuoys].collected = false;
        numBuoys++;
    }
    // Sprinkle boost buoys (yellow) on the track
    for (int i = 0; i < TRACK_SEGS && numBuoys < MAX_BUOYS; i += 8) {
        float offset = (float)GetRandomValue(-30, 30) / 10.0f;
        buoys[numBuoys].pos = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], offset));
        buoys[numBuoys].pos.y = 0.5f;
        buoys[numBuoys].radius = 0.6f;
        buoys[numBuoys].side = 0;
        buoys[numBuoys].isBoost = true;
        buoys[numBuoys].collected = false;
        numBuoys++;
    }

    // Generate islands outside the track
    numIslands = 0;
    for (int i = 0; i < MAX_ISLANDS; i++) {
        float angle = (float)i / MAX_ISLANDS * 2.0f * PI + (float)GetRandomValue(0, 30) / 100.0f;
        // Place far from track center, outside the circuit
        float dist = 180.0f + (float)GetRandomValue(0, 800) / 10.0f;
        islands[i].pos = (Vector3){cosf(angle) * dist, 0, sinf(angle) * dist};
        islands[i].radius = 8.0f + (float)GetRandomValue(0, 120) / 10.0f;
        islands[i].height = 2.0f + (float)GetRandomValue(0, 60) / 10.0f;
        numIslands++;
    }

    // Place palm trees on islands
    numPalms = 0;
    for (int isl = 0; isl < numIslands && numPalms < MAX_PALMS; isl++) {
        int treesOnIsland = 2 + GetRandomValue(0, 2);
        for (int t = 0; t < treesOnIsland && numPalms < MAX_PALMS; t++) {
            float a = (float)GetRandomValue(0, 628) / 100.0f;
            float r = (float)GetRandomValue(10, 70) / 100.0f * islands[isl].radius;
            palms[numPalms].pos = (Vector3){
                islands[isl].pos.x + cosf(a) * r,
                islands[isl].height * 0.7f,
                islands[isl].pos.z + sinf(a) * r
            };
            palms[numPalms].height = 4.0f + (float)GetRandomValue(0, 30) / 10.0f;
            palms[numPalms].lean = 0.15f + (float)GetRandomValue(0, 20) / 100.0f;
            palms[numPalms].leanDir = (float)GetRandomValue(0, 628) / 100.0f;
            numPalms++;
        }
    }

    // Generate clouds scattered across the sky
    for (int i = 0; i < MAX_CLOUDS; i++) {
        clouds[i].pos = (Vector3){
            (float)GetRandomValue(-3000, 3000) / 10.0f,
            30.0f + (float)GetRandomValue(0, 200) / 10.0f,
            (float)GetRandomValue(-3000, 3000) / 10.0f
        };
        clouds[i].scaleX = 6.0f + (float)GetRandomValue(0, 80) / 10.0f;
        clouds[i].scaleY = 2.0f + (float)GetRandomValue(0, 20) / 10.0f;
        clouds[i].scaleZ = 5.0f + (float)GetRandomValue(0, 60) / 10.0f;
    }

    // Generate birds circling in the sky
    for (int i = 0; i < MAX_BIRDS; i++) {
        birds[i].centerX = (float)GetRandomValue(-1500, 1500) / 10.0f;
        birds[i].centerZ = (float)GetRandomValue(-1500, 1500) / 10.0f;
        birds[i].radius = 15.0f + (float)GetRandomValue(0, 200) / 10.0f;
        birds[i].angle = (float)GetRandomValue(0, 628) / 100.0f;
        birds[i].speed = 0.3f + (float)GetRandomValue(0, 30) / 100.0f;
        birds[i].flapPhase = (float)GetRandomValue(0, 628) / 100.0f;
        birds[i].pos.y = 25.0f + (float)GetRandomValue(0, 150) / 10.0f;
    }
}

int NearestSegment(Vector3 pos) {
    float bestDist = 1e9f;
    int best = 0;
    for (int i = 0; i < TRACK_SEGS; i++) {
        float d = Vector3Distance(pos, trackPts[i]);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void SpawnSpray(Vector3 pos, Vector3 vel, float size, Color col) {
    Spray *s = &spray[sprayIdx % MAX_SPRAY];
    s->pos = pos;
    s->vel = vel;
    s->life = 0.5f + (float)GetRandomValue(0, 30) / 100.0f;
    s->size = size;
    s->color = col;
    s->active = true;
    sprayIdx++;
}

void InitRace(void) {
    GenerateTrack();

    Color boatColors[] = {
        {200, 40, 40, 255},
        {40, 80, 200, 255},
        {40, 180, 40, 255},
        {200, 160, 30, 255},
    };

    for (int i = 0; i < NUM_BOATS; i++) {
        // Stagger on grid
        float lateral = (i % 2 == 0) ? -2.0f : 2.0f;
        int row = i / 2;
        int startSeg = (TRACK_SEGS - 2 - row) % TRACK_SEGS;
        boats[i].pos = Vector3Add(trackPts[startSeg], Vector3Scale(trackNormals[startSeg], lateral));
        boats[i].pos.y = 0;
        boats[i].rotation = atan2f(trackDirs[startSeg].x, trackDirs[startSeg].z);
        boats[i].speed = 0;
        boats[i].pitch = 0;
        boats[i].roll = 0;
        boats[i].lap = 0;
        boats[i].nextWP = 0;
        boats[i].currentSeg = startSeg;
        boats[i].isPlayer = (i == 0);
        boats[i].finished = false;
        boats[i].boostTimer = 0;
        boats[i].boostFuel = 0;
        boats[i].color = boatColors[i];
        boats[i].aiNoise = (float)GetRandomValue(-10, 10) / 100.0f;
        boats[i].steerInput = 0;
        boats[i].wakeTimer = 0;
    }
    playerIdx = 0;
    raceTime = 0;
    raceStarted = false;
    countdownTimer = 4.0f;
    gameTime = 0;

    for (int i = 0; i < MAX_SPRAY; i++) spray[i].active = false;
    for (int i = 0; i < NUM_BOATS; i++) { wakes[i].head = 0; wakes[i].count = 0; }
    shakeAmount = 0;
    waveIntensity = 1.0f;
    // Reset boost buoys
    for (int i = 0; i < numBuoys; i++) buoys[i].collected = false;
}

void UpdateBoat(Boat *b, int boatIdx, float dt) {
    Vector3 forward = {sinf(b->rotation), 0, cosf(b->rotation)};
    float wh = WaterHeight(b->pos.x, b->pos.z, gameTime);

    // --- Water/airborne physics ---
    if (b->airborne) {
        b->velY -= GRAVITY * dt;
        b->pos.y += b->velY * dt;
        // Land when hitting water
        if (b->pos.y <= wh) {
            b->pos.y = wh;
            b->airborne = false;
            // Splash on landing
            if (b->isPlayer) shakeAmount = fminf(fabsf(b->velY) * 0.05f, 0.6f);
            float splashSpeed = fabsf(b->velY);
            for (int s = 0; s < 8; s++) {
                float a = (float)s / 8 * 2.0f * PI;
                Vector3 sv = {cosf(a) * splashSpeed * 0.3f, splashSpeed * 0.4f, sinf(a) * splashSpeed * 0.3f};
                SpawnSpray(b->pos, sv, 0.15f, (Color){200, 230, 255, 220});
            }
            b->velY = 0;
        }
        // Pitch nose down in air
        float airPitch = 0.2f;
        b->pitch += (airPitch - b->pitch) * 3.0f * dt;
    } else {
        // On water: follow surface
        b->pos.y += (wh - b->pos.y) * 8.0f * dt;
        if (b->pos.y < wh) b->pos.y = wh;

        // Pitch from wave slope
        float whAhead = WaterHeight(b->pos.x + forward.x * 1.5f, b->pos.z + forward.z * 1.5f, gameTime);
        float targetPitch = (wh - whAhead) * 0.3f;
        b->pitch += (targetPitch - b->pitch) * 5.0f * dt;

        // Jump detection: if going fast uphill on a wave, launch
        float whBehind = WaterHeight(b->pos.x - forward.x * 1.5f, b->pos.z - forward.z * 1.5f, gameTime);
        float slope = (wh - whBehind) / 1.5f;  // positive = going uphill
        if (slope > JUMP_THRESHOLD && b->speed > 20.0f) {
            b->airborne = true;
            b->velY = slope * b->speed * 0.15f;
        }
    }

    if (b->finished) {
        b->speed *= 0.97f;
        b->pos = Vector3Add(b->pos, Vector3Scale(forward, b->speed * dt));
        b->roll *= 0.95f;
        return;
    }

    float accel = 0, steer = 0;
    bool wantDrift = false;

    if (b->isPlayer && raceStarted) {
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) accel = 1;
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) accel = -0.5f;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) steer = 1;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) steer = -1;
        // Drift: hold Z/shift while turning
        wantDrift = (IsKeyDown(KEY_Z) || IsKeyDown(KEY_LEFT_SHIFT)) && fabsf(steer) > 0.3f;
        if (IsKeyPressed(KEY_SPACE) && b->boostFuel > 0.2f) {
            b->boostTimer = 1.5f;
            shakeAmount = 0.4f;
            b->boostFuel -= 0.3f;
            if (b->boostFuel < 0) b->boostFuel = 0;
        }
    } else if (!b->isPlayer && raceStarted) {
        int wp = b->nextWP;
        Vector3 target = trackPts[wp];
        float noise = sinf(gameTime * 1.5f + b->aiNoise * 50.0f) * 3.0f;
        target = Vector3Add(target, Vector3Scale(trackNormals[wp], noise));
        Vector3 toTarget = Vector3Subtract(target, b->pos);
        toTarget.y = 0;
        float targetAngle = atan2f(toTarget.x, toTarget.z);
        float angleDiff = targetAngle - b->rotation;
        while (angleDiff > PI) angleDiff -= 2 * PI;
        while (angleDiff < -PI) angleDiff += 2 * PI;
        steer = angleDiff * 2.0f;
        if (steer > 1) steer = 1;
        if (steer < -1) steer = -1;
        accel = 0.85f + b->aiNoise;
    }

    // --- Drifting ---
    if (wantDrift && b->speed > DRIFT_MIN_SPEED && !b->airborne) {
        b->drifting = true;
        b->driftTimer += dt;
    } else {
        // Drift exit boost: short speed bonus based on drift duration
        if (b->drifting && b->driftTimer > 0.5f) {
            float driftBoost = fminf(b->driftTimer * 0.3f, 0.8f);
            b->boostFuel += driftBoost;
            if (b->boostFuel > 1.0f) b->boostFuel = 1.0f;
        }
        b->drifting = false;
        b->driftTimer = 0;
    }

    // --- Drafting: check if behind another boat ---
    b->drafting = false;
    for (int i = 0; i < NUM_BOATS; i++) {
        if (i == boatIdx || boats[i].finished) continue;
        Vector3 toOther = Vector3Subtract(boats[i].pos, b->pos);
        toOther.y = 0;
        float dist = Vector3Length(toOther);
        if (dist < 0.5f || dist > DRAFT_DIST) continue;
        // Check if other boat is roughly ahead of us
        Vector3 toDir = Vector3Normalize(toOther);
        float dot = forward.x * toDir.x + forward.z * toDir.z;
        if (dot > cosf(DRAFT_ANGLE)) {
            b->drafting = true;
            break;
        }
    }

    // Boost
    float maxSpd = BOAT_MAX_SPEED;
    float accelMult = 1.0f;
    if (b->boostTimer > 0) {
        b->boostTimer -= dt;
        maxSpd *= 1.4f;
        accelMult = 1.8f;
    }
    if (b->drafting) maxSpd *= DRAFT_BONUS;

    // Accelerate/brake
    if (accel > 0) b->speed += BOAT_ACCEL * accel * accelMult * dt;
    else if (accel < 0) b->speed += BOAT_BRAKE * accel * dt;

    float drag = b->drifting ? DRIFT_DRAG : BOAT_DRAG;
    b->speed *= drag;
    if (b->speed > maxSpd) b->speed = maxSpd;
    if (b->speed < 0) b->speed = 0;

    // Steering
    float turnMult = b->drifting ? DRIFT_TURN_MULT : 1.0f;
    float turnRate = BOAT_TURN * steer * fminf(b->speed / 15.0f, 1.0f) * turnMult;
    b->rotation += turnRate * dt;
    b->steerInput = steer;

    // Lean — more dramatic when drifting
    float leanMult = b->drifting ? 0.45f : 0.25f;
    float targetRoll = -steer * leanMult * fminf(b->speed / 20.0f, 1.0f);
    b->roll += (targetRoll - b->roll) * 5.0f * dt;

    // Move forward (reduced lateral grip when drifting — slide outward)
    b->pos = Vector3Add(b->pos, Vector3Scale(forward, b->speed * dt));
    if (b->drifting) {
        Vector3 right = {cosf(b->rotation), 0, -sinf(b->rotation)};
        float slideForce = b->speed * 0.08f * -steer;
        b->pos = Vector3Add(b->pos, Vector3Scale(right, slideForce * dt));
    }

    // Track waypoint progression — advance through multiple waypoints if needed
    b->currentSeg = NearestSegment(b->pos);
    for (int step = 0; step < 5; step++) {  // allow skipping ahead up to 5 waypoints per frame
        float distToWP = Vector3Distance(b->pos, trackPts[b->nextWP]);
        if (distToWP >= TRACK_WIDTH * 1.5f) break;
        int prev = b->nextWP;
        b->nextWP = (b->nextWP + 1) % TRACK_SEGS;
        // Lap complete when wrapping from second half back to first half
        if (prev >= TRACK_SEGS / 2 && b->nextWP < TRACK_SEGS / 4) {
            b->lap++;
            waveIntensity = 1.0f + (float)b->lap * 0.4f;
            if (b->lap >= TOTAL_LAPS) b->finished = true;
        }
    }

    // Spray effects
    if (b->speed > 8.0f && !b->airborne) {
        b->wakeTimer -= dt;
        if (b->wakeTimer <= 0) {
            b->wakeTimer = 0.03f;
            Vector3 sprayPos = b->pos;
            sprayPos.y = wh + 0.2f;
            float side = (sprayIdx % 2 == 0) ? -0.5f : 0.5f;
            Vector3 right = {cosf(b->rotation), 0, -sinf(b->rotation)};
            sprayPos = Vector3Add(sprayPos, Vector3Scale(right, side));
            Vector3 sprayVel = {
                right.x * side * 3.0f + (float)GetRandomValue(-10, 10) / 10.0f,
                2.0f + (float)GetRandomValue(0, 20) / 10.0f,
                right.z * side * 3.0f + (float)GetRandomValue(-10, 10) / 10.0f
            };
            SpawnSpray(sprayPos, sprayVel, 0.1f + b->speed * 0.005f, (Color){200, 220, 255, 200});
        }
    }

    // Boost buoy collection
    for (int i = 0; i < numBuoys; i++) {
        if (!buoys[i].isBoost || buoys[i].collected) continue;
        float d = Vector3Distance(b->pos, buoys[i].pos);
        if (d < buoys[i].radius + 1.0f) {
            buoys[i].collected = true;
            b->boostFuel += 0.25f;
            if (b->boostFuel > 1.0f) b->boostFuel = 1.0f;
        }
    }

    // Record wake trail position
    if (b->speed > 3.0f && !b->airborne) {
        Wake *w = &wakes[boatIdx];
        w->points[w->head] = b->pos;
        w->points[w->head].y = WaterHeight(b->pos.x, b->pos.z, gameTime) + 0.05f;
        w->head = (w->head + 1) % WAKE_POINTS;
        if (w->count < WAKE_POINTS) w->count++;
    }
}

// Rotate vector by roll, pitch, yaw (in that order)
Vector3 BoatRotateVec(Vector3 v, float pitch, float yaw, float roll) {
    // Roll (Z axis)
    float cr = cosf(roll), sr = sinf(roll);
    float x1 = v.x * cr - v.y * sr;
    float y1 = v.x * sr + v.y * cr;
    // Pitch (X axis)
    float cp = cosf(pitch), sp = sinf(pitch);
    float y2 = y1 * cp - v.z * sp;
    float z2 = y1 * sp + v.z * cp;
    // Yaw (Y axis)
    float cy = cosf(yaw), sy = sinf(yaw);
    float x3 = x1 * cy + z2 * sy;
    float z3 = -x1 * sy + z2 * cy;
    return (Vector3){x3, y2, z3};
}

void DrawCubeRotated(Vector3 center, float w, float h, float d,
                     float pitch, float yaw, float roll, Color col) {
    Vector3 corners[8] = {
        {-w/2,-h/2,-d/2},{w/2,-h/2,-d/2},{w/2,h/2,-d/2},{-w/2,h/2,-d/2},
        {-w/2,-h/2,d/2},{w/2,-h/2,d/2},{w/2,h/2,d/2},{-w/2,h/2,d/2}
    };
    for (int c = 0; c < 8; c++)
        corners[c] = Vector3Add(center, BoatRotateVec(corners[c], pitch, yaw, roll));
    int faces[6][4] = {{0,1,2,3},{4,5,6,7},{0,4,7,3},{1,5,6,2},{0,1,5,4},{3,2,6,7}};
    for (int f = 0; f < 6; f++) {
        DrawTriangle3D(corners[faces[f][0]], corners[faces[f][1]], corners[faces[f][2]], col);
        DrawTriangle3D(corners[faces[f][0]], corners[faces[f][2]], corners[faces[f][3]], col);
        DrawTriangle3D(corners[faces[f][2]], corners[faces[f][1]], corners[faces[f][0]], col);
        DrawTriangle3D(corners[faces[f][3]], corners[faces[f][2]], corners[faces[f][0]], col);
    }
}

void DrawBoatModel(Boat *b) {
    float pitch = b->pitch, yaw = b->rotation, roll = b->roll;

    for (int i = 0; i < boatBodyCount; i++) {
        Part *part = &boatBody[i];
        Color col = part->color;
        if (i == 1 || i == 7 || i == 8) col = b->color;  // hull + sponsons

        Vector3 rotOffset = BoatRotateVec(part->offset, pitch, yaw, roll);
        Vector3 worldPos = Vector3Add(b->pos, rotOffset);

        switch (part->type) {
            case PART_CUBE:
                DrawCubeRotated(worldPos, part->size.x, part->size.y, part->size.z,
                    pitch, yaw, roll, col);
                break;
            case PART_SPHERE:
                DrawSphere(worldPos, part->size.x, col);
                break;
            case PART_CYLINDER: {
                Vector3 topOff = {part->offset.x, part->offset.y + part->size.y, part->offset.z};
                Vector3 top = Vector3Add(b->pos, BoatRotateVec(topOff, pitch, yaw, roll));
                DrawCylinderEx(worldPos, top, part->size.x, part->size.x, 8, col);
                break;
            }
            case PART_CONE: {
                Vector3 topOff = {part->offset.x, part->offset.y, part->offset.z + part->size.y};
                Vector3 top = Vector3Add(b->pos, BoatRotateVec(topOff, pitch, yaw, roll));
                DrawCylinderEx(worldPos, top, part->size.x, part->size.z, 8, col);
                break;
            }
        }
    }

    // Engine wake glow when boosting
    if (b->boostTimer > 0) {
        Vector3 exhaust = Vector3Add(b->pos, BoatRotateVec((Vector3){0, 0.1f, -1.2f}, pitch, yaw, roll));
        DrawSphere(exhaust, 0.15f + sinf(gameTime * 25) * 0.05f, (Color){255, 180, 50, 200});
    }
}

// Draw water surface as grid of quads
void DrawWater(Vector3 camPos) {
    float cx = floorf(camPos.x / 8) * 8;
    float cz = floorf(camPos.z / 8) * 8;
    float range = 250.0f;
    Color deepWater = {20, 60, 120, 255};
    Color shallowWater = {40, 100, 160, 255};

    for (float gx = cx - range; gx < cx + range; gx += 8) {
        for (float gz = cz - range; gz < cz + range; gz += 8) {
            float d = sqrtf((gx - camPos.x) * (gx - camPos.x) + (gz - camPos.z) * (gz - camPos.z));
            if (d > range) continue;
            float h00 = WaterHeight(gx, gz, gameTime);
            float h10 = WaterHeight(gx + 8, gz, gameTime);
            float h01 = WaterHeight(gx, gz + 8, gameTime);
            float h11 = WaterHeight(gx + 8, gz + 8, gameTime);
            // Color varies with height
            float avgH = (h00 + h10 + h01 + h11) * 0.25f;
            float t = (avgH + BASE_WAVE_HEIGHT) / (BASE_WAVE_HEIGHT * 2.0f);
            if (t < 0) t = 0; if (t > 1) t = 1;
            Color c = {
                (unsigned char)(deepWater.r + (shallowWater.r - deepWater.r) * t),
                (unsigned char)(deepWater.g + (shallowWater.g - deepWater.g) * t),
                (unsigned char)(deepWater.b + (shallowWater.b - deepWater.b) * t),
                255
            };
            Vector3 v0 = {gx, h00, gz};
            Vector3 v1 = {gx + 8, h10, gz};
            Vector3 v2 = {gx + 8, h11, gz + 8};
            Vector3 v3 = {gx, h01, gz + 8};
            DrawTriangle3D(v0, v1, v2, c);
            DrawTriangle3D(v0, v2, v3, c);
            DrawTriangle3D(v2, v1, v0, c);
            DrawTriangle3D(v3, v2, v0, c);
        }
    }
}

void DrawTrackMarkers(void) {
    // Draw track boundary lines on water surface
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        float hw = TRACK_WIDTH * 0.5f;
        Vector3 lA = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], -hw));
        Vector3 lB = Vector3Add(trackPts[next], Vector3Scale(trackNormals[next], -hw));
        Vector3 rA = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], hw));
        Vector3 rB = Vector3Add(trackPts[next], Vector3Scale(trackNormals[next], hw));
        lA.y = WaterHeight(lA.x, lA.z, gameTime) + 0.1f;
        lB.y = WaterHeight(lB.x, lB.z, gameTime) + 0.1f;
        rA.y = WaterHeight(rA.x, rA.z, gameTime) + 0.1f;
        rB.y = WaterHeight(rB.x, rB.z, gameTime) + 0.1f;
        DrawLine3D(lA, lB, (Color){255, 80, 80, 180});
        DrawLine3D(rA, rB, (Color){80, 255, 80, 180});
    }

    // Checkpoint gates at every 10th segment
    int cpInterval = TRACK_SEGS / 8;
    for (int c = 0; c < 8; c++) {
        int seg = c * cpInterval;
        float hw = TRACK_WIDTH * 0.5f + 1.0f;
        Vector3 left = Vector3Add(trackPts[seg], Vector3Scale(trackNormals[seg], -hw));
        Vector3 right = Vector3Add(trackPts[seg], Vector3Scale(trackNormals[seg], hw));
        float whl = WaterHeight(left.x, left.z, gameTime);
        float whr = WaterHeight(right.x, right.z, gameTime);
        left.y = whl; right.y = whr;
        float poleH = 5.0f;
        // Gate color: start/finish = gold, others = white/blue
        Color poleCol = (c == 0) ? (Color){220, 200, 40, 255} : (Color){200, 200, 220, 255};
        Color barCol = (c == 0) ? (Color){220, 200, 40, 255} : (Color){80, 150, 255, 255};
        // Left pole
        DrawCylinder(left, 0.15f, 0.15f, poleH, 6, poleCol);
        // Right pole
        DrawCylinder(right, 0.15f, 0.15f, poleH, 6, poleCol);
        // Top bar
        Vector3 topL = {left.x, left.y + poleH, left.z};
        Vector3 topR = {right.x, right.y + poleH, right.z};
        DrawCylinderEx(topL, topR, 0.1f, 0.1f, 6, barCol);
        // Checkpoint number
        if (c > 0) {
            Vector3 mid = Vector3Lerp(topL, topR, 0.5f);
            mid.y -= 0.5f;
            float sz = 0.6f;
            DrawSphere(mid, sz, barCol);
        }
    }
}

// Draw an arrow on the HUD pointing toward the player's next waypoint
void DrawNextWaypointIndicator(Boat *player, Camera3D cam) {
    int nw = player->nextWP;
    Vector2 wpScreen = GetWorldToScreen(trackPts[nw], cam);
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    // Only show arrow if waypoint is off-screen or far away
    bool offScreen = (wpScreen.x < 0 || wpScreen.x > sw || wpScreen.y < 0 || wpScreen.y > sh);
    float dist = Vector3Distance(player->pos, trackPts[nw]);

    if (offScreen || dist > 40.0f) {
        // Direction from screen center to waypoint
        float cx = sw / 2.0f, cy = sh / 2.0f;
        float dx = wpScreen.x - cx, dy = wpScreen.y - cy;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 1.0f) { dx /= len; dy /= len; }
        // Clamp to edge of screen with margin
        float margin = 50.0f;
        float arrowX = cx + dx * (fminf(sw, sh) * 0.4f);
        float arrowY = cy + dy * (fminf(sw, sh) * 0.4f);
        if (arrowX < margin) arrowX = margin;
        if (arrowX > sw - margin) arrowX = sw - margin;
        if (arrowY < margin) arrowY = margin;
        if (arrowY > sh - margin) arrowY = sh - margin;
        // Arrow triangle
        float angle = atan2f(dy, dx);
        float sz = 12.0f;
        Vector2 tip = {arrowX + cosf(angle) * sz, arrowY + sinf(angle) * sz};
        Vector2 bl = {arrowX + cosf(angle + 2.5f) * sz, arrowY + sinf(angle + 2.5f) * sz};
        Vector2 br = {arrowX + cosf(angle - 2.5f) * sz, arrowY + sinf(angle - 2.5f) * sz};
        DrawTriangle(tip, bl, br, (Color){255, 220, 50, 200});
    }
}

void DrawWake(int boatIdx) {
    Wake *w = &wakes[boatIdx];
    if (w->count < 2) return;
    Boat *b = &boats[boatIdx];
    float right_x = cosf(b->rotation);
    float right_z = -sinf(b->rotation);

    for (int i = 1; i < w->count; i++) {
        int idx0 = (w->head - w->count + i - 1 + WAKE_POINTS) % WAKE_POINTS;
        int idx1 = (w->head - w->count + i + WAKE_POINTS) % WAKE_POINTS;
        Vector3 p0 = w->points[idx0];
        Vector3 p1 = w->points[idx1];
        // Clamp wake points to current water surface
        float wh0 = WaterHeight(p0.x, p0.z, gameTime) + 0.05f;
        float wh1 = WaterHeight(p1.x, p1.z, gameTime) + 0.05f;
        if (p0.y < wh0) p0.y = wh0;
        if (p1.y < wh1) p1.y = wh1;
        float t = (float)i / w->count;  // 0 = oldest, 1 = newest
        float width = (1.0f - t) * 2.0f + 0.3f;  // wider at back
        unsigned char alpha = (unsigned char)((1.0f - t) * 120);
        Color wc = {200, 230, 255, alpha};
        Vector3 l0 = {p0.x - right_x * width, p0.y, p0.z - right_z * width};
        Vector3 r0 = {p0.x + right_x * width, p0.y, p0.z + right_z * width};
        Vector3 l1 = {p1.x - right_x * width * 0.9f, p1.y, p1.z - right_z * width * 0.9f};
        Vector3 r1 = {p1.x + right_x * width * 0.9f, p1.y, p1.z + right_z * width * 0.9f};
        DrawTriangle3D(l0, r0, r1, wc);
        DrawTriangle3D(l0, r1, l1, wc);
        DrawTriangle3D(r1, r0, l0, wc);
        DrawTriangle3D(l1, r1, l0, wc);
    }
}

void DrawIslands(void) {
    for (int i = 0; i < numIslands; i++) {
        Island *isl = &islands[i];
        // Sandy beach base
        DrawCylinder(isl->pos, isl->radius * 1.1f, isl->radius * 0.9f, 0.3f, 12,
            (Color){220, 200, 150, 255});
        // Green hill
        Vector3 hillPos = {isl->pos.x, 0.2f, isl->pos.z};
        DrawCylinder(hillPos, isl->radius * 0.3f, isl->radius * 0.8f, isl->height, 10,
            (Color){50, 130, 50, 255});
        // Rocky peak
        Vector3 peak = {isl->pos.x, isl->height * 0.6f, isl->pos.z};
        DrawSphere(peak, isl->radius * 0.25f, (Color){100, 95, 85, 255});
    }
}

void DrawPalms(void) {
    for (int i = 0; i < numPalms; i++) {
        Palm *p = &palms[i];
        // Trunk: leaning cylinder
        Vector3 base = p->pos;
        Vector3 top = {
            base.x + sinf(p->leanDir) * p->lean * p->height,
            base.y + p->height,
            base.z + cosf(p->leanDir) * p->lean * p->height
        };
        DrawCylinderEx(base, top, 0.15f, 0.08f, 6, (Color){140, 100, 50, 255});
        // Fronds: small cones radiating from top
        for (int f = 0; f < 5; f++) {
            float fa = (float)f / 5 * 2.0f * PI;
            Vector3 frondEnd = {
                top.x + cosf(fa) * 2.0f,
                top.y - 0.8f,
                top.z + sinf(fa) * 2.0f
            };
            DrawCylinderEx(top, frondEnd, 0.02f, 0.3f, 4, (Color){40, 160, 40, 255});
        }
    }
}

void DrawBoatShadow(Boat *b) {
    // Dark ellipse on water below boat
    float wh = WaterHeight(b->pos.x, b->pos.z, gameTime);
    Vector3 shadowPos = {b->pos.x, wh + 0.03f, b->pos.z};
    float cy = cosf(b->rotation), sy = sinf(b->rotation);
    // Elongated shadow along forward axis
    int segs = 12;
    float lenHalf = 1.2f, widHalf = 0.5f;
    Color sc = {0, 0, 0, 50};
    for (int s = 0; s < segs; s++) {
        float a0 = (float)s / segs * 2.0f * PI;
        float a1 = (float)(s+1) / segs * 2.0f * PI;
        // Ellipse in local space then rotate by yaw
        float lx0 = cosf(a0) * widHalf, lz0 = sinf(a0) * lenHalf;
        float lx1 = cosf(a1) * widHalf, lz1 = sinf(a1) * lenHalf;
        Vector3 v0 = shadowPos;
        Vector3 v1 = {shadowPos.x + lx0*cy + lz0*sy, shadowPos.y, shadowPos.z - lx0*sy + lz0*cy};
        Vector3 v2 = {shadowPos.x + lx1*cy + lz1*sy, shadowPos.y, shadowPos.z - lx1*sy + lz1*cy};
        DrawTriangle3D(v0, v1, v2, sc);
        DrawTriangle3D(v2, v1, v0, sc);
    }
}

void DrawFoam(Vector3 camPos) {
    // White caps on wave peaks near camera
    float range = 80.0f;
    float cx = floorf(camPos.x / 6) * 6;
    float cz = floorf(camPos.z / 6) * 6;
    for (float gx = cx - range; gx < cx + range; gx += 6) {
        for (float gz = cz - range; gz < cz + range; gz += 6) {
            float h = WaterHeight(gx, gz, gameTime);
            if (h > BASE_WAVE_HEIGHT * 0.7f) {
                float intensity = (h - BASE_WAVE_HEIGHT * 0.7f) / (BASE_WAVE_HEIGHT * 0.5f);
                if (intensity > 1) intensity = 1;
                unsigned char alpha = (unsigned char)(intensity * 100);
                Vector3 fp = {gx, h + 0.05f, gz};
                float foamSize = 0.4f + intensity * 0.6f;
                Color fc = {255, 255, 255, alpha};
                // Small flat quad
                Vector3 v0 = {fp.x - foamSize, fp.y, fp.z - foamSize};
                Vector3 v1 = {fp.x + foamSize, fp.y, fp.z - foamSize};
                Vector3 v2 = {fp.x + foamSize, fp.y, fp.z + foamSize};
                Vector3 v3 = {fp.x - foamSize, fp.y, fp.z + foamSize};
                DrawTriangle3D(v0, v1, v2, fc);
                DrawTriangle3D(v0, v2, v3, fc);
            }
        }
    }
}

void DrawSkyGradient(Camera3D cam) {
    // Large dome of quads above — gradient from deep blue at horizon to light blue at zenith
    float skyR = 400.0f;
    int rings = 8;
    int segs = 16;
    for (int r = 0; r < rings; r++) {
        float t0 = (float)r / rings;
        float t1 = (float)(r + 1) / rings;
        float elev0 = t0 * PI * 0.45f;  // 0 = horizon, ~81 degrees = top
        float elev1 = t1 * PI * 0.45f;
        // Color gradient: horizon = warm light blue, zenith = deep blue
        Color c0 = {
            (unsigned char)(160 - t0 * 100),
            (unsigned char)(200 - t0 * 60),
            (unsigned char)(240 - t0 * 30), 255
        };
        Color c1 = {
            (unsigned char)(160 - t1 * 100),
            (unsigned char)(200 - t1 * 60),
            (unsigned char)(240 - t1 * 30), 255
        };
        // Use average color for the strip
        Color c = {(unsigned char)((c0.r + c1.r)/2), (unsigned char)((c0.g + c1.g)/2),
                   (unsigned char)((c0.b + c1.b)/2), 255};
        for (int s = 0; s < segs; s++) {
            float a0 = (float)s / segs * 2.0f * PI;
            float a1 = (float)(s + 1) / segs * 2.0f * PI;
            Vector3 v00 = {cam.position.x + cosf(a0) * cosf(elev0) * skyR,
                           cam.position.y + sinf(elev0) * skyR,
                           cam.position.z + sinf(a0) * cosf(elev0) * skyR};
            Vector3 v10 = {cam.position.x + cosf(a1) * cosf(elev0) * skyR,
                           cam.position.y + sinf(elev0) * skyR,
                           cam.position.z + sinf(a1) * cosf(elev0) * skyR};
            Vector3 v01 = {cam.position.x + cosf(a0) * cosf(elev1) * skyR,
                           cam.position.y + sinf(elev1) * skyR,
                           cam.position.z + sinf(a0) * cosf(elev1) * skyR};
            Vector3 v11 = {cam.position.x + cosf(a1) * cosf(elev1) * skyR,
                           cam.position.y + sinf(elev1) * skyR,
                           cam.position.z + sinf(a1) * cosf(elev1) * skyR};
            DrawTriangle3D(v00, v10, v11, c);
            DrawTriangle3D(v00, v11, v01, c);
            DrawTriangle3D(v11, v10, v00, c);
            DrawTriangle3D(v01, v11, v00, c);
        }
    }
}

void DrawClouds(void) {
    for (int i = 0; i < MAX_CLOUDS; i++) {
        CloudObj *cl = &clouds[i];
        // Main body
        DrawSphere(cl->pos, cl->scaleY, (Color){245, 245, 250, 230});
        // Puffs around it
        DrawSphere((Vector3){cl->pos.x - cl->scaleX * 0.4f, cl->pos.y - 0.3f, cl->pos.z},
            cl->scaleY * 0.8f, (Color){240, 240, 248, 220});
        DrawSphere((Vector3){cl->pos.x + cl->scaleX * 0.4f, cl->pos.y - 0.2f, cl->pos.z + 1.0f},
            cl->scaleY * 0.85f, (Color){242, 242, 250, 225});
        DrawSphere((Vector3){cl->pos.x, cl->pos.y + cl->scaleY * 0.3f, cl->pos.z - cl->scaleZ * 0.3f},
            cl->scaleY * 0.7f, (Color){248, 248, 255, 215});
        DrawSphere((Vector3){cl->pos.x + cl->scaleX * 0.2f, cl->pos.y - 0.5f, cl->pos.z - cl->scaleZ * 0.2f},
            cl->scaleY * 0.75f, (Color){238, 240, 248, 210});
    }
}

void UpdateBirds(float dt) {
    for (int i = 0; i < MAX_BIRDS; i++) {
        birds[i].angle += birds[i].speed * dt;
        birds[i].pos.x = birds[i].centerX + cosf(birds[i].angle) * birds[i].radius;
        birds[i].pos.z = birds[i].centerZ + sinf(birds[i].angle) * birds[i].radius;
    }
}

void DrawBirds(void) {
    for (int i = 0; i < MAX_BIRDS; i++) {
        Bird *bd = &birds[i];
        float flap = sinf(gameTime * 6.0f + bd->flapPhase) * 0.4f;
        float yaw = bd->angle + PI * 0.5f;  // face direction of travel
        float cy = cosf(yaw), sy = sinf(yaw);

        Vector3 body = bd->pos;
        // Body
        DrawSphere(body, 0.15f, (Color){30, 30, 30, 255});
        // Left wing
        Vector3 lw = {body.x + cy * 0.6f, body.y + flap, body.z - sy * 0.6f};
        Vector3 lwTip = {body.x + cy * 1.2f, body.y + flap * 1.5f, body.z - sy * 1.2f};
        DrawLine3D(body, lw, (Color){30, 30, 30, 255});
        DrawLine3D(lw, lwTip, (Color){30, 30, 30, 255});
        // Right wing
        Vector3 rw = {body.x - cy * 0.6f, body.y + flap, body.z + sy * 0.6f};
        Vector3 rwTip = {body.x - cy * 1.2f, body.y + flap * 1.5f, body.z + sy * 1.2f};
        DrawLine3D(body, rw, (Color){30, 30, 30, 255});
        DrawLine3D(rw, rwTip, (Color){30, 30, 30, 255});
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Boat Race");
    MaximizeWindow();
    SetTargetFPS(60);

    InitRace();

    Camera3D camera = {0};
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.up = (Vector3){0, 1, 0};

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        gameTime += dt;

        // Countdown
        if (!raceStarted) {
            countdownTimer -= dt;
            if (countdownTimer <= 0) raceStarted = true;
        }

        // Restart
        if (boats[playerIdx].finished && IsKeyPressed(KEY_ENTER)) InitRace();

        // Update boats
        for (int i = 0; i < NUM_BOATS; i++) UpdateBoat(&boats[i], i, dt);

        // Update birds
        UpdateBirds(dt);

        // Update spray
        for (int i = 0; i < MAX_SPRAY; i++) {
            if (!spray[i].active) continue;
            spray[i].pos = Vector3Add(spray[i].pos, Vector3Scale(spray[i].vel, dt));
            spray[i].vel.y -= 8.0f * dt;
            spray[i].life -= dt;
            if (spray[i].life <= 0) spray[i].active = false;
        }

        // Reset boost buoys periodically
        if ((int)(gameTime * 10) % 200 == 0) {
            for (int i = 0; i < numBuoys; i++)
                if (buoys[i].isBoost) buoys[i].collected = false;
        }

        // Screen shake decay
        if (shakeAmount > 0) shakeAmount -= shakeDecay * dt * shakeAmount;
        if (shakeAmount < 0.01f) shakeAmount = 0;

        // FOV scales with speed + extra during boost
        Boat *pb = &boats[playerIdx];
        float speedPct = pb->speed / BOAT_MAX_SPEED;
        if (speedPct > 1) speedPct = 1;
        float targetFov = 50.0f + speedPct * 15.0f;
        if (pb->boostTimer > 0) targetFov += 12.0f;
        camera.fovy += (targetFov - camera.fovy) * 6.0f * dt;

        // Camera: behind player boat
        float cy = cosf(pb->rotation), sy = sinf(pb->rotation);
        Vector3 camTarget = pb->pos;
        camTarget.y += 0.8f;
        camera.target = camTarget;
        camera.position = (Vector3){
            pb->pos.x - sy * 5.0f,
            pb->pos.y + 2.5f,
            pb->pos.z - cy * 5.0f
        };
        // Apply screen shake
        if (shakeAmount > 0.01f) {
            camera.position.x += (float)GetRandomValue(-100, 100) / 100.0f * shakeAmount;
            camera.position.y += (float)GetRandomValue(-100, 100) / 100.0f * shakeAmount;
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){60, 140, 220, 255});
        BeginMode3D(camera);

        // Sky gradient dome
        DrawSkyGradient(camera);

        // Clouds
        DrawClouds();

        // Birds
        DrawBirds();

        // Water
        DrawWater(camera.position);

        // Foam on wave peaks
        DrawFoam(camera.position);

        // Islands and palm trees
        DrawIslands();
        DrawPalms();

        // Track markers
        DrawTrackMarkers();

        // Buoys
        for (int i = 0; i < numBuoys; i++) {
            if (buoys[i].isBoost && buoys[i].collected) continue;
            Vector3 bp = buoys[i].pos;
            bp.y = WaterHeight(bp.x, bp.z, gameTime) + 0.3f;
            float bob = sinf(gameTime * 2.0f + bp.x * 0.5f) * 0.15f;
            bp.y += bob;
            Color bc;
            if (buoys[i].isBoost) bc = (Color){255, 220, 50, 255};
            else if (buoys[i].side < 0) bc = (Color){220, 50, 50, 255};
            else bc = (Color){50, 220, 50, 255};
            DrawSphere(bp, buoys[i].radius, bc);
            // Pole below buoy
            Vector3 pole = bp;
            pole.y -= 0.8f;
            DrawCylinder(pole, 0.05f, 0.05f, 0.8f, 4, (Color){100, 100, 100, 255});
        }

        // Start/finish gate
        {
            float hw = TRACK_WIDTH * 0.5f + 2.0f;
            Vector3 left = Vector3Add(trackPts[0], Vector3Scale(trackNormals[0], -hw));
            Vector3 right = Vector3Add(trackPts[0], Vector3Scale(trackNormals[0], hw));
            left.y = 1.0f; right.y = 1.0f;
            DrawCylinder(left, 0.2f, 0.2f, 6.0f, 6, (Color){200, 200, 200, 255});
            DrawCylinder(right, 0.2f, 0.2f, 6.0f, 6, (Color){200, 200, 200, 255});
            Vector3 bar1 = left; bar1.y = 7.0f;
            Vector3 bar2 = right; bar2.y = 7.0f;
            DrawCylinderEx(bar1, bar2, 0.15f, 0.15f, 6, (Color){220, 220, 40, 255});
            // Checkered banner
            float barLen = Vector3Distance(bar1, bar2);
            int checks = 10;
            for (int c = 0; c < checks; c++) {
                float t = (float)c / checks;
                Vector3 p = Vector3Lerp(bar1, bar2, t + 0.5f / checks);
                p.y -= 0.5f;
                Color chk = ((c % 2) == 0) ? WHITE : BLACK;
                DrawCube(p, barLen / checks * 0.9f, 0.8f, 0.05f, chk);
            }
        }

        // Boat shadows on water
        for (int i = 0; i < NUM_BOATS; i++) DrawBoatShadow(&boats[i]);

        // Wake trails
        for (int i = 0; i < NUM_BOATS; i++) DrawWake(i);

        // Boats
        for (int i = 0; i < NUM_BOATS; i++) DrawBoatModel(&boats[i]);

        // Spray particles
        for (int i = 0; i < MAX_SPRAY; i++) {
            if (!spray[i].active) continue;
            float alpha = spray[i].life / 0.8f;
            if (alpha > 1) alpha = 1;
            Color c = spray[i].color;
            c.a = (unsigned char)(alpha * c.a);
            DrawSphere(spray[i].pos, spray[i].size * alpha, c);
        }

        EndMode3D();

        // --- HUD ---
        // Waypoint direction arrow
        DrawNextWaypointIndicator(&boats[playerIdx], camera);
        Boat *player = &boats[playerIdx];

        // Position
        int position = 1;
        for (int i = 0; i < NUM_BOATS; i++) {
            if (i == playerIdx) continue;
            if (boats[i].lap > player->lap ||
                (boats[i].lap == player->lap && boats[i].nextWP > player->nextWP))
                position++;
        }
        DrawText(TextFormat("%s", position == 1 ? "1ST" : position == 2 ? "2ND" :
            position == 3 ? "3RD" : "4TH"), 20, 20, 30, GOLD);

        // Lap counter
        int dispLap = player->lap + 1;
        if (dispLap > TOTAL_LAPS) dispLap = TOTAL_LAPS;
        DrawText(TextFormat("LAP %d/%d", dispLap, TOTAL_LAPS), 20, 55, 18, WHITE);

        // Speed bar
        float spdPct = player->speed / BOAT_MAX_SPEED;
        if (spdPct > 1) spdPct = 1;
        DrawRectangle(20, 80, 150, 14, (Color){40, 40, 50, 255});
        DrawRectangle(20, 80, (int)(150 * spdPct), 14, (Color){50, 180, 255, 255});
        DrawRectangleLines(20, 80, 150, 14, (Color){100, 100, 120, 255});
        DrawText("SPEED", 175, 80, 12, (Color){150, 150, 160, 255});

        // Boost gauge
        DrawRectangle(20, 100, 150, 14, (Color){40, 40, 50, 255});
        DrawRectangle(20, 100, (int)(150 * player->boostFuel), 14, (Color){255, 200, 50, 255});
        DrawRectangleLines(20, 100, 150, 14, (Color){100, 100, 120, 255});
        DrawText("BOOST [SPACE]", 175, 100, 12, (Color){150, 150, 160, 255});

        // Timer
        if (raceStarted && !player->finished) raceTime += dt;
        int mins = (int)raceTime / 60;
        float secs = raceTime - mins * 60;
        DrawText(TextFormat("%d:%05.2f", mins, secs),
            GetScreenWidth() - 130, 20, 20, WHITE);

        // Countdown
        if (!raceStarted && countdownTimer > 0) {
            int cnt = (int)countdownTimer;
            const char *txt = cnt >= 3 ? "3" : cnt >= 2 ? "2" : cnt >= 1 ? "1" : "GO!";
            Color cc = cnt >= 1 ? RED : GREEN;
            int tw = MeasureText(txt, 60);
            DrawText(txt, GetScreenWidth()/2 - tw/2, GetScreenHeight()/2 - 40, 60, cc);
        }

        // Finish
        if (player->finished) {
            const char *fTxt = "RACE COMPLETE!";
            int fw = MeasureText(fTxt, 36);
            DrawText(fTxt, GetScreenWidth()/2 - fw/2, GetScreenHeight()/2 - 50, 36, GOLD);
            DrawText(TextFormat("Position: %s", position == 1 ? "1ST" : position == 2 ? "2ND" :
                position == 3 ? "3RD" : "4TH"),
                GetScreenWidth()/2 - 60, GetScreenHeight()/2, 20, WHITE);
            DrawText(TextFormat("Time: %d:%05.2f", mins, secs),
                GetScreenWidth()/2 - 60, GetScreenHeight()/2 + 25, 20, WHITE);
            DrawText("Press ENTER to restart", GetScreenWidth()/2 - 100, GetScreenHeight()/2 + 55, 16, (Color){180,180,180,255});
        }

        // Status indicators
        if (player->drafting)
            DrawText("DRAFTING", 20, 120, 14, (Color){100, 200, 255, 255});
        if (player->drifting)
            DrawText(TextFormat("DRIFT! %.1fs", player->driftTimer), 20, 138, 14, (Color){255, 150, 50, 255});
        if (player->airborne)
            DrawText("AIRBORNE!", 20, 156, 14, (Color){255, 255, 100, 255});

        // Controls hint
        DrawText("Arrows: Steer  SPACE: Boost  Z/Shift+Turn: Drift", 10, GetScreenHeight() - 25, 12, (Color){100,100,120,255});

        DrawFPS(GetScreenWidth() - 80, GetScreenHeight() - 20);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
