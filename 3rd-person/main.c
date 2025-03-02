#include "raylib.h"
#include "raymath.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 450
#define PLAYER_SPEED 5.0f
#define CAMERA_DISTANCE 10.0f
#define CAMERA_ROTATION_SPEED 2.0f
#define MAX_PITCH 1.4f
#define JUMP_FORCE 8.0f
#define GRAVITY 20.0f

int main(void) {
    // Initialize window
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Third Person - Raylib");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);  // Make window resizable
    MaximizeWindow();  // Start with maximized window

    // Define player properties
    Vector3 playerPos = { 0.0f, 1.0f, 0.0f }; // Player starts slightly above ground
    float playerHeight = 2.0f;
    float playerRadius = 0.5f;
    float verticalVelocity = 0.0f; // For jumping
    bool isGrounded = true;        // Track if player is on ground

    // Set up third-person camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ playerPos.x, playerPos.y + 5.0f, playerPos.z + CAMERA_DISTANCE };
    camera.target = playerPos;
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Camera angles for rotation (in radians)
    float cameraYaw = 0.0f;
    float cameraPitch = 0.5f;

    // Ground plane
    Vector3 groundPos = { 0.0f, 0.0f, 0.0f };
    float groundSize = 50.0f;

    // Game loop
    while (!WindowShouldClose()) {
        // Check if gamepad is available
        if (!IsGamepadAvailable(0)) {
            DrawText("No gamepad detected!", 10, 50, 20, RED);
        }

        // Get raw input from left stick (or keyboard fallback)
        Vector2 input = { 0.0f, 0.0f };
        if (IsGamepadAvailable(0)) {
            input.x = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            input.y = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        } else {
            if (IsKeyDown(KEY_A)) input.x = -1.0f;
            if (IsKeyDown(KEY_D)) input.x = 1.0f;
            if (IsKeyDown(KEY_W)) input.y = -1.0f;
            if (IsKeyDown(KEY_S)) input.y = 1.0f;
        }

        // Normalize input to prevent exceeding max speed
        float inputLength = Vector2Length(input);
        if (inputLength > 1.0f) input = Vector2Normalize(input);

        // Calculate camera's forward direction (ignore Y for movement)
        Vector3 cameraForward = Vector3Subtract(camera.target, camera.position);
        cameraForward.y = 0.0f;
        cameraForward = Vector3Normalize(cameraForward);

        // Calculate camera's right direction
        Vector3 cameraRight = Vector3CrossProduct(cameraForward, camera.up);
        cameraRight = Vector3Normalize(cameraRight);

        // Compute movement direction relative to camera
        Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
        moveDir = Vector3Add(moveDir, Vector3Scale(cameraForward, -input.y));
        moveDir = Vector3Add(moveDir, Vector3Scale(cameraRight, input.x));
        moveDir = Vector3Scale(Vector3Normalize(moveDir), PLAYER_SPEED * GetFrameTime() * inputLength);

        // Apply horizontal movement
        if (inputLength > 0.1f) {
            playerPos.x += moveDir.x;
            playerPos.z += moveDir.z;
        }

        // Handle jumping (A button on Xbox controller)
        if (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) && isGrounded) {
            verticalVelocity = JUMP_FORCE;
            isGrounded = false;
        }

        // Apply gravity and update vertical position
        verticalVelocity -= GRAVITY * GetFrameTime();
        playerPos.y += verticalVelocity * GetFrameTime();

        // Ground collision
        if (playerPos.y <= 1.0f) { // Assuming ground level is y = 0, player base at y = 1
            playerPos.y = 1.0f;
            verticalVelocity = 0.0f;
            isGrounded = true;
        }

        // Update camera rotation with right stick
        if (IsGamepadAvailable(0)) {
            float rightStickX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X);
            float rightStickY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
            cameraYaw -= rightStickX * CAMERA_ROTATION_SPEED * GetFrameTime();
            cameraPitch += rightStickY * CAMERA_ROTATION_SPEED * GetFrameTime();
        }

        // Clamp pitch
        if (cameraPitch < -MAX_PITCH) cameraPitch = -MAX_PITCH;
        if (cameraPitch > MAX_PITCH) cameraPitch = MAX_PITCH;

        // Update camera position
        Vector3 cameraOffset = { 0.0f, 0.0f, CAMERA_DISTANCE };
        cameraOffset.x = sinf(cameraYaw) * cosf(cameraPitch) * CAMERA_DISTANCE;
        cameraOffset.z = cosf(cameraYaw) * cosf(cameraPitch) * CAMERA_DISTANCE;
        cameraOffset.y = sinf(cameraPitch) * CAMERA_DISTANCE + 2.0f;
        camera.position = Vector3Add(playerPos, cameraOffset);
        camera.target = playerPos;

        // Begin drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Begin 3D mode
        BeginMode3D(camera);

        // Draw ground plane
        DrawPlane(groundPos, (Vector2){ groundSize, groundSize }, DARKGRAY);

        // Draw player (simple capsule)
        DrawCapsule((Vector3){ playerPos.x, playerPos.y - playerHeight / 2, playerPos.z },
                    (Vector3){ playerPos.x, playerPos.y + playerHeight / 2, playerPos.z },
                    playerRadius, 8, 8, GREEN);

        // Draw environment object (box)
        DrawCube((Vector3){ 5.0f, 0.5f, 5.0f }, 1.0f, 1.0f, 1.0f, BROWN);

        EndMode3D();

        // Draw UI/debug info
        DrawText("Left Stick: Move | Right Stick: Camera | A: Jump", 10, 10, 20, BLACK);
        DrawFPS(10, 30);
        if (!IsGamepadAvailable(0)) DrawText("No gamepad detected! Use WASD.", 10, 50, 20, RED);

        EndDrawing();
    }

    // Cleanup
    CloseWindow();
    return 0;
}
