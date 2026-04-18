#include "raylib.h"
#include "raymath.h" // Add this line for vector math functions
#include <stdbool.h>

// Player and Enemy structure
typedef struct {
    Vector3 position;
    int health;
    bool isTurn;
} Character;

// Game state
typedef struct {
    Character player;
    Character enemy;
    bool playerTurn;
} GameState;

int main(void) {
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Baldur's Gate 3 Clone - Raylib");

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    GameState game = { 0 };
    game.player = (Character){ (Vector3){ 0.0f, 0.5f, 0.0f }, 100, true };
    game.enemy = (Character){ (Vector3){ 5.0f, 0.5f, 5.0f }, 50, false };
    game.playerTurn = true;

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        if (game.playerTurn) {
            if (IsKeyDown(KEY_W)) game.player.position.z -= 0.1f;
            if (IsKeyDown(KEY_S)) game.player.position.z += 0.1f;
            if (IsKeyDown(KEY_A)) game.player.position.x -= 0.1f;
            if (IsKeyDown(KEY_D)) game.player.position.x += 0.1f;

            if (IsKeyPressed(KEY_SPACE)) {
                float distance = Vector3Distance(game.player.position, game.enemy.position);
                if (distance < 2.0f) {
                    game.enemy.health -= 10;
                    game.playerTurn = false;
                }
            }
        } else {
            Vector3 direction = Vector3Subtract(game.player.position, game.enemy.position);
            float distance = Vector3Length(direction);
            if (distance > 1.5f) {
                direction = Vector3Normalize(direction);
                game.enemy.position.x += direction.x * 0.05f;
                game.enemy.position.z += direction.z * 0.05f;
            } else {
                game.player.health -= 5;
            }
            game.playerTurn = true;
        }

        camera.target = game.player.position;

        BeginDrawing();
        ClearBackground(RAYWHITE);

        BeginMode3D(camera);
            DrawPlane((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector2){ 20.0f, 20.0f }, DARKGRAY);
            DrawCube(game.player.position, 1.0f, 1.0f, 1.0f, BLUE);
            DrawCube(game.enemy.position, 1.0f, 1.0f, 1.0f, RED);
            DrawGrid(20, 1.0f);
        EndMode3D();

        DrawText(TextFormat("Player HP: %d", game.player.health), 10, 10, 20, BLACK);
        DrawText(TextFormat("Enemy HP: %d", game.enemy.health), 10, 40, 20, BLACK);
        DrawText(game.playerTurn ? "Player's Turn" : "Enemy's Turn", 10, 70, 20, BLACK);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
