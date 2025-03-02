#include "raylib.h"
#include "raymath.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 450
#define PLAYER_SPEED 5.0f
#define CAMERA_DISTANCE 3.0f
#define CAMERA_DISTANCE_AIM 2.0f  // Closer camera when aiming
#define CAMERA_ROTATION_SPEED 2.0f
#define SHOULDER_OFFSET 3.0f      // Horizontal offset for over-the-shoulder view
#define MAX_PITCH 1.4f
#define JUMP_FORCE 8.0f
#define GRAVITY 20.0f
#define MAX_BULLETS 30
#define BULLET_SPEED 30.0f
#define BULLET_RADIUS 0.2f
#define BULLET_LIFETIME 3.0f  // Seconds before bullet disappears

// Define bullet structure
typedef struct {
    Vector3 position;
    Vector3 direction;
    float lifetime;
    bool active;
} Bullet;

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
    bool isAiming = false;         // Track if player is aiming
    float shootCooldown = 0.0f;    // Time until next shot is allowed

    // Initialize bullets
    Bullet bullets[MAX_BULLETS] = { 0 };
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = false;
    }

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
    float currentCameraDistance = CAMERA_DISTANCE;
    float currentShoulderOffset = SHOULDER_OFFSET; // Current shoulder offset (will change when aiming)

    // Ground plane
    Vector3 groundPos = { 0.0f, 0.0f, 0.0f };
    float groundSize = 50.0f;

    // Game loop
    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        
        // Check if gamepad is available
        bool gamepadConnected = IsGamepadAvailable(0);
        if (!gamepadConnected) {
            DrawText("No gamepad detected!", 10, 50, 20, RED);
        }

        // Get input from left stick AND keyboard (allow simultaneous use)
        Vector2 input = { 0.0f, 0.0f };
        
        // Get gamepad input if available
        if (gamepadConnected) {
            input.x += GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            input.y += GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        }
        
        // Always check keyboard input (regardless of gamepad connection)
        if (IsKeyDown(KEY_A)) input.x += -1.0f;
        if (IsKeyDown(KEY_D)) input.x += 1.0f;
        if (IsKeyDown(KEY_W)) input.y += -1.0f;
        if (IsKeyDown(KEY_S)) input.y += 1.0f;

        // Check aiming (left trigger or keyboard)
        float leftTrigger = gamepadConnected ? GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER) : 0.0f;
        bool previousAiming = isAiming;
        isAiming = leftTrigger > 0.5f || IsKeyDown(KEY_RIGHT_ALT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);  // Alt or right mouse as keyboard alternative
        
        // Shooting (right trigger or keyboard/mouse)
        float rightTrigger = gamepadConnected ? GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) : 0.0f;
        bool isShooting = ((rightTrigger > 0.5f || IsKeyDown(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) && shootCooldown <= 0.0f);
        
        // Update shooting cooldown
        if (shootCooldown > 0.0f) {
            shootCooldown -= deltaTime;
        }
        
        // Handle shooting
        if (isShooting && isAiming) {
            // Find an inactive bullet
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) {
                    // For shooting, use the direction player is facing
                    // This ensures bullets go toward the center of the screen (the crosshair)
                    Vector3 shootDirection;
                    if (isAiming) {
                        // When aiming, shoot where the camera is pointing (center of screen)
                        shootDirection = Vector3Subtract(camera.target, camera.position);
                    } else {
                        // When not aiming, shoot forward relative to player orientation
                        shootDirection = (Vector3){
                            sinf(cameraYaw),
                            0,  // No vertical component when not aiming
                            cosf(cameraYaw)
                        };
                    }
                    bullets[i].direction = Vector3Normalize(shootDirection);
                    
                    // Set bullet position at player's head level (slightly in front)
                    bullets[i].position = Vector3Add(playerPos, 
                                                    (Vector3){ 
                                                        bullets[i].direction.x * 1.0f, 
                                                        playerHeight * 0.75f, 
                                                        bullets[i].direction.z * 1.0f 
                                                    });
                    
                    bullets[i].lifetime = BULLET_LIFETIME;
                    bullets[i].active = true;
                    shootCooldown = 0.2f;  // 0.2 seconds between shots
                    break;
                }
            }
        }
        
        // Smoothly adjust camera distance based on aiming
        float targetDistance = isAiming ? CAMERA_DISTANCE_AIM : CAMERA_DISTANCE;
        currentCameraDistance = Lerp(currentCameraDistance, targetDistance, 15.0f * deltaTime);
        
        // Adjust shoulder offset based on aiming - reduce offset when aiming for better accuracy
        float targetShoulderOffset = isAiming ? SHOULDER_OFFSET * 0.5f : SHOULDER_OFFSET;
        currentShoulderOffset = Lerp(currentShoulderOffset, targetShoulderOffset, 15.0f * deltaTime);
        
        // If we just started aiming, immediately snap to a better position
        if (isAiming && !previousAiming) {
            currentCameraDistance = CAMERA_DISTANCE_AIM; // Skip the lerp for immediate transition
            currentShoulderOffset = SHOULDER_OFFSET * 0.5f;
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
        
        // Apply movement speed multiplier if aiming (slower when aiming)
        float speedMultiplier = isAiming ? 0.5f : 1.0f;
        moveDir = Vector3Scale(Vector3Normalize(moveDir), PLAYER_SPEED * deltaTime * inputLength * speedMultiplier);

        // Apply horizontal movement
        if (inputLength > 0.1f) {
            playerPos.x += moveDir.x;
            playerPos.z += moveDir.z;
        }

        // Handle jumping (A button on Xbox controller or keyboard)
        if (((gamepadConnected && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) || 
             IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_C)) && isGrounded) {
            verticalVelocity = JUMP_FORCE;
            isGrounded = false;
        }

        // Apply gravity and update vertical position
        verticalVelocity -= GRAVITY * deltaTime;
        playerPos.y += verticalVelocity * deltaTime;

        // Ground collision
        if (playerPos.y <= 1.0f) { // Assuming ground level is y = 0, player base at y = 1
            playerPos.y = 1.0f;
            verticalVelocity = 0.0f;
            isGrounded = true;
        }

        // Update camera rotation with both right stick AND mouse (simultaneous use)
        float cameraSensitivityMultiplier = isAiming ? 0.5f : 1.0f;
        
        // Process gamepad right stick input if connected
        if (gamepadConnected) {
            float rightStickX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X);
            float rightStickY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
            
            // Apply gamepad camera control (if stick is being moved)
            if (fabsf(rightStickX) > 0.1f || fabsf(rightStickY) > 0.1f) {
                cameraYaw -= rightStickX * CAMERA_ROTATION_SPEED * deltaTime;
                cameraPitch += rightStickY * CAMERA_ROTATION_SPEED * deltaTime * cameraSensitivityMultiplier;
            }
        }
        
        // Process mouse input (always check, regardless of gamepad)
        Vector2 mouseDelta = GetMouseDelta();
        // Apply mouse camera control if mouse is moving or right button is held
        if ((fabsf(mouseDelta.x) > 0.0f || fabsf(mouseDelta.y) > 0.0f) && 
            (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))) {
            
            float mouseXSensitivity = 0.003f;
            float mouseYSensitivity = 0.003f;
            
            cameraYaw   -= mouseDelta.x * mouseXSensitivity;
            cameraPitch += mouseDelta.y * mouseYSensitivity;
        }

        // Clamp pitch
        if (cameraPitch < -MAX_PITCH) cameraPitch = -MAX_PITCH;
        if (cameraPitch > MAX_PITCH) cameraPitch = MAX_PITCH;

        // Update camera position for over-the-shoulder view
        Vector3 cameraOffset = { 0.0f, 0.0f, currentCameraDistance };
        
        // Calculate base offset (similar to before)
        cameraOffset.x = sinf(cameraYaw) * cosf(cameraPitch) * currentCameraDistance;
        cameraOffset.z = cosf(cameraYaw) * cosf(cameraPitch) * currentCameraDistance;
        cameraOffset.y = sinf(cameraPitch) * currentCameraDistance + 2.0f;
        
        // Calculate right vector for shoulder offset
        Vector3 right = { 
            sinf(cameraYaw + PI/2), 
            0.0f, 
            cosf(cameraYaw + PI/2) 
        };
        
        // Apply shoulder offset (right side by default)
        Vector3 shoulderVector = Vector3Scale(right, currentShoulderOffset);
        cameraOffset = Vector3Add(cameraOffset, shoulderVector);
        
        // Update camera position and target
        camera.position = Vector3Add(playerPos, cameraOffset);
        
        // Adjust target position slightly to account for the offset view
        Vector3 targetOffset = Vector3Scale(shoulderVector, 0.2f); // Subtle target adjustment
        camera.target = Vector3Add(playerPos, targetOffset);

        // Update bullets
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bullets[i].active) {
                // Update position
                Vector3 movement = Vector3Scale(bullets[i].direction, BULLET_SPEED * deltaTime);
                bullets[i].position = Vector3Add(bullets[i].position, movement);
                
                // Update lifetime
                bullets[i].lifetime -= deltaTime;
                
                // Deactivate bullet if lifetime expired
                if (bullets[i].lifetime <= 0.0f) {
                    bullets[i].active = false;
                }
                
                // Simple ground collision
                if (bullets[i].position.y < 0.0f) {
                    bullets[i].active = false;
                }
            }
        }

        // Begin drawing
        BeginDrawing();
        ClearBackground(SKYBLUE);  // Changed to SKYBLUE for better visibility

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
        
        // Draw active bullets
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bullets[i].active) {
                DrawSphere(bullets[i].position, BULLET_RADIUS, YELLOW);
                // Optional: Draw trail
                Vector3 trailEnd = Vector3Subtract(bullets[i].position, 
                                                  Vector3Scale(bullets[i].direction, BULLET_SPEED * 0.05f));
                DrawLine3D(bullets[i].position, trailEnd, RED);
            }
        }
        
        // Draw aiming reticle when aiming
        if (isAiming) {
            // Get a point in front of the player for aiming
            Vector3 aimPoint = Vector3Add(camera.position, 
                                         Vector3Scale(Vector3Normalize(Vector3Subtract(camera.target, camera.position)), 100.0f));
            
            // Draw a small sphere at the aim point for debug
            DrawSphere(aimPoint, 0.2f, RED);  // Uncomment for debugging aim
        }

        EndMode3D();

        // Draw UI/debug info
        DrawText("Left Stick: Move | Right Stick: Camera | A: Jump", 10, 10, 20, BLACK);
        DrawText("Left Trigger: Aim | Right Trigger: Shoot", 10, 35, 20, BLACK);
        DrawFPS(10, 60);
        
        // Draw a simple crosshair in the exact middle of the screen
        int centerX = GetScreenWidth() / 2;
        int centerY = GetScreenHeight() / 2;
        int crosshairSize = 10;
        
        // Draw crosshair lines
        DrawLine(centerX - crosshairSize, centerY, centerX + crosshairSize, centerY, RED);
        DrawLine(centerX, centerY - crosshairSize, centerX, centerY + crosshairSize, RED);
        
        if (gamepadConnected) {
            DrawText("Gamepad Connected", 10, 135, 20, GREEN);
        }

        EndDrawing();
    }

    // Cleanup
    CloseWindow();
    return 0;
}
