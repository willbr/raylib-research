#include "raylib.h"
#include "raymath.h"

// Define constants
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define FIELD_WIDTH 80.0f
#define FIELD_DEPTH 40.0f
#define PLAYER_SIZE 2.0f
#define BALL_RADIUS 1.0f
#define PLAYER_SPEED 5.0f
#define AI_SPEED 4.0f
#define BASE_KICK_SPEED 10.0f
#define MAX_KICK_SPEED 20.0f
#define KICK_CHARGE_RATE 10.0f
#define GRAVITY 20.0f
#define BOUNCE_DAMPING 0.8f
#define FRICTION 0.99f
#define GOAL_WIDTH 10.0f
#define GOAL_HEIGHT 5.0f
#define GOAL_DEPTH 2.0f
#define WIN_SCORE 5

// Define game states
typedef enum { START, PLAYING, GAME_OVER } GameState;

// Define player structure
typedef struct {
    Vector3 position;
    float kickCharge;
} Player;

// Define AI structure
typedef struct {
    Vector3 position;
} AI;

// Define ball structure
typedef struct {
    Vector3 position;
    Vector3 velocity;
} Ball;

int main(void) {
    // Initialize window
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "FIFA 98 Clone 3D");
    SetTargetFPS(60);

    // Set up camera
    Camera3D camera = {0};
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 45;
    camera.projection = CAMERA_PERSPECTIVE;

    // Initialize game objects
    Player player = { .position = {0, PLAYER_SIZE / 2, 0}, .kickCharge = 0.0f };
    AI ai = { .position = {0, PLAYER_SIZE / 2, 10} };
    Ball ball = { .position = {0, BALL_RADIUS, 0}, .velocity = {0, 0, 0} };

    // Define field dimensions (moved to top scope for drawing)
    float halfFieldWidth = FIELD_WIDTH / 2;
    float halfFieldDepth = FIELD_DEPTH / 2;

    // Define goals
    float halfGoalWidth = GOAL_WIDTH / 2;
    BoundingBox playerGoal = { // Opponent scores here
        (Vector3){-halfGoalWidth, 0, -halfFieldDepth - GOAL_DEPTH},
        (Vector3){halfGoalWidth, GOAL_HEIGHT, -halfFieldDepth}
    };
    BoundingBox opponentGoal = { // Player scores here
        (Vector3){-halfGoalWidth, 0, halfFieldDepth},
        (Vector3){halfGoalWidth, GOAL_HEIGHT, halfFieldDepth + GOAL_DEPTH}
    };

    // Initialize game state and scores
    GameState state = START;
    int playerScore = 0;
    int opponentScore = 0;

    // Game loop
    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();

        // Update based on game state
        switch (state) {
            case START:
                if (IsKeyPressed(KEY_SPACE)) {
                    state = PLAYING;
                }
                break;

            case PLAYING:
                // Update player movement
                if (IsKeyDown(KEY_RIGHT)) player.position.x += PLAYER_SPEED * deltaTime;
                if (IsKeyDown(KEY_LEFT)) player.position.x -= PLAYER_SPEED * deltaTime;
                if (IsKeyDown(KEY_UP)) player.position.z += PLAYER_SPEED * deltaTime;
                if (IsKeyDown(KEY_DOWN)) player.position.z -= PLAYER_SPEED * deltaTime;

                // Clamp player position
                float playerHalfSize = PLAYER_SIZE / 2;
                if (player.position.x < -halfFieldWidth + playerHalfSize) player.position.x = -halfFieldWidth + playerHalfSize;
                else if (player.position.x > halfFieldWidth - playerHalfSize) player.position.x = halfFieldWidth - playerHalfSize;
                if (player.position.z < -halfFieldDepth + playerHalfSize) player.position.z = -halfFieldDepth + playerHalfSize;
                else if (player.position.z > halfFieldDepth - playerHalfSize) player.position.z = halfFieldDepth - playerHalfSize;

                // Charge kick power
                if (IsKeyDown(KEY_SPACE)) {
                    player.kickCharge += KICK_CHARGE_RATE * deltaTime;
                    if (player.kickCharge > 1.0f) player.kickCharge = 1.0f;
                } else {
                    player.kickCharge = 0.0f;
                }

                // Update AI movement
                Vector3 aiToBall = Vector3Subtract(ball.position, ai.position);
                Vector3 aiDirection = Vector3Normalize(aiToBall);
                ai.position.x += aiDirection.x * AI_SPEED * deltaTime;
                ai.position.z += aiDirection.z * AI_SPEED * deltaTime;

                // Clamp AI position
                if (ai.position.x < -halfFieldWidth + playerHalfSize) ai.position.x = -halfFieldWidth + playerHalfSize;
                else if (ai.position.x > halfFieldWidth - playerHalfSize) ai.position.x = halfFieldWidth - playerHalfSize;
                if (ai.position.z < -halfFieldDepth + playerHalfSize) ai.position.z = -halfFieldDepth + playerHalfSize;
                else if (ai.position.z > halfFieldDepth - playerHalfSize) ai.position.z = halfFieldDepth - playerHalfSize;

                // Update ball physics
                ball.velocity.y -= GRAVITY * deltaTime;
                ball.position = Vector3Add(ball.position, Vector3Scale(ball.velocity, deltaTime));

                // Ground collision
                if (ball.position.y < BALL_RADIUS) {
                    ball.position.y = BALL_RADIUS;
                    ball.velocity.y = -ball.velocity.y * BOUNCE_DAMPING;
                    ball.velocity.x *= FRICTION;
                    ball.velocity.z *= FRICTION;
                }

                // Wall collisions
                if (ball.position.x < -halfFieldWidth) {
                    ball.position.x = -halfFieldWidth;
                    ball.velocity.x = -ball.velocity.x;
                } else if (ball.position.x > halfFieldWidth) {
                    ball.position.x = halfFieldWidth;
                    ball.velocity.x = -ball.velocity.x;
                }
                if (ball.position.z < -halfFieldDepth) {
                    ball.position.z = -halfFieldDepth;
                    ball.velocity.z = -ball.velocity.z;
                } else if (ball.position.z > halfFieldDepth) {
                    ball.position.z = halfFieldDepth;
                    ball.velocity.z = -ball.velocity.z;
                }

                // Player-ball collision
                BoundingBox playerBox = {
                    (Vector3){player.position.x - playerHalfSize, player.position.y - playerHalfSize, player.position.z - playerHalfSize},
                    (Vector3){player.position.x + playerHalfSize, player.position.y + playerHalfSize, player.position.z + playerHalfSize}
                };
                if (CheckCollisionBoxSphere(playerBox, ball.position, BALL_RADIUS)) {
                    Vector3 direction = Vector3Normalize(Vector3Subtract(ball.position, player.position));
                    float kickStrength = BASE_KICK_SPEED + (MAX_KICK_SPEED - BASE_KICK_SPEED) * player.kickCharge;
                    ball.velocity = Vector3Scale(direction, kickStrength);
                    ball.velocity.y += 5 + 5 * player.kickCharge;
                    player.kickCharge = 0.0f;
                }

                // AI-ball collision
                BoundingBox aiBox = {
                    (Vector3){ai.position.x - playerHalfSize, ai.position.y - playerHalfSize, ai.position.z - playerHalfSize},
                    (Vector3){ai.position.x + playerHalfSize, ai.position.y + playerHalfSize, ai.position.z + playerHalfSize}
                };
                if (CheckCollisionBoxSphere(aiBox, ball.position, BALL_RADIUS)) {
                    Vector3 direction = Vector3Normalize(Vector3Subtract(ball.position, ai.position));
                    ball.velocity = Vector3Scale(direction, BASE_KICK_SPEED);
                    ball.velocity.y += 5;
                }

                // Check for goals (fixed function name)
                bool goalScored = false;
                if (CheckCollisionBoxSphere(opponentGoal, ball.position, BALL_RADIUS)) { // Player scores
                    playerScore++;
                    goalScored = true;
                } else if (CheckCollisionBoxSphere(playerGoal, ball.position, BALL_RADIUS)) { // Opponent scores
                    opponentScore++;
                    goalScored = true;
                }
                if (goalScored) {
                    ball.position = (Vector3){0, BALL_RADIUS, 0};
                    ball.velocity = (Vector3){0, 0, 0};
                }

                // Check win condition
                if (playerScore >= WIN_SCORE || opponentScore >= WIN_SCORE) {
                    state = GAME_OVER;
                }

                // Update camera to follow the ball
                camera.position = Vector3Add(ball.position, (Vector3){0, 15, -20});
                camera.target = ball.position;
                break;

            case GAME_OVER:
                if (IsKeyPressed(KEY_SPACE)) {
                    player.position = (Vector3){0, PLAYER_SIZE / 2, 0};
                    ai.position = (Vector3){0, PLAYER_SIZE / 2, 10};
                    ball.position = (Vector3){0, BALL_RADIUS, 0};
                    ball.velocity = (Vector3){0, 0, 0};
                    playerScore = 0;
                    opponentScore = 0;
                    state = START;
                }
                break;
        }

        // Drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);
        if (state != START) {
            BeginMode3D(camera);

            // Draw field
            DrawPlane((Vector3){0, 0, 0}, (Vector2){FIELD_WIDTH, FIELD_DEPTH}, GREEN);

            // Draw field lines (using pre-declared halfFieldWidth and halfFieldDepth)
            DrawLine3D((Vector3){-halfFieldWidth, 0, -halfFieldDepth}, (Vector3){halfFieldWidth, 0, -halfFieldDepth}, WHITE);
            DrawLine3D((Vector3){halfFieldWidth, 0, -halfFieldDepth}, (Vector3){halfFieldWidth, 0, halfFieldDepth}, WHITE);
            DrawLine3D((Vector3){halfFieldWidth, 0, halfFieldDepth}, (Vector3){-halfFieldWidth, 0, halfFieldDepth}, WHITE);
            DrawLine3D((Vector3){-halfFieldWidth, 0, halfFieldDepth}, (Vector3){-halfFieldWidth, 0, -halfFieldDepth}, WHITE);
            DrawLine3D((Vector3){-halfFieldWidth, 0, 0}, (Vector3){halfFieldWidth, 0, 0}, WHITE);

            // Draw goals
            DrawCubeWires((Vector3){0, GOAL_HEIGHT / 2, halfFieldDepth + GOAL_DEPTH / 2}, 
                          GOAL_WIDTH, GOAL_HEIGHT, GOAL_DEPTH, PURPLE);
            DrawCubeWires((Vector3){0, GOAL_HEIGHT / 2, -halfFieldDepth - GOAL_DEPTH / 2}, 
                          GOAL_WIDTH, GOAL_HEIGHT, GOAL_DEPTH, PURPLE);

            // Draw player
            DrawCube(player.position, PLAYER_SIZE, PLAYER_SIZE, PLAYER_SIZE, BLUE);

            // Draw AI
            DrawCube(ai.position, PLAYER_SIZE, PLAYER_SIZE, PLAYER_SIZE, YELLOW);

            // Draw ball
            DrawSphere(ball.position, BALL_RADIUS, RED);

            EndMode3D();
        }

        // Draw UI
        switch (state) {
            case START:
                DrawText("Press SPACE to Start", SCREEN_WIDTH/2 - 100, SCREEN_HEIGHT/2, 20, BLACK);
                break;
            case PLAYING:
                DrawText(TextFormat("Player: %d", playerScore), 10, 10, 20, BLACK);
                DrawText(TextFormat("Opponent: %d", opponentScore), 10, 40, 20, BLACK);
                DrawRectangle(10, 70, 100, 10, LIGHTGRAY);
                DrawRectangle(10, 70, 100 * player.kickCharge, 10, RED);
                break;
            case GAME_OVER:
                DrawText(playerScore >= WIN_SCORE ? "Player Wins!" : "Opponent Wins!", 
                         SCREEN_WIDTH/2 - 70, SCREEN_HEIGHT/2 - 20, 30, GREEN);
                DrawText("Press SPACE to Restart", SCREEN_WIDTH/2 - 100, SCREEN_HEIGHT/2 + 20, 20, BLACK);
                DrawText(TextFormat("Final Score - Player: %d  Opponent: %d", playerScore, opponentScore), 
                         10, 10, 20, BLACK);
                break;
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
