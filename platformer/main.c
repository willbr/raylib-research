#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

// Screen
#define SCREEN_W 800
#define SCREEN_H 600
#define TILE_SIZE 16

// Physics (Celeste-inspired tight controls)
#define GRAVITY         900.0f
#define MAX_FALL        350.0f
#define JUMP_FORCE     -280.0f
#define JUMP_CUT_MULT   0.5f    // release jump early = cut velocity
#define RUN_ACCEL       800.0f
#define RUN_DECEL      1200.0f
#define MAX_RUN         160.0f
#define COYOTE_TIME      0.08f  // can still jump after leaving edge
#define JUMP_BUFFER      0.1f   // press jump slightly before landing
#define WALL_SLIDE_MAX   60.0f
#define WALL_JUMP_H     180.0f
#define WALL_JUMP_V    -260.0f
#define WALL_GRAB_STAM    2.0f  // seconds of wall grab stamina
#define DASH_SPEED      250.0f
#define DASH_TIME         0.15f
#define DASH_COOLDOWN     0.0f  // resets on ground/wall

// Particles
#define MAX_PARTICLES 64
#define MAX_COINS     50
#define MAX_SPIKES    40
#define MAX_SPRINGS    10

// Tile types
#define T_EMPTY  0
#define T_SOLID  1
#define T_SPIKE  2
#define T_COIN   3
#define T_SPRING 4
#define T_FLAG   5
#define T_CLOUD  6  // one-way platform

// Level dimensions
#define LEVEL_W 60
#define LEVEL_H 40

typedef struct {
    Vector2 pos;
    Vector2 vel;
    Color color;
    float life;
    float maxLife;
    float size;
} Particle;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    Vector2 rem;     // sub-pixel remainder for precise movement
    bool onGround;
    bool onWall;
    int wallDir;     // -1 left, 1 right
    float coyoteTimer;
    float jumpBufferTimer;
    bool canDash;
    bool dashing;
    float dashTimer;
    Vector2 dashDir;
    float wallStamina;
    bool dead;
    float deathTimer;
    int coins;
    bool facingRight;
    float animTimer;
    bool wasOnGround;  // for landing particles
    bool levelComplete;
} Player;

// Level data
static int tiles[LEVEL_H][LEVEL_W];
static Vector2 spawnPoint;
static Vector2 flagPoint;

// Particles
static Particle particles[MAX_PARTICLES];
static int particleIdx = 0;

void SpawnParticle(Vector2 pos, Vector2 vel, Color col, float life, float size) {
    Particle *p = &particles[particleIdx];
    p->pos = pos;
    p->vel = vel;
    p->color = col;
    p->life = life;
    p->maxLife = life;
    p->size = size;
    particleIdx = (particleIdx + 1) % MAX_PARTICLES;
}

void SpawnBurst(Vector2 pos, Color col, int count) {
    for (int i = 0; i < count; i++) {
        float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
        float speed = (float)GetRandomValue(30, 120);
        Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed };
        float life = 0.2f + (float)GetRandomValue(0, 30) / 100.0f;
        float size = 1.0f + (float)GetRandomValue(0, 30) / 10.0f;
        SpawnParticle(pos, vel, col, life, size);
    }
}

// Level generation: a hand-crafted platforming gauntlet
void BuildLevel(void) {
    for (int y = 0; y < LEVEL_H; y++)
        for (int x = 0; x < LEVEL_W; x++)
            tiles[y][x] = T_EMPTY;

    // Ground floor
    for (int x = 0; x < LEVEL_W; x++) {
        tiles[LEVEL_H - 1][x] = T_SOLID;
        tiles[LEVEL_H - 2][x] = T_SOLID;
    }

    // Walls on edges
    for (int y = 0; y < LEVEL_H; y++) {
        tiles[y][0] = T_SOLID;
        tiles[y][LEVEL_W - 1] = T_SOLID;
    }

    // --- Section 1: Basic platforms with coins ---
    // Small gap
    tiles[LEVEL_H - 3][8] = T_SOLID;
    tiles[LEVEL_H - 3][9] = T_SOLID;
    tiles[LEVEL_H - 3][10] = T_SOLID;
    // Coins above
    tiles[LEVEL_H - 5][8] = T_COIN;
    tiles[LEVEL_H - 5][9] = T_COIN;
    tiles[LEVEL_H - 5][10] = T_COIN;

    // Stepping stones
    for (int i = 0; i < 3; i++) {
        int x = 14 + i * 4;
        tiles[LEVEL_H - 4 - i][x] = T_SOLID;
        tiles[LEVEL_H - 4 - i][x + 1] = T_SOLID;
        tiles[LEVEL_H - 5 - i][x] = T_COIN;
    }

    // --- Section 2: Spikes and precision ---
    // Spike pit
    for (int x = 25; x < 30; x++) {
        tiles[LEVEL_H - 3][x] = T_SPIKE;
    }
    // Platforms over spike pit
    tiles[LEVEL_H - 5][26] = T_SOLID;
    tiles[LEVEL_H - 5][27] = T_SOLID;
    tiles[LEVEL_H - 7][28] = T_SOLID;
    tiles[LEVEL_H - 7][29] = T_SOLID;
    tiles[LEVEL_H - 6][28] = T_COIN;

    // Cloud platforms (one-way)
    tiles[LEVEL_H - 6][32] = T_CLOUD;
    tiles[LEVEL_H - 6][33] = T_CLOUD;
    tiles[LEVEL_H - 9][35] = T_CLOUD;
    tiles[LEVEL_H - 9][36] = T_CLOUD;
    tiles[LEVEL_H - 8][35] = T_COIN;
    tiles[LEVEL_H - 8][36] = T_COIN;

    // Spring
    tiles[LEVEL_H - 3][34] = T_SPRING;

    // --- Section 3: Wall jump section ---
    // Vertical corridor with walls
    for (int y = LEVEL_H - 15; y < LEVEL_H - 3; y++) {
        tiles[y][38] = T_SOLID;
        tiles[y][43] = T_SOLID;
    }
    // Coins in the wall jump corridor
    tiles[LEVEL_H - 7][40] = T_COIN;
    tiles[LEVEL_H - 10][41] = T_COIN;
    tiles[LEVEL_H - 13][40] = T_COIN;
    // Spikes at bottom of corridor
    tiles[LEVEL_H - 3][39] = T_SPIKE;
    tiles[LEVEL_H - 3][40] = T_SPIKE;
    tiles[LEVEL_H - 3][41] = T_SPIKE;
    tiles[LEVEL_H - 3][42] = T_SPIKE;

    // --- Section 4: Dash challenge ---
    // Wide gap requiring dash
    // Platform before gap
    tiles[LEVEL_H - 16][44] = T_SOLID;
    tiles[LEVEL_H - 16][45] = T_SOLID;
    tiles[LEVEL_H - 16][46] = T_SOLID;
    // Platform after gap
    tiles[LEVEL_H - 16][51] = T_SOLID;
    tiles[LEVEL_H - 16][52] = T_SOLID;
    tiles[LEVEL_H - 16][53] = T_SOLID;
    // Coins in the dash gap
    tiles[LEVEL_H - 16][48] = T_COIN;
    tiles[LEVEL_H - 16][49] = T_COIN;

    // --- Section 5: Final climb to flag ---
    // Ascending platforms
    tiles[LEVEL_H - 18][54] = T_SOLID;
    tiles[LEVEL_H - 18][55] = T_SOLID;
    tiles[LEVEL_H - 20][52] = T_CLOUD;
    tiles[LEVEL_H - 20][53] = T_CLOUD;
    tiles[LEVEL_H - 22][55] = T_SOLID;
    tiles[LEVEL_H - 22][56] = T_SOLID;
    tiles[LEVEL_H - 21][55] = T_COIN;
    tiles[LEVEL_H - 21][56] = T_COIN;

    // Spring to reach the top
    tiles[LEVEL_H - 23][56] = T_SPRING;

    // Flag platform
    tiles[LEVEL_H - 28][54] = T_SOLID;
    tiles[LEVEL_H - 28][55] = T_SOLID;
    tiles[LEVEL_H - 28][56] = T_SOLID;
    tiles[LEVEL_H - 28][57] = T_SOLID;

    // Flag
    tiles[LEVEL_H - 29][55] = T_FLAG;

    spawnPoint = (Vector2){ 3 * TILE_SIZE, (LEVEL_H - 4) * TILE_SIZE };
    flagPoint = (Vector2){ 55 * TILE_SIZE, (LEVEL_H - 29) * TILE_SIZE };
}

bool TileIsSolid(int tx, int ty) {
    if (tx < 0 || tx >= LEVEL_W || ty < 0 || ty >= LEVEL_H) return false;
    return tiles[ty][tx] == T_SOLID;
}

bool TileIsCloud(int tx, int ty) {
    if (tx < 0 || tx >= LEVEL_W || ty < 0 || ty >= LEVEL_H) return false;
    return tiles[ty][tx] == T_CLOUD;
}

// AABB tile collision: check if a rectangle overlaps any solid tile
bool RectCollidesLevel(float x, float y, float w, float h) {
    int x1 = (int)(x / TILE_SIZE);
    int y1 = (int)(y / TILE_SIZE);
    int x2 = (int)((x + w - 0.01f) / TILE_SIZE);
    int y2 = (int)((y + h - 0.01f) / TILE_SIZE);
    for (int ty = y1; ty <= y2; ty++)
        for (int tx = x1; tx <= x2; tx++)
            if (TileIsSolid(tx, ty)) return true;
    return false;
}

// Check cloud platform collision (only from above)
bool RectOnCloud(float x, float y, float w, float h) {
    int x1 = (int)(x / TILE_SIZE);
    int x2 = (int)((x + w - 0.01f) / TILE_SIZE);
    int ty = (int)((y + h) / TILE_SIZE);
    for (int tx = x1; tx <= x2; tx++)
        if (TileIsCloud(tx, ty)) return true;
    return false;
}

// Check if player touches a tile type
bool TouchesTile(float x, float y, float w, float h, int type) {
    int x1 = (int)(x / TILE_SIZE);
    int y1 = (int)(y / TILE_SIZE);
    int x2 = (int)((x + w - 0.01f) / TILE_SIZE);
    int y2 = (int)((y + h - 0.01f) / TILE_SIZE);
    for (int ty = y1; ty <= y2; ty++)
        for (int tx = x1; tx <= x2; tx++)
            if (tx >= 0 && tx < LEVEL_W && ty >= 0 && ty < LEVEL_H && tiles[ty][tx] == type)
                return true;
    return false;
}

// Collect coins the player overlaps
void CollectCoins(Player *p, float x, float y, float w, float h) {
    int x1 = (int)(x / TILE_SIZE);
    int y1 = (int)(y / TILE_SIZE);
    int x2 = (int)((x + w - 0.01f) / TILE_SIZE);
    int y2 = (int)((y + h - 0.01f) / TILE_SIZE);
    for (int ty = y1; ty <= y2; ty++) {
        for (int tx = x1; tx <= x2; tx++) {
            if (tx >= 0 && tx < LEVEL_W && ty >= 0 && ty < LEVEL_H && tiles[ty][tx] == T_COIN) {
                tiles[ty][tx] = T_EMPTY;
                p->coins++;
                SpawnBurst((Vector2){ tx * TILE_SIZE + 8, ty * TILE_SIZE + 8 }, YELLOW, 8);
            }
        }
    }
}

// Check for spring bounce
bool HitsSpring(float x, float y, float w, float h) {
    int x1 = (int)(x / TILE_SIZE);
    int y1 = (int)(y / TILE_SIZE);
    int x2 = (int)((x + w - 0.01f) / TILE_SIZE);
    int y2 = (int)((y + h - 0.01f) / TILE_SIZE);
    for (int ty = y1; ty <= y2; ty++)
        for (int tx = x1; tx <= x2; tx++)
            if (tx >= 0 && tx < LEVEL_W && ty >= 0 && ty < LEVEL_H && tiles[ty][tx] == T_SPRING)
                return true;
    return false;
}

void MoveX(Player *p, float amount) {
    p->rem.x += amount;
    int move = (int)p->rem.x;
    if (move == 0) return;
    p->rem.x -= move;
    int sign = (move > 0) ? 1 : -1;
    float pw = 6, ph = 12;  // player hitbox

    for (int i = 0; i < abs(move); i++) {
        if (!RectCollidesLevel(p->pos.x + sign, p->pos.y, pw, ph)) {
            p->pos.x += sign;
        } else {
            p->vel.x = 0;
            p->rem.x = 0;
            break;
        }
    }
}

void MoveY(Player *p, float amount) {
    p->rem.y += amount;
    int move = (int)p->rem.y;
    if (move == 0) return;
    p->rem.y -= move;
    int sign = (move > 0) ? 1 : -1;
    float pw = 6, ph = 12;

    for (int i = 0; i < abs(move); i++) {
        bool blocked = RectCollidesLevel(p->pos.x, p->pos.y + sign, pw, ph);
        // Cloud platform: only block from above (moving down)
        if (!blocked && sign > 0) {
            float feetNow = p->pos.y + ph;
            float feetNext = p->pos.y + sign + ph;
            int tileNow = (int)(feetNow / TILE_SIZE);
            int tileNext = (int)(feetNext / TILE_SIZE);
            if (tileNext > tileNow && RectOnCloud(p->pos.x, p->pos.y + sign, pw, ph)) {
                blocked = true;
            }
        }
        if (!blocked) {
            p->pos.y += sign;
        } else {
            if (sign > 0) p->onGround = true;
            p->vel.y = 0;
            p->rem.y = 0;
            break;
        }
    }
}

void KillPlayer(Player *p) {
    if (p->dead) return;
    p->dead = true;
    p->deathTimer = 0.6f;
    SpawnBurst(p->pos, RED, 20);
}

void RespawnPlayer(Player *p) {
    p->pos = spawnPoint;
    p->vel = (Vector2){ 0 };
    p->rem = (Vector2){ 0 };
    p->dead = false;
    p->onGround = false;
    p->onWall = false;
    p->coyoteTimer = 0;
    p->jumpBufferTimer = 0;
    p->canDash = true;
    p->dashing = false;
    p->dashTimer = 0;
    p->wallStamina = WALL_GRAB_STAM;
    p->facingRight = true;
    p->levelComplete = false;
}

int main(void) {
    InitWindow(SCREEN_W, SCREEN_H, "Platformer");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    BuildLevel();

    Player player = { 0 };
    RespawnPlayer(&player);

    // Zoom: how many pixels per tile on screen
    float zoom = 3.0f;

    Camera2D camera = { 0 };
    camera.zoom = zoom;

    float coinAnimTimer = 0.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;  // cap for stability

        coinAnimTimer += dt;

        // --- Player update ---
        float pw = 6, ph = 12;  // hitbox size

        if (player.dead) {
            player.deathTimer -= dt;
            if (player.deathTimer <= 0.0f) {
                RespawnPlayer(&player);
            }
            goto draw;
        }

        if (player.levelComplete) {
            goto draw;
        }

        // Store previous ground state
        player.wasOnGround = player.onGround;

        // Check ground
        player.onGround = RectCollidesLevel(player.pos.x, player.pos.y + 1, pw, ph)
                       || (player.vel.y >= 0 && RectOnCloud(player.pos.x, player.pos.y + 1, pw, ph));

        // Check walls
        player.onWall = false;
        if (!player.onGround) {
            if (RectCollidesLevel(player.pos.x - 1, player.pos.y, pw, ph)) {
                player.onWall = true;
                player.wallDir = -1;
            } else if (RectCollidesLevel(player.pos.x + 1, player.pos.y, pw, ph)) {
                player.onWall = true;
                player.wallDir = 1;
            }
        }

        // Coyote time
        if (player.onGround) {
            player.coyoteTimer = COYOTE_TIME;
            player.canDash = true;
            player.wallStamina = WALL_GRAB_STAM;
        } else {
            player.coyoteTimer -= dt;
        }

        // Wall resets dash
        if (player.onWall) {
            player.canDash = true;
        }

        // Jump buffer
        if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_SPACE)) {
            player.jumpBufferTimer = JUMP_BUFFER;
        } else {
            player.jumpBufferTimer -= dt;
        }

        // Dash input (Celeste-style)
        if ((IsKeyPressed(KEY_X) || IsKeyPressed(KEY_LEFT_SHIFT)) && player.canDash && !player.dashing) {
            player.dashing = true;
            player.dashTimer = DASH_TIME;
            player.canDash = false;

            // Dash direction from input, or facing direction
            float dx = 0, dy = 0;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) dx = -1;
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) dx = 1;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) dy = -1;
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) dy = 1;
            if (dx == 0 && dy == 0) dx = player.facingRight ? 1 : -1;
            float len = sqrtf(dx * dx + dy * dy);
            player.dashDir = (Vector2){ dx / len, dy / len };
            player.vel = (Vector2){ player.dashDir.x * DASH_SPEED, player.dashDir.y * DASH_SPEED };

            SpawnBurst(player.pos, WHITE, 12);
        }

        if (player.dashing) {
            player.dashTimer -= dt;
            if (player.dashTimer <= 0.0f) {
                player.dashing = false;
                // Preserve some momentum
                player.vel.x *= 0.6f;
                if (player.vel.y < 0) player.vel.y *= 0.5f;
            } else {
                // During dash: override velocity, no gravity
                player.vel = (Vector2){ player.dashDir.x * DASH_SPEED, player.dashDir.y * DASH_SPEED };
                // Dash trail particles
                SpawnParticle(player.pos, (Vector2){ 0, 0 }, WHITE, 0.2f, 2.0f);
                goto move;
            }
        }

        // Horizontal movement
        {
            int inputX = 0;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) inputX = -1;
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) inputX = 1;

            if (inputX != 0) {
                player.facingRight = (inputX > 0);
                // Accelerate
                player.vel.x += inputX * RUN_ACCEL * dt;
                if (fabsf(player.vel.x) > MAX_RUN)
                    player.vel.x = (player.vel.x > 0 ? 1 : -1) * MAX_RUN;
            } else {
                // Decelerate
                if (player.vel.x > 0) {
                    player.vel.x -= RUN_DECEL * dt;
                    if (player.vel.x < 0) player.vel.x = 0;
                } else if (player.vel.x < 0) {
                    player.vel.x += RUN_DECEL * dt;
                    if (player.vel.x > 0) player.vel.x = 0;
                }
            }
        }

        // Wall slide
        if (player.onWall && !player.onGround && player.vel.y > 0) {
            if (player.vel.y > WALL_SLIDE_MAX)
                player.vel.y = WALL_SLIDE_MAX;
            // Wall slide particles
            if (GetRandomValue(0, 3) == 0) {
                SpawnParticle(
                    (Vector2){ player.pos.x + (player.wallDir < 0 ? -2 : pw + 2), player.pos.y + ph/2 },
                    (Vector2){ 0, -20 }, WHITE, 0.15f, 1.0f);
            }
        }

        // Gravity
        player.vel.y += GRAVITY * dt;
        if (player.vel.y > MAX_FALL) player.vel.y = MAX_FALL;

        // Jump
        if (player.jumpBufferTimer > 0.0f) {
            if (player.coyoteTimer > 0.0f) {
                // Normal jump
                player.vel.y = JUMP_FORCE;
                player.coyoteTimer = 0;
                player.jumpBufferTimer = 0;
                SpawnBurst((Vector2){ player.pos.x + pw/2, player.pos.y + ph }, LIGHTGRAY, 6);
            } else if (player.onWall) {
                // Wall jump
                player.vel.x = -player.wallDir * WALL_JUMP_H;
                player.vel.y = WALL_JUMP_V;
                player.jumpBufferTimer = 0;
                player.facingRight = (player.wallDir < 0);
                SpawnBurst((Vector2){ player.pos.x + (player.wallDir > 0 ? -2 : pw + 2), player.pos.y + ph/2 }, LIGHTGRAY, 8);
            }
        }

        // Variable jump height (release to cut)
        if ((IsKeyReleased(KEY_Z) || IsKeyReleased(KEY_SPACE)) && player.vel.y < 0) {
            player.vel.y *= JUMP_CUT_MULT;
        }

move:
        // Apply movement with collision
        MoveX(&player, player.vel.x * dt);
        MoveY(&player, player.vel.y * dt);

        // Landing particles
        if (player.onGround && !player.wasOnGround) {
            SpawnBurst((Vector2){ player.pos.x + pw/2, player.pos.y + ph }, LIGHTGRAY, 5);
        }

        // Collect coins
        CollectCoins(&player, player.pos.x, player.pos.y, pw, ph);

        // Spring bounce
        if (player.onGround && HitsSpring(player.pos.x, player.pos.y + 1, pw, ph)) {
            player.vel.y = JUMP_FORCE * 1.6f;
            player.onGround = false;
            player.canDash = true;
            SpawnBurst((Vector2){ player.pos.x + pw/2, player.pos.y + ph }, GREEN, 10);
        }

        // Spike death
        if (TouchesTile(player.pos.x + 1, player.pos.y + 1, pw - 2, ph - 2, T_SPIKE)) {
            KillPlayer(&player);
        }

        // Flag / level complete
        if (TouchesTile(player.pos.x, player.pos.y, pw, ph, T_FLAG)) {
            player.levelComplete = true;
            SpawnBurst((Vector2){ player.pos.x + pw/2, player.pos.y + ph/2 }, YELLOW, 30);
        }

        // Fall off bottom
        if (player.pos.y > LEVEL_H * TILE_SIZE + 32) {
            KillPlayer(&player);
        }

draw:
        // --- Camera ---
        {
            float screenW = (float)GetScreenWidth();
            float screenH = (float)GetScreenHeight();
            camera.offset = (Vector2){ screenW / 2.0f, screenH / 2.0f };
            // Target player center
            Vector2 target = { player.pos.x + pw/2, player.pos.y + ph/2 };
            camera.target = Vector2Lerp(camera.target, target, 8.0f * dt);

            // Clamp camera to level bounds
            float halfViewW = screenW / (2.0f * zoom);
            float halfViewH = screenH / (2.0f * zoom);
            if (camera.target.x < halfViewW) camera.target.x = halfViewW;
            if (camera.target.y < halfViewH) camera.target.y = halfViewH;
            if (camera.target.x > LEVEL_W * TILE_SIZE - halfViewW)
                camera.target.x = LEVEL_W * TILE_SIZE - halfViewW;
            if (camera.target.y > LEVEL_H * TILE_SIZE - halfViewH)
                camera.target.y = LEVEL_H * TILE_SIZE - halfViewH;
        }

        // --- Update particles ---
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particles[i].life <= 0) continue;
            particles[i].pos.x += particles[i].vel.x * dt;
            particles[i].pos.y += particles[i].vel.y * dt;
            particles[i].vel.y += 200.0f * dt;  // gravity on particles
            particles[i].life -= dt;
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){ 20, 20, 40, 255 });  // dark sky

        BeginMode2D(camera);

            // Background: parallax mountains
            {
                float bgScroll = camera.target.x * 0.3f;
                for (int i = 0; i < 20; i++) {
                    float bx = i * 80 - fmodf(bgScroll, 80.0f) - 80;
                    float bh = 30.0f + sinf(i * 1.7f) * 20.0f;
                    DrawTriangle(
                        (Vector2){ bx, LEVEL_H * TILE_SIZE },
                        (Vector2){ bx + 40, LEVEL_H * TILE_SIZE - bh },
                        (Vector2){ bx + 80, LEVEL_H * TILE_SIZE },
                        (Color){ 30, 30, 60, 255 });
                }
            }

            // Draw tiles
            for (int y = 0; y < LEVEL_H; y++) {
                for (int x = 0; x < LEVEL_W; x++) {
                    float tx = x * TILE_SIZE;
                    float ty = y * TILE_SIZE;

                    switch (tiles[y][x]) {
                        case T_SOLID: {
                            // Check neighbors for visual variety
                            bool top = (y > 0 && tiles[y-1][x] == T_SOLID);
                            Color col = top ? (Color){ 80, 70, 60, 255 } : (Color){ 60, 140, 60, 255 };
                            DrawRectangle(tx, ty, TILE_SIZE, TILE_SIZE, col);
                            // Grass tufts on top
                            if (!top) {
                                DrawRectangle(tx + 2, ty, 3, 2, (Color){ 80, 180, 80, 255 });
                                DrawRectangle(tx + 9, ty, 4, 2, (Color){ 70, 160, 70, 255 });
                            }
                            break;
                        }
                        case T_SPIKE:
                            // Triangle spikes
                            DrawTriangle(
                                (Vector2){ tx, ty + TILE_SIZE },
                                (Vector2){ tx + TILE_SIZE / 2, ty + 4 },
                                (Vector2){ tx + TILE_SIZE, ty + TILE_SIZE },
                                RED);
                            break;
                        case T_COIN: {
                            // Animated spinning coin
                            float squeeze = fabsf(cosf(coinAnimTimer * 4.0f + x));
                            float cx = tx + TILE_SIZE / 2;
                            float cy = ty + TILE_SIZE / 2;
                            float w = 3.0f * squeeze + 1.0f;
                            DrawRectangle(cx - w, cy - 4, w * 2, 8, GOLD);
                            DrawRectangleLines(cx - w, cy - 4, w * 2, 8, ORANGE);
                            break;
                        }
                        case T_SPRING:
                            DrawRectangle(tx + 2, ty + 8, TILE_SIZE - 4, 8, DARKGRAY);
                            DrawRectangle(tx + 4, ty + 4, TILE_SIZE - 8, 4, GREEN);
                            DrawRectangle(tx + 3, ty + 4, TILE_SIZE - 6, 2, LIME);
                            break;
                        case T_FLAG:
                            // Flag pole
                            DrawRectangle(tx + 7, ty, 2, TILE_SIZE, LIGHTGRAY);
                            // Flag
                            DrawTriangle(
                                (Vector2){ tx + 9, ty + 1 },
                                (Vector2){ tx + 9, ty + 8 },
                                (Vector2){ tx + 16, ty + 4 },
                                YELLOW);
                            break;
                        case T_CLOUD:
                            // One-way platform (dashed look)
                            DrawRectangle(tx, ty, TILE_SIZE, 4, WHITE);
                            DrawRectangle(tx + 2, ty + 1, 4, 2, (Color){ 200, 220, 255, 255 });
                            DrawRectangle(tx + 10, ty + 1, 4, 2, (Color){ 200, 220, 255, 255 });
                            break;
                    }
                }
            }

            // Draw particles
            for (int i = 0; i < MAX_PARTICLES; i++) {
                if (particles[i].life <= 0) continue;
                float alpha = particles[i].life / particles[i].maxLife;
                Color c = particles[i].color;
                c.a = (unsigned char)(alpha * 255);
                float s = particles[i].size * alpha;
                DrawRectangle(particles[i].pos.x - s/2, particles[i].pos.y - s/2, s, s, c);
            }

            // Draw player
            if (!player.dead) {
                Color bodyCol = player.dashing ? WHITE : (Color){ 100, 160, 255, 255 };
                // Body
                DrawRectangle(player.pos.x, player.pos.y + 4, pw, ph - 4, bodyCol);
                // Head
                DrawRectangle(player.pos.x + 1, player.pos.y, pw - 2, 5, (Color){ 240, 200, 160, 255 });
                // Eye
                float eyeX = player.facingRight ? player.pos.x + 4 : player.pos.x + 1;
                DrawRectangle(eyeX, player.pos.y + 1, 1, 2, BLACK);
                // Hair (Celeste-style)
                Color hairCol = player.canDash ? (Color){ 220, 50, 50, 255 } : (Color){ 80, 120, 200, 255 };
                float hairDir = player.facingRight ? -1 : 1;
                DrawRectangle(player.pos.x + (player.facingRight ? 0 : pw - 3), player.pos.y - 1, 3, 3, hairCol);
                DrawRectangle(player.pos.x + (player.facingRight ? -1 : pw - 1), player.pos.y, 2, 2, hairCol);
            }

        EndMode2D();

        // --- HUD ---
        int sw = GetScreenWidth();

        // Coins
        DrawRectangle(sw - 110, 12, 100, 30, (Color){ 0, 0, 0, 120 });
        DrawRectangle(sw - 105, 20, 8, 8, GOLD);
        DrawText(TextFormat("x %d", player.coins), sw - 90, 17, 20, WHITE);

        // Dash indicator
        if (!player.dead) {
            Color dashCol = player.canDash ? (Color){ 220, 50, 50, 255 } : (Color){ 80, 80, 100, 255 };
            DrawCircle(sw / 2, 30, 8, dashCol);
        }

        // Death
        if (player.dead) {
            const char *deathText = "...";
            int dw = MeasureText(deathText, 30);
            DrawText(deathText, sw/2 - dw/2, GetScreenHeight()/2 - 15, 30, RED);
        }

        // Level complete
        if (player.levelComplete) {
            const char *winText = "LEVEL COMPLETE!";
            int ww = MeasureText(winText, 40);
            DrawText(winText, sw/2 - ww/2, GetScreenHeight()/2 - 40, 40, YELLOW);
            DrawText(TextFormat("Coins: %d", player.coins), sw/2 - 50, GetScreenHeight()/2 + 10, 24, WHITE);
        }

        // Controls
        DrawText("Arrows/WASD: Move   Z/Space: Jump   X/Shift: Dash", 10, GetScreenHeight() - 22, 14, (Color){ 150, 150, 170, 255 });

        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
