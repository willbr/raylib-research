#include "raylib.h"
#include "raymath.h"
#include <stdlib.h>
#include <math.h>

#define MAX_UNITS 10
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define UNIT_RADIUS 15.0f
#define TARGET_RADIUS 5.0f // Size of the target marker

// Unit structure
typedef struct {
    Vector2 position;
    Vector2 target;
    bool selected;
    float speed;
    bool isMoving; // Tracks if the unit is moving towards a target
} Unit;

// Initialize units
void InitUnits(Unit* units) {
    for (int i = 0; i < MAX_UNITS; i++) {
        units[i].position = (Vector2){ (float)(rand() % SCREEN_WIDTH), (float)(rand() % SCREEN_HEIGHT) };
        units[i].target = units[i].position;
        units[i].selected = false;
        units[i].speed = 100.0f; // Pixels per second
        units[i].isMoving = false;
    }
}

// Move unit towards target with collision avoidance
void UpdateUnit(Unit* unit, Unit* units, int unitIndex, float deltaTime) {
    Vector2 direction = { unit->target.x - unit->position.x, unit->target.y - unit->position.y };
    float distance = Vector2Distance(unit->position, unit->target);

    if (distance > 5.0f) { // Move if not close enough
        direction.x /= distance; // Normalize
        direction.y /= distance;

        // Move towards target
        unit->position.x += direction.x * unit->speed * deltaTime;
        unit->position.y += direction.y * unit->speed * deltaTime;
        unit->isMoving = true; // Unit is moving
    } else {
        unit->isMoving = false; // Unit has arrived
    }

    // Collision with other units
    for (int i = 0; i < MAX_UNITS; i++) {
        if (i == unitIndex) continue; // Skip self

        Vector2 otherPos = units[i].position;
        float distToOther = Vector2Distance(unit->position, otherPos);

        if (distToOther < UNIT_RADIUS * 2) { // Collision detected
            Vector2 pushDir = Vector2Normalize(Vector2Subtract(unit->position, otherPos));
            float overlap = (UNIT_RADIUS * 2 - distToOther) * 0.5f;

            // Push units apart
            unit->position.x += pushDir.x * overlap;
            unit->position.y += pushDir.y * overlap;
            units[i].position.x -= pushDir.x * overlap;
            units[i].position.y -= pushDir.y * overlap;
        }
    }

    // Keep units within screen bounds
    unit->position.x = Clamp(unit->position.x, UNIT_RADIUS, SCREEN_WIDTH - UNIT_RADIUS);
    unit->position.y = Clamp(unit->position.y, UNIT_RADIUS, SCREEN_HEIGHT - UNIT_RADIUS);
}

// Check if a point is inside a rectangle
bool PointInRect(Vector2 point, Rectangle rect) {
    return (point.x >= rect.x && point.x <= rect.x + rect.width &&
            point.y >= rect.y && point.y <= rect.y + rect.height);
}

// Main entry point
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Simple RTS Clone with Targets");
    SetTargetFPS(60);

    Unit units[MAX_UNITS];
    InitUnits(units);

    Vector2 selectionStart = { 0 };
    Vector2 selectionEnd = { 0 };
    bool isSelecting = false;

    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();

        // Input handling
        Vector2 mousePos = GetMousePosition();

        // Start selection rectangle
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            selectionStart = mousePos;
            isSelecting = true;

            // Single-click selection
            bool clickedUnit = false;
            for (int i = 0; i < MAX_UNITS; i++) {
                float dist = Vector2Distance(mousePos, units[i].position);
                if (dist < UNIT_RADIUS) {
                    units[i].selected = !units[i].selected;
                    clickedUnit = true;
                    isSelecting = false;
                    break;
                }
            }
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
            Rectangle selectionRect = {
                fminf(selectionStart.x, selectionEnd.x),
                fminf(selectionStart.y, selectionEnd.y),
                fabsf(selectionEnd.x - selectionStart.x),
                fabsf(selectionEnd.y - selectionStart.y)
            };

            for (int i = 0; i < MAX_UNITS; i++) {
                if (PointInRect(units[i].position, selectionRect)) {
                    units[i].selected = true;
                }
            }
            isSelecting = false;
        }

        // Move selected units
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            Vector2 targetPos = GetMousePosition();
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

        // Draw selection rectangle if active
        if (isSelecting) {
            Rectangle selectionRect = {
                fminf(selectionStart.x, selectionEnd.x),
                fminf(selectionStart.y, selectionEnd.y),
                fabsf(selectionEnd.x - selectionStart.x),
                fabsf(selectionEnd.y - selectionStart.y)
            };
            DrawRectangleLinesEx(selectionRect, 2, GREEN);
        }

        // Draw units and their targets
        for (int i = 0; i < MAX_UNITS; i++) {
            // Draw unit
            Color unitColor = units[i].selected ? GREEN : BLUE;
            DrawCircleV(units[i].position, UNIT_RADIUS, unitColor);

            // Draw target marker if unit is moving
            if (units[i].isMoving) {
                DrawCircleV(units[i].target, TARGET_RADIUS, RED);
            }
        }

        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
