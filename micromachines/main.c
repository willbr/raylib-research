#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

// Track
#define TRACK_SEGS      48
#define TRACK_WIDTH     40.0f
#define NUM_CARS         4
#define TOTAL_LAPS       3

// Car physics (top-down arcade style)
#define CAR_ACCEL       280.0f
#define CAR_BRAKE       350.0f
#define CAR_MAX_SPEED   220.0f
#define CAR_TURN_SPEED    4.0f
#define CAR_DRAG          0.98f
#define CAR_OFFTRACK_DRAG 0.92f
#define CAR_LENGTH       10.0f
#define CAR_WIDTH         6.0f

// Items on track
#define MAX_OBSTACLES   20
#define MAX_BOOSTS       8
#define BOOST_MULT       1.6f
#define BOOST_TIME       1.0f
#define OIL_SPIN_TIME    0.8f

typedef enum {
    OBS_OIL,
    OBS_CONE,
} ObsType;

typedef struct {
    Vector2 pos;
    ObsType type;
    bool active;
} Obstacle;

typedef struct {
    Vector2 pos;
    float angle;    // rotation of boost pad
    bool active;
    float respawn;
} BoostPad;

typedef struct {
    Vector2 pos;
    float rotation;  // radians
    float speed;
    float angVel;    // for spin-outs
    bool spinning;
    float spinTimer;
    float boostTimer;
    int lap;
    int nextWP;
    bool isPlayer;
    bool finished;
    Color color;
    // AI
    float aiSteerNoise;
} Car;

// Track data
static Vector2 trackPts[TRACK_SEGS];
static Vector2 trackNormals[TRACK_SEGS];
static Obstacle obstacles[MAX_OBSTACLES];
static BoostPad boosts[MAX_BOOSTS];
static int numObstacles = 0;
static int numBoosts = 0;

void GenerateTrack(void) {
    // Kitchen table shaped track — irregular loop with tight corners
    // Generate using a deformed ellipse with bumps
    for (int i = 0; i < TRACK_SEGS; i++) {
        float t = (float)i / TRACK_SEGS * 2.0f * PI;
        // Base ellipse
        float rx = 200.0f, ry = 140.0f;
        // Add bumps for interesting corners
        float bump = sinf(t * 3.0f) * 40.0f + cosf(t * 5.0f) * 20.0f;
        float r = sqrtf((rx * cosf(t)) * (rx * cosf(t)) + (ry * sinf(t)) * (ry * sinf(t)));
        r += bump;
        trackPts[i] = (Vector2){ cosf(t) * r, sinf(t) * r };
    }

    // Smooth the track (average neighbors a few times)
    for (int pass = 0; pass < 3; pass++) {
        Vector2 temp[TRACK_SEGS];
        for (int i = 0; i < TRACK_SEGS; i++) {
            int prev = (i - 1 + TRACK_SEGS) % TRACK_SEGS;
            int next = (i + 1) % TRACK_SEGS;
            temp[i] = (Vector2){
                (trackPts[prev].x + trackPts[i].x * 2 + trackPts[next].x) / 4.0f,
                (trackPts[prev].y + trackPts[i].y * 2 + trackPts[next].y) / 4.0f
            };
        }
        for (int i = 0; i < TRACK_SEGS; i++) trackPts[i] = temp[i];
    }

    // Normals
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        Vector2 dir = Vector2Subtract(trackPts[next], trackPts[i]);
        dir = Vector2Normalize(dir);
        trackNormals[i] = (Vector2){ -dir.y, dir.x };
    }

    // Place obstacles along track
    numObstacles = 0;
    for (int i = 4; i < TRACK_SEGS; i += 5) {
        if (numObstacles >= MAX_OBSTACLES) break;
        float side = ((i % 2) == 0) ? -1.0f : 1.0f;
        float offset = side * TRACK_WIDTH * 0.3f;
        obstacles[numObstacles].pos = Vector2Add(trackPts[i], Vector2Scale(trackNormals[i], offset));
        obstacles[numObstacles].type = (i % 3 == 0) ? OBS_OIL : OBS_CONE;
        obstacles[numObstacles].active = true;
        numObstacles++;
    }

    // Place boost pads
    numBoosts = 0;
    for (int i = 6; i < TRACK_SEGS; i += 12) {
        if (numBoosts >= MAX_BOOSTS) break;
        boosts[numBoosts].pos = trackPts[i];
        int next = (i + 1) % TRACK_SEGS;
        Vector2 dir = Vector2Subtract(trackPts[next], trackPts[i]);
        boosts[numBoosts].angle = atan2f(dir.y, dir.x);
        boosts[numBoosts].active = true;
        boosts[numBoosts].respawn = 0;
        numBoosts++;
    }
}

int NearestSegment(Vector2 pos) {
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < TRACK_SEGS; i++) {
        float d = Vector2Distance(pos, trackPts[i]);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

float DistFromTrack(Vector2 pos) {
    int seg = NearestSegment(pos);
    int next = (seg + 1) % TRACK_SEGS;
    Vector2 segDir = Vector2Subtract(trackPts[next], trackPts[seg]);
    float segLen = Vector2Length(segDir);
    if (segLen < 0.001f) return Vector2Distance(pos, trackPts[seg]);
    segDir = Vector2Scale(segDir, 1.0f / segLen);
    Vector2 toPos = Vector2Subtract(pos, trackPts[seg]);
    float dot = Vector2DotProduct(toPos, segDir);
    Vector2 closest = Vector2Add(trackPts[seg], Vector2Scale(segDir, Clamp(dot, 0, segLen)));
    return Vector2Distance(pos, closest);
}

float CarProgress(Car *c) {
    return (float)c->lap * TRACK_SEGS + (float)c->nextWP;
}

int CompareRank(const void *a, const void *b) {
    float pa = CarProgress((Car *)a);
    float pb = CarProgress((Car *)b);
    return (pa < pb) - (pa > pb);
}

void DrawCar(Car *c) {
    float cs = cosf(c->rotation), sn = sinf(c->rotation);
    float hw = CAR_WIDTH / 2.0f, hl = CAR_LENGTH / 2.0f;

    // Four corners of the car body
    Vector2 corners[4] = {
        { c->pos.x + (-hl)*cs - (-hw)*sn, c->pos.y + (-hl)*sn + (-hw)*cs },
        { c->pos.x + ( hl)*cs - (-hw)*sn, c->pos.y + ( hl)*sn + (-hw)*cs },
        { c->pos.x + ( hl)*cs - ( hw)*sn, c->pos.y + ( hl)*sn + ( hw)*cs },
        { c->pos.x + (-hl)*cs - ( hw)*sn, c->pos.y + (-hl)*sn + ( hw)*cs },
    };

    // Body
    DrawTriangle(corners[0], corners[1], corners[2], c->color);
    DrawTriangle(corners[0], corners[2], corners[3], c->color);

    // Darker roof
    Color roof = { c->color.r * 3/4, c->color.g * 3/4, c->color.b * 3/4, 255 };
    float rScale = 0.6f;
    Vector2 rCorners[4];
    for (int i = 0; i < 4; i++) {
        rCorners[i] = Vector2Lerp(c->pos, corners[i], rScale);
    }
    DrawTriangle(rCorners[0], rCorners[1], rCorners[2], roof);
    DrawTriangle(rCorners[0], rCorners[2], rCorners[3], roof);

    // Windshield (front)
    Vector2 ws0 = Vector2Lerp(corners[1], corners[0], 0.25f);
    Vector2 ws1 = Vector2Lerp(corners[2], corners[3], 0.25f);
    Vector2 ws2 = Vector2Lerp(corners[1], corners[0], 0.4f);
    Vector2 ws3 = Vector2Lerp(corners[2], corners[3], 0.4f);
    DrawTriangle(ws0, ws2, ws3, SKYBLUE);
    DrawTriangle(ws0, ws3, ws1, SKYBLUE);

    // Outline
    for (int i = 0; i < 4; i++) {
        DrawLineV(corners[i], corners[(i+1)%4], BLACK);
    }

    // Boost flame
    if (c->boostTimer > 0) {
        Vector2 back = { c->pos.x - cs * hl * 1.3f, c->pos.y - sn * hl * 1.3f };
        DrawCircleV(back, 3, ORANGE);
        DrawCircleV(back, 2, YELLOW);
    }

    // Spin indicator
    if (c->spinning) {
        DrawCircleLines(c->pos.x, c->pos.y, CAR_LENGTH * 0.8f, YELLOW);
    }
}

void DrawTrack(void) {
    // Track surface
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        Vector2 p0 = trackPts[i], p1 = trackPts[next];
        Vector2 n0 = trackNormals[i], n1 = trackNormals[next];
        float hw = TRACK_WIDTH / 2.0f;

        Vector2 inner0 = Vector2Add(p0, Vector2Scale(n0, -hw));
        Vector2 outer0 = Vector2Add(p0, Vector2Scale(n0,  hw));
        Vector2 inner1 = Vector2Add(p1, Vector2Scale(n1, -hw));
        Vector2 outer1 = Vector2Add(p1, Vector2Scale(n1,  hw));

        // Road
        Color roadCol = (i % 4 < 2) ? (Color){90, 90, 95, 255} : (Color){85, 85, 90, 255};
        DrawTriangle(inner0, outer0, inner1, roadCol);
        DrawTriangle(outer0, outer1, inner1, roadCol);

        // Edge lines (white dashes)
        if (i % 3 == 0) {
            DrawLineEx(inner0, inner1, 1.0f, WHITE);
            DrawLineEx(outer0, outer1, 1.0f, WHITE);
        }

        // Center dashes
        Vector2 center0 = Vector2Lerp(inner0, outer0, 0.5f);
        Vector2 center1 = Vector2Lerp(inner1, outer1, 0.5f);
        if (i % 4 == 0) {
            DrawLineEx(center0, center1, 0.8f, YELLOW);
        }
    }

    // Start/finish line
    Vector2 p = trackPts[0];
    Vector2 n = trackNormals[0];
    float hw = TRACK_WIDTH / 2.0f;
    Vector2 left = Vector2Add(p, Vector2Scale(n, -hw));
    Vector2 right = Vector2Add(p, Vector2Scale(n, hw));
    // Checkered pattern
    int checks = 8;
    for (int c = 0; c < checks; c++) {
        float t0 = (float)c / checks;
        float t1 = (float)(c+1) / checks;
        Vector2 a = Vector2Lerp(left, right, t0);
        Vector2 b = Vector2Lerp(left, right, t1);
        int next = 1 % TRACK_SEGS;
        Vector2 dir = Vector2Normalize(Vector2Subtract(trackPts[next], p));
        Vector2 a2 = Vector2Add(a, Vector2Scale(dir, 4));
        Vector2 b2 = Vector2Add(b, Vector2Scale(dir, 4));
        Color chk = ((c % 2) == 0) ? WHITE : BLACK;
        DrawTriangle(a, b, b2, chk);
        DrawTriangle(a, b2, a2, chk);
    }
}

void DrawObstacles(void) {
    for (int i = 0; i < numObstacles; i++) {
        if (!obstacles[i].active) continue;
        if (obstacles[i].type == OBS_OIL) {
            // Oil slick — dark circle
            DrawCircleV(obstacles[i].pos, 8, (Color){30, 30, 40, 200});
            DrawCircleV(obstacles[i].pos, 5, (Color){20, 20, 30, 180});
            // Rainbow sheen
            DrawCircleV(Vector2Add(obstacles[i].pos, (Vector2){2, -2}), 3, (Color){80, 60, 120, 80});
        } else {
            // Cone — orange triangle
            DrawCircleV(obstacles[i].pos, 4, ORANGE);
            DrawCircleV(obstacles[i].pos, 2, WHITE);
        }
    }
}

void DrawBoostPads(void) {
    for (int i = 0; i < numBoosts; i++) {
        if (!boosts[i].active) continue;
        float cs = cosf(boosts[i].angle), sn = sinf(boosts[i].angle);
        Vector2 p = boosts[i].pos;
        // Arrow shape
        float pulse = 0.8f + sinf((float)GetTime() * 6.0f + i) * 0.2f;
        Color col = (Color){ 255, (unsigned char)(100 * pulse), 0, 200 };

        Vector2 tip = { p.x + cs * 8, p.y + sn * 8 };
        Vector2 bl = { p.x - cs * 6 - sn * 5, p.y - sn * 6 + cs * 5 };
        Vector2 br = { p.x - cs * 6 + sn * 5, p.y - sn * 6 - cs * 5 };
        DrawTriangle(tip, br, bl, col);

        // Second smaller arrow behind
        Vector2 tip2 = { p.x - cs * 2, p.y - sn * 2 };
        Vector2 bl2 = { p.x - cs * 10 - sn * 3, p.y - sn * 10 + cs * 3 };
        Vector2 br2 = { p.x - cs * 10 + sn * 3, p.y - sn * 10 - cs * 3 };
        Color col2 = col; col2.a = 120;
        DrawTriangle(tip2, br2, bl2, col2);
    }
}

int main(void) {
    InitWindow(800, 600, "Micro Machines - Top Down Racing");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    GenerateTrack();

    // Init cars
    Car cars[NUM_CARS];
    Color carColors[] = { BLUE, RED, (Color){0,180,0,255}, YELLOW };
    for (int i = 0; i < NUM_CARS; i++) {
        float offset = (float)(i - NUM_CARS/2) * (TRACK_WIDTH / (NUM_CARS + 1));
        cars[i].pos = Vector2Add(trackPts[0], Vector2Scale(trackNormals[0], offset));
        Vector2 dir = Vector2Subtract(trackPts[1], trackPts[0]);
        cars[i].rotation = atan2f(dir.y, dir.x);
        cars[i].speed = 0;
        cars[i].angVel = 0;
        cars[i].spinning = false;
        cars[i].spinTimer = 0;
        cars[i].boostTimer = 0;
        cars[i].lap = 0;
        cars[i].nextWP = 1;
        cars[i].isPlayer = (i == 0);
        cars[i].finished = false;
        cars[i].color = carColors[i];
        cars[i].aiSteerNoise = (float)GetRandomValue(-30, 30) / 100.0f;
    }

    Camera2D camera = { 0 };
    camera.zoom = 1.5f;

    bool raceOver = false;
    int playerRank = 0;
    float countdown = 3.99f;
    bool raceStarted = false;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;

        float screenW = (float)GetScreenWidth();
        float screenH = (float)GetScreenHeight();

        // Countdown
        if (!raceStarted) {
            countdown -= dt;
            if (countdown <= 0.0f) raceStarted = true;
        }

        // --- Update cars ---
        for (int c = 0; c < NUM_CARS; c++) {
            Car *car = &cars[c];
            if (car->finished) continue;

            // Spin-out
            if (car->spinning) {
                car->spinTimer -= dt;
                car->rotation += car->angVel * dt;
                car->speed *= 0.96f;
                if (car->spinTimer <= 0) {
                    car->spinning = false;
                    car->angVel = 0;
                }
                goto moveCar;
            }

            float accel = 0, steer = 0;
            float maxSpd = CAR_MAX_SPEED;
            if (car->boostTimer > 0) {
                car->boostTimer -= dt;
                maxSpd *= BOOST_MULT;
            }

            if (!raceStarted) goto skipInput;

            if (car->isPlayer) {
                if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))     accel =  CAR_ACCEL;
                if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))   accel = -CAR_BRAKE;
                if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))   steer =  CAR_TURN_SPEED;
                if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))  steer = -CAR_TURN_SPEED;
            } else {
                // AI: steer toward next waypoint
                Vector2 target = trackPts[car->nextWP];
                // Add some offset noise so AI cars don't all drive the same line
                target = Vector2Add(target, Vector2Scale(trackNormals[car->nextWP], car->aiSteerNoise * TRACK_WIDTH * 0.3f));

                Vector2 toTarget = Vector2Subtract(target, car->pos);
                float targetAngle = atan2f(toTarget.y, toTarget.x);
                float angleDiff = targetAngle - car->rotation;
                while (angleDiff > PI)  angleDiff -= 2.0f * PI;
                while (angleDiff < -PI) angleDiff += 2.0f * PI;

                steer = Clamp(angleDiff * 4.0f, -CAR_TURN_SPEED, CAR_TURN_SPEED);
                accel = CAR_ACCEL * 0.88f;

                // Rubber banding
                float playerProg = CarProgress(&cars[0]);
                float myProg = CarProgress(car);
                if (myProg > playerProg + 4) accel *= 0.7f;
                else if (myProg < playerProg - 4) accel *= 1.15f;

                // Slow for sharp turns
                if (fabsf(angleDiff) > 0.5f) accel *= 0.6f;
            }

skipInput:
            // Physics
            car->speed += accel * dt;

            // Drag
            float trackDist = DistFromTrack(car->pos);
            bool offTrack = (trackDist > TRACK_WIDTH / 2.0f);
            car->speed *= offTrack ? CAR_OFFTRACK_DRAG : CAR_DRAG;
            car->speed = Clamp(car->speed, -60.0f, maxSpd);

            // Steering (speed-dependent, reversed when going backward)
            float turnMult = Clamp(fabsf(car->speed) / 80.0f, 0.2f, 1.0f);
            if (car->speed < 0) steer = -steer;
            car->rotation += steer * turnMult * dt;

moveCar:;
            // Move
            float cs = cosf(car->rotation), sn = sinf(car->rotation);
            car->pos.x += cs * car->speed * dt;
            car->pos.y += sn * car->speed * dt;

            // Waypoint progression
            float wpDist = Vector2Distance(car->pos, trackPts[car->nextWP]);
            if (wpDist < 25.0f) {
                int prev = car->nextWP;
                car->nextWP = (car->nextWP + 1) % TRACK_SEGS;
                if (car->nextWP == 0 && prev == TRACK_SEGS - 1) {
                    car->lap++;
                    if (car->lap >= TOTAL_LAPS) {
                        car->finished = true;
                        if (car->isPlayer) raceOver = true;
                    }
                }
            }

            // Obstacle collision
            for (int i = 0; i < numObstacles; i++) {
                if (!obstacles[i].active) continue;
                float d = Vector2Distance(car->pos, obstacles[i].pos);
                if (d < 10.0f) {
                    if (obstacles[i].type == OBS_OIL && !car->spinning) {
                        car->spinning = true;
                        car->spinTimer = OIL_SPIN_TIME;
                        car->angVel = (GetRandomValue(0, 1) ? 1 : -1) * 12.0f;
                    } else if (obstacles[i].type == OBS_CONE) {
                        car->speed *= 0.5f;
                        obstacles[i].active = false;
                    }
                }
            }

            // Boost pad collision
            for (int i = 0; i < numBoosts; i++) {
                if (!boosts[i].active) continue;
                float d = Vector2Distance(car->pos, boosts[i].pos);
                if (d < 10.0f) {
                    car->boostTimer = BOOST_TIME;
                    car->speed = fmaxf(car->speed, CAR_MAX_SPEED * 1.2f);
                    boosts[i].active = false;
                    boosts[i].respawn = 5.0f;
                }
            }
        }

        // Car-car collision
        for (int i = 0; i < NUM_CARS; i++) {
            for (int j = i + 1; j < NUM_CARS; j++) {
                Vector2 diff = Vector2Subtract(cars[i].pos, cars[j].pos);
                float dist = Vector2Length(diff);
                float minDist = CAR_LENGTH;
                if (dist < minDist && dist > 0.001f) {
                    Vector2 push = Vector2Scale(Vector2Normalize(diff), (minDist - dist) * 0.5f);
                    cars[i].pos = Vector2Add(cars[i].pos, push);
                    cars[j].pos = Vector2Subtract(cars[j].pos, push);
                    // Speed exchange on bump
                    float avg = (cars[i].speed + cars[j].speed) * 0.5f;
                    cars[i].speed = avg * 0.95f;
                    cars[j].speed = avg * 0.95f;
                }
            }
        }

        // Respawn boost pads
        for (int i = 0; i < numBoosts; i++) {
            if (!boosts[i].active) {
                boosts[i].respawn -= dt;
                if (boosts[i].respawn <= 0) boosts[i].active = true;
            }
        }

        // --- Camera: follow player, slight lead ---
        {
            Car *p = &cars[0];
            float lead = p->speed * 0.15f;
            float cs = cosf(p->rotation), sn = sinf(p->rotation);
            Vector2 target = { p->pos.x + cs * lead, p->pos.y + sn * lead };
            camera.target = Vector2Lerp(camera.target, target, 6.0f * dt);
            camera.offset = (Vector2){ screenW / 2.0f, screenH / 2.0f };

            // Dynamic zoom: zoom out at speed
            float targetZoom = 1.8f - Clamp(fabsf(p->speed) / CAR_MAX_SPEED, 0, 1) * 0.5f;
            camera.zoom += (targetZoom - camera.zoom) * 3.0f * dt;
        }

        // --- Rankings ---
        Car ranked[NUM_CARS];
        for (int i = 0; i < NUM_CARS; i++) ranked[i] = cars[i];
        qsort(ranked, NUM_CARS, sizeof(Car), CompareRank);
        for (int i = 0; i < NUM_CARS; i++) {
            if (ranked[i].isPlayer) { playerRank = i + 1; break; }
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){ 60, 120, 50, 255 }); // grass

        BeginMode2D(camera);
            // Ground texture: darker patches for grass variety
            for (int gx = -400; gx < 400; gx += 30) {
                for (int gy = -300; gy < 300; gy += 30) {
                    if ((gx/30 + gy/30) % 3 == 0)
                        DrawRectangle(gx, gy, 30, 30, (Color){55, 110, 45, 255});
                }
            }

            DrawTrack();
            DrawBoostPads();
            DrawObstacles();

            // Draw shadows under cars
            for (int c = 0; c < NUM_CARS; c++) {
                DrawEllipse(cars[c].pos.x + 2, cars[c].pos.y + 2,
                           CAR_LENGTH/2, CAR_WIDTH/2, (Color){0,0,0,60});
            }

            // Draw cars (sorted by Y for simple depth)
            // Simple bubble sort for 4 cars
            int order[NUM_CARS] = {0, 1, 2, 3};
            for (int i = 0; i < NUM_CARS - 1; i++)
                for (int j = 0; j < NUM_CARS - 1 - i; j++)
                    if (cars[order[j]].pos.y > cars[order[j+1]].pos.y) {
                        int tmp = order[j]; order[j] = order[j+1]; order[j+1] = tmp;
                    }
            for (int i = 0; i < NUM_CARS; i++) DrawCar(&cars[order[i]]);

        EndMode2D();

        // --- HUD ---
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();

        // Position
        const char *ordinals[] = { "1st", "2nd", "3rd", "4th" };
        DrawText(ordinals[playerRank - 1], sw - 90, 15, 44, WHITE);

        // Lap
        int dispLap = cars[0].lap + 1;
        if (dispLap > TOTAL_LAPS) dispLap = TOTAL_LAPS;
        DrawText(TextFormat("LAP %d/%d", dispLap, TOTAL_LAPS), sw - 130, 60, 22, WHITE);

        // Speed bar
        float speedPct = Clamp(fabsf(cars[0].speed) / CAR_MAX_SPEED, 0, 1.5f);
        DrawRectangle(20, sh - 35, 120, 16, (Color){40,40,40,180});
        Color barCol = (cars[0].boostTimer > 0) ? ORANGE : GREEN;
        DrawRectangle(21, sh - 34, (int)(118 * Clamp(speedPct, 0, 1)), 14, barCol);
        DrawText("SPD", 25, sh - 33, 12, WHITE);

        // Boost indicator
        if (cars[0].boostTimer > 0) {
            DrawText("BOOST!", sw/2 - 35, 20, 24, ORANGE);
        }
        if (cars[0].spinning) {
            DrawText("SPIN!", sw/2 - 28, 20, 24, YELLOW);
        }

        // Countdown
        if (!raceStarted) {
            int cd = (int)ceilf(countdown);
            if (cd > 0) {
                const char *cdText = TextFormat("%d", cd);
                int cdw = MeasureText(cdText, 80);
                DrawText(cdText, sw/2 - cdw/2, sh/2 - 40, 80, WHITE);
            }
        }
        // "GO!" flash
        if (raceStarted && countdown > -0.8f) {
            int gow = MeasureText("GO!", 80);
            DrawText("GO!", sw/2 - gow/2, sh/2 - 40, 80, GREEN);
        }

        // Race finish
        if (raceOver) {
            const char *finText = TextFormat("FINISH! %s place", ordinals[playerRank - 1]);
            int fw = MeasureText(finText, 44);
            DrawRectangle(sw/2 - fw/2 - 10, sh/2 - 30, fw + 20, 55, (Color){0,0,0,180});
            DrawText(finText, sw/2 - fw/2, sh/2 - 22, 44, GOLD);
        }

        // Mini-map
        {
            float mmScale = 0.18f;
            int mmx = sw - 130, mmy = sh - 130;
            DrawRectangle(mmx - 5, mmy - 5, 125, 125, (Color){0,0,0,120});

            // Track outline on minimap
            for (int i = 0; i < TRACK_SEGS; i++) {
                int next = (i + 1) % TRACK_SEGS;
                Vector2 a = { mmx + 55 + trackPts[i].x * mmScale, mmy + 55 + trackPts[i].y * mmScale };
                Vector2 b = { mmx + 55 + trackPts[next].x * mmScale, mmy + 55 + trackPts[next].y * mmScale };
                DrawLineV(a, b, (Color){120,120,120,200});
            }

            // Cars on minimap
            for (int c = 0; c < NUM_CARS; c++) {
                float mx = mmx + 55 + cars[c].pos.x * mmScale;
                float my = mmy + 55 + cars[c].pos.y * mmScale;
                DrawCircle(mx, my, cars[c].isPlayer ? 3 : 2, cars[c].color);
            }
        }

        // Controls
        DrawText("WASD/Arrows: Drive   Avoid oil & cones   Hit boost pads!", 10, sh - 18, 12, (Color){180,180,180,200});

        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
