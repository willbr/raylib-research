#include "raylib.h"
#include <math.h>
#include <stdlib.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define PLAYER_SPEED 2.0f
#define ROTATION_SPEED 0.05f

// Player structure
typedef struct {
    Vector3 position;
    float rotation;
    BoundingBox bounds;
} Player;

// Enemy structure
typedef struct {
    Vector3 position;
    float rotation;
    float visionAngle;
    float visionDistance;
    bool alerted;
} Enemy;

// Fallback vector math functions (in case Raylib versions differ)
Vector3 Vector3Subtract(Vector3 v1, Vector3 v2) {
    return (Vector3){ v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

float Vector3Length(Vector3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 Vector3Normalize(Vector3 v) {
    float length = Vector3Length(v);
    if (length == 0.0f) return (Vector3){ 0.0f, 0.0f, 0.0f };
    return (Vector3){ v.x / length, v.y / length, v.z / length };
}

float Vector3DotProduct(Vector3 v1, Vector3 v2) {
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

// Function prototypes
void UpdatePlayer(Player* player, Camera3D* camera);
void UpdateEnemy(Enemy* enemy, Player* player);
bool CheckDetection(Player* player, Enemy* enemy);
void DrawCone(Vector3 position, float rotation, float distance, float angle, Color color);

int main(void) {
    // Initialization
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Metal Gear Clone");
    SetTargetFPS(60);

    // Camera setup
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Player setup
    Player player = { 0 };
    player.position = (Vector3){ 0.0f, 0.0f, 0.0f };
    player.rotation = 0.0f;
    player.bounds = (BoundingBox){
        (Vector3){ -0.5f, -0.5f, -0.5f },
        (Vector3){ 0.5f, 0.5f, 0.5f }
    };

    // Enemy setup
    Enemy enemy = { 0 };
    enemy.position = (Vector3){ 5.0f, 0.0f, 5.0f };
    enemy.rotation = 0.0f;
    enemy.visionAngle = 45.0f;
    enemy.visionDistance = 5.0f;
    enemy.alerted = false;

    // Ground plane
    Mesh groundMesh = GenMeshPlane(20.0f, 20.0f, 10, 10);
    Model ground = LoadModelFromMesh(groundMesh);

    while (!WindowShouldClose()) {
        // Update
        UpdatePlayer(&player, &camera);
        UpdateEnemy(&enemy, &player);

        // Draw
        BeginDrawing();
            ClearBackground(RAYWHITE);
            
            BeginMode3D(camera);
                DrawModel(ground, (Vector3){0.0f, -0.1f, 0.0f}, 1.0f, GRAY);
                DrawCubeV(player.position, (Vector3){1.0f, 1.0f, 1.0f}, BLUE);
                DrawCubeV(enemy.position, (Vector3){1.0f, 1.0f, 1.0f}, 
                         enemy.alerted ? RED : GREEN);
                DrawCone(enemy.position, enemy.rotation, enemy.visionDistance, 
                        enemy.visionAngle, enemy.alerted ? RED : YELLOW);
            EndMode3D();

            DrawText(enemy.alerted ? "ALERT!" : "Stealth Mode", 10, 10, 20, 
                    enemy.alerted ? RED : GREEN);
            DrawFPS(10, 40);
        EndDrawing();
    }

    UnloadModel(ground);
    CloseWindow();
    return 0;
}

void UpdatePlayer(Player* player, Camera3D* camera) {
    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move.z += PLAYER_SPEED * GetFrameTime();
    if (IsKeyDown(KEY_S)) move.z -= PLAYER_SPEED * GetFrameTime();
    if (IsKeyDown(KEY_A)) move.x -= PLAYER_SPEED * GetFrameTime();
    if (IsKeyDown(KEY_D)) move.x += PLAYER_SPEED * GetFrameTime();

    player->rotation -= GetMouseDelta().x * ROTATION_SPEED;

    float angle = player->rotation;
    Vector3 newPos = player->position;
    newPos.x += move.x * cosf(angle) - move.z * sinf(angle);
    newPos.z += move.x * sinf(angle) + move.z * cosf(angle);
    player->position = newPos;

    camera->target = player->position;
    camera->position.x = player->position.x - sinf(player->rotation) * 10.0f;
    camera->position.z = player->position.z - cosf(player->rotation) * 10.0f;
}

void UpdateEnemy(Enemy* enemy, Player* player) {
    enemy->rotation += 0.5f * GetFrameTime();
    enemy->alerted = CheckDetection(player, enemy);
}

bool CheckDetection(Player* player, Enemy* enemy) {
    Vector3 directionToPlayer = Vector3Subtract(player->position, enemy->position);
    float distance = Vector3Length(directionToPlayer);
    
    if (distance > enemy->visionDistance) return false;
    
    Vector3 enemyForward = (Vector3){
        -sinf(enemy->rotation),
        0.0f,
        -cosf(enemy->rotation)
    };
    
    Vector3 normalizedDir = Vector3Normalize(directionToPlayer);
    float dot = Vector3DotProduct(enemyForward, normalizedDir);
    float angle = acosf(dot) * RAD2DEG;
    
    return angle < enemy->visionAngle / 2.0f;
}

void DrawCone(Vector3 position, float rotation, float distance, float angle, Color color) {
    int segments = 16;
    float radius = tanf(angle * DEG2RAD / 2.0f) * distance;
    
    for (int i = 0; i < segments; i++) {
        float a1 = rotation + (angle * i / segments) - angle/2;
        float a2 = rotation + (angle * (i + 1) / segments) - angle/2;
        
        Vector3 v1 = {
            position.x + sinf(a1) * distance,
            position.y,
            position.z + cosf(a1) * distance
        };
        Vector3 v2 = {
            position.x + sinf(a2) * distance,
            position.y,
            position.z + cosf(a2) * distance
        };
        
        DrawTriangle3D(position, v1, v2, Fade(color, 0.3f));
    }
}
