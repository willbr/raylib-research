#include "raylib.h"
#include "raymath.h"
#include <stdlib.h>
#include <math.h>

#define MAX_UNITS 10
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define UNIT_RADIUS 1.0f
#define UNIT_HEIGHT 2.0f
#define TARGET_RADIUS 0.3f // Size of the target marker
#define GROUND_SIZE 50.0f   // Increased ground size from 20.0f to 50.0f

// Unit structure
typedef struct {
    Vector3 position;
    Vector3 target;
    bool selected;
    float speed;
    bool isMoving; // Tracks if the unit is moving towards a target
} Unit;

// Convert a 2D screen position to a 3D world position (on ground plane)
Vector3 ScreenToWorld(Vector2 screenPos, Camera camera) {
    // Create a ray from screen position
    Ray ray = GetMouseRay(screenPos, camera);
    
    // Calculate ground plane (y = 0)
    Vector3 groundNormal = { 0.0f, 1.0f, 0.0f };
    Vector3 groundPoint = { 0.0f, 0.0f, 0.0f };
    
    // Calculate intersection with ground plane
    float denominator = Vector3DotProduct(groundNormal, ray.direction);
    
    if (fabs(denominator) > 0.0001f) {
        float t = Vector3DotProduct(Vector3Subtract(groundPoint, ray.position), groundNormal) / denominator;
        
        if (t >= 0.0f) {
            return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
        }
    }
    
    return (Vector3){ 0.0f, 0.0f, 0.0f }; // Default if no intersection
}

// Check if a 3D point is within the ground boundaries
bool IsPositionValid(Vector3 position) {
    float halfGround = GROUND_SIZE / 2.0f;
    return (position.x >= -halfGround && position.x <= halfGround &&
            position.z >= -halfGround && position.z <= halfGround);
}

// Initialize units
void InitUnits(Unit* units) {
    float halfGround = GROUND_SIZE / 2.0f;
    
    for (int i = 0; i < MAX_UNITS; i++) {
        // Random position on the ground plane
        units[i].position = (Vector3){ 
            ((float)rand() / RAND_MAX) * GROUND_SIZE - halfGround,
            0.0f, // Y is always 0 (ground level)
            ((float)rand() / RAND_MAX) * GROUND_SIZE - halfGround
        };
        units[i].target = units[i].position;
        units[i].selected = false;
        units[i].speed = 5.0f; // Units per second
        units[i].isMoving = false;
    }
}

// Move unit towards target with collision avoidance
void UpdateUnit(Unit* unit, Unit* units, int unitIndex, float deltaTime) {
    Vector3 direction = Vector3Subtract(unit->target, unit->position);
    direction.y = 0.0f; // Ensure movement is only on the xz plane
    
    float distance = Vector3Length(direction);

    if (distance > 0.1f) { // Move if not close enough
        direction = Vector3Normalize(direction);

        // Move towards target
        Vector3 newPosition = Vector3Add(unit->position, Vector3Scale(direction, unit->speed * deltaTime));
        
        // Only update if the new position is valid
        if (IsPositionValid(newPosition)) {
            unit->position = newPosition;
        }
        
        unit->isMoving = true; // Unit is moving
    } else {
        unit->isMoving = false; // Unit has arrived
    }

    // Collision with other units
    for (int i = 0; i < MAX_UNITS; i++) {
        if (i == unitIndex) continue; // Skip self

        Vector3 otherPos = units[i].position;
        Vector3 posDiff = Vector3Subtract(unit->position, otherPos);
        posDiff.y = 0.0f; // Only consider horizontal distance
        
        float distToOther = Vector3Length(posDiff);

        if (distToOther < UNIT_RADIUS * 2) { // Collision detected
            Vector3 pushDir = Vector3Normalize(posDiff);
            float overlap = (UNIT_RADIUS * 2 - distToOther) * 0.5f;

            // Push units apart (only on xz plane)
            Vector3 push = Vector3Scale(pushDir, overlap);
            unit->position = Vector3Add(unit->position, push);
            units[i].position = Vector3Subtract(units[i].position, push);
            
            // Ensure units stay within bounds
            if (!IsPositionValid(unit->position)) {
                unit->position = Vector3Subtract(unit->position, push);
            }
            
            if (!IsPositionValid(units[i].position)) {
                units[i].position = Vector3Add(units[i].position, push);
            }
        }
    }
}

// Main entry point
int main(void) {
    // Initialize window
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "3D Orthographic RTS");
    SetTargetFPS(144);
    
    // Default window flags
    SetWindowState(FLAG_WINDOW_RESIZABLE);  // Make window resizable
    MaximizeWindow();  // Start with maximized window
    
    // Initialize 3D camera (orthographic view)
    Camera camera = { 0 };
    camera.position = (Vector3){ 20.0f, 20.0f, 20.0f };  // Camera position - moved further back
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };       // Camera looking at point
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };           // Camera up vector
    camera.fovy = 45.0f;                                 // Camera field-of-view Y
    camera.projection = CAMERA_ORTHOGRAPHIC;             // Camera projection type - CHANGED TO ORTHOGRAPHIC
    
    Unit units[MAX_UNITS];
    InitUnits(units);
    
    Vector2 selectionStart = { 0 };
    Vector2 selectionEnd = { 0 };
    bool isSelecting = false;
    
    // Create unit model
    Model unitModel = LoadModelFromMesh(GenMeshCylinder(UNIT_RADIUS, UNIT_HEIGHT, 8));
    
    // Create unit materials for selected and unselected states
    Material matBlue = LoadMaterialDefault();
    matBlue.maps[MATERIAL_MAP_DIFFUSE].color = BLUE;
    
    Material matGreen = LoadMaterialDefault();
    matGreen.maps[MATERIAL_MAP_DIFFUSE].color = GREEN;
    
    // Create ground plane
    Model groundModel = LoadModelFromMesh(GenMeshPlane(GROUND_SIZE, GROUND_SIZE, 1, 1));
    Material groundMaterial = LoadMaterialDefault();
    groundMaterial.maps[MATERIAL_MAP_DIFFUSE].color = LIGHTGRAY;
    groundModel.materials[0] = groundMaterial;

    // Set orthographic camera scale
    float cameraScale = 40.0f;  // Starting with a more zoomed out view for larger ground
    float minZoom = 5.0f;       // Minimum zoom level (more zoomed in)
    float maxZoom = 100.0f;     // Maximum zoom level (more zoomed out) - increased further
    float zoomIncrement = 3.0f; // How much to zoom per mouse wheel tick - increased for better control
    
    // Camera movement speed (units per second)
    float cameraMoveSpeed = 40.0f;  // Increased for faster navigation of larger map

    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        
        // Camera panning with WASD
        Vector3 cameraMoveDir = {0};
        
        // Camera zooming with mouse wheel
        float mouseWheelMove = GetMouseWheelMove();
        if (mouseWheelMove != 0) {
            // Decrease scale (zoom in) when scrolling up, increase (zoom out) when scrolling down
            cameraScale -= mouseWheelMove * zoomIncrement;
            
            // Clamp zoom level to min and max values
            if (cameraScale < minZoom) cameraScale = minZoom;
            if (cameraScale > maxZoom) cameraScale = maxZoom;
        }
        
        // Get camera right vector (for proper strafing)
        Vector3 cameraRight = Vector3CrossProduct(
            Vector3Subtract(camera.target, camera.position),
            camera.up
        );
        cameraRight = Vector3Normalize(cameraRight);
        
        // Get camera forward vector in the xz plane (for proper forward/backward movement)
        Vector3 cameraForward = Vector3Subtract(camera.target, camera.position);
        cameraForward.y = 0.0f; // Lock to xz plane
        cameraForward = Vector3Normalize(cameraForward);
        
        // Apply movement based on keys
        if (IsKeyDown(KEY_W)) {
            cameraMoveDir = Vector3Add(cameraMoveDir, cameraForward);
        }
        if (IsKeyDown(KEY_S)) {
            cameraMoveDir = Vector3Subtract(cameraMoveDir, cameraForward);
        }
        if (IsKeyDown(KEY_D)) {
            cameraMoveDir = Vector3Add(cameraMoveDir, cameraRight);
        }
        if (IsKeyDown(KEY_A)) {
            cameraMoveDir = Vector3Subtract(cameraMoveDir, cameraRight);
        }
        
        // Normalize movement direction if needed
        if (Vector3Length(cameraMoveDir) > 0) {
            cameraMoveDir = Vector3Normalize(cameraMoveDir);
            
            // Calculate movement distance
            float moveDistance = cameraMoveSpeed * deltaTime;
            Vector3 movement = Vector3Scale(cameraMoveDir, moveDistance);
            
            // Move both camera position and target together to maintain view direction
            camera.position = Vector3Add(camera.position, movement);
            camera.target = Vector3Add(camera.target, movement);
        }
        
        // Check for maximize/fullscreen toggle
        if (IsKeyPressed(KEY_F11) || IsKeyPressed(KEY_F)) {
            // Toggle fullscreen
            if (!IsWindowFullscreen()) {
                int monitor = GetCurrentMonitor();
                SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
                ToggleFullscreen();
            } else {
                ToggleFullscreen();
                SetWindowSize(SCREEN_WIDTH, SCREEN_HEIGHT);
            }
        }
        
        // Check for maximize (not fullscreen)
        if (IsKeyPressed(KEY_M)) {
            if (IsWindowMaximized()) {
                RestoreWindow();
            } else {
                MaximizeWindow();
            }
        }
        
        // Update orthographic scale with window size
        float width = (float)GetScreenWidth();
        float height = (float)GetScreenHeight();
        float aspect = width / height;
        
        // Set orthographic camera scale
        camera.fovy = cameraScale; 
        
        // Input handling
        Vector2 mousePos = GetMousePosition();
        
        // Start selection rectangle
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            selectionStart = mousePos;
            isSelecting = true;
            
            // Single-click selection in 3D (ray cast to find clicked unit)
            Ray ray = GetMouseRay(mousePos, camera);
            
            bool clickedUnit = false;
            for (int i = 0; i < MAX_UNITS; i++) {
                // Test for ray collision with the unit sphere
                RayCollision collision = GetRayCollisionSphere(ray, units[i].position, UNIT_RADIUS);
                
                if (collision.hit) {
                    // Toggle selection of the unit
                    units[i].selected = !units[i].selected;
                    clickedUnit = true;
                    isSelecting = false;
                    break;
                }
            }
            
            // Deselect all if clicking on empty space
            if (!clickedUnit) {
                for (int i = 0; i < MAX_UNITS; i++) {
                    units[i].selected = false;
                }
            }
        }
        
        // Update selection rectangle while dragging
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && isSelecting) {
            selectionEnd = mousePos;
        }
        
        // Finish selection rectangle and select units
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && isSelecting) {
            // For simplicity, we'll project units to screen space and check rectangle collision
            Rectangle selectionRect = {
                fminf(selectionStart.x, selectionEnd.x),
                fminf(selectionStart.y, selectionEnd.y),
                fabsf(selectionEnd.x - selectionStart.x),
                fabsf(selectionEnd.y - selectionStart.y)
            };
            
            for (int i = 0; i < MAX_UNITS; i++) {
                // Convert 3D position to screen space
                Vector2 screenPos = GetWorldToScreen(units[i].position, camera);
                
                // Check if screen position is inside selection rectangle
                if (screenPos.x >= selectionRect.x && screenPos.x <= selectionRect.x + selectionRect.width &&
                    screenPos.y >= selectionRect.y && screenPos.y <= selectionRect.y + selectionRect.height) {
                    units[i].selected = true;
                }
            }
            
            isSelecting = false;
        }
        
        // Move selected units
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            // Convert mouse position to 3D world position on the ground
            Vector3 targetPos = ScreenToWorld(mousePos, camera);
            
            // Assign target to selected units
            for (int i = 0; i < MAX_UNITS; i++) {
                if (units[i].selected) {
                    units[i].target = targetPos;
                    units[i].isMoving = true; // Set moving flag when target is set
                }
            }
        }
        
        // Update units with collision
        for (int i = 0; i < MAX_UNITS; i++) {
            UpdateUnit(&units[i], units, i, deltaTime);
        }
        
        // Drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);
        
        BeginMode3D(camera);
        
        // Draw ground
        DrawModel(groundModel, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
        
        // Draw enhanced grid lines for better visibility
        // Draw primary grid in a darker gray
        Color gridColor = GRAY;
        float gridSpacing = 2.0f; // Space between grid lines
        int gridLines = (int)(GROUND_SIZE / gridSpacing);
        
        // Draw primary grid lines
        for (int i = -gridLines/2; i <= gridLines/2; i++) {
            float pos = i * gridSpacing;
            
            // X-axis lines (along Z)
            DrawLine3D(
                (Vector3){pos, 0.01f, -GROUND_SIZE/2}, 
                (Vector3){pos, 0.01f, GROUND_SIZE/2}, 
                gridColor
            );
            
            // Z-axis lines (along X)
            DrawLine3D(
                (Vector3){-GROUND_SIZE/2, 0.01f, pos}, 
                (Vector3){GROUND_SIZE/2, 0.01f, pos}, 
                gridColor
            );
        }
        
        // Draw major axes with more prominent color
        Color axisColor = DARKGRAY;
        DrawLine3D((Vector3){0, 0.02f, -GROUND_SIZE/2}, (Vector3){0, 0.02f, GROUND_SIZE/2}, axisColor); // Z axis
        DrawLine3D((Vector3){-GROUND_SIZE/2, 0.02f, 0}, (Vector3){GROUND_SIZE/2, 0.02f, 0}, axisColor); // X axis
        
        // Draw units and their targets
        for (int i = 0; i < MAX_UNITS; i++) {
            // Draw unit model with appropriate color
            unitModel.materials[0] = units[i].selected ? matGreen : matBlue;
            DrawModel(unitModel, units[i].position, 1.0f, WHITE);
            
            // Draw target marker if unit is moving
            if (units[i].isMoving) {
                DrawSphere(units[i].target, TARGET_RADIUS, RED);
            }
        }
        
        EndMode3D();
        
        // Draw selection rectangle if active (in 2D screen space)
        if (isSelecting) {
            Rectangle selectionRect = {
                fminf(selectionStart.x, selectionEnd.x),
                fminf(selectionStart.y, selectionEnd.y),
                fabsf(selectionEnd.x - selectionStart.x),
                fabsf(selectionEnd.y - selectionStart.y)
            };
            DrawRectangleLinesEx(selectionRect, 2, GREEN);
        }
        
        DrawFPS(10, 10);
        EndDrawing();
    }
    
    // Unload models and materials
    UnloadModel(unitModel);
    UnloadModel(groundModel);
    
    CloseWindow();
    return 0;
}
