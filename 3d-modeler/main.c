#include "raylib.h"
#include "raymath.h"  // Added for FLT_MAX and other math utilities
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <float.h>

#define MAX_MODELS 100

typedef enum {
    GIZMO_NONE,
    GIZMO_MOVE,
    GIZMO_ROTATE,
    GIZMO_SCALE
} GizmoMode;

typedef enum {
    AXIS_NONE,
    AXIS_X,
    AXIS_Y,
    AXIS_Z
} GizmoAxis;

typedef struct {
    Model model;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale;
} SceneObject;

// Helper function to transform a point by a matrix
Vector3 TransformVector3(Vector3 v, Matrix m)
{
    Vector3 result;
    result.x = v.x * m.m0 + v.y * m.m4 + v.z * m.m8 + m.m12;
    result.y = v.x * m.m1 + v.y * m.m5 + v.z * m.m9 + m.m13;
    result.z = v.x * m.m2 + v.y * m.m6 + v.z * m.m10 + m.m14;
    return result;
}

int main(void)
{
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 450;
    
    InitWindow(screenWidth, screenHeight, "Raylib 3D Model Editor");
    SetTargetFPS(60);

    // Camera setup
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Scene objects
    SceneObject objects[MAX_MODELS];
    int objectCount = 0;
    int selectedObject = -1;

    // Initialize first object
    objects[objectCount].model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    objects[objectCount].position = (Vector3){ 0.0f, 0.0f, 0.0f };
    objects[objectCount].rotation = (Vector3){ 0.0f, 0.0f, 0.0f };
    objects[objectCount].scale = (Vector3){ 1.0f, 1.0f, 1.0f };
    objectCount++;

    float cameraDistance = 10.0f;
    float cameraAngleX = 45.0f;
    float cameraAngleY = 45.0f;

    GizmoMode gizmoMode = GIZMO_NONE;
    GizmoAxis selectedAxis = AXIS_NONE;
    Vector2 lastMousePos = { 0 };

    // Main game loop
    while (!WindowShouldClose())
    {
        // Update camera
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            Vector2 mouseDelta = GetMouseDelta();
            cameraAngleX += mouseDelta.x * 0.2f;
            cameraAngleY += mouseDelta.y * 0.2f;
            if (cameraAngleY > 89.0f) cameraAngleY = 89.0f;
            if (cameraAngleY < -89.0f) cameraAngleY = -89.0f;
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0)
        {
            cameraDistance -= wheel * 1.0f;
            if (cameraDistance < 2.0f) cameraDistance = 2.0f;
        }

        float camX = cameraDistance * cosf(cameraAngleX * DEG2RAD) * cosf(cameraAngleY * DEG2RAD);
        float camY = cameraDistance * sinf(cameraAngleY * DEG2RAD);
        float camZ = cameraDistance * sinf(cameraAngleX * DEG2RAD) * cosf(cameraAngleY * DEG2RAD);
        camera.position = (Vector3){ camX, camY, camZ };

        // Add new objects
        if (IsKeyPressed(KEY_C) && objectCount < MAX_MODELS)
        {
            objects[objectCount].model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
            objects[objectCount].position = (Vector3){ 0.0f, 0.0f, 0.0f };
            objects[objectCount].rotation = (Vector3){ 0.0f, 0.0f, 0.0f };
            objects[objectCount].scale = (Vector3){ 1.0f, 1.0f, 1.0f };
            objectCount++;
        }
        if (IsKeyPressed(KEY_V) && objectCount < MAX_MODELS)
        {
            objects[objectCount].model = LoadModelFromMesh(GenMeshSphere(0.5f, 16, 16));
            objects[objectCount].position = (Vector3){ 0.0f, 0.0f, 0.0f };
            objects[objectCount].rotation = (Vector3){ 0.0f, 0.0f, 0.0f };
            objects[objectCount].scale = (Vector3){ 1.0f, 1.0f, 1.0f };
            objectCount++;
        }

        // Object selection
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && gizmoMode == GIZMO_NONE)
        {
            Ray mouseRay = GetMouseRay(GetMousePosition(), camera);
            selectedObject = -1;
            float closestDist = FLT_MAX;
            
            for (int i = 0; i < objectCount; i++)
            {
                Matrix transform = MatrixIdentity();
                transform = MatrixMultiply(transform, MatrixRotateX(objects[i].rotation.x * DEG2RAD));
                transform = MatrixMultiply(transform, MatrixRotateY(objects[i].rotation.y * DEG2RAD));
                transform = MatrixMultiply(transform, MatrixRotateZ(objects[i].rotation.z * DEG2RAD));
                transform = MatrixMultiply(transform, MatrixScale(objects[i].scale.x, objects[i].scale.y, objects[i].scale.z));
                transform = MatrixMultiply(transform, MatrixTranslate(objects[i].position.x, objects[i].position.y, objects[i].position.z));
                
                BoundingBox bounds = GetModelBoundingBox(objects[i].model);
                Vector3 corners[8] = {
                    {bounds.min.x, bounds.min.y, bounds.min.z},
                    {bounds.max.x, bounds.min.y, bounds.min.z},
                    {bounds.min.x, bounds.max.y, bounds.min.z},
                    {bounds.max.x, bounds.max.y, bounds.min.z},
                    {bounds.min.x, bounds.min.y, bounds.max.z},
                    {bounds.max.x, bounds.min.y, bounds.max.z},
                    {bounds.min.x, bounds.max.y, bounds.max.z},
                    {bounds.max.x, bounds.max.y, bounds.max.z}
                };
                
                for (int j = 0; j < 8; j++)
                {
                    corners[j] = TransformVector3(corners[j], transform);
                }
                
                RayCollision collision = GetRayCollisionBox(mouseRay, (BoundingBox){
                    .min = {fminf(fminf(fminf(fminf(fminf(fminf(fminf(corners[0].x, corners[1].x), corners[2].x), corners[3].x), corners[4].x), corners[5].x), corners[6].x), corners[7].x),
                            fminf(fminf(fminf(fminf(fminf(fminf(fminf(corners[0].y, corners[1].y), corners[2].y), corners[3].y), corners[4].y), corners[5].y), corners[6].y), corners[7].y),
                            fminf(fminf(fminf(fminf(fminf(fminf(fminf(corners[0].z, corners[1].z), corners[2].z), corners[3].z), corners[4].z), corners[5].z), corners[6].z), corners[7].z)},
                    .max = {fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(corners[0].x, corners[1].x), corners[2].x), corners[3].x), corners[4].x), corners[5].x), corners[6].x), corners[7].x),
                            fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(corners[0].y, corners[1].y), corners[2].y), corners[3].y), corners[4].y), corners[5].y), corners[6].y), corners[7].y),
                            fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(corners[0].z, corners[1].z), corners[2].z), corners[3].z), corners[4].z), corners[5].z), corners[6].z), corners[7].z)}
                });
                
                if (collision.hit && collision.distance < closestDist)
                {
                    closestDist = collision.distance;
                    selectedObject = i;
                }
            }
        }

        // Gizmo handling for selected object
        if (selectedObject >= 0 && selectedObject < objectCount)
        {
            SceneObject* obj = &objects[selectedObject];
            
            // Calculate transformation
            Matrix transform = MatrixIdentity();
            transform = MatrixMultiply(transform, MatrixRotateX(obj->rotation.x * DEG2RAD));
            transform = MatrixMultiply(transform, MatrixRotateY(obj->rotation.y * DEG2RAD));
            transform = MatrixMultiply(transform, MatrixRotateZ(obj->rotation.z * DEG2RAD));
            transform = MatrixMultiply(transform, MatrixScale(obj->scale.x, obj->scale.y, obj->scale.z));
            transform = MatrixMultiply(transform, MatrixTranslate(obj->position.x, obj->position.y, obj->position.z));

            // Calculate transformed bounding box
            BoundingBox baseBounds = GetModelBoundingBox(obj->model);
            Vector3 corners[8] = {
                {baseBounds.min.x, baseBounds.min.y, baseBounds.min.z},
                {baseBounds.max.x, baseBounds.min.y, baseBounds.min.z},
                {baseBounds.min.x, baseBounds.max.y, baseBounds.min.z},
                {baseBounds.max.x, baseBounds.max.y, baseBounds.min.z},
                {baseBounds.min.x, baseBounds.min.y, baseBounds.max.z},
                {baseBounds.max.x, baseBounds.min.y, baseBounds.max.z},
                {baseBounds.min.x, baseBounds.max.y, baseBounds.max.z},
                {baseBounds.max.x, baseBounds.max.y, baseBounds.max.z}
            };

            Vector3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
            Vector3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            for (int i = 0; i < 8; i++)
            {
                Vector3 transformed = TransformVector3(corners[i], transform);
                min.x = fminf(min.x, transformed.x);
                min.y = fminf(min.y, transformed.y);
                min.z = fminf(min.z, transformed.z);
                max.x = fmaxf(max.x, transformed.x);
                max.y = fmaxf(max.y, transformed.y);
                max.z = fmaxf(max.z, transformed.z);
            }

            Vector3 boundsSize = { max.x - min.x, max.y - min.y, max.z - min.z };

            // Gizmo mode selection
            if (IsKeyPressed(KEY_M)) gizmoMode = GIZMO_MOVE;
            if (IsKeyPressed(KEY_R)) gizmoMode = GIZMO_ROTATE;
            if (IsKeyPressed(KEY_S)) gizmoMode = GIZMO_SCALE;

            // Gizmo interaction
            Vector2 mousePos = GetMousePosition();
            float gizmoLength = fmaxf(fmaxf(boundsSize.x, boundsSize.y), boundsSize.z) * 0.2f;
            if (gizmoLength < 0.5f) gizmoLength = 0.5f;
            float gizmoSphereSize = gizmoLength * 0.2f;

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                Ray mouseRay = GetMouseRay(mousePos, camera);
                
                if (gizmoMode != GIZMO_NONE)
                {
                    RayCollision collisionX = GetRayCollisionSphere(mouseRay, 
                        (Vector3){obj->position.x + gizmoLength, obj->position.y, obj->position.z}, gizmoSphereSize);
                    RayCollision collisionY = GetRayCollisionSphere(mouseRay, 
                        (Vector3){obj->position.x, obj->position.y + gizmoLength, obj->position.z}, gizmoSphereSize);
                    RayCollision collisionZ = GetRayCollisionSphere(mouseRay, 
                        (Vector3){obj->position.x, obj->position.y, obj->position.z + gizmoLength}, gizmoSphereSize);
                    
                    if (collisionX.hit) selectedAxis = AXIS_X;
                    else if (collisionY.hit) selectedAxis = AXIS_Y;
                    else if (collisionZ.hit) selectedAxis = AXIS_Z;
                    else selectedAxis = AXIS_NONE;
                    
                    lastMousePos = mousePos;
                }
            }

            // Apply gizmo transformations
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selectedAxis != AXIS_NONE)
            {
                Vector2 mouseDelta = GetMouseDelta();
                
                Vector3 axisDir = {0};
                if (selectedAxis == AXIS_X) axisDir = (Vector3){1.0f, 0.0f, 0.0f};
                else if (selectedAxis == AXIS_Y) axisDir = (Vector3){0.0f, 1.0f, 0.0f};
                else if (selectedAxis == AXIS_Z) axisDir = (Vector3){0.0f, 0.0f, 1.0f};
                
                Vector2 screenAxisStart = GetWorldToScreen(obj->position, camera);
                Vector3 axisEnd = {
                    obj->position.x + axisDir.x * gizmoLength,
                    obj->position.y + axisDir.y * gizmoLength,
                    obj->position.z + axisDir.z * gizmoLength
                };
                Vector2 screenAxisEnd = GetWorldToScreen(axisEnd, camera);
                
                Vector2 screenAxis = {screenAxisEnd.x - screenAxisStart.x, screenAxisEnd.y - screenAxisStart.y};
                float axisLength = sqrtf(screenAxis.x * screenAxis.x + screenAxis.y * screenAxis.y);
                if (axisLength > 0)
                {
                    screenAxis.x /= axisLength;
                    screenAxis.y /= axisLength;
                }
                
                float movement = (mouseDelta.x * screenAxis.x + mouseDelta.y * screenAxis.y) * 0.05f;
                
                switch (gizmoMode)
                {
                    case GIZMO_MOVE:
                        if (selectedAxis == AXIS_X) obj->position.x += movement;
                        if (selectedAxis == AXIS_Y) obj->position.y += movement;
                        if (selectedAxis == AXIS_Z) obj->position.z += movement;
                        break;
                    case GIZMO_ROTATE:
                        if (selectedAxis == AXIS_X) obj->rotation.x += movement * 50.0f;
                        if (selectedAxis == AXIS_Y) obj->rotation.y += movement * 50.0f;
                        if (selectedAxis == AXIS_Z) obj->rotation.z += movement * 50.0f;
                        break;
                    case GIZMO_SCALE:
                        if (selectedAxis == AXIS_X) obj->scale.x += movement;
                        if (selectedAxis == AXIS_Y) obj->scale.y += movement;
                        if (selectedAxis == AXIS_Z) obj->scale.z += movement;
                        break;
                    default: break;
                }
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                selectedAxis = AXIS_NONE;
        }

        // Load new model
        if (IsKeyPressed(KEY_L) && objectCount < MAX_MODELS)
        {
            FilePathList droppedFiles = LoadDroppedFiles();
            if (droppedFiles.count > 0)
            {
                objects[objectCount].model = LoadModel(droppedFiles.paths[0]);
                objects[objectCount].position = (Vector3){ 0.0f, 0.0f, 0.0f };
                objects[objectCount].rotation = (Vector3){ 0.0f, 0.0f, 0.0f };
                objects[objectCount].scale = (Vector3){ 1.0f, 1.0f, 1.0f };
                objectCount++;
            }
            UnloadDroppedFiles(droppedFiles);
        }

        // Reset transformations for selected object
        if (IsKeyPressed(KEY_Z) && selectedObject >= 0)
        {
            objects[selectedObject].position = (Vector3){ 0.0f, 0.0f, 0.0f };
            objects[selectedObject].rotation = (Vector3){ 0.0f, 0.0f, 0.0f };
            objects[selectedObject].scale = (Vector3){ 1.0f, 1.0f, 1.0f };
        }

        // Drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);
        
        BeginMode3D(camera);
        
        // Draw all objects
        for (int i = 0; i < objectCount; i++)
        {
            Matrix transform = MatrixIdentity();
            transform = MatrixMultiply(transform, MatrixRotateX(objects[i].rotation.x * DEG2RAD));
            transform = MatrixMultiply(transform, MatrixRotateY(objects[i].rotation.y * DEG2RAD));
            transform = MatrixMultiply(transform, MatrixRotateZ(objects[i].rotation.z * DEG2RAD));
            transform = MatrixMultiply(transform, MatrixScale(objects[i].scale.x, objects[i].scale.y, objects[i].scale.z));
            transform = MatrixMultiply(transform, MatrixTranslate(objects[i].position.x, objects[i].position.y, objects[i].position.z));
            
            objects[i].model.transform = transform;
            DrawModel(objects[i].model, (Vector3){0}, 1.0f, (i == selectedObject) ? YELLOW : WHITE);
        }
        
        // Draw gizmo for selected object
        if (selectedObject >= 0 && gizmoMode != GIZMO_NONE)
        {
            SceneObject* obj = &objects[selectedObject];
            BoundingBox baseBounds = GetModelBoundingBox(obj->model);
            Vector3 corners[8] = {
                {baseBounds.min.x, baseBounds.min.y, baseBounds.min.z},
                {baseBounds.max.x, baseBounds.min.y, baseBounds.min.z},
                {baseBounds.min.x, baseBounds.max.y, baseBounds.min.z},
                {baseBounds.max.x, baseBounds.max.y, baseBounds.min.z},
                {baseBounds.min.x, baseBounds.min.y, baseBounds.max.z},
                {baseBounds.max.x, baseBounds.min.y, baseBounds.max.z},
                {baseBounds.min.x, baseBounds.max.y, baseBounds.max.z},
                {baseBounds.max.x, baseBounds.max.y, baseBounds.max.z}
            };
            Matrix transform = obj->model.transform;
            Vector3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
            Vector3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            for (int i = 0; i < 8; i++)
            {
                Vector3 transformed = TransformVector3(corners[i], transform);
                min.x = fminf(min.x, transformed.x);
                min.y = fminf(min.y, transformed.y);
                min.z = fminf(min.z, transformed.z);
                max.x = fmaxf(max.x, transformed.x);
                max.y = fmaxf(max.y, transformed.y);
                max.z = fmaxf(max.z, transformed.z);
            }
            Vector3 boundsSize = { max.x - min.x, max.y - min.y, max.z - min.z };
            float gizmoLength = fmaxf(fmaxf(boundsSize.x, boundsSize.y), boundsSize.z) * 0.2f;
            if (gizmoLength < 0.5f) gizmoLength = 0.5f;
            float gizmoSphereSize = gizmoLength * 0.2f;

            DrawLine3D(obj->position, (Vector3){obj->position.x + gizmoLength, obj->position.y, obj->position.z}, RED);
            DrawSphere((Vector3){obj->position.x + gizmoLength, obj->position.y, obj->position.z}, gizmoSphereSize, RED);
            
            DrawLine3D(obj->position, (Vector3){obj->position.x, obj->position.y + gizmoLength, obj->position.z}, GREEN);
            DrawSphere((Vector3){obj->position.x, obj->position.y + gizmoLength, obj->position.z}, gizmoSphereSize, GREEN);
            
            DrawLine3D(obj->position, (Vector3){obj->position.x, obj->position.y, obj->position.z + gizmoLength}, BLUE);
            DrawSphere((Vector3){obj->position.x, obj->position.y, obj->position.z + gizmoLength}, gizmoSphereSize, BLUE);
        }
        
        DrawGrid(10, 1.0f);
        EndMode3D();

        // UI
        DrawText("Controls:", 10, 10, 20, DARKGRAY);
        DrawText("Right Mouse: Orbit Camera", 10, 30, 20, DARKGRAY);
        DrawText("Mouse Wheel: Zoom", 10, 50, 20, DARKGRAY);
        DrawText("C: Add Cube", 10, 70, 20, DARKGRAY);
        DrawText("V: Add Sphere", 10, 90, 20, DARKGRAY);
        DrawText("M: Move Mode", 10, 110, 20, DARKGRAY);
        DrawText("R: Rotate Mode", 10, 130, 20, DARKGRAY);
        DrawText("S: Scale Mode", 10, 150, 20, DARKGRAY);
        DrawText("Left Click: Select/Use Gizmo", 10, 170, 20, DARKGRAY);
        DrawText("Z: Reset Selected", 10, 190, 20, DARKGRAY);
        DrawText("L + Drop: Load Model", 10, 210, 20, DARKGRAY);
        
        char modeText[32];
        sprintf(modeText, "Mode: %s", 
                gizmoMode == GIZMO_MOVE ? "Move" : 
                gizmoMode == GIZMO_ROTATE ? "Rotate" : 
                gizmoMode == GIZMO_SCALE ? "Scale" : "None");
        DrawText(modeText, 10, screenHeight - 60, 20, DARKGRAY);
        
        char selectText[32];
        sprintf(selectText, "Selected: %d", selectedObject);
        DrawText(selectText, 10, screenHeight - 40, 20, DARKGRAY);
        
        DrawFPS(10, screenHeight - 20);
        EndDrawing();
    }

    // Cleanup
    for (int i = 0; i < objectCount; i++)
    {
        UnloadModel(objects[i].model);
    }
    CloseWindow();
    return 0;
}
