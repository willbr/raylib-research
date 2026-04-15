#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "../common/objects3d.h"
#include <math.h>
#include <stdlib.h>

// Star Fox style on-rails shooter: Arwing flies forward automatically,
// player steers within a corridor, shoots enemies and dodges obstacles

#define SCROLL_SPEED    40.0f     // how fast the world scrolls
#define STEER_SPEED     20.0f     // lateral/vertical movement speed
#define CORRIDOR_W      25.0f     // half-width of play area
#define CORRIDOR_H      18.0f     // half-height of play area
#define BARREL_ROLL_SPD  8.0f
#define TILT_FACTOR      0.4f     // roll when steering left/right
#define PITCH_FACTOR     0.3f     // pitch when steering up/down

#define MAX_BULLETS     32
#define BULLET_SPEED   120.0f
#define BULLET_LIFE      1.5f

#define MAX_ENEMIES     24
#define ENEMY_SPAWN_DIST 300.0f
#define ENEMY_SPEED      15.0f

#define MAX_BUILDINGS   40
#define MAX_PILLARS     30
#define MAX_RINGS       12
#define MAX_EXPLOSIONS  30
#define MAX_PARTICLES   60

#define GROUND_Y         0.0f

// --- Arwing model ---
static Part arwingFallback[] = {
    // Fuselage
    CONE(0, 0, 0.6f,        0.25f, 1.2f, 0.0f, COL(200,200,210,255)),   // nose cone
    CUBE(0, 0, -0.3f,       0.5f, 0.3f, 1.0f, COL(180,180,195,255)),    // body
    CUBE(0, 0.05f, -0.9f,   0.35f, 0.25f, 0.4f, COL(160,160,175,255)), // rear body
    // Wings
    CUBE(-1.1f, -0.05f, -0.1f,  1.4f, 0.04f, 0.5f, COL(180,185,200,255)),  // left wing
    CUBE( 1.1f, -0.05f, -0.1f,  1.4f, 0.04f, 0.5f, COL(180,185,200,255)),  // right wing
    // Wing tips (blue)
    CUBE(-1.75f, -0.05f, 0.0f,  0.15f, 0.06f, 0.35f, COL(40,80,200,255)),
    CUBE( 1.75f, -0.05f, 0.0f,  0.15f, 0.06f, 0.35f, COL(40,80,200,255)),
    // Tail fins
    CUBE(0, 0.3f, -1.0f,    0.04f, 0.35f, 0.3f, COL(40,80,200,255)),   // vertical
    CUBE(-0.3f, 0.1f, -1.0f, 0.3f, 0.04f, 0.2f, COL(170,170,185,255)), // left h-tail
    CUBE( 0.3f, 0.1f, -1.0f, 0.3f, 0.04f, 0.2f, COL(170,170,185,255)), // right h-tail
    // Engines
    CYL(-0.5f, -0.05f, -0.7f,  0.08f, 0.3f, COL(100,100,110,255)),
    CYL( 0.5f, -0.05f, -0.7f,  0.08f, 0.3f, COL(100,100,110,255)),
    // Cockpit canopy
    SPHERE(0, 0.15f, 0.1f,  0.18f, COL(100,180,230,180)),
    // Laser cannons on wings
    CYL(-1.5f, -0.08f, 0.3f,  0.03f, 0.4f, COL(80,80,90,255)),
    CYL( 1.5f, -0.08f, 0.3f,  0.03f, 0.4f, COL(80,80,90,255)),
};

// Enemy fighter
static Part enemyFallback[] = {
    CUBE(0, 0, 0,            0.6f, 0.25f, 0.8f, COL(120,40,40,255)),
    CUBE(-0.7f, 0, 0,        0.6f, 0.04f, 0.35f, COL(140,50,50,255)),
    CUBE( 0.7f, 0, 0,        0.6f, 0.04f, 0.35f, COL(140,50,50,255)),
    CONE(0, 0, 0.5f,         0.15f, 0.4f, 0.0f, COL(100,30,30,255)),
    SPHERE(0, 0.1f, 0.1f,    0.12f, COL(200,200,50,200)),
};

#define MAX_OBJ_PARTS 32
static Part arwingParts[MAX_OBJ_PARTS];
static int arwingPartCount = 0;
static Part enemyParts[MAX_OBJ_PARTS];
static int enemyPartCount = 0;

#define LoadOrCreate LoadOrCreateObject

// --- Game types ---

typedef struct {
    Vector3 pos;        // x,y = position in corridor, z = scroll distance
    float roll;
    float pitch;
    float rollVel;      // for barrel roll
    bool barrelRolling;
    float barrelRollAngle;
    int hp;
    int score;
    float shootCooldown;
    float iframes;      // invincibility after hit
} Player;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float life;
    bool active;
} Bullet;

typedef struct {
    Vector3 pos;
    Vector3 vel;        // movement pattern
    float hp;
    float sinOffset;    // for weaving patterns
    int type;           // 0=straight, 1=weave, 2=dive
    bool active;
} Enemy;

typedef struct {
    Vector3 pos;
    float w, h, d;
    Color color;
} Obstacle;

typedef struct {
    Vector3 pos;
    float radius;
    bool collected;
} RingObj;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float life, maxLife;
    float size;
    Color color;
    bool active;
} Particle;

// Use common library functions
#define RotateVec RotateVec3D
#define DrawCubeRotated DrawCubeRotated3D
#define DrawShip(parts, count, pos, pitch, yaw, roll) \
    DrawObject3DRotated(parts, count, pos, pitch, yaw, roll)

// --- Globals ---
static Player player;
static Bullet bullets[MAX_BULLETS];
static Enemy enemies[MAX_ENEMIES];
static Obstacle buildings[MAX_BUILDINGS];
static Obstacle pillars[MAX_PILLARS];
static RingObj rings[MAX_RINGS];
static Particle particles[MAX_PARTICLES];
static float scrollZ = 0;         // total distance scrolled
static float spawnTimer = 0;
static float ringSpawnTimer = 0;
static float gameTime = 0;
static bool gameOver = false;
static float gameOverTimer = 0;

// Terrain
float TerrainHeight(float x, float z) {
    float h = sinf(x * 0.015f) * 3.0f + cosf(z * 0.01f) * 2.0f;
    h += sinf(x * 0.04f + z * 0.02f) * 1.5f;
    if (h < 0) h = 0;
    return h;
}

// Spawn explosion particles
void SpawnExplosion(Vector3 pos, int count, Color baseCol) {
    for (int i = 0; i < MAX_PARTICLES && count > 0; i++) {
        if (particles[i].active) continue;
        float a1 = (float)GetRandomValue(0, 628) / 100.0f;
        float a2 = (float)GetRandomValue(-314, 314) / 200.0f;
        float spd = (float)GetRandomValue(3, 18);
        particles[i].pos = pos;
        particles[i].vel = (Vector3){
            cosf(a1)*cosf(a2)*spd, fabsf(sinf(a2))*spd + 2.0f, sinf(a1)*cosf(a2)*spd
        };
        int r = GetRandomValue(0, 2);
        if (r == 0) particles[i].color = (Color){255, 200, 50, 255};
        else if (r == 1) particles[i].color = (Color){255, 100, 0, 255};
        else particles[i].color = baseCol;
        particles[i].life = 0.4f + (float)GetRandomValue(0, 60) / 100.0f;
        particles[i].maxLife = particles[i].life;
        particles[i].size = 0.1f + (float)GetRandomValue(0, 25) / 100.0f;
        particles[i].active = true;
        count--;
    }
}

void SpawnEnemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) continue;
        enemies[i].active = true;
        enemies[i].hp = 1;
        enemies[i].type = GetRandomValue(0, 2);
        enemies[i].sinOffset = (float)GetRandomValue(0, 628) / 100.0f;
        float ex = (float)GetRandomValue(-200, 200) / 10.0f;
        float ey = 5.0f + (float)GetRandomValue(0, 120) / 10.0f;
        enemies[i].pos = (Vector3){ex, ey, scrollZ + ENEMY_SPAWN_DIST};
        if (enemies[i].type == 0) {
            enemies[i].vel = (Vector3){0, 0, -ENEMY_SPEED};
        } else if (enemies[i].type == 1) {
            enemies[i].vel = (Vector3){0, 0, -ENEMY_SPEED * 0.7f};
        } else {
            // Dive toward player
            enemies[i].pos.y = 15.0f + (float)GetRandomValue(0, 50) / 10.0f;
            enemies[i].vel = (Vector3){0, -ENEMY_SPEED * 0.3f, -ENEMY_SPEED * 0.8f};
        }
        break;
    }
}

static float nextBuildingZ = 0;
static float nextPillarZ = 0;
static float nextRingZ = 0;

void SpawnObstaclesInit(void) {
    nextBuildingZ = scrollZ + 50.0f;
    nextPillarZ = scrollZ + 50.0f;
    nextRingZ = scrollZ + 50.0f;
    for (int i = 0; i < MAX_BUILDINGS; i++) buildings[i].pos.z = -9999;
    for (int i = 0; i < MAX_PILLARS; i++) pillars[i].pos.z = -9999;
    for (int i = 0; i < MAX_RINGS; i++) { rings[i].pos.z = -9999; rings[i].collected = true; }
}

void RecycleObstacles(void) {
    float behindZ = scrollZ - 80.0f;
    float aheadZ = scrollZ + 350.0f;

    // Recycle buildings that passed behind camera into new positions ahead
    while (nextBuildingZ < aheadZ) {
        // Find a slot that's behind the camera
        int slot = -1;
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            if (buildings[i].pos.z < behindZ) { slot = i; break; }
        }
        if (slot < 0) break;
        float x = (float)GetRandomValue(-350, 350) / 10.0f;
        float h = 5.0f + (float)GetRandomValue(0, 150) / 10.0f;
        float w = 3.0f + (float)GetRandomValue(0, 40) / 10.0f;
        float d = 3.0f + (float)GetRandomValue(0, 40) / 10.0f;
        float bGroundH = TerrainHeight(x, nextBuildingZ);
        buildings[slot].pos = (Vector3){x, bGroundH + h / 2.0f, nextBuildingZ};
        buildings[slot].w = w;
        buildings[slot].h = h;
        buildings[slot].d = d;
        int cv = GetRandomValue(0, 2);
        if (cv == 0) buildings[slot].color = (Color){130, 125, 120, 255};
        else if (cv == 1) buildings[slot].color = (Color){100, 110, 120, 255};
        else buildings[slot].color = (Color){140, 130, 110, 255};
        nextBuildingZ += 15.0f + (float)GetRandomValue(0, 100) / 10.0f;
    }

    // Recycle pillars
    while (nextPillarZ < aheadZ) {
        int slot = -1;
        for (int i = 0; i < MAX_PILLARS; i++) {
            if (pillars[i].pos.z < behindZ) { slot = i; break; }
        }
        if (slot < 0) break;
        float x = (float)GetRandomValue(-250, 250) / 10.0f;
        float h = 10.0f + (float)GetRandomValue(0, 100) / 10.0f;
        float groundH = TerrainHeight(x, nextPillarZ);
        pillars[slot].pos = (Vector3){x, groundH, nextPillarZ};
        pillars[slot].w = 1.5f;
        pillars[slot].h = h;
        pillars[slot].d = 1.5f;
        pillars[slot].color = (Color){90, 85, 80, 255};
        nextPillarZ += 20.0f + (float)GetRandomValue(0, 100) / 10.0f;
    }

    // Recycle rings
    while (nextRingZ < aheadZ) {
        int slot = -1;
        for (int i = 0; i < MAX_RINGS; i++) {
            if (rings[i].pos.z < behindZ || rings[i].collected) { slot = i; break; }
        }
        if (slot < 0) break;
        float x = (float)GetRandomValue(-150, 150) / 10.0f;
        float y = 5.0f + (float)GetRandomValue(0, 100) / 10.0f;
        rings[slot].pos = (Vector3){x, y, nextRingZ};
        rings[slot].radius = 3.0f;
        rings[slot].collected = false;
        nextRingZ += 25.0f + (float)GetRandomValue(0, 100) / 10.0f;
    }
}

void InitGame(void) {
    LoadOrCreate("objects/arwing.obj3d", arwingParts, &arwingPartCount,
        arwingFallback, sizeof(arwingFallback)/sizeof(Part));
    LoadOrCreate("objects/enemy_fighter.obj3d", enemyParts, &enemyPartCount,
        enemyFallback, sizeof(enemyFallback)/sizeof(Part));

    player = (Player){
        .pos = {0, 8.0f, 0},
        .hp = 3,
        .score = 0,
    };
    scrollZ = 0;
    spawnTimer = 0;
    ringSpawnTimer = 0;
    gameTime = 0;
    gameOver = false;
    gameOverTimer = 0;

    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;

    SpawnObstaclesInit();
    RecycleObstacles();
}

int main(void) {
    InitWindow(1280, 720, "Star Fox");
    SetTargetFPS(60);

    InitGame();

    Camera3D camera = {0};
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        gameTime += dt;

        // --- Restart ---
        if (gameOver) {
            gameOverTimer += dt;
            if (IsKeyPressed(KEY_ENTER) && gameOverTimer > 1.0f) InitGame();
        }

        if (!gameOver) {
            // --- Scroll ---
            scrollZ += SCROLL_SPEED * dt;

            // --- Player input ---
            float sx = 0, sy = 0;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  sx = 1;
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) sx = -1;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    sy = 1;
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))  sy = -1;

            player.pos.x += sx * STEER_SPEED * dt;
            player.pos.y += sy * STEER_SPEED * dt;
            player.pos.z = scrollZ; // locked to scroll

            // Clamp to corridor
            if (player.pos.x < -CORRIDOR_W) player.pos.x = -CORRIDOR_W;
            if (player.pos.x >  CORRIDOR_W) player.pos.x =  CORRIDOR_W;
            if (player.pos.y < 1.5f) player.pos.y = 1.5f;
            if (player.pos.y > CORRIDOR_H) player.pos.y = CORRIDOR_H;

            // Cosmetic tilt
            float targetRoll = -sx * TILT_FACTOR;
            player.roll += (targetRoll - player.roll) * 6.0f * dt;
            float targetPitch = -sy * PITCH_FACTOR;
            player.pitch += (targetPitch - player.pitch) * 6.0f * dt;

            // Barrel roll (Z or L-shift double-tap simulated as hold)
            if ((IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_LEFT_SHIFT)) && !player.barrelRolling) {
                player.barrelRolling = true;
                player.barrelRollAngle = 0;
                player.iframes = 0.6f;
            }
            if (player.barrelRolling) {
                player.barrelRollAngle += BARREL_ROLL_SPD * dt * 2.0f * PI;
                if (player.barrelRollAngle >= 2.0f * PI) {
                    player.barrelRolling = false;
                    player.barrelRollAngle = 0;
                }
            }

            // Invincibility timer
            if (player.iframes > 0) player.iframes -= dt;

            // Shoot
            player.shootCooldown -= dt;
            if ((IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_X)) && player.shootCooldown <= 0) {
                player.shootCooldown = 0.12f;
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!bullets[i].active) {
                        bullets[i].active = true;
                        bullets[i].pos = player.pos;
                        bullets[i].pos.z += 2.0f;
                        bullets[i].vel = (Vector3){0, 0, BULLET_SPEED};
                        bullets[i].life = BULLET_LIFE;
                        break;
                    }
                }
            }

            // --- Update bullets ---
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) continue;
                bullets[i].pos = Vector3Add(bullets[i].pos, Vector3Scale(bullets[i].vel, dt));
                bullets[i].life -= dt;
                if (bullets[i].life <= 0) bullets[i].active = false;
            }

            // --- Spawn enemies ---
            spawnTimer -= dt;
            if (spawnTimer <= 0) {
                SpawnEnemy();
                spawnTimer = 0.8f + (float)GetRandomValue(0, 100) / 100.0f;
            }

            // --- Update enemies ---
            for (int i = 0; i < MAX_ENEMIES; i++) {
                if (!enemies[i].active) continue;
                if (enemies[i].type == 1) {
                    // Weaving
                    enemies[i].pos.x += sinf(gameTime * 3.0f + enemies[i].sinOffset) * 8.0f * dt;
                }
                enemies[i].pos = Vector3Add(enemies[i].pos, Vector3Scale(enemies[i].vel, dt));
                // Remove if behind player
                if (enemies[i].pos.z < scrollZ - 30.0f) enemies[i].active = false;
            }

            // --- Bullet-enemy collision ---
            for (int b = 0; b < MAX_BULLETS; b++) {
                if (!bullets[b].active) continue;
                for (int e = 0; e < MAX_ENEMIES; e++) {
                    if (!enemies[e].active) continue;
                    float dist = Vector3Distance(bullets[b].pos, enemies[e].pos);
                    if (dist < 1.8f) {
                        enemies[e].hp -= 1;
                        bullets[b].active = false;
                        if (enemies[e].hp <= 0) {
                            SpawnExplosion(enemies[e].pos, 15, (Color){255, 80, 30, 255});
                            enemies[e].active = false;
                            player.score += 10;
                        }
                        break;
                    }
                }
            }

            // --- Player-enemy collision ---
            if (player.iframes <= 0) {
                for (int e = 0; e < MAX_ENEMIES; e++) {
                    if (!enemies[e].active) continue;
                    float dist = Vector3Distance(player.pos, enemies[e].pos);
                    if (dist < 2.0f) {
                        player.hp--;
                        player.iframes = 1.5f;
                        SpawnExplosion(enemies[e].pos, 10, (Color){255,100,0,255});
                        enemies[e].active = false;
                        if (player.hp <= 0) {
                            gameOver = true;
                            gameOverTimer = 0;
                            SpawnExplosion(player.pos, 25, (Color){255,200,50,255});
                        }
                        break;
                    }
                }
            }

            // --- Player-building collision ---
            if (player.iframes <= 0) {
                for (int i = 0; i < MAX_BUILDINGS; i++) {
                    Obstacle *ob = &buildings[i];
                    if (fabsf(player.pos.z - ob->pos.z) < ob->d/2 + 1.0f &&
                        fabsf(player.pos.x - ob->pos.x) < ob->w/2 + 1.0f &&
                        player.pos.y < ob->pos.y + ob->h/2 + 0.5f &&
                        player.pos.y > ob->pos.y - ob->h/2 - 0.5f) {
                        player.hp--;
                        player.iframes = 1.5f;
                        if (player.hp <= 0) {
                            gameOver = true;
                            gameOverTimer = 0;
                            SpawnExplosion(player.pos, 25, (Color){255,200,50,255});
                        }
                    }
                }
            }

            // --- Ring collection ---
            for (int i = 0; i < MAX_RINGS; i++) {
                if (rings[i].collected) continue;
                float dist = Vector3Distance(player.pos, rings[i].pos);
                if (dist < rings[i].radius + 1.0f) {
                    rings[i].collected = true;
                    player.score += 5;
                    player.iframes = 0.3f; // brief shield
                }
            }

            // --- Recycle obstacles continuously ---
            RecycleObstacles();
        }

        // --- Update particles ---
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            particles[i].pos = Vector3Add(particles[i].pos, Vector3Scale(particles[i].vel, dt));
            particles[i].vel.y -= 15.0f * dt;
            particles[i].life -= dt;
            if (particles[i].life <= 0) particles[i].active = false;
        }

        // --- Camera: behind and above player ---
        Vector3 camTarget = player.pos;
        camTarget.z += 15.0f;
        camera.target = camTarget;
        camera.position = (Vector3){
            player.pos.x,
            player.pos.y + 2.5f,
            player.pos.z - 6.0f
        };
        camera.up = (Vector3){0, 1, 0};

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){25, 30, 50, 255});

        BeginMode3D(camera);

        // Ground — draw grid of ground quads around player
        {
            float gx0 = floorf((player.pos.x - 200) / 10) * 10;
            float gz0 = floorf((scrollZ - 100) / 10) * 10;
            for (float gx = gx0; gx < gx0 + 400; gx += 10) {
                for (float gz = gz0; gz < gz0 + 400; gz += 10) {
                    float h = TerrainHeight(gx + 5, gz + 5);
                    Color gc;
                    if (h < 0.5f) gc = (Color){50, 110, 50, 255};
                    else if (h < 2.0f) gc = (Color){60, 120, 55, 255};
                    else gc = (Color){70, 130, 60, 255};
                    DrawCube((Vector3){gx + 5, h * 0.5f - 0.05f, gz + 5}, 10.2f, h + 0.1f, 10.2f, gc);
                }
            }
        }

        // Buildings
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            if (buildings[i].pos.z < scrollZ - 80 || buildings[i].pos.z > scrollZ + 350) continue;
            Obstacle *ob = &buildings[i];
            DrawCube(ob->pos, ob->w, ob->h, ob->d, ob->color);
            DrawCubeWires(ob->pos, ob->w, ob->h, ob->d, (Color){60,60,60,255});
        }

        // Pillars
        for (int i = 0; i < MAX_PILLARS; i++) {
            if (pillars[i].pos.z < scrollZ - 80 || pillars[i].pos.z > scrollZ + 350) continue;
            Obstacle *ob = &pillars[i];
            DrawCylinder(ob->pos, ob->w * 0.5f, ob->w * 0.4f, ob->h, 6, ob->color);
        }

        // Rings
        for (int i = 0; i < MAX_RINGS; i++) {
            if (rings[i].collected) continue;
            if (rings[i].pos.z < scrollZ - 20 || rings[i].pos.z > scrollZ + 350) continue;
            float r = rings[i].radius;
            Color rc = (Color){255, 220, 50, 200};
            int segs = 24;
            for (int s = 0; s < segs; s++) {
                float a0 = (float)s / segs * 2.0f * PI;
                float a1 = (float)(s+1) / segs * 2.0f * PI;
                Vector3 p0 = {rings[i].pos.x + cosf(a0)*r, rings[i].pos.y + sinf(a0)*r, rings[i].pos.z};
                Vector3 p1 = {rings[i].pos.x + cosf(a1)*r, rings[i].pos.y + sinf(a1)*r, rings[i].pos.z};
                DrawCylinderEx(p0, p1, 0.15f, 0.15f, 4, rc);
            }
        }

        // Enemies
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].active) continue;
            if (enemies[i].pos.z < scrollZ - 20 || enemies[i].pos.z > scrollZ + 350) continue;
            float enemyYaw = PI; // facing player
            DrawShip(enemyParts, enemyPartCount, enemies[i].pos, 0, enemyYaw, 0);
        }

        // Bullets — green laser bolts
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].active) continue;
            DrawCube(bullets[i].pos, 0.1f, 0.1f, 0.8f, (Color){100, 255, 100, 255});
        }

        // Player (blink when invincible)
        if (!gameOver) {
            bool visible = true;
            if (player.iframes > 0 && (int)(player.iframes * 10) % 2 == 0) visible = false;
            if (visible) {
                float drawRoll = player.roll + (player.barrelRolling ? player.barrelRollAngle : 0);
                DrawShip(arwingParts, arwingPartCount, player.pos, player.pitch, 0, drawRoll);

                // Engine glow
                Vector3 engL = Vector3Add(player.pos, RotateVec((Vector3){-0.5f, -0.05f, -1.1f}, player.pitch, 0, drawRoll));
                Vector3 engR = Vector3Add(player.pos, RotateVec((Vector3){ 0.5f, -0.05f, -1.1f}, player.pitch, 0, drawRoll));
                DrawSphere(engL, 0.12f + sinf(gameTime * 20) * 0.03f, (Color){100, 150, 255, 200});
                DrawSphere(engR, 0.12f + sinf(gameTime * 20) * 0.03f, (Color){100, 150, 255, 200});
            }
        }

        // Particles
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            float alpha = particles[i].life / particles[i].maxLife;
            Color c = particles[i].color;
            c.a = (unsigned char)(alpha * 255);
            DrawSphere(particles[i].pos, particles[i].size * alpha, c);
        }

        EndMode3D();

        // --- HUD ---
        // Crosshair
        int cx = GetScreenWidth() / 2, cy = GetScreenHeight() / 2;
        DrawCircleLines(cx, cy, 15, (Color){100, 255, 100, 150});
        DrawLine(cx - 20, cy, cx - 8, cy, (Color){100, 255, 100, 150});
        DrawLine(cx + 8, cy, cx + 20, cy, (Color){100, 255, 100, 150});
        DrawLine(cx, cy - 20, cx, cy - 8, (Color){100, 255, 100, 150});
        DrawLine(cx, cy + 8, cx, cy + 20, (Color){100, 255, 100, 150});

        // Shield bar
        DrawText("SHIELD", 20, 20, 14, (Color){150,150,150,255});
        for (int i = 0; i < 3; i++) {
            Color bc = (i < player.hp) ? (Color){50, 200, 50, 255} : (Color){60, 60, 60, 255};
            DrawRectangle(90 + i * 35, 20, 30, 16, bc);
            DrawRectangleLines(90 + i * 35, 20, 30, 16, (Color){100,100,100,255});
        }

        // Score
        DrawText(TextFormat("SCORE: %d", player.score), GetScreenWidth() - 180, 20, 20, WHITE);

        // Distance
        DrawText(TextFormat("%.0fm", scrollZ), GetScreenWidth() / 2 - 30, 20, 16, (Color){150,200,255,255});

        // Barrel roll prompt
        if (!player.barrelRolling) {
            DrawText("[Z] BARREL ROLL", 20, GetScreenHeight() - 35, 12, (Color){100,100,120,255});
        }

        // Game over
        if (gameOver) {
            const char *goText = "MISSION FAILED";
            int goW = MeasureText(goText, 40);
            DrawText(goText, GetScreenWidth()/2 - goW/2, GetScreenHeight()/2 - 40, 40, RED);
            DrawText(TextFormat("SCORE: %d", player.score),
                GetScreenWidth()/2 - 60, GetScreenHeight()/2 + 10, 20, WHITE);
            if (gameOverTimer > 1.0f)
                DrawText("Press ENTER to retry", GetScreenWidth()/2 - 100, GetScreenHeight()/2 + 40, 16, (Color){180,180,180,255});
        }

        DrawFPS(10, GetScreenHeight() - 20);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
