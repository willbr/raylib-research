#include "raylib.h"
#include "raymath.h"

int main(void) {
    // Initialize window
    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "Sega Rally Clone");
    SetTargetFPS(60);

    // Camera setup
    Camera3D camera = { 0 };
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Car variables
    Vector3 carPosition = {0.0f, 0.5f, 0.0f};
    float carRotation = 0.0f;
    float carSpeed = 0.0f;
    Vector3 carVelocity = {0.0f, 0.0f, 0.0f};
    const float maxSpeed = 20.0f;
    const float acceleration = 5.0f;
    const float deceleration = 3.0f;
    const float turnSpeed = 90.0f;

    // Create a simple cube model for the car
    Mesh cubeMesh = GenMeshCube(2.0f, 1.0f, 4.0f);
    Model carModel = LoadModelFromMesh(cubeMesh);

    // Wheel offsets (relative to car center)
    Vector3 wheelOffsets[4] = {
        {-1.0f, -0.5f, 1.5f},  // Front left
        {1.0f, -0.5f, 1.5f},   // Front right
        {-1.0f, -0.5f, -1.5f}, // Rear left
        {1.0f, -0.5f, -1.5f}   // Rear right
    };

    // Checkpoints
    Vector3 checkpoints[4] = {
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 20.0f},
        {20.0f, 0.0f, 20.0f},
        {20.0f, 0.0f, 0.0f}
    };
    int currentCheckpoint = 0;
    int lap = 0;

    // Game loop
    while (!WindowShouldClose()) {
        // Delta time for frame-rate independence
        float deltaTime = GetFrameTime();

        // Surface friction based on position (x > 10 is gravel)
        float surfaceFriction = (carPosition.x > 10.0f) ? 0.7f : 1.0f;

        // Handle input and update car physics
        if (IsKeyDown(KEY_UP)) {
            carSpeed += acceleration * surfaceFriction * deltaTime;
            if (carSpeed > maxSpeed) carSpeed = maxSpeed;
        } else if (IsKeyDown(KEY_DOWN)) {
            carSpeed -= acceleration * surfaceFriction * deltaTime;
            if (carSpeed < -maxSpeed / 2) carSpeed = -maxSpeed / 2;
        } else {
            if (carSpeed > 0.0f) {
                carSpeed -= deceleration * deltaTime;
                if (carSpeed < 0.0f) carSpeed = 0.0f;
            } else if (carSpeed < 0.0f) {
                carSpeed += deceleration * deltaTime;
                if (carSpeed > 0.0f) carSpeed = 0.0f;
            }
        }

        if (IsKeyDown(KEY_LEFT)) {
            carRotation += turnSpeed * surfaceFriction * deltaTime;
        } else if (IsKeyDown(KEY_RIGHT)) {
            carRotation -= turnSpeed * surfaceFriction * deltaTime;
        }

        // Update velocity and position
        float angleRad = carRotation * DEG2RAD;
        Vector3 forward = {sinf(angleRad), 0.0f, cosf(angleRad)};
        carVelocity = Vector3Scale(forward, carSpeed);
        carPosition = Vector3Add(carPosition, Vector3Scale(carVelocity, deltaTime));

        // Collision with boundaries
        if (carPosition.x < -25.0f) {
            carPosition.x = -25.0f;
            carVelocity.x = 0.0f;
        } else if (carPosition.x > 25.0f) {
            carPosition.x = 25.0f;
            carVelocity.x = 0.0f;
        }
        if (carPosition.z < -25.0f) {
            carPosition.z = -25.0f;
            carVelocity.z = 0.0f;
        } else if (carPosition.z > 25.0f) {
            carPosition.z = 25.0f;
            carVelocity.z = 0.0f;
        }

        // Update camera
        Vector3 direction = {sinf(angleRad), 0.0f, cosf(angleRad)};
        Vector3 cameraPosition = Vector3Add(carPosition, Vector3Scale(direction, -5.0f));
        cameraPosition.y = carPosition.y + 2.0f;
        camera.position = cameraPosition;
        camera.target = carPosition;

        // Check checkpoint collision
        if (Vector3Distance(carPosition, checkpoints[currentCheckpoint]) < 2.0f) {
            currentCheckpoint++;
            if (currentCheckpoint >= 4) {
                currentCheckpoint = 0;
                lap++;
            }
        }

        // Drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);

        BeginMode3D(camera);
        // Draw track
        DrawPlane((Vector3){0.0f, 0.0f, 0.0f}, (Vector2){50.0f, 50.0f}, GREEN);

        // Draw car using Model with transformations
        DrawModelEx(carModel, carPosition, (Vector3){0.0f, 1.0f, 0.0f}, carRotation, (Vector3){1.0f, 1.0f, 1.0f}, RED);

        // Draw wheels
        for (int i = 0; i < 4; i++) {
            Vector3 wheelPos = Vector3Add(carPosition, Vector3RotateByAxisAngle(wheelOffsets[i], (Vector3){0.0f, 1.0f, 0.0f}, angleRad));
            DrawSphere(wheelPos, 0.5f, BLACK);
        }

        // Draw checkpoints
        for (int i = 0; i < 4; i++) {
            DrawSphere(checkpoints[i], 1.0f, BLUE);
        }
        EndMode3D();

        // Draw 2D UI
        double time = GetTime();
        DrawText(TextFormat("Lap: %d/3", lap), 10, 10, 20, BLACK);
        DrawText(TextFormat("Time: %.2f", time), 10, 30, 20, BLACK);
        if (lap >= 3) {
            DrawText("You Win!", screenWidth/2 - 50, screenHeight/2 - 10, 30, GREEN);
        }

        // Draw mini-map
        int miniMapX = screenWidth - 110;
        int miniMapY = 10;
        int miniMapSize = 100;
        DrawRectangle(miniMapX, miniMapY, miniMapSize, miniMapSize, LIGHTGRAY);
        float carX = (carPosition.x + 25.0f) / 50.0f * miniMapSize;
        float carZ = (carPosition.z + 25.0f) / 50.0f * miniMapSize;
        DrawCircle(miniMapX + carX, miniMapY + carZ, 3, RED);
        for (int i = 0; i < 4; i++) {
            float cpX = (checkpoints[i].x + 25.0f) / 50.0f * miniMapSize;
            float cpZ = (checkpoints[i].z + 25.0f) / 50.0f * miniMapSize;
            DrawCircle(miniMapX + cpX, miniMapY + cpZ, 2, BLUE);
        }

        EndDrawing();
    }

    // Cleanup
    UnloadModel(carModel); // Unload the model to free memory
    CloseWindow();
    return 0;
}
