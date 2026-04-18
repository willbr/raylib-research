#include "raylib.h"
#include "raymath.h"
#include <stdlib.h>  // for rand()
#include <math.h>

// Structure definitions
typedef struct {
    Vector3 position;
    Vector3 velocity;
    float rotation;
    bool hasball;
} Player;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    bool inPlay;
} Ball;

typedef struct {
    Vector3 position;
    Vector2 dimensions;  // width and length of the field
} Field;

// Global game state
typedef struct {
    Player player;
    Player opponents[10];
    Ball ball;
    Field field;
    Camera3D camera;
    int score_home;
    int score_away;
} GameState;

// Constants
#define PLAYER_SPEED 5.0f
#define BALL_SPEED 7.0f
#define FIELD_WIDTH 105.0f
#define FIELD_LENGTH 68.0f
#define GOAL_WIDTH 7.32f
#define GOAL_HEIGHT 2.44f

// Function prototypes
void InitGame(GameState *game);
void UpdateGame(GameState *game);
void DrawGame(GameState *game);
void UpdatePlayer(Player *player, Ball *ball, Field *field);
void UpdateBall(Ball *ball, Field *field);
bool CheckCollision(Vector3 pos1, Vector3 pos2, float radius);
void UpdateGameCamera(Camera3D *camera, Player *player, Ball *ball);  // Renamed from UpdateCamera

int main(void) {
    // Initialize window
    InitWindow(1280, 720, "FIFA 98 Clone");
    SetTargetFPS(60);

    // Initialize game state
    GameState game;
    InitGame(&game);

    // Main game loop
    while (!WindowShouldClose()) {
        UpdateGame(&game);
        
        BeginDrawing();
            ClearBackground(RAYWHITE);
            BeginMode3D(game.camera);
                DrawGame(&game);
            EndMode3D();
            
            // Draw UI
            DrawText(TextFormat("Home: %d - Away: %d", game.score_home, game.score_away), 10, 10, 20, BLACK);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

void InitGame(GameState *game) {
    // Initialize field
    game->field.position = (Vector3){ 0.0f, 0.0f, 0.0f };
    game->field.dimensions = (Vector2){ FIELD_WIDTH, FIELD_LENGTH };
    
    // Initialize player
    game->player.position = (Vector3){ 0.0f, 0.0f, 20.0f };
    game->player.velocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    game->player.rotation = 0.0f;
    game->player.hasball = false;
    
    // Initialize ball
    game->ball.position = (Vector3){ 0.0f, 0.5f, 0.0f };
    game->ball.velocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    game->ball.inPlay = true;
    
    // Initialize camera
    game->camera.position = (Vector3){ 0.0f, 30.0f, 40.0f };
    game->camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    game->camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    game->camera.fovy = 45.0f;
    game->camera.projection = CAMERA_PERSPECTIVE;
    
    // Initialize scores
    game->score_home = 0;
    game->score_away = 0;
    
    // Initialize opponents (basic positions)
    for (int i = 0; i < 10; i++) {
        game->opponents[i].position = (Vector3){
            (float)((rand() % (int)FIELD_WIDTH) - FIELD_WIDTH/2),
            0.0f,
            (float)((rand() % (int)FIELD_LENGTH) - FIELD_LENGTH/2)
        };
        game->opponents[i].velocity = (Vector3){ 0.0f, 0.0f, 0.0f };
        game->opponents[i].hasball = false;
    }
}

void UpdateGame(GameState *game) {
    UpdatePlayer(&game->player, &game->ball, &game->field);
    UpdateBall(&game->ball, &game->field);
    UpdateGameCamera(&game->camera, &game->player, &game->ball);  // Updated function name
    
    // Check for goals
    if (game->ball.position.x > FIELD_WIDTH/2 - GOAL_WIDTH/2 &&
        game->ball.position.x < FIELD_WIDTH/2 + GOAL_WIDTH/2 &&
        game->ball.position.z > -GOAL_WIDTH/2 &&
        game->ball.position.z < GOAL_WIDTH/2) {
        game->score_home++;
        game->ball.position = (Vector3){ 0.0f, 0.5f, 0.0f };
        game->ball.velocity = (Vector3){ 0.0f, 0.0f, 0.0f };
    }
}

void UpdatePlayer(Player *player, Ball *ball, Field *field) {
    // Player movement
    if (IsKeyDown(KEY_W)) player->velocity.z = -PLAYER_SPEED;
    else if (IsKeyDown(KEY_S)) player->velocity.z = PLAYER_SPEED;
    else player->velocity.z = 0.0f;
    
    if (IsKeyDown(KEY_A)) player->velocity.x = -PLAYER_SPEED;
    else if (IsKeyDown(KEY_D)) player->velocity.x = PLAYER_SPEED;
    else player->velocity.x = 0.0f;
    
    // Update player position
    player->position.x += player->velocity.x * GetFrameTime();
    player->position.z += player->velocity.z * GetFrameTime();
    
    // Keep player within field bounds
    player->position.x = Clamp(player->position.x, -field->dimensions.x/2, field->dimensions.x/2);
    player->position.z = Clamp(player->position.z, -field->dimensions.y/2, field->dimensions.y/2);
    
    // Ball interaction
    if (IsKeyPressed(KEY_SPACE) && CheckCollision(player->position, ball->position, 2.0f)) {
        Vector3 kickDir = Vector3Normalize(Vector3Subtract(
            (Vector3){GetMouseX() - GetScreenWidth()/2, 0, GetMouseY() - GetScreenHeight()/2},
            player->position
        ));
        ball->velocity = Vector3Scale(kickDir, BALL_SPEED);
    }
}

void UpdateBall(Ball *ball, Field *field) {
    // Apply ball physics
    ball->position.x += ball->velocity.x * GetFrameTime();
    ball->position.z += ball->velocity.z * GetFrameTime();
    
    // Ball friction
    ball->velocity = Vector3Scale(ball->velocity, 0.98f);
    
    // Keep ball in bounds
    if (ball->position.x < -field->dimensions.x/2 || ball->position.x > field->dimensions.x/2) {
        ball->velocity.x *= -0.8f;
        ball->position.x = Clamp(ball->position.x, -field->dimensions.x/2, field->dimensions.x/2);
    }
    if (ball->position.z < -field->dimensions.y/2 || ball->position.z > field->dimensions.y/2) {
        ball->velocity.z *= -0.8f;
        ball->position.z = Clamp(ball->position.z, -field->dimensions.y/2, field->dimensions.y/2);
    }
}

void UpdateGameCamera(Camera3D *camera, Player *player, Ball *ball) {  // Renamed from UpdateCamera
    // Camera follows player and ball
    Vector3 targetPos = Vector3Add(
        Vector3Scale(player->position, 0.7f),
        Vector3Scale(ball->position, 0.3f)
    );
    camera->target = Vector3Lerp(camera->target, targetPos, 0.1f);
    
    // Adjust camera position based on target
    camera->position.x = camera->target.x;
    camera->position.z = camera->target.z + 40.0f;
}

bool CheckCollision(Vector3 pos1, Vector3 pos2, float radius) {
    return Vector3Distance(pos1, pos2) < radius;
}

void DrawGame(GameState *game) {
    // Draw field
    DrawPlane((Vector3){0.0f, 0.0f, 0.0f}, (Vector2){FIELD_WIDTH, FIELD_LENGTH}, GREEN);
    
    // Draw field lines
    DrawLine3D(
        (Vector3){-FIELD_WIDTH/2, 0.1f, 0.0f},
        (Vector3){FIELD_WIDTH/2, 0.1f, 0.0f},
        WHITE
    );
    DrawCircle3D((Vector3){0.0f, 0.1f, 0.0f}, 9.15f, (Vector3){1.0f, 0.0f, 0.0f}, 90.0f, WHITE);
    
    // Draw goals
    DrawCube(
        (Vector3){FIELD_WIDTH/2, GOAL_HEIGHT/2, 0.0f},
        GOAL_WIDTH, GOAL_HEIGHT, 0.5f,
        WHITE
    );
    DrawCube(
        (Vector3){-FIELD_WIDTH/2, GOAL_HEIGHT/2, 0.0f},
        GOAL_WIDTH, GOAL_HEIGHT, 0.5f,
        WHITE
    );
    
    // Draw player
    DrawCube(game->player.position, 1.0f, 2.0f, 1.0f, BLUE);
    
    // Draw opponents
    for (int i = 0; i < 10; i++) {
        DrawCube(game->opponents[i].position, 1.0f, 2.0f, 1.0f, RED);
    }
    
    // Draw ball
    DrawSphere(game->ball.position, 0.5f, ORANGE);
}
