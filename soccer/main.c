#include "raylib.h"
#include "raymath.h"
#include <math.h>

// Pitch dimensions (real proportions scaled down)
#define PITCH_W 68.0f
#define PITCH_H 105.0f
#define HALF_W  (PITCH_W / 2.0f)
#define HALF_H  (PITCH_H / 2.0f)

// Goal dimensions
#define GOAL_WIDTH   7.32f
#define GOAL_DEPTH   2.5f
#define GOAL_HEIGHT  2.44f

// Gameplay
#define TEAM_SIZE       5
#define PLAYER_RADIUS   0.5f
#define PLAYER_SPEED    12.0f
#define AI_SPEED        9.0f
#define SPRINT_MULT     1.5f
#define BALL_RADIUS     0.3f
#define BALL_FRICTION   0.985f
#define KICK_POWER      22.0f
#define PASS_POWER      16.0f
#define SHOOT_POWER     28.0f
#define SWITCH_COOLDOWN 0.3f

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float radius;
} Ball;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float rotation;  // facing angle in radians
    bool hasBall;
    int team;        // 0 = player team (blue), 1 = AI team (red)
} Player;

// Formation positions (relative to own half, mirrored for away team)
// Stored as (x, z) offsets from center
static const Vector2 formations[TEAM_SIZE] = {
    {  0.0f, -40.0f },  // GK
    { -15.0f, -15.0f }, // LB
    {  15.0f, -15.0f }, // RB
    { -10.0f,  10.0f }, // LF
    {  10.0f,  10.0f }, // RF
};

// Find nearest player on a team to a point
int NearestPlayer(Player *players, int team, Vector3 point) {
    int best = -1;
    float bestDist = 1e9f;
    for (int i = 0; i < TEAM_SIZE; i++) {
        int idx = team * TEAM_SIZE + i;
        float d = Vector3Distance(players[idx].pos, point);
        if (d < bestDist) {
            bestDist = d;
            best = idx;
        }
    }
    return best;
}

// Clamp ball/player to pitch bounds
Vector3 ClampToPitch(Vector3 p, float margin) {
    p.x = Clamp(p.x, -HALF_W + margin, HALF_W - margin);
    p.z = Clamp(p.z, -HALF_H + margin, HALF_H - margin);
    return p;
}

// Check if position is inside a goal
int InsideGoal(Vector3 p) {
    float halfGoal = GOAL_WIDTH / 2.0f;
    // Home goal (z = -HALF_H)
    if (p.z < -HALF_H && p.z > -HALF_H - GOAL_DEPTH &&
        p.x > -halfGoal && p.x < halfGoal && p.y < GOAL_HEIGHT)
        return 1;  // scored on home side
    // Away goal (z = +HALF_H)
    if (p.z > HALF_H && p.z < HALF_H + GOAL_DEPTH &&
        p.x > -halfGoal && p.x < halfGoal && p.y < GOAL_HEIGHT)
        return 2;  // scored on away side
    return 0;
}

void ResetPositions(Player *players, Ball *ball) {
    ball->pos = (Vector3){ 0.0f, BALL_RADIUS, 0.0f };
    ball->vel = (Vector3){ 0.0f, 0.0f, 0.0f };

    for (int t = 0; t < 2; t++) {
        float side = (t == 0) ? 1.0f : -1.0f;
        for (int i = 0; i < TEAM_SIZE; i++) {
            int idx = t * TEAM_SIZE + i;
            players[idx].pos = (Vector3){
                formations[i].x,
                0.0f,
                formations[i].y * side
            };
            players[idx].vel = (Vector3){ 0 };
            players[idx].rotation = (t == 0) ? 0.0f : PI;
            players[idx].hasBall = false;
            players[idx].team = t;
        }
    }
}

void DrawPitch(void) {
    // Green grass
    DrawPlane((Vector3){ 0, 0, 0 }, (Vector2){ PITCH_W, PITCH_H }, DARKGREEN);

    float lineY = 0.02f;
    Color lineCol = WHITE;

    // Touchlines and goal lines
    DrawLine3D((Vector3){ -HALF_W, lineY, -HALF_H }, (Vector3){  HALF_W, lineY, -HALF_H }, lineCol);
    DrawLine3D((Vector3){ -HALF_W, lineY,  HALF_H }, (Vector3){  HALF_W, lineY,  HALF_H }, lineCol);
    DrawLine3D((Vector3){ -HALF_W, lineY, -HALF_H }, (Vector3){ -HALF_W, lineY,  HALF_H }, lineCol);
    DrawLine3D((Vector3){  HALF_W, lineY, -HALF_H }, (Vector3){  HALF_W, lineY,  HALF_H }, lineCol);

    // Halfway line
    DrawLine3D((Vector3){ -HALF_W, lineY, 0 }, (Vector3){ HALF_W, lineY, 0 }, lineCol);

    // Center circle (approximated with line segments)
    float centerR = 9.15f;
    int segs = 32;
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / segs * 2.0f * PI;
        float a1 = (float)(i + 1) / segs * 2.0f * PI;
        DrawLine3D(
            (Vector3){ cosf(a0) * centerR, lineY, sinf(a0) * centerR },
            (Vector3){ cosf(a1) * centerR, lineY, sinf(a1) * centerR },
            lineCol);
    }

    // Center spot
    DrawSphere((Vector3){ 0, lineY, 0 }, 0.2f, lineCol);

    // Penalty areas (16.5m from goal line, 40.32m wide)
    float paW = 20.16f, paH = 16.5f;
    for (int side = -1; side <= 1; side += 2) {
        float gz = side * HALF_H;
        float inner = gz - side * paH;
        DrawLine3D((Vector3){ -paW, lineY, gz },    (Vector3){ -paW, lineY, inner }, lineCol);
        DrawLine3D((Vector3){  paW, lineY, gz },    (Vector3){  paW, lineY, inner }, lineCol);
        DrawLine3D((Vector3){ -paW, lineY, inner }, (Vector3){  paW, lineY, inner }, lineCol);
    }

    // Goal posts
    float halfGoal = GOAL_WIDTH / 2.0f;
    Color postCol = LIGHTGRAY;
    for (int side = -1; side <= 1; side += 2) {
        float gz = side * HALF_H;
        float back = gz + side * GOAL_DEPTH;
        // Left post
        DrawCylinderEx(
            (Vector3){ -halfGoal, 0, gz }, (Vector3){ -halfGoal, GOAL_HEIGHT, gz },
            0.06f, 0.06f, 8, postCol);
        // Right post
        DrawCylinderEx(
            (Vector3){  halfGoal, 0, gz }, (Vector3){  halfGoal, GOAL_HEIGHT, gz },
            0.06f, 0.06f, 8, postCol);
        // Crossbar
        DrawCylinderEx(
            (Vector3){ -halfGoal, GOAL_HEIGHT, gz }, (Vector3){ halfGoal, GOAL_HEIGHT, gz },
            0.06f, 0.06f, 8, postCol);
        // Net sides
        DrawLine3D((Vector3){ -halfGoal, 0, gz }, (Vector3){ -halfGoal, 0, back }, postCol);
        DrawLine3D((Vector3){  halfGoal, 0, gz }, (Vector3){  halfGoal, 0, back }, postCol);
        DrawLine3D((Vector3){ -halfGoal, GOAL_HEIGHT, gz }, (Vector3){ -halfGoal, GOAL_HEIGHT, back }, postCol);
        DrawLine3D((Vector3){  halfGoal, GOAL_HEIGHT, gz }, (Vector3){  halfGoal, GOAL_HEIGHT, back }, postCol);
        DrawLine3D((Vector3){ -halfGoal, 0, back }, (Vector3){ -halfGoal, GOAL_HEIGHT, back }, postCol);
        DrawLine3D((Vector3){  halfGoal, 0, back }, (Vector3){  halfGoal, GOAL_HEIGHT, back }, postCol);
        DrawLine3D((Vector3){ -halfGoal, GOAL_HEIGHT, back }, (Vector3){ halfGoal, GOAL_HEIGHT, back }, postCol);
        DrawLine3D((Vector3){ -halfGoal, 0, back }, (Vector3){ halfGoal, 0, back }, postCol);
        // Net mesh (vertical strips)
        for (int n = 0; n <= 4; n++) {
            float t = (float)n / 4.0f;
            float nx = -halfGoal + t * GOAL_WIDTH;
            DrawLine3D((Vector3){ nx, 0, back }, (Vector3){ nx, GOAL_HEIGHT, back }, postCol);
        }
    }
}

void DrawPlayerModel(Player *p, bool isSelected) {
    Color bodyCol = (p->team == 0) ? BLUE : RED;
    Color shortCol = (p->team == 0) ? WHITE : WHITE;

    // Body (cylinder)
    DrawCylinderEx(
        (Vector3){ p->pos.x, 0.0f, p->pos.z },
        (Vector3){ p->pos.x, 1.4f, p->pos.z },
        PLAYER_RADIUS * 0.7f, PLAYER_RADIUS * 0.5f, 8, bodyCol);

    // Head
    DrawSphere((Vector3){ p->pos.x, 1.6f, p->pos.z }, 0.2f,
               (Color){ 240, 200, 160, 255 });

    // Shorts
    DrawCylinderEx(
        (Vector3){ p->pos.x, 0.0f, p->pos.z },
        (Vector3){ p->pos.x, 0.5f, p->pos.z },
        PLAYER_RADIUS * 0.6f, PLAYER_RADIUS * 0.7f, 8, shortCol);

    // Selection ring
    if (isSelected) {
        DrawCircle3D(
            (Vector3){ p->pos.x, 0.03f, p->pos.z },
            PLAYER_RADIUS * 1.5f,
            (Vector3){ 1, 0, 0 }, 90.0f, YELLOW);
    }

    // Facing direction indicator
    float dx = sinf(p->rotation) * 0.8f;
    float dz = cosf(p->rotation) * 0.8f;
    DrawLine3D(
        (Vector3){ p->pos.x, 0.05f, p->pos.z },
        (Vector3){ p->pos.x + dx, 0.05f, p->pos.z + dz },
        isSelected ? YELLOW : bodyCol);
}

int main(void) {
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Soccer - ISS64/FIFA98 Style");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    // Camera: high angled broadcast view
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 55.0f, -60.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // State
    Player players[TEAM_SIZE * 2];
    Ball ball;
    ResetPositions(players, &ball);

    int controlled = 0;  // index of player-controlled character
    int score[2] = { 0, 0 };
    float switchTimer = 0.0f;
    float goalTimer = 0.0f;  // freeze after goal
    bool goalScored = false;
    int goalSide = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // --- Goal celebration freeze ---
        if (goalScored) {
            goalTimer -= dt;
            if (goalTimer <= 0.0f) {
                goalScored = false;
                ResetPositions(players, &ball);
                controlled = NearestPlayer(players, 0, ball.pos);
            }
            // Still draw but skip gameplay update
            goto draw;
        }

        // --- Switch controlled player ---
        switchTimer -= dt;
        if (IsKeyPressed(KEY_LEFT_SHIFT) && switchTimer <= 0.0f) {
            // Pick nearest team 0 player to the ball who isn't the current one
            float bestDist = 1e9f;
            int bestIdx = controlled;
            for (int i = 0; i < TEAM_SIZE; i++) {
                if (i == controlled) continue;
                float d = Vector3Distance(players[i].pos, ball.pos);
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = i;
                }
            }
            controlled = bestIdx;
            switchTimer = SWITCH_COOLDOWN;
        }

        // --- Player input ---
        {
            Player *p = &players[controlled];
            Vector3 move = { 0 };
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    move.z += 1.0f;
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  move.z -= 1.0f;
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  move.x -= 1.0f;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) move.x += 1.0f;

            if (Vector3Length(move) > 0.0f) {
                move = Vector3Normalize(move);
                float spd = PLAYER_SPEED;
                if (IsKeyDown(KEY_LEFT_CONTROL)) spd *= SPRINT_MULT;
                p->vel = Vector3Scale(move, spd);
                p->rotation = atan2f(move.x, move.z);
            } else {
                p->vel = (Vector3){ 0 };
            }
        }

        // --- AI for team 0 (non-controlled teammates) ---
        for (int i = 0; i < TEAM_SIZE; i++) {
            if (i == controlled) continue;
            Player *p = &players[i];

            // Move toward formation position offset by ball position
            Vector3 formBase = {
                formations[i].x,
                0.0f,
                formations[i].y + ball.pos.z * 0.4f  // shift with ball
            };
            formBase.z = Clamp(formBase.z, -HALF_H + 3.0f, HALF_H - 3.0f);

            Vector3 toForm = Vector3Subtract(formBase, p->pos);
            toForm.y = 0;
            float dist = Vector3Length(toForm);
            if (dist > 1.0f) {
                p->vel = Vector3Scale(Vector3Normalize(toForm), AI_SPEED * 0.6f);
                p->rotation = atan2f(toForm.x, toForm.z);
            } else {
                p->vel = (Vector3){ 0 };
            }
        }

        // --- AI for team 1 ---
        {
            int aiNearest = NearestPlayer(players, 1, ball.pos);
            for (int i = 0; i < TEAM_SIZE; i++) {
                int idx = TEAM_SIZE + i;
                Player *p = &players[idx];

                if (idx == aiNearest) {
                    // Chase ball
                    Vector3 toBall = Vector3Subtract(ball.pos, p->pos);
                    toBall.y = 0;
                    float dist = Vector3Length(toBall);
                    if (dist > 0.5f) {
                        p->vel = Vector3Scale(Vector3Normalize(toBall), AI_SPEED);
                        p->rotation = atan2f(toBall.x, toBall.z);
                    } else {
                        p->vel = (Vector3){ 0 };
                    }
                } else {
                    // Formation with offset toward ball
                    Vector3 formBase = {
                        formations[i].x,
                        0.0f,
                        -formations[i].y + ball.pos.z * 0.4f
                    };
                    formBase.z = Clamp(formBase.z, -HALF_H + 3.0f, HALF_H - 3.0f);

                    Vector3 toForm = Vector3Subtract(formBase, p->pos);
                    toForm.y = 0;
                    float dist = Vector3Length(toForm);
                    if (dist > 1.0f) {
                        p->vel = Vector3Scale(Vector3Normalize(toForm), AI_SPEED * 0.6f);
                        p->rotation = atan2f(toForm.x, toForm.z);
                    } else {
                        p->vel = (Vector3){ 0 };
                    }
                }
            }
        }

        // --- Move all players ---
        for (int i = 0; i < TEAM_SIZE * 2; i++) {
            players[i].pos = Vector3Add(players[i].pos, Vector3Scale(players[i].vel, dt));
            players[i].pos = ClampToPitch(players[i].pos, PLAYER_RADIUS);
            players[i].pos.y = 0.0f;
        }

        // --- Player-player collision ---
        for (int i = 0; i < TEAM_SIZE * 2; i++) {
            for (int j = i + 1; j < TEAM_SIZE * 2; j++) {
                Vector3 diff = Vector3Subtract(players[i].pos, players[j].pos);
                diff.y = 0;
                float dist = Vector3Length(diff);
                float minDist = PLAYER_RADIUS * 2.0f;
                if (dist < minDist && dist > 0.001f) {
                    Vector3 push = Vector3Scale(Vector3Normalize(diff), (minDist - dist) * 0.5f);
                    players[i].pos = Vector3Add(players[i].pos, push);
                    players[j].pos = Vector3Subtract(players[j].pos, push);
                }
            }
        }

        // --- Ball physics ---
        ball.pos = Vector3Add(ball.pos, Vector3Scale(ball.vel, dt));
        ball.vel.x *= BALL_FRICTION;
        ball.vel.z *= BALL_FRICTION;

        // Ball gravity (for lofted shots/bounces)
        ball.vel.y -= 30.0f * dt;
        if (ball.pos.y < BALL_RADIUS) {
            ball.pos.y = BALL_RADIUS;
            ball.vel.y = -ball.vel.y * 0.4f;  // bounce
            if (fabsf(ball.vel.y) < 0.5f) ball.vel.y = 0.0f;
        }

        // Ball-player collision / dribbling / tackling
        for (int i = 0; i < TEAM_SIZE * 2; i++) {
            Vector3 diff = Vector3Subtract(ball.pos, players[i].pos);
            diff.y = 0;
            float dist = Vector3Length(diff);
            float contactDist = PLAYER_RADIUS + BALL_RADIUS;

            if (dist < contactDist) {
                // Push ball out
                if (dist > 0.001f) {
                    Vector3 pushDir = Vector3Normalize(diff);
                    ball.pos.x = players[i].pos.x + pushDir.x * contactDist;
                    ball.pos.z = players[i].pos.z + pushDir.z * contactDist;
                }

                // Dribble: carry ball if moving toward it
                float velDot = Vector3DotProduct(players[i].vel, diff);
                if (velDot > 0 && Vector3Length(players[i].vel) > 0.5f) {
                    ball.vel.x = players[i].vel.x * 1.05f;
                    ball.vel.z = players[i].vel.z * 1.05f;
                    players[i].hasBall = true;
                } else {
                    // Nudge ball away
                    if (dist > 0.001f) {
                        Vector3 nudge = Vector3Scale(Vector3Normalize(diff), 3.0f);
                        ball.vel.x += nudge.x;
                        ball.vel.z += nudge.z;
                    }
                    players[i].hasBall = false;
                }

                // AI kick when near ball
                if (players[i].team == 1 && players[i].hasBall) {
                    // Shoot toward player goal (z = -HALF_H)
                    Vector3 goalTarget = { 0.0f, 0.0f, -HALF_H };
                    Vector3 kickDir = Vector3Normalize(Vector3Subtract(goalTarget, ball.pos));
                    ball.vel = Vector3Scale(kickDir, SHOOT_POWER * 0.7f);
                    ball.vel.y = 2.0f;
                    players[i].hasBall = false;
                }
            } else {
                players[i].hasBall = false;
            }
        }

        // --- Player kick/pass/shoot ---
        if (players[controlled].hasBall || Vector3Distance(players[controlled].pos, ball.pos) < PLAYER_RADIUS + BALL_RADIUS + 0.3f) {
            if (IsKeyPressed(KEY_SPACE)) {
                // Shoot: toward away goal
                Vector3 goalTarget = {
                    GetRandomValue(-20, 20) / 10.0f,
                    0.0f,
                    HALF_H
                };
                Vector3 shootDir = Vector3Normalize(Vector3Subtract(goalTarget, ball.pos));
                ball.vel = Vector3Scale(shootDir, SHOOT_POWER);
                ball.vel.y = 3.0f;
                players[controlled].hasBall = false;
            } else if (IsKeyPressed(KEY_E)) {
                // Pass: toward nearest teammate
                float bestDist = 1e9f;
                int bestMate = -1;
                for (int i = 0; i < TEAM_SIZE; i++) {
                    if (i == controlled) continue;
                    float d = Vector3Distance(players[i].pos, players[controlled].pos);
                    if (d < bestDist) {
                        bestDist = d;
                        bestMate = i;
                    }
                }
                if (bestMate >= 0) {
                    Vector3 passDir = Vector3Normalize(
                        Vector3Subtract(players[bestMate].pos, ball.pos));
                    ball.vel = Vector3Scale(passDir, PASS_POWER);
                    ball.vel.y = 1.0f;
                    players[controlled].hasBall = false;
                    // Auto-switch to receiver
                    controlled = bestMate;
                }
            } else if (IsKeyPressed(KEY_Q)) {
                // Through ball: kick in facing direction
                Vector3 kickDir = {
                    sinf(players[controlled].rotation),
                    0.0f,
                    cosf(players[controlled].rotation)
                };
                ball.vel = Vector3Scale(kickDir, KICK_POWER);
                ball.vel.y = 1.5f;
                players[controlled].hasBall = false;
            }
        }

        // --- Ball out of bounds ---
        if (ball.pos.x < -HALF_W - 2.0f || ball.pos.x > HALF_W + 2.0f ||
            ball.pos.z < -HALF_H - GOAL_DEPTH - 2.0f || ball.pos.z > HALF_H + GOAL_DEPTH + 2.0f) {
            ball.pos = (Vector3){
                Clamp(ball.pos.x, -HALF_W + 1.0f, HALF_W - 1.0f),
                BALL_RADIUS,
                Clamp(ball.pos.z, -HALF_H + 1.0f, HALF_H - 1.0f)
            };
            ball.vel = (Vector3){ 0 };
        }

        // Sideline bounce
        if (ball.pos.x < -HALF_W + BALL_RADIUS) { ball.pos.x = -HALF_W + BALL_RADIUS; ball.vel.x = -ball.vel.x * 0.5f; }
        if (ball.pos.x >  HALF_W - BALL_RADIUS) { ball.pos.x =  HALF_W - BALL_RADIUS; ball.vel.x = -ball.vel.x * 0.5f; }

        // Goal check (before clamping z)
        int goal = InsideGoal(ball.pos);
        if (goal == 1) {
            score[1]++;  // AI scored on player's goal
            goalScored = true;
            goalTimer = 2.0f;
        } else if (goal == 2) {
            score[0]++;  // Player scored
            goalScored = true;
            goalTimer = 2.0f;
        }

        // Endline bounce (outside goal)
        float halfGoal = GOAL_WIDTH / 2.0f;
        bool inGoalX = ball.pos.x > -halfGoal && ball.pos.x < halfGoal;
        if (!inGoalX) {
            if (ball.pos.z < -HALF_H + BALL_RADIUS) { ball.pos.z = -HALF_H + BALL_RADIUS; ball.vel.z = -ball.vel.z * 0.5f; }
            if (ball.pos.z >  HALF_H - BALL_RADIUS) { ball.pos.z =  HALF_H - BALL_RADIUS; ball.vel.z = -ball.vel.z * 0.5f; }
        }

        // --- Camera follows ball (broadcast style) ---
        {
            float camX = ball.pos.z * 0.3f;  // slight pan with play
            float camZ = -55.0f;
            camera.position = (Vector3){ camX, 50.0f, ball.pos.z + camZ };
            camera.target = (Vector3){ ball.pos.x * 0.5f, 0.0f, ball.pos.z };
        }

        // Auto-switch to nearest player when ball is loose
        if (!players[controlled].hasBall && switchTimer <= 0.0f) {
            int nearest = NearestPlayer(players, 0, ball.pos);
            if (nearest != controlled && Vector3Distance(players[nearest].pos, ball.pos) < 5.0f) {
                controlled = nearest;
                switchTimer = SWITCH_COOLDOWN;
            }
        }

draw:
        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){ 30, 80, 30, 255 });

        BeginMode3D(camera);
            DrawPitch();

            // Draw players
            for (int i = 0; i < TEAM_SIZE * 2; i++) {
                DrawPlayerModel(&players[i], (i == controlled));
            }

            // Draw ball
            DrawSphere(ball.pos, BALL_RADIUS, WHITE);
            DrawSphereWires(ball.pos, BALL_RADIUS, 6, 6, DARKGRAY);

            // Ball shadow
            DrawCircle3D(
                (Vector3){ ball.pos.x, 0.01f, ball.pos.z },
                BALL_RADIUS * 0.8f,
                (Vector3){ 1, 0, 0 }, 90.0f,
                (Color){ 0, 0, 0, 80 });
        EndMode3D();

        // --- HUD ---
        int sw = GetScreenWidth();

        // Score
        const char *scoreText = TextFormat("%d - %d", score[0], score[1]);
        int scoreW = MeasureText(scoreText, 40);
        DrawText(scoreText, sw / 2 - scoreW / 2, 15, 40, WHITE);

        DrawText("BLUE", sw / 2 - scoreW / 2 - 70, 22, 24, BLUE);
        DrawText("RED", sw / 2 + scoreW / 2 + 20, 22, 24, RED);

        // Goal notification
        if (goalScored) {
            const char *goalText = "GOAL!";
            int gw = MeasureText(goalText, 80);
            DrawText(goalText, sw / 2 - gw / 2, GetScreenHeight() / 2 - 40, 80, YELLOW);
        }

        // Controls
        DrawText("WASD/Arrows: Move  SPACE: Shoot  E: Pass  Q: Through ball", 10, GetScreenHeight() - 50, 16, WHITE);
        DrawText("L-Shift: Switch player  L-Ctrl: Sprint", 10, GetScreenHeight() - 30, 16, WHITE);

        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
