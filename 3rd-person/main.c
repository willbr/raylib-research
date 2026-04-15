#include "raylib.h"
#include "raymath.h"
#include "../common/objects3d.h"
#include <math.h>
#include <stdlib.h>

// Third-person shooter: over-the-shoulder camera, cover, enemies, waves

#define MAP_SIZE       50.0f
#define MAX_BULLETS    64
#define MAX_ENEMIES    16
#define MAX_PARTICLES  48
#define MAX_WALLS      24
#define MAX_COVERS     16
#define PLAYER_SPEED   7.0f
#define SPRINT_MULT    1.6f
#define AIM_SPEED_MULT 0.4f
#define CAM_DIST       4.0f
#define CAM_DIST_AIM   2.2f
#define SHOULDER_OFF   1.2f
#define SHOULDER_AIM   0.6f
#define JUMP_FORCE     8.0f
#define GRAVITY       20.0f
#define BULLET_SPEED  40.0f

typedef struct {
    Vector3 pos;
    float yaw;       // facing direction (radians)
    float velY;
    bool grounded;
    float hp, maxHp;
    float fireTimer;
    float reloadTimer;
    int ammo, maxAmmo;
    bool aiming;
    bool sprinting;
    bool crouching;
    int kills;
    float damageFlash;
} Player;

typedef struct {
    Vector3 pos;
    float yaw;
    float hp, maxHp;
    float speed;
    float fireTimer;
    float sightRange;
    bool active;
    bool alerted;
    float hitFlash;
} Enemy;

typedef struct {
    Vector3 pos, vel;
    float life, damage;
    int owner; // -1=player, >=0 enemy index
    bool active;
} Bullet;

typedef struct { Vector3 pos; float w, h, d; } Wall;
typedef struct { Vector3 pos; float w, h, d; bool destructible; float hp; bool active; } Cover;

// Player model
static Part playerParts[] = {
    // Boots
    CUBE(-0.12f, 0.08f, 0, 0.14f, 0.16f, 0.2f, COL(50,45,40,255)),
    CUBE( 0.12f, 0.08f, 0, 0.14f, 0.16f, 0.2f, COL(50,45,40,255)),
    // Legs
    CUBE(-0.1f, 0.35f, 0, 0.12f, 0.38f, 0.12f, COL(60,70,60,255)),
    CUBE( 0.1f, 0.35f, 0, 0.12f, 0.38f, 0.12f, COL(60,70,60,255)),
    // Belt
    CUBE(0, 0.55f, 0, 0.4f, 0.06f, 0.28f, COL(50,40,30,255)),
    // Torso
    CUBE(0, 0.8f, 0, 0.4f, 0.45f, 0.25f, COL(50,80,50,255)),
    // Shoulders
    SPHERE(-0.25f, 1.0f, 0, 0.08f, COL(50,80,50,255)),
    SPHERE( 0.25f, 1.0f, 0, 0.08f, COL(50,80,50,255)),
    // Arms
    CUBE(-0.3f, 0.75f, 0.05f, 0.08f, 0.35f, 0.08f, COL(180,155,130,255)),
    CUBE( 0.3f, 0.75f, 0.05f, 0.08f, 0.35f, 0.08f, COL(180,155,130,255)),
    // Hands
    SPHERE(-0.3f, 0.55f, 0.08f, 0.05f, COL(180,155,130,255)),
    SPHERE( 0.3f, 0.55f, 0.08f, 0.05f, COL(180,155,130,255)),
    // Neck
    CYL(0, 1.02f, 0, 0.06f, 0.06f, COL(180,155,130,255)),
    // Head
    SPHERE(0, 1.18f, 0, 0.14f, COL(180,155,130,255)),
    // Helmet
    SPHERE(0, 1.24f, 0, 0.16f, COL(60,80,60,255)),
};

// Enemy model
static Part enemyParts[] = {
    CUBE(-0.1f, 0.08f, 0, 0.13f, 0.16f, 0.18f, COL(40,35,30,255)),
    CUBE( 0.1f, 0.08f, 0, 0.13f, 0.16f, 0.18f, COL(40,35,30,255)),
    CUBE(-0.09f, 0.35f, 0, 0.11f, 0.36f, 0.11f, COL(50,50,55,255)),
    CUBE( 0.09f, 0.35f, 0, 0.11f, 0.36f, 0.11f, COL(50,50,55,255)),
    CUBE(0, 0.55f, 0, 0.38f, 0.06f, 0.26f, COL(40,35,30,255)),
    CUBE(0, 0.78f, 0, 0.38f, 0.42f, 0.24f, COL(140,50,40,255)),
    CUBE(-0.28f, 0.72f, 0.05f, 0.07f, 0.32f, 0.07f, COL(170,145,120,255)),
    CUBE( 0.28f, 0.72f, 0.05f, 0.07f, 0.32f, 0.07f, COL(170,145,120,255)),
    CYL(0, 0.98f, 0, 0.05f, 0.05f, COL(170,145,120,255)),
    SPHERE(0, 1.12f, 0, 0.13f, COL(170,145,120,255)),
    SPHERE(0, 1.17f, 0, 0.14f, COL(60,30,30,255)), // beret
};

static Player player;
static Enemy enemies[MAX_ENEMIES];
static Bullet bullets[MAX_BULLETS];
static Particle3D particles[MAX_PARTICLES];
static Wall walls[MAX_WALLS];
static int numWalls = 0;
static Cover covers[MAX_COVERS];
static int numCovers = 0;
static int wave = 0, enemiesAlive = 0;
static float waveTimer = 3.0f;
static float gameTime = 0;
static bool gameOver = false;
static ScreenShake shake;
static float camYaw = 0, camPitch = 0.3f;

bool CheckWallCol(Vector3 pos, float r) {
    float hs = MAP_SIZE / 2;
    if (pos.x < -hs+r || pos.x > hs-r || pos.z < -hs+r || pos.z > hs-r) return true;
    for (int i = 0; i < numWalls; i++) {
        if (fabsf(pos.x - walls[i].pos.x) < walls[i].w/2+r &&
            fabsf(pos.z - walls[i].pos.z) < walls[i].d/2+r &&
            pos.y < walls[i].h) return true;
    }
    for (int i = 0; i < numCovers; i++) {
        if (!covers[i].active) continue;
        if (fabsf(pos.x - covers[i].pos.x) < covers[i].w/2+r &&
            fabsf(pos.z - covers[i].pos.z) < covers[i].d/2+r &&
            pos.y < covers[i].h) return true;
    }
    return false;
}

void BuildLevel(void) {
    numWalls = 0; numCovers = 0;
    Color wc = {100,95,88,255};
    float hs = MAP_SIZE/2;
    // Border walls
    walls[numWalls++] = (Wall){{0, 2, -hs}, MAP_SIZE, 4, 0.5f};
    walls[numWalls++] = (Wall){{0, 2, hs}, MAP_SIZE, 4, 0.5f};
    walls[numWalls++] = (Wall){{-hs, 2, 0}, 0.5f, 4, MAP_SIZE};
    walls[numWalls++] = (Wall){{hs, 2, 0}, 0.5f, 4, MAP_SIZE};
    // Buildings
    walls[numWalls++] = (Wall){{-15, 2, -15}, 8, 4, 6};
    walls[numWalls++] = (Wall){{15, 2, 15}, 6, 4, 8};
    walls[numWalls++] = (Wall){{-18, 2, 10}, 4, 4, 4};
    walls[numWalls++] = (Wall){{18, 2, -10}, 4, 4, 4};
    // Low walls for cover
    walls[numWalls++] = (Wall){{-5, 0.6f, -8}, 4, 1.2f, 0.4f};
    walls[numWalls++] = (Wall){{8, 0.6f, 5}, 0.4f, 1.2f, 4};
    walls[numWalls++] = (Wall){{-8, 0.6f, 12}, 5, 1.2f, 0.4f};
    walls[numWalls++] = (Wall){{0, 0.6f, 0}, 3, 1.2f, 0.4f};
    // Crates/barrels as destructible cover
    covers[numCovers++] = (Cover){{-3, 0.5f, -4}, 1, 1, 1, true, 30, true};
    covers[numCovers++] = (Cover){{5, 0.5f, -12}, 1, 1, 1, true, 30, true};
    covers[numCovers++] = (Cover){{-10, 0.5f, 5}, 1, 1, 1, true, 30, true};
    covers[numCovers++] = (Cover){{12, 0.5f, 0}, 1, 1, 1, true, 30, true};
    covers[numCovers++] = (Cover){{0, 0.5f, 10}, 1, 1, 1, true, 30, true};
    covers[numCovers++] = (Cover){{-6, 0.5f, -15}, 1, 1, 1, true, 30, true};
    covers[numCovers++] = (Cover){{10, 0.5f, 8}, 1, 1, 1, true, 30, true};
}

void SpawnWave(void) {
    wave++;
    int count = 2 + wave * 2;
    if (count > MAX_ENEMIES) count = MAX_ENEMIES;
    float hs = MAP_SIZE/2 - 3;
    for (int i = 0; i < count; i++) {
        if (enemies[i].active) continue;
        float angle = (float)GetRandomValue(0, 628) / 100.0f;
        float dist = hs * 0.6f + (float)GetRandomValue(0, (int)(hs*0.4f*10)) / 10.0f;
        enemies[i] = (Enemy){
            .pos = {cosf(angle)*dist, 0, sinf(angle)*dist},
            .yaw = 0, .hp = 30 + wave*5, .maxHp = 30 + wave*5,
            .speed = 3.0f + (float)GetRandomValue(0,20)/10.0f,
            .fireTimer = (float)GetRandomValue(10,30)/10.0f,
            .sightRange = 20.0f, .active = true, .alerted = false, .hitFlash = 0
        };
        enemiesAlive++;
    }
}

void InitGame(void) {
    player = (Player){
        .pos = {0, 0, -10}, .yaw = 0, .hp = 100, .maxHp = 100,
        .ammo = 12, .maxAmmo = 12, .kills = 0
    };
    player.grounded = true;
    memset(enemies, 0, sizeof(enemies));
    memset(bullets, 0, sizeof(bullets));
    memset(particles, 0, sizeof(particles));
    wave = 0; enemiesAlive = 0; waveTimer = 3.0f;
    gameOver = false; gameTime = 0;
    shake = (ScreenShake){0};
    camYaw = 0; camPitch = 0.3f;
    BuildLevel();
}

void FireBullet(Vector3 origin, Vector3 dir, float damage, int owner) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) continue;
        bullets[i] = (Bullet){
            .pos = origin, .vel = Vector3Scale(Vector3Normalize(dir), BULLET_SPEED),
            .life = 2.0f, .damage = damage, .owner = owner, .active = true
        };
        break;
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Third Person Shooter");
    MaximizeWindow();
    SetTargetFPS(60);
    DisableCursor();

    InitGame();

    Camera3D camera = {0};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.up = (Vector3){0, 1, 0};

    float curCamDist = CAM_DIST;
    float curShoulder = SHOULDER_OFF;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        gameTime += dt;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        if (gameOver) {
            if (IsKeyPressed(KEY_ENTER)) { InitGame(); DisableCursor(); }
        } else {
            // --- Mouse look (always on) ---
            Vector2 md = GetMouseDelta();
            camYaw -= md.x * 0.003f;
            camPitch += md.y * 0.003f;
            if (camPitch < -1.2f) camPitch = -1.2f;
            if (camPitch > 1.2f) camPitch = 1.2f;

            // --- Input ---
            player.aiming = IsMouseButtonDown(MOUSE_RIGHT_BUTTON);
            player.sprinting = IsKeyDown(KEY_LEFT_SHIFT) && !player.aiming;
            player.crouching = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C);

            Vector3 camFwd = {sinf(camYaw), 0, cosf(camYaw)};
            Vector3 camRight = {cosf(camYaw), 0, -sinf(camYaw)};

            float speed = PLAYER_SPEED * dt;
            if (player.aiming) speed *= AIM_SPEED_MULT;
            if (player.sprinting) speed *= SPRINT_MULT;
            if (player.crouching) speed *= 0.5f;

            Vector3 move = {0};
            if (IsKeyDown(KEY_W)) move = Vector3Add(move, Vector3Scale(camFwd, -speed));
            if (IsKeyDown(KEY_S)) move = Vector3Add(move, Vector3Scale(camFwd, speed));
            if (IsKeyDown(KEY_A)) move = Vector3Add(move, Vector3Scale(camRight, -speed));
            if (IsKeyDown(KEY_D)) move = Vector3Add(move, Vector3Scale(camRight, speed));

            // Face movement direction (or camera direction when aiming)
            if (player.aiming) {
                player.yaw = camYaw;
            } else if (Vector3Length(move) > 0.01f) {
                player.yaw = atan2f(move.x, move.z);
            }

            // Apply movement with wall collision
            Vector3 np = Vector3Add(player.pos, (Vector3){move.x, 0, 0});
            if (!CheckWallCol(np, 0.3f)) player.pos.x = np.x;
            np = Vector3Add(player.pos, (Vector3){0, 0, move.z});
            if (!CheckWallCol(np, 0.3f)) player.pos.z = np.z;

            // Jump
            if (IsKeyPressed(KEY_SPACE) && player.grounded && !player.crouching) {
                player.velY = JUMP_FORCE; player.grounded = false;
            }
            player.velY -= GRAVITY * dt;
            player.pos.y += player.velY * dt;
            if (player.pos.y <= 0) { player.pos.y = 0; player.velY = 0; player.grounded = true; }

            // Reload
            if (player.reloadTimer > 0) {
                player.reloadTimer -= dt;
                if (player.reloadTimer <= 0) { player.ammo = player.maxAmmo; }
            }
            if (IsKeyPressed(KEY_R) && player.reloadTimer <= 0 && player.ammo < player.maxAmmo)
                player.reloadTimer = 1.5f;

            // Shoot
            player.fireTimer -= dt;
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && player.fireTimer <= 0 && player.ammo > 0 && player.reloadTimer <= 0) {
                player.fireTimer = 0.2f;
                player.ammo--;
                Vector3 shootDir = Vector3Subtract(camera.target, camera.position);
                Vector3 origin = {player.pos.x, player.pos.y + (player.crouching ? 0.8f : 1.4f), player.pos.z};
                // Small spread
                shootDir.x += (float)GetRandomValue(-20,20)/1000.0f;
                shootDir.y += (float)GetRandomValue(-20,20)/1000.0f;
                shootDir.z += (float)GetRandomValue(-20,20)/1000.0f;
                FireBullet(origin, shootDir, 15, -1);
                ShakeTrigger(&shake, 0.08f);
                if (player.ammo <= 0) player.reloadTimer = 1.5f;
            }

            // --- Update enemies ---
            for (int e = 0; e < MAX_ENEMIES; e++) {
                if (!enemies[e].active) continue;
                enemies[e].hitFlash -= dt;
                Vector3 toPlayer = Vector3Subtract(player.pos, enemies[e].pos);
                toPlayer.y = 0;
                float dist = Vector3Length(toPlayer);
                enemies[e].yaw = atan2f(toPlayer.x, toPlayer.z);

                if (dist < enemies[e].sightRange) enemies[e].alerted = true;

                if (enemies[e].alerted) {
                    if (dist > 8.0f) {
                        // Move toward player
                        Vector3 dir = Vector3Normalize(toPlayer);
                        Vector3 newP = Vector3Add(enemies[e].pos, Vector3Scale(dir, enemies[e].speed * dt));
                        if (!CheckWallCol(newP, 0.3f)) enemies[e].pos = newP;
                    }
                    // Shoot at player
                    if (dist < enemies[e].sightRange) {
                        enemies[e].fireTimer -= dt;
                        if (enemies[e].fireTimer <= 0) {
                            enemies[e].fireTimer = 1.5f + (float)GetRandomValue(0,10)/10.0f;
                            Vector3 shootDir = Vector3Subtract(
                                (Vector3){player.pos.x, player.pos.y + 1.0f, player.pos.z},
                                (Vector3){enemies[e].pos.x, enemies[e].pos.y + 0.9f, enemies[e].pos.z});
                            // Enemy inaccuracy
                            shootDir.x += (float)GetRandomValue(-80,80)/1000.0f;
                            shootDir.z += (float)GetRandomValue(-80,80)/1000.0f;
                            FireBullet((Vector3){enemies[e].pos.x, enemies[e].pos.y + 0.9f, enemies[e].pos.z},
                                       shootDir, 8, e);
                        }
                    }
                }
                // Push apart
                for (int j = e+1; j < MAX_ENEMIES; j++) {
                    if (!enemies[j].active) continue;
                    Vector3 diff = Vector3Subtract(enemies[e].pos, enemies[j].pos);
                    diff.y = 0;
                    float d = Vector3Length(diff);
                    if (d < 1.0f && d > 0.01f) {
                        Vector3 push = Vector3Scale(Vector3Normalize(diff), (1.0f-d)*0.5f);
                        enemies[e].pos = Vector3Add(enemies[e].pos, push);
                        enemies[j].pos = Vector3Subtract(enemies[j].pos, push);
                    }
                }
            }

            // --- Update bullets ---
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) continue;
                bullets[i].pos = Vector3Add(bullets[i].pos, Vector3Scale(bullets[i].vel, dt));
                bullets[i].life -= dt;
                if (bullets[i].life <= 0 || CheckWallCol(bullets[i].pos, 0.05f)) {
                    SpawnParticleBurst(particles, MAX_PARTICLES, bullets[i].pos, 3, 1, 3, 0.1f, 0.3f, 0.02f, 0.06f);
                    bullets[i].active = false; continue;
                }
                // Player bullets hit enemies
                if (bullets[i].owner == -1) {
                    for (int e = 0; e < MAX_ENEMIES; e++) {
                        if (!enemies[e].active) continue;
                        if (Vector3Distance(bullets[i].pos, (Vector3){enemies[e].pos.x, enemies[e].pos.y+0.7f, enemies[e].pos.z}) < 0.7f) {
                            enemies[e].hp -= bullets[i].damage;
                            enemies[e].hitFlash = 0.1f;
                            enemies[e].alerted = true;
                            SpawnParticleBurst(particles, MAX_PARTICLES, bullets[i].pos, 4, 2, 5, 0.2f, 0.5f, 0.03f, 0.1f);
                            bullets[i].active = false;
                            if (enemies[e].hp <= 0) {
                                enemies[e].active = false; enemiesAlive--;
                                player.kills++;
                                SpawnParticleBurst(particles, MAX_PARTICLES, enemies[e].pos, 10, 2, 7, 0.3f, 0.8f, 0.05f, 0.15f);
                                ShakeTrigger(&shake, 0.12f);
                            }
                            break;
                        }
                    }
                }
                // Enemy bullets hit player
                if (bullets[i].owner >= 0 && bullets[i].active) {
                    if (Vector3Distance(bullets[i].pos, (Vector3){player.pos.x, player.pos.y+0.8f, player.pos.z}) < 0.5f) {
                        player.hp -= bullets[i].damage;
                        player.damageFlash = 0.2f;
                        ShakeTrigger(&shake, 0.15f);
                        bullets[i].active = false;
                        if (player.hp <= 0) { player.hp = 0; gameOver = true; EnableCursor(); }
                    }
                }
                // Bullets hit destructible cover
                for (int c = 0; c < numCovers; c++) {
                    if (!covers[c].active) continue;
                    if (fabsf(bullets[i].pos.x - covers[c].pos.x) < covers[c].w/2+0.1f &&
                        fabsf(bullets[i].pos.z - covers[c].pos.z) < covers[c].d/2+0.1f &&
                        bullets[i].pos.y < covers[c].h + 0.1f) {
                        covers[c].hp -= bullets[i].damage;
                        SpawnParticleBurst(particles, MAX_PARTICLES, bullets[i].pos, 3, 1, 3, 0.1f, 0.3f, 0.02f, 0.06f);
                        bullets[i].active = false;
                        if (covers[c].hp <= 0) {
                            covers[c].active = false;
                            SpawnParticleBurst(particles, MAX_PARTICLES, covers[c].pos, 8, 2, 6, 0.3f, 0.7f, 0.04f, 0.12f);
                        }
                        break;
                    }
                }
            }

            // Waves
            if (enemiesAlive <= 0) {
                waveTimer -= dt;
                if (waveTimer <= 0) { SpawnWave(); waveTimer = 5.0f; }
            }
            player.damageFlash -= dt;
        }

        UpdateParticles3D(particles, MAX_PARTICLES, dt, 10.0f);
        ShakeUpdate(&shake, dt);
        Vector2 shOff = ShakeOffset(&shake);

        // --- Camera ---
        float tDist = player.aiming ? CAM_DIST_AIM : CAM_DIST;
        float tShoulder = player.aiming ? SHOULDER_AIM : SHOULDER_OFF;
        curCamDist += (tDist - curCamDist) * 8.0f * dt;
        curShoulder += (tShoulder - curShoulder) * 8.0f * dt;

        float eyeH = player.crouching ? 0.7f : 1.5f;
        Vector3 camOffset = {
            sinf(camYaw) * cosf(camPitch) * curCamDist,
            sinf(camPitch) * curCamDist + eyeH,
            cosf(camYaw) * cosf(camPitch) * curCamDist
        };
        Vector3 shoulderVec = {cosf(camYaw), 0, -sinf(camYaw)};
        camOffset = Vector3Add(camOffset, Vector3Scale(shoulderVec, curShoulder));
        camera.position = Vector3Add(player.pos, camOffset);
        // Camera wall collision — pull camera closer if it would clip through geometry
        {
            Vector3 playerEye = {player.pos.x, player.pos.y + eyeH, player.pos.z};
            Vector3 camDir = Vector3Subtract(camera.position, playerEye);
            float camLen = Vector3Length(camDir);
            if (camLen > 0.1f) {
                Vector3 camStep = Vector3Scale(camDir, 1.0f / camLen);
                float safeDist = camLen;
                for (float t = 0.3f; t < camLen; t += 0.2f) {
                    Vector3 testPos = Vector3Add(playerEye, Vector3Scale(camStep, t));
                    if (CheckWallCol(testPos, 0.15f)) {
                        safeDist = t - 0.3f;
                        if (safeDist < 0.5f) safeDist = 0.5f;
                        break;
                    }
                }
                if (safeDist < camLen) {
                    camera.position = Vector3Add(playerEye, Vector3Scale(camStep, safeDist));
                }
            }
        }
        camera.position.x += shOff.x;
        camera.position.y += shOff.y;

        Vector3 targetOff = Vector3Scale(shoulderVec, curShoulder * 0.15f);
        camera.target = Vector3Add(player.pos, targetOff);
        camera.target.y += eyeH - 0.3f;

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){40, 50, 55, 255});
        BeginMode3D(camera);

        // Floor
        DrawPlane((Vector3){0,0,0}, (Vector2){MAP_SIZE, MAP_SIZE}, (Color){55, 60, 52, 255});
        for (float g = -MAP_SIZE/2; g <= MAP_SIZE/2; g += 5) {
            DrawLine3D((Vector3){g,0.01f,-MAP_SIZE/2}, (Vector3){g,0.01f,MAP_SIZE/2}, (Color){50,55,48,255});
            DrawLine3D((Vector3){-MAP_SIZE/2,0.01f,g}, (Vector3){MAP_SIZE/2,0.01f,g}, (Color){50,55,48,255});
        }

        // Walls with detail
        for (int i = 0; i < numWalls; i++) {
            Wall *wl = &walls[i];
            bool tall = wl->h > 2;
            Color wc = tall ? (Color){100,95,88,255} : (Color){85,80,72,255};
            DrawCube(wl->pos, wl->w, wl->h, wl->d, wc);
            // Base trim
            float bw = wl->w > 1 ? wl->w : wl->w + 0.04f;
            float bd = wl->d > 1 ? wl->d : wl->d + 0.04f;
            DrawCube((Vector3){wl->pos.x, 0.08f, wl->pos.z}, bw, 0.16f, bd,
                (Color){wc.r-25, wc.g-25, wc.b-25, 255});
            // Top cap
            DrawCube((Vector3){wl->pos.x, wl->h-0.04f, wl->pos.z}, bw, 0.08f, bd,
                (Color){wc.r+10, wc.g+10, wc.b+10, 255});
            // Mortar lines
            for (float ly = 0.4f; ly < wl->h; ly += 0.7f) {
                DrawCube((Vector3){wl->pos.x, ly, wl->pos.z}, bw+0.01f, 0.015f, bd+0.01f,
                    (Color){wc.r-12, wc.g-12, wc.b-12, 180});
            }
            DrawCubeWires(wl->pos, wl->w, wl->h, wl->d, (Color){wc.r-20, wc.g-20, wc.b-20, 255});
            // Tall walls get windows
            if (tall && wl->w > 3 && wl->d < 1) {
                for (float wx = wl->pos.x - wl->w/3; wx <= wl->pos.x + wl->w/3; wx += wl->w/3) {
                    DrawCube((Vector3){wx, 2.5f, wl->pos.z + wl->d/2 + 0.01f}, 0.8f, 0.8f, 0.02f,
                        (Color){40,50,60,200});
                }
            }
        }

        // Destructible cover (crates with detail)
        for (int i = 0; i < numCovers; i++) {
            if (!covers[i].active) continue;
            Cover *cv = &covers[i];
            DrawCube(cv->pos, cv->w, cv->h, cv->d, (Color){140,110,70,255});
            // Plank lines
            DrawCube(cv->pos, cv->w+0.02f, 0.06f, cv->d+0.02f, (Color){120,90,50,255});
            DrawCube((Vector3){cv->pos.x, cv->pos.y+cv->h/2, cv->pos.z}, cv->w+0.02f, 0.06f, cv->d+0.02f, (Color){120,90,50,255});
            DrawCube((Vector3){cv->pos.x, cv->pos.y-cv->h/2+0.03f, cv->pos.z}, cv->w+0.02f, 0.06f, cv->d+0.02f, (Color){120,90,50,255});
            DrawCubeWires(cv->pos, cv->w, cv->h, cv->d, (Color){100,75,45,255});
        }

        // Ambient scenery — lamp posts at corners
        {
            float lp[][2] = {{-20,-20},{20,-20},{-20,20},{20,20}};
            for (int i = 0; i < 4; i++) {
                Vector3 base = {lp[i][0], 0, lp[i][1]};
                DrawCylinder(base, 0.08f, 0.08f, 3.5f, 6, (Color){60,60,65,255});
                DrawSphere((Vector3){base.x, 3.5f, base.z}, 0.2f, (Color){255,240,180,200});
                // Light glow
                DrawSphere((Vector3){base.x, 3.5f, base.z}, 0.4f + sinf(gameTime*2+i)*0.05f,
                    (Color){255,240,180,40});
            }
        }

        // Player character
        {
            int pc = sizeof(playerParts)/sizeof(Part);
            Object3D pobj = {playerParts, pc, player.pos, player.yaw};
            if (player.crouching) {
                // Draw lower
                pobj.pos.y -= 0.3f;
            }
            DrawObject3D(&pobj);
        }

        // Enemies
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].active) continue;
            int ec = sizeof(enemyParts)/sizeof(Part);
            if (enemies[e].hitFlash > 0) {
                DrawCube((Vector3){enemies[e].pos.x, enemies[e].pos.y+0.7f, enemies[e].pos.z}, 0.5f, 1.4f, 0.3f, WHITE);
            } else {
                Object3D eobj = {enemyParts, ec, enemies[e].pos, enemies[e].yaw};
                DrawObject3D(&eobj);
            }
            // HP bar
            if (enemies[e].hp < enemies[e].maxHp) {
                Vector2 sp = GetWorldToScreen((Vector3){enemies[e].pos.x, enemies[e].pos.y+1.8f, enemies[e].pos.z}, camera);
                if (sp.y > 0 && sp.y < sh) {
                    float pct = enemies[e].hp / enemies[e].maxHp;
                    DrawRectangle(sp.x-15, sp.y, 30, 4, (Color){40,40,40,200});
                    Color hc = pct > 0.5f ? (Color){50,200,80,255} : (Color){220,40,40,255};
                    DrawRectangle(sp.x-15, sp.y, (int)(30*pct), 4, hc);
                }
            }
        }

        // Bullets
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].active) continue;
            Color bc = (bullets[i].owner == -1) ? YELLOW : (Color){255,100,50,255};
            DrawSphere(bullets[i].pos, 0.06f, bc);
            Vector3 trail = Vector3Subtract(bullets[i].pos, Vector3Scale(bullets[i].vel, 0.02f));
            DrawLine3D(bullets[i].pos, trail, bc);
        }

        DrawParticles3D(particles, MAX_PARTICLES);
        EndMode3D();

        // --- HUD ---
        // Crosshair
        int cx = sw/2, cy = sh/2;
        int cs = player.aiming ? 6 : 10;
        Color cc = player.aiming ? (Color){255,50,50,200} : (Color){255,255,255,150};
        DrawLine(cx-cs, cy, cx-3, cy, cc);
        DrawLine(cx+3, cy, cx+cs, cy, cc);
        DrawLine(cx, cy-cs, cx, cy-3, cc);
        DrawLine(cx, cy+3, cx, cy+cs, cc);
        if (player.aiming) DrawCircleLines(cx, cy, 12, (Color){255,50,50,100});

        // HP bar
        DrawRectangle(20, sh-45, 180, 18, (Color){30,30,30,200});
        float hpPct = player.hp / player.maxHp;
        Color hpC = hpPct > 0.5f ? (Color){50,200,80,255} : hpPct > 0.25f ? (Color){240,200,40,255} : (Color){220,40,40,255};
        DrawRectangle(20, sh-45, (int)(180*hpPct), 18, hpC);
        DrawRectangleLines(20, sh-45, 180, 18, (Color){80,80,80,255});
        DrawText(TextFormat("%.0f", player.hp), 25, sh-43, 14, WHITE);

        // Ammo
        DrawText(TextFormat("%d / %d", player.ammo, player.maxAmmo), sw-140, sh-45, 18, WHITE);
        if (player.reloadTimer > 0) DrawText("RELOADING", sw-140, sh-65, 14, YELLOW);

        // Wave + kills
        DrawText(TextFormat("Wave %d", wave), 20, 15, 22, WHITE);
        DrawText(TextFormat("Kills: %d  Enemies: %d", player.kills, enemiesAlive), 20, 42, 14, (Color){180,180,180,255});
        if (enemiesAlive <= 0 && !gameOver)
            DrawText(TextFormat("Next wave in %.0f...", waveTimer), sw/2-80, 70, 18, GOLD);

        // Controls
        DrawText("WASD: Move  RMB: Aim  LMB: Shoot  R: Reload  Shift: Sprint  Ctrl: Crouch", 20, sh-18, 11, (Color){80,90,80,255});

        // Damage flash
        if (player.damageFlash > 0)
            DrawRectangle(0, 0, sw, sh, (Color){200,30,30,(unsigned char)(fminf(player.damageFlash,1)*300)});

        // Game over
        if (gameOver) {
            DrawRectangle(0, 0, sw, sh, (Color){0,0,0,150});
            int goW = MeasureText("YOU DIED", 48);
            DrawText("YOU DIED", sw/2-goW/2, sh/2-50, 48, RED);
            DrawText(TextFormat("Waves: %d  Kills: %d", wave, player.kills), sw/2-80, sh/2+10, 20, WHITE);
            DrawText("Press ENTER to restart", sw/2-100, sh/2+40, 16, (Color){180,180,180,255});
        }

        DrawFPS(sw-80, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
