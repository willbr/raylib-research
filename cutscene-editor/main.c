#include "raylib.h"
#include "raymath.h"

// Define a simple object structure for the scene
typedef struct {
    Vector3 position;
    Vector3 rotation;
    Color color;
    bool isActive;
} SceneObject;

// Cutscene state
typedef struct {
    bool isPlaying;
    float time;
} Cutscene;

int main(void) {
    // Initialize window and OpenGL context
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "3D Cutscene Editor - Raylib");

    // Set up camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };  // Camera position
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };      // Camera looking at origin
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };          // Camera up vector (Y-axis)
    camera.fovy = 45.0f;                                 // Field of view
    camera.projection = CAMERA_PERSPECTIVE;              // Perspective projection

    // Define a sample object (cube)
    SceneObject cube = { 
        .position = (Vector3){ 0.0f, 0.0f, 0.0f },
        .rotation = (Vector3){ 0.0f, 0.0f, 0.0f },
        .color = RED,
        .isActive = true 
    };

    // Cutscene state
    Cutscene cutscene = { .isPlaying = false, .time = 0.0f };

    SetTargetFPS(60);  // Set frame rate

    // Main loop
    while (!WindowShouldClose()) {
        // Update
        if (!cutscene.isPlaying && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            // Only update camera when right mouse button is held
            UpdateCamera(&camera, CAMERA_FREE);
        }

        // Toggle play/pause with Space
        if (IsKeyPressed(KEY_SPACE)) {
            cutscene.isPlaying = !cutscene.isPlaying;
        }

        // Simulate cutscene when playing
        if (cutscene.isPlaying) {
            cutscene.time += GetFrameTime();
            // Simple animation: move cube along X-axis
            cube.position.x = sinf(cutscene.time) * 5.0f;  // Oscillate between -5 and 5
            cube.rotation.y += 45.0f * GetFrameTime();     // Rotate cube
        }

        // Reset cutscene with R key
        if (IsKeyPressed(KEY_R)) {
            cutscene.time = 0.0f;
            cube.position = (Vector3){ 0.0f, 0.0f, 0.0f };
            cube.rotation = (Vector3){ 0.0f, 0.0f, 0.0f };
            cutscene.isPlaying = false;
        }

        // Begin drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // 3D rendering
        BeginMode3D(camera);
        
        // Draw the cube
        if (cube.isActive) {
            DrawCubeV(cube.position, (Vector3){ 2.0f, 2.0f, 2.0f }, cube.color);
            DrawCubeWiresV(cube.position, (Vector3){ 2.0f, 2.0f, 2.0f }, BLACK);
        }

        // Draw a grid for reference
        DrawGrid(10, 1.0f);

        EndMode3D();

        // 2D UI overlay
        DrawText("Controls:", 10, 10, 20, DARKGRAY);
        DrawText("Right Mouse + WASD: Move Camera (when not playing)", 10, 30, 20, DARKGRAY);
        DrawText("Space: Play/Pause Cutscene", 10, 50, 20, DARKGRAY);
        DrawText("R: Reset Cutscene", 10, 70, 20, DARKGRAY);
        DrawText(TextFormat("Cutscene Time: %.2f s", cutscene.time), 10, 110, 20, DARKGRAY);
        DrawText(cutscene.isPlaying ? "Playing" : "Paused", 10, 130, 20, cutscene.isPlaying ? GREEN : RED);

        DrawFPS(screenWidth - 80, 10);

        EndDrawing();
    }

    // Cleanup
    CloseWindow();
    return 0;
}
