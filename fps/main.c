#include "raylib.h"
#include "raymath.h"
#include <stdio.h>

// Player structure
typedef struct Player {
    Vector3 position;
    float yaw;      // Horizontal rotation (degrees)
    float pitch;    // Vertical rotation (degrees)
    float velocityY;// Vertical velocity for jumping/gravity
    bool isGrounded;// Track if player is on the ground
} Player;

// Enemy structure
typedef struct Enemy {
    Vector3 position;
    float health;
    bool active;
} Enemy;

// Bullet structure - replaces missile structure
typedef struct Bullet {
    Vector3 position;
    Vector3 velocity;
    bool active;
    float lifetime;
} Bullet;

// Cloud structure
typedef struct Cloud {
    Vector3 position;
    float size;
} Cloud;

#define MAX_BULLETS 200  // Maximum number of bullets that can be active at once
#define FIRE_RATE 0.1f   // Time between shots in seconds

int main(void) {

    // Window and camera setup
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Simple 3D FPS Game");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);  // Make window resizable
    MaximizeWindow();  // Start with maximized window

    // Player initialization
    Player player = { 
        .position = { 0.0f, 1.0f, 0.0f }, // Start at origin, raised slightly
        .yaw = 0.0f,
        .pitch = 0.0f,
        .velocityY = 0.0f,
        .isGrounded = true
    };

    // Camera setup (FPS style)
    Camera3D camera = { 0 };
    camera.position = player.position;
    camera.target = (Vector3){ 0.0f, 1.0f, 1.0f }; // Look forward initially
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 75.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Enemy setup (simple cube as enemy)
    Enemy enemy = {
        .position = { 5.0f, 0.5f, 5.0f },
        .health = 100.0f,
        .active = true
    };

    // Bullet array initialization
    Bullet bullets[MAX_BULLETS] = { 0 };
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = false;
    }
    
    float shootTimer = 0.0f;  // Timer to control fire rate

    // Cloud initialization (static positions for simplicity)
    Cloud clouds[] = {
        {{ -10.0f, 20.0f, -10.0f }, 2.0f},
        {{  10.0f, 22.0f, -15.0f }, 1.5f},
        {{   0.0f, 25.0f,  10.0f }, 2.5f},
        {{ -15.0f, 23.0f,   5.0f }, 1.8f},
        {{  12.0f, 21.0f,   8.0f }, 2.2f}
    };
    const int numClouds = sizeof(clouds) / sizeof(clouds[0]);

    // Disable cursor for FPS control
    DisableCursor();

    // Physics constants
    const float gravity = -20.0f;    // Gravity acceleration (negative Y)
    const float jumpForce = 8.0f;    // Initial upward velocity for jump
    const float groundHeight = 0.0f; // Ground level
    const float bulletSpeed = 30.0f; // Bullet travel speed (faster than the old missile)
    const float bulletLifetime = 1.5f; // Bullet duration in seconds
    
    // Bullet spread variables
    const float spreadFactor = 0.05f; // Controls how much bullets spread

    // Ammo counter
    int bulletsFired = 0;
    int bulletsHit = 0;

    // Gun sway variables
    float swayTime = 0.0f;           // Accumulates time for sway animation
    const float swaySpeed = 5.0f;    // Speed of sway oscillation
    const float swayAmplitude = 0.05f; // Magnitude of sway

    // Main game loop
    while (!WindowShouldClose()) {
        // --- Update ---
        float deltaTime = GetFrameTime();
        float speed = 5.0f * deltaTime;
        Vector3 move = { 0.0f, 0.0f, 0.0f };

        // Player movement (horizontal)
        if (IsKeyDown(KEY_W)) move.z += speed;
        if (IsKeyDown(KEY_S)) move.z -= speed;
        if (IsKeyDown(KEY_A)) move.x -= speed;
        if (IsKeyDown(KEY_D)) move.x += speed;

        // Mouse look (vertical inverted)
        player.yaw -= GetMouseDelta().x * 0.08f;
        player.pitch += GetMouseDelta().y * 0.08f;
        player.pitch = Clamp(player.pitch, -89.0f, 89.0f);

        // Calculate forward and right vectors
        Vector3 forward = { 
            sinf(player.yaw * DEG2RAD), 
            -tanf(player.pitch * DEG2RAD), 
            cosf(player.yaw * DEG2RAD) 
        };
        forward = Vector3Normalize(forward);
        Vector3 right = Vector3CrossProduct(forward, (Vector3){ 0.0f, 1.0f, 0.0f });
        right = Vector3Normalize(right);

        // Apply horizontal movement
        player.position = Vector3Add(player.position, Vector3Scale(forward, move.z));
        player.position = Vector3Add(player.position, Vector3Scale(right, move.x));

        // Jumping and gravity
        if (IsKeyPressed(KEY_SPACE) && player.isGrounded) {
            player.velocityY = jumpForce;
            player.isGrounded = false;
        }

        // Apply gravity
        player.velocityY += gravity * deltaTime;
        player.position.y += player.velocityY * deltaTime;

        // Check ground collision
        if (player.position.y <= groundHeight) {
            player.position.y = groundHeight;
            player.velocityY = 0.0f;
            player.isGrounded = true;
        }

        // Update camera
        camera.position = player.position;
        camera.position.y += 1.0f; // Offset camera to eye level
        camera.target = Vector3Add(camera.position, forward);

        // Update gun sway (only when moving)
        if (move.x != 0.0f || move.z != 0.0f) {
            swayTime += deltaTime * swaySpeed;
        } else {
            swayTime = 0.0f; // Reset sway when not moving
        }

        // Update shoot timer
        shootTimer -= deltaTime;

        // Shooting mechanics with rapid fire
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && shootTimer <= 0.0f) {
            // Find unused bullet
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) {
                    // Calculate gun position with sway
                    Vector3 gunBase = Vector3Add(camera.position, Vector3Scale(right, 0.3f));
                    gunBase = Vector3Subtract(gunBase, (Vector3){ 0.0f, 0.2f, 0.0f });
                    
                    float swayX = sinf(swayTime) * swayAmplitude;
                    float swayY = cosf(swayTime) * swayAmplitude;
                    gunBase = Vector3Add(gunBase, Vector3Scale(right, swayX));
                    gunBase = Vector3Add(gunBase, (Vector3){ 0.0f, swayY, 0.0f });
                    
                    Vector3 gunTip = Vector3Add(gunBase, Vector3Scale(forward, 0.6f));
                    
                    // Apply small random spread to bullet direction
                    Vector3 bulletDir = forward;
                    bulletDir.x += GetRandomValue(-100, 100) / 1000.0f * spreadFactor;
                    bulletDir.y += GetRandomValue(-100, 100) / 1000.0f * spreadFactor;
                    bulletDir.z += GetRandomValue(-100, 100) / 1000.0f * spreadFactor;
                    bulletDir = Vector3Normalize(bulletDir);
                    
                    // Set bullet properties
                    bullets[i].position = gunTip;
                    bullets[i].velocity = Vector3Scale(bulletDir, bulletSpeed);
                    bullets[i].active = true;
                    bullets[i].lifetime = bulletLifetime;
                    
                    // Reset timer and count bullet
                    shootTimer = FIRE_RATE;
                    bulletsFired++;
                    break;
                }
            }
        }

        // Update bullets
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bullets[i].active) {
                bullets[i].position = Vector3Add(bullets[i].position, Vector3Scale(bullets[i].velocity, deltaTime));
                bullets[i].lifetime -= deltaTime;

                // Check for enemy hit
                if (enemy.active) {
                    Vector3 enemyDir = Vector3Subtract(enemy.position, bullets[i].position);
                    float distance = Vector3Length(enemyDir);
                    if (distance < 0.5f) {
                        enemy.health -= 10.0f; // Less damage per bullet since we're firing more
                        bullets[i].active = false;
                        bulletsHit++;
                        
                        if (enemy.health <= 0.0f) {
                            enemy.active = false;
                            enemy.health = 0.0f;
                        }
                    }
                }

                // Deactivate bullet if lifetime expired
                if (bullets[i].lifetime <= 0.0f) {
                    bullets[i].active = false;
                }
            }
        }

        // --- Draw ---
        BeginDrawing();
        // Draw gradient sky (top blue, bottom lighter)
        ClearBackground(SKYBLUE); // Base sky color
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), SKYBLUE, (Color){ 135, 206, 235, 255 }); // Gradient to lighter blue

        BeginMode3D(camera);
            // Draw ground plane
            DrawPlane((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector2){ 20.0f, 20.0f }, GRAY);

            // Draw enemy if active
            if (enemy.active) {
                DrawCube(enemy.position, 1.0f, 1.0f, 1.0f, RED);
                DrawCubeWires(enemy.position, 1.0f, 1.0f, 1.0f, BLACK);
            }

            // Draw bullets
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (bullets[i].active) {
                    // Draw bullet as small sphere
                    DrawSphere(bullets[i].position, 0.05f, YELLOW);
                }
            }

            // Draw some scenery (cubes)
            DrawCube((Vector3){ -5.0f, 0.5f, -5.0f }, 1.0f, 1.0f, 1.0f, BLUE);
            DrawCube((Vector3){ 5.0f, 0.5f, -5.0f }, 1.0f, 1.0f, 1.0f, GREEN);

            // Draw clouds
            for (int i = 0; i < numClouds; i++) {
                DrawSphere(clouds[i].position, clouds[i].size, (Color){ 255, 255, 255, 180 }); // Semi-transparent white
            }

            // Draw gun (cylinder) pointing forward with sway
            Vector3 gunBase = Vector3Add(camera.position, Vector3Scale(right, 0.3f));
            gunBase = Vector3Subtract(gunBase, (Vector3){ 0.0f, 0.2f, 0.0f });
            Vector3 gunTip = Vector3Add(gunBase, Vector3Scale(forward, 0.6f));

            float swayX = sinf(swayTime) * swayAmplitude;
            float swayY = cosf(swayTime) * swayAmplitude;
            gunBase = Vector3Add(gunBase, Vector3Scale(right, swayX));
            gunBase = Vector3Add(gunBase, (Vector3){ 0.0f, swayY, 0.0f });
            gunTip = Vector3Add(gunTip, Vector3Scale(right, swayX));
            gunTip = Vector3Add(gunTip, (Vector3){ 0.0f, swayY, 0.0f });

            DrawCylinderEx(gunBase, gunTip, 0.05f, 0.05f, 16, DARKGRAY);
            DrawCylinderWiresEx(gunBase, gunTip, 0.05f, 0.05f, 16, BLACK);
        EndMode3D();

        // Draw crosshair
        DrawRectangle(GetScreenWidth()/2 - 2, GetScreenHeight()/2 - 2, 4, 4, BLACK);

        // Draw HUD
        DrawText(TextFormat("Enemy Health: %.0f", enemy.health), 10, 10, 20, DARKGRAY);
        DrawText(TextFormat("Bullets Fired: %d", bulletsFired), 10, 40, 20, DARKGRAY);
        DrawText(TextFormat("Hits: %d", bulletsHit), 10, 70, 20, DARKGRAY);
        if (bulletsFired > 0) {
            DrawText(TextFormat("Accuracy: %.1f%%", (float)bulletsHit/bulletsFired * 100.0f), 10, 100, 20, DARKGRAY);
        }
        DrawFPS(10, 130);

        EndDrawing();
    }

    // Cleanup
    CloseWindow();
    return 0;
}
