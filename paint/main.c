#include "raylib.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "paint_common.h"

void SaveCanvas(Canvas* canvas, const char* filename)
{
    FILE* file = fopen(filename, "wb");
    if (!file) return;
    
    // Write layer count
    fwrite(&MAX_LAYERS, sizeof(int), 1, file);
    
    // Write current layer
    fwrite(&canvas->currentLayer, sizeof(int), 1, file);
    
    // Write each layer
    for (int l = 0; l < MAX_LAYERS; l++) {
        Layer* layer = &canvas->layers[l];
        
        // Write visibility
        fwrite(&layer->visible, sizeof(bool), 1, file);
        
        // Write polygon count
        fwrite(&layer->polygonCount, sizeof(int), 1, file);
        
        // Write each polygon
        for (int p = 0; p < layer->polygonCount; p++) {
            Polygon* poly = &layer->polygons[p];
            
            // Write point count, color, thickness, filled, and style
            fwrite(&poly->pointCount, sizeof(int), 1, file);
            fwrite(&poly->color, sizeof(Color), 1, file);
            fwrite(&poly->thickness, sizeof(float), 1, file);
            fwrite(&poly->filled, sizeof(bool), 1, file);
            fwrite(&poly->style, sizeof(PaintStyle), 1, file);
            
            // Write points
            fwrite(poly->points, sizeof(Vector2), poly->pointCount, file);
        }
    }
    
    fclose(file);
};
void LoadCanvas(Canvas* canvas, const char* filename)
{
    FILE* file = fopen(filename, "rb");
    if (!file) return;
    
    // Clear current canvas
    for (int l = 0; l < MAX_LAYERS; l++) {
        canvas->layers[l].polygonCount = 0;
        canvas->layers[l].visible = true;
    }
    
    // Read layer count and verify
    int layerCount;
    fread(&layerCount, sizeof(int), 1, file);
    if (layerCount != MAX_LAYERS) {
        fclose(file);
        return; // Incompatible file format
    }
    
    // Read current layer
    fread(&canvas->currentLayer, sizeof(int), 1, file);
    
    // Read each layer
    for (int l = 0; l < MAX_LAYERS; l++) {
        Layer* layer = &canvas->layers[l];
        
        // Read visibility
        fread(&layer->visible, sizeof(bool), 1, file);
        
        // Read polygon count
        fread(&layer->polygonCount, sizeof(int), 1, file);
        if (layer->polygonCount > MAX_POLYGONS) {
            layer->polygonCount = MAX_POLYGONS;
        }
        
        // Read each polygon
        for (int p = 0; p < layer->polygonCount; p++) {
            Polygon* poly = &layer->polygons[p];
            
            // Read point count, color, thickness, filled, and style
            fread(&poly->pointCount, sizeof(int), 1, file);
            if (poly->pointCount > MAX_POINTS) {
                poly->pointCount = MAX_POINTS;
            }
            
            fread(&poly->color, sizeof(Color), 1, file);
            fread(&poly->thickness, sizeof(float), 1, file);
            fread(&poly->filled, sizeof(bool), 1, file);
            fread(&poly->style, sizeof(PaintStyle), 1, file);
            
            // Read points
            fread(poly->points, sizeof(Vector2), poly->pointCount, file);
        }
    }
    
    fclose(file);
    
    // Reset undo/redo
    canvas->undoCount = 0;
    canvas->redoCount = 0;
};
void AddUndo(Canvas* canvas);
void Undo(Canvas* canvas);
void Redo(Canvas* canvas)
{
    if (canvas->redoCount <= 0) return;
    
    // Save current state for undo
    Layer undoState[MAX_LAYERS];
    for (int l = 0; l < MAX_LAYERS; l++) {
        undoState[l] = canvas->layers[l];
    }
    
    // Restore from redo buffer
    for (int l = 0; l < MAX_LAYERS; l++) {
        canvas->layers[l] = canvas->undoBuffer[0].layers[l];
    }
    
    // Shift redo buffer
    for (int i = 0; i < MAX_UNDO - 1; i++) {
        canvas->undoBuffer[i] = canvas->undoBuffer[i + 1];
    }
    
    canvas->redoCount--;
    canvas->undoCount++;
};
Color GetRandomColorVariation(Color baseColor, int range)
{
    Color newColor = baseColor;
    
    // Add random variation to each color component
    newColor.r = Clamp(newColor.r + (rand() % (range * 2) - range), 0, 255);
    newColor.g = Clamp(newColor.g + (rand() % (range * 2) - range), 0, 255);
    newColor.b = Clamp(newColor.b + (rand() % (range * 2) - range), 0, 255);
    
    return newColor;
};
void ApplyAbstractStyle(Polygon* polygon)
{
    // For abstract style, we add slight distortion to points
    for (int i = 0; i < polygon->pointCount; i++) {
        // Add random offset to each point for a more hand-drawn look
        polygon->points[i].x += (rand() % 20) - 10;
        polygon->points[i].y += (rand() % 20) - 10;
    }
};
void ApplyGeometricStyle(Polygon* polygon)
{
    // For geometric style, we want clean, simplified shapes
    
    if (polygon->pointCount <= 2) return; // Cannot simplify lines further
    
    // Find center of polygon
    Vector2 center = {0};
    for (int i = 0; i < polygon->pointCount; i++) {
        center.x += polygon->points[i].x;
        center.y += polygon->points[i].y;
    }
    center.x /= polygon->pointCount;
    center.y /= polygon->pointCount;
    
    // Find average radius
    float avgRadius = 0;
    for (int i = 0; i < polygon->pointCount; i++) {
        avgRadius += Vector2Distance(center, polygon->points[i]);
    }
    avgRadius /= polygon->pointCount;
    
    // Simplify to regular polygon (for polygons with more than 4 points)
    if (polygon->pointCount > 4) {
        int simplifiedSides = (polygon->pointCount < 8) ? polygon->pointCount : 8;
        
        for (int i = 0; i < simplifiedSides; i++) {
            float angle = i * 2 * PI / simplifiedSides;
            polygon->points[i].x = center.x + cosf(angle) * avgRadius;
            polygon->points[i].y = center.y + sinf(angle) * avgRadius;
        }
        
        polygon->pointCount = simplifiedSides;
    } else {
        // For rectangles and triangles, make them more regular
        for (int i = 0; i < polygon->pointCount; i++) {
            // Snap angles to nearest 45 degrees
            float angle = atan2f(polygon->points[i].y - center.y, polygon->points[i].x - center.x);
            float snappedAngle = roundf(angle / (PI/4)) * (PI/4);
            
            float dist = Vector2Distance(center, polygon->points[i]);
            polygon->points[i].x = center.x + cosf(snappedAngle) * dist;
            polygon->points[i].y = center.y + sinf(snappedAngle) * dist;
        }
    }
};

int main(void)
{
    // Initialize window
    const int screenWidth = 1280;
    const int screenHeight = 720;
    InitWindow(screenWidth, screenHeight, "Stylized Paint Program");
    SetTargetFPS(60);

    // Initialize canvas
    Canvas canvas = {0};
    canvas.currentLayer = 0;
    for (int i = 0; i < MAX_LAYERS; i++) {
        canvas.layers[i].visible = true;
    }

    // Initialize tool settings
    Tool currentTool = TOOL_BRUSH;
    Color selectedColor = RED;
    float brushSize = 10.0f;
    PaintStyle currentStyle = STYLE_ABSTRACT;

    // Define UI layout
    Rectangle colorPaletteBounds = { 10, 10, 200, 80 };
    Rectangle toolbarBounds = { 10, 100, 200, 300 };
    Rectangle layerPanelBounds = { 10, 410, 200, 300 };
    Rectangle canvasBounds = { 220, 10, screenWidth - 230, screenHeight - 20 };
    Rectangle exportBounds = { 10, screenHeight - 40, 120, 30 };

    // Main game loop
    while (!WindowShouldClose())
    {
        // Handle input
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Z)) Undo(&canvas);
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Y)) Redo(&canvas);
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S)) SaveCanvas(&canvas, "painting.spp");
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_O)) LoadCanvas(&canvas, "painting.spp");
        
        HandleInput(canvasBounds, &canvas, currentTool, selectedColor, brushSize, currentStyle);

        // Draw
        BeginDrawing();
            ClearBackground(RAYWHITE);
            
            // Draw UI elements
            DrawColorPalette(colorPaletteBounds, &selectedColor);
            DrawToolbar(toolbarBounds, &currentTool, &brushSize, &currentStyle);
            DrawLayerPanel(layerPanelBounds, &canvas);
            
            // Draw Export button
            AddExportButton(exportBounds, &canvas);
            
            // Draw canvas and artwork
            DrawRectangleRec(canvasBounds, WHITE);
            DrawRectangleLinesEx(canvasBounds, 1, BLACK);
            DrawCanvas(canvasBounds, &canvas);
            
            // Draw current brush preview
            if (CheckCollisionPointRec(GetMousePosition(), canvasBounds)) {
                DrawCircleV(GetMousePosition(), brushSize/2, ColorAlpha(selectedColor, 0.5f));
            }
            
            // Draw UI labels
            DrawText("Color Palette", colorPaletteBounds.x, colorPaletteBounds.y - 20, 10, BLACK);
            DrawText("Tools", toolbarBounds.x, toolbarBounds.y - 20, 10, BLACK);
            DrawText("Layers", layerPanelBounds.x, layerPanelBounds.y - 20, 10, BLACK);
            
            // Draw cursor position
            DrawText(TextFormat("Mouse: %d, %d", (int)GetMousePosition().x, (int)GetMousePosition().y), 10, screenHeight - 20, 10, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

void DrawColorPalette(Rectangle bounds, Color* selectedColor)
{
    DrawRectangleRec(bounds, LIGHTGRAY);
    
    const int colorSize = 20;
    const int colorsPerRow = bounds.width / colorSize;
    const Color colors[] = {
        BLACK, WHITE, RED, GREEN, BLUE, YELLOW, ORANGE, PURPLE, BROWN, PINK, 
        DARKGRAY, GRAY, MAROON, DARKGREEN, DARKBLUE, DARKPURPLE, DARKBROWN,
        VIOLET, GOLD, BEIGE, SKYBLUE, LIME
    };
    
    const int colorCount = sizeof(colors) / sizeof(Color);
    
    for (int i = 0; i < colorCount; i++) {
        int x = bounds.x + (i % colorsPerRow) * colorSize;
        int y = bounds.y + (i / colorsPerRow) * colorSize;
        
        DrawRectangle(x, y, colorSize, colorSize, colors[i]);
        DrawRectangleLines(x, y, colorSize, colorSize, BLACK);
        
        // Check if this color is selected
        if (ColorToInt(*selectedColor) == ColorToInt(colors[i])) {
            DrawRectangleLines(x + 1, y + 1, colorSize - 2, colorSize - 2, WHITE);
            DrawRectangleLines(x + 2, y + 2, colorSize - 4, colorSize - 4, BLACK);
        }
        
        // Check for click
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && 
            CheckCollisionPointRec(GetMousePosition(), (Rectangle){x, y, colorSize, colorSize})) {
            *selectedColor = colors[i];
        }
    }
    
    // Draw currently selected color
    DrawRectangle(bounds.x + bounds.width - 40, bounds.y + 10, 30, 30, *selectedColor);
    DrawRectangleLines(bounds.x + bounds.width - 40, bounds.y + 10, 30, 30, BLACK);
}

void DrawToolbar(Rectangle bounds, Tool* currentTool, float* brushSize, PaintStyle* style)
{
    DrawRectangleRec(bounds, LIGHTGRAY);
    
    // Tool buttons
    const char* toolNames[] = {
        "Brush", "Polygon", "Line", "Rect", "Eraser", "Eyedropper"
    };
    
    for (int i = 0; i < 6; i++) {
        Rectangle toolRect = { bounds.x + 10, bounds.y + 10 + i * 30, 100, 25 };
        DrawRectangleRec(toolRect, (*currentTool == i) ? SKYBLUE : LIGHTGRAY);
        DrawRectangleLinesEx(toolRect, 1, BLACK);
        DrawText(toolNames[i], toolRect.x + 10, toolRect.y + 5, 10, BLACK);
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && 
            CheckCollisionPointRec(GetMousePosition(), toolRect)) {
            *currentTool = i;
        }
    }
    
    // Brush size slider
    Rectangle sliderBounds = { bounds.x + 10, bounds.y + 200, 180, 20 };
    float sliderValue = (*brushSize - MIN_BRUSH_SIZE) / (MAX_BRUSH_SIZE - MIN_BRUSH_SIZE);
    
    DrawRectangleRec(sliderBounds, LIGHTGRAY);
    DrawRectangleLinesEx(sliderBounds, 1, BLACK);
    DrawRectangle(sliderBounds.x, sliderBounds.y, sliderBounds.width * sliderValue, sliderBounds.height, SKYBLUE);
    DrawText("Brush Size", sliderBounds.x, sliderBounds.y - 15, 10, BLACK);
    DrawText(TextFormat("%.0f", *brushSize), sliderBounds.x + sliderBounds.width + 10, sliderBounds.y, 10, BLACK);
    
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && 
        CheckCollisionPointRec(GetMousePosition(), sliderBounds)) {
        float newValue = (GetMousePosition().x - sliderBounds.x) / sliderBounds.width;
        newValue = Clamp(newValue, 0.0f, 1.0f);
        *brushSize = MIN_BRUSH_SIZE + newValue * (MAX_BRUSH_SIZE - MIN_BRUSH_SIZE);
    }
    
    // Style buttons
    const char* styleNames[] = {
        "Normal", "Abstract", "Geometric"
    };
    
    for (int i = 0; i < 3; i++) {
        Rectangle styleRect = { bounds.x + 10, bounds.y + 240 + i * 30, 100, 25 };
        DrawRectangleRec(styleRect, (*style == i) ? SKYBLUE : LIGHTGRAY);
        DrawRectangleLinesEx(styleRect, 1, BLACK);
        DrawText(styleNames[i], styleRect.x + 10, styleRect.y + 5, 10, BLACK);
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && 
            CheckCollisionPointRec(GetMousePosition(), styleRect)) {
            *style = i;
        }
    }
}

void DrawLayerPanel(Rectangle bounds, Canvas* canvas)
{
    DrawRectangleRec(bounds, LIGHTGRAY);
    
    // Layer buttons
    for (int i = 0; i < MAX_LAYERS; i++) {
        Rectangle layerRect = { bounds.x + 10, bounds.y + 10 + i * 30, 100, 25 };
        DrawRectangleRec(layerRect, (canvas->currentLayer == i) ? SKYBLUE : LIGHTGRAY);
        DrawRectangleLinesEx(layerRect, 1, BLACK);
        DrawText(TextFormat("Layer %d", i + 1), layerRect.x + 10, layerRect.y + 5, 10, BLACK);
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && 
            CheckCollisionPointRec(GetMousePosition(), layerRect)) {
            canvas->currentLayer = i;
        }
        
        // Visibility toggle
        Rectangle visRect = { bounds.x + 120, bounds.y + 10 + i * 30, 25, 25 };
        DrawRectangleRec(visRect, canvas->layers[i].visible ? GREEN : RED);
        DrawRectangleLinesEx(visRect, 1, BLACK);
        DrawText(canvas->layers[i].visible ? "V" : "H", visRect.x + 7, visRect.y + 5, 10, BLACK);
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && 
            CheckCollisionPointRec(GetMousePosition(), visRect)) {
            canvas->layers[i].visible = !canvas->layers[i].visible;
        }
    }
    
    // Layer controls
    const char* controlNames[] = { "Clear Layer", "Merge Down" };
    
    for (int i = 0; i < 2; i++) {
        Rectangle controlRect = { bounds.x + 10, bounds.y + 180 + i * 30, 120, 25 };
        DrawRectangleRec(controlRect, LIGHTGRAY);
        DrawRectangleLinesEx(controlRect, 1, BLACK);
        DrawText(controlNames[i], controlRect.x + 10, controlRect.y + 5, 10, BLACK);
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && 
            CheckCollisionPointRec(GetMousePosition(), controlRect)) {
            if (i == 0) {
                // Clear current layer
                AddUndo(canvas);
                canvas->layers[canvas->currentLayer].polygonCount = 0;
            } else if (i == 1 && canvas->currentLayer > 0) {
                // Merge down
                AddUndo(canvas);
                Layer* currentLayer = &canvas->layers[canvas->currentLayer];
                Layer* lowerLayer = &canvas->layers[canvas->currentLayer - 1];
                
                // Copy polygons from current layer to lower layer
                for (int p = 0; p < currentLayer->polygonCount; p++) {
                    if (lowerLayer->polygonCount < MAX_POLYGONS) {
                        lowerLayer->polygons[lowerLayer->polygonCount] = currentLayer->polygons[p];
                        lowerLayer->polygonCount++;
                    }
                }
                
                // Clear current layer
                currentLayer->polygonCount = 0;
            }
        }
    }
    
    // Undo/Redo buttons
    Rectangle undoRect = { bounds.x + 10, bounds.y + 250, 50, 25 };
    Rectangle redoRect = { bounds.x + 70, bounds.y + 250, 50, 25 };
    
    DrawRectangleRec(undoRect, LIGHTGRAY);
    DrawRectangleLinesEx(undoRect, 1, BLACK);
    DrawText("Undo", undoRect.x + 10, undoRect.y + 5, 10, BLACK);
    
    DrawRectangleRec(redoRect, LIGHTGRAY);
    DrawRectangleLinesEx(redoRect, 1, BLACK);
    DrawText("Redo", redoRect.x + 10, redoRect.y + 5, 10, BLACK);
    
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointRec(GetMousePosition(), undoRect)) {
            Undo(canvas);
        } else if (CheckCollisionPointRec(GetMousePosition(), redoRect)) {
            Redo(canvas);
        }
    }
}

void DrawCanvas(Rectangle bounds, Canvas* canvas)
{
    // Draw all visible layers
    for (int l = 0; l < MAX_LAYERS; l++) {
        if (!canvas->layers[l].visible) continue;
        
        Layer* layer = &canvas->layers[l];
        
        for (int i = 0; i < layer->polygonCount; i++) {
            Polygon* poly = &layer->polygons[i];
            
            // Draw based on point count
            if (poly->pointCount == 1) {
                // Single point (brush stroke)
                DrawCircleV(poly->points[0], poly->thickness / 2, poly->color);
            } else if (poly->pointCount == 2) {
                // Line
                DrawLineEx(poly->points[0], poly->points[1], poly->thickness, poly->color);
            } else if (poly->pointCount >= 3) {
                if (poly->style == STYLE_NORMAL) {
                    // Regular polygon
                    if (poly->filled) {
                        // Draw filled polygon
                        Vector2* points = (Vector2*)malloc(poly->pointCount * sizeof(Vector2));
                        for (int j = 0; j < poly->pointCount; j++) {
                            points[j] = poly->points[j];
                        }
                        DrawTriangleFan(points, poly->pointCount, poly->color);
                        free(points);
                    } else {
                        // Draw polygon outline
                        for (int j = 0; j < poly->pointCount; j++) {
                            int nextIdx = (j + 1) % poly->pointCount;
                            DrawLineEx(poly->points[j], poly->points[nextIdx], poly->thickness, poly->color);
                        }
                    }
                } else if (poly->style == STYLE_ABSTRACT) {
                    // Abstract style with color variations
                    for (int j = 0; j < poly->pointCount; j++) {
                        int nextIdx = (j + 1) % poly->pointCount;
                        Color lineColor = GetRandomColorVariation(poly->color, 30);
                        DrawLineEx(poly->points[j], poly->points[nextIdx], poly->thickness, lineColor);
                        
                        // Draw additional strokes for abstract effect
                        Vector2 midPoint = {
                            (poly->points[j].x + poly->points[nextIdx].x) / 2,
                            (poly->points[j].y + poly->points[nextIdx].y) / 2
                        };
                        
                        float angle = atan2f(poly->points[nextIdx].y - poly->points[j].y, 
                                            poly->points[nextIdx].x - poly->points[j].x);
                        
                        Vector2 perpPoint = {
                            midPoint.x + sinf(angle) * (10 + rand() % 20),
                            midPoint.y - cosf(angle) * (10 + rand() % 20)
                        };
                        
                        DrawLineEx(midPoint, perpPoint, poly->thickness / 2, lineColor);
                    }
                } else if (poly->style == STYLE_GEOMETRIC) {
                    // Geometric style with simplified shapes
                    // Find center of polygon
                    Vector2 center = {0};
                    for (int j = 0; j < poly->pointCount; j++) {
                        center.x += poly->points[j].x;
                        center.y += poly->points[j].y;
                    }
                    center.x /= poly->pointCount;
                    center.y /= poly->pointCount;
                    
                    // Draw simplified geometric shape
                    if (poly->pointCount <= 4) {
                        // For triangles and quads, just draw the shape
                        Vector2* points = (Vector2*)malloc(poly->pointCount * sizeof(Vector2));
                        for (int j = 0; j < poly->pointCount; j++) {
                            points[j] = poly->points[j];
                        }
                        DrawTriangleFan(points, poly->pointCount, poly->color);
                        free(points);
                    } else {
                        // For more complex shapes, simplify to a regular polygon
                        float radius = 0;
                        for (int j = 0; j < poly->pointCount; j++) {
                            float dist = Vector2Distance(center, poly->points[j]);
                            radius += dist;
                        }
                        radius /= poly->pointCount;
                        
                        int sides = fmin(poly->pointCount, 8); // Simplify to max 8 sides
                        for (int j = 0; j < sides; j++) {
                            float angle1 = j * 2 * PI / sides;
                            float angle2 = (j + 1) * 2 * PI / sides;
                            
                            Vector2 p1 = {
                                center.x + cosf(angle1) * radius,
                                center.y + sinf(angle1) * radius
                            };
                            
                            Vector2 p2 = {
                                center.x + cosf(angle2) * radius,
                                center.y + sinf(angle2) * radius
                            };
                            
                            DrawLineEx(p1, p2, poly->thickness, poly->color);
                        }
                        
                        if (poly->filled) {
                            DrawCircleV(center, radius * 0.8f, ColorAlpha(poly->color, 0.5f));
                        }
                    }
                }
            }
        }
    }
}

void HandleInput(Rectangle canvasBounds, Canvas* canvas, Tool currentTool, Color selectedColor, float brushSize, PaintStyle style)
{
    static bool isDrawing = false;
    static Polygon currentPolygon = {0};
    static Vector2 lastPoint = {0};
    
    Layer* currentLayer = &canvas->layers[canvas->currentLayer];
    Vector2 mousePos = GetMousePosition();
    
    // Check if mouse is inside canvas
    bool mouseInCanvas = CheckCollisionPointRec(mousePos, canvasBounds);
    
    // Handle mouse button press
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && mouseInCanvas) {
        if (currentTool == TOOL_EYEDROPPER) {
            // Sample color from canvas
            // This is a simplified approach - actual implementation would need to read pixel colors
            for (int l = MAX_LAYERS - 1; l >= 0; l--) {
                if (!canvas->layers[l].visible) continue;
                
                Layer* layer = &canvas->layers[l];
                for (int i = layer->polygonCount - 1; i >= 0; i--) {
                    Polygon* poly = &layer->polygons[i];
                    
                    // Check if point is within a brush stroke
                    if (poly->pointCount == 1 && 
                        Vector2Distance(mousePos, poly->points[0]) <= poly->thickness / 2) {
                        selectedColor = poly->color;
                        return;
                    }
                    
                    // Check if point is on a line
                    if (poly->pointCount == 2) {
                        // Simple line segment proximity check
                        Vector2 a = poly->points[0];
                        Vector2 b = poly->points[1];
                        Vector2 p = mousePos;
                        
                        Vector2 ab = { b.x - a.x, b.y - a.y };
                        Vector2 ap = { p.x - a.x, p.y - a.y };
                        
                        float lenSq = ab.x * ab.x + ab.y * ab.y;
                        float dot = ab.x * ap.x + ab.y * ap.y;
                        float t = Clamp(dot / lenSq, 0.0f, 1.0f);
                        
                        Vector2 closest = {
                            a.x + ab.x * t,
                            a.y + ab.y * t
                        };
                        
                        if (Vector2Distance(p, closest) <= poly->thickness / 2) {
                            selectedColor = poly->color;
                            return;
                        }
                    }
                }
            }
        } else {
            // Start drawing
            isDrawing = true;
            
            // Add undo state
            AddUndo(canvas);
            
            // Initialize current polygon
            currentPolygon.pointCount = 0;
            currentPolygon.color = selectedColor;
            currentPolygon.thickness = brushSize;
            currentPolygon.filled = false;
            currentPolygon.style = style;
            
            // Add first point
            currentPolygon.points[currentPolygon.pointCount++] = mousePos;
            lastPoint = mousePos;
            
            // For brush tool, immediately add the point to the canvas
            if (currentTool == TOOL_BRUSH || currentTool == TOOL_ERASER) {
                if (currentLayer->polygonCount < MAX_POLYGONS) {
                    // For eraser, override color to white/background
                    if (currentTool == TOOL_ERASER) {
                        currentPolygon.color = WHITE;
                    }
                    
                    currentLayer->polygons[currentLayer->polygonCount] = currentPolygon;
                    currentLayer->polygonCount++;
                }
            }
        }
    }
    
    // Handle mouse movement while drawing
    if (isDrawing && mouseInCanvas) {
        if (currentTool == TOOL_BRUSH || currentTool == TOOL_ERASER) {
            // For brush, create new points as we move
            if (Vector2Distance(mousePos, lastPoint) >= brushSize / 4) {
                // Initialize new polygon for this segment
                Polygon newPoint = {0};
                newPoint.pointCount = 1;
                newPoint.points[0] = mousePos;
                newPoint.color = currentTool == TOOL_ERASER ? WHITE : selectedColor;
                newPoint.thickness = brushSize;
                newPoint.filled = true;
                newPoint.style = style;
                
                // Add to canvas
                if (currentLayer->polygonCount < MAX_POLYGONS) {
                    currentLayer->polygons[currentLayer->polygonCount] = newPoint;
                    currentLayer->polygonCount++;
                }
                
                // Also add a line connecting to previous point for smooth strokes
                if (Vector2Distance(mousePos, lastPoint) <= brushSize * 2) {
                    Polygon line = {0};
                    line.pointCount = 2;
                    line.points[0] = lastPoint;
                    line.points[1] = mousePos;
                    line.color = currentTool == TOOL_ERASER ? WHITE : selectedColor;
                    line.thickness = brushSize;
                    line.style = style;
                    
                    if (currentLayer->polygonCount < MAX_POLYGONS) {
                        currentLayer->polygons[currentLayer->polygonCount] = line;
                        currentLayer->polygonCount++;
                    }
                }
                
                lastPoint = mousePos;
            }
        } else if (currentTool == TOOL_LINE) {
            // For line tool, update the second point
            if (currentPolygon.pointCount == 1) {
                currentPolygon.pointCount = 2;
            }
            currentPolygon.points[1] = mousePos;
        } else if (currentTool == TOOL_RECT) {
            // For rectangle tool, update the points to form a rectangle
            if (currentPolygon.pointCount == 1) {
                currentPolygon.pointCount = 4; // A rectangle has 4 points
            }
            
            float x1 = currentPolygon.points[0].x;
            float y1 = currentPolygon.points[0].y;
            float x2 = mousePos.x;
            float y2 = mousePos.y;
            
            currentPolygon.points[0] = (Vector2){ x1, y1 };
            currentPolygon.points[1] = (Vector2){ x2, y1 };
            currentPolygon.points[2] = (Vector2){ x2, y2 };
            currentPolygon.points[3] = (Vector2){ x1, y2 };
        } else if (currentTool == TOOL_POLYGON) {
            // For polygon tool, we add points on clicks (handled below)
        }
    }
    
    // Handle mouse button release
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && isDrawing) {
        if (currentTool == TOOL_LINE || currentTool == TOOL_RECT) {
            // Finalize polygon and add to canvas
            if (currentLayer->polygonCount < MAX_POLYGONS) {
                // Apply style effects
                if (style == STYLE_ABSTRACT) {
                    ApplyAbstractStyle(&currentPolygon);
                } else if (style == STYLE_GEOMETRIC) {
                    ApplyGeometricStyle(&currentPolygon);
                }
                
                currentLayer->polygons[currentLayer->polygonCount] = currentPolygon;
                currentLayer->polygonCount++;
            }
            isDrawing = false;
        } else if (currentTool == TOOL_POLYGON) {
            // For polygon tool, keep adding points on each click
            if (currentPolygon.pointCount < MAX_POINTS) {
                currentPolygon.points[currentPolygon.pointCount++] = mousePos;
            }
            
            // Right-click to finish the polygon
            if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
                if (currentPolygon.pointCount >= 3) {
                    if (currentLayer->polygonCount < MAX_POLYGONS) {
                        // Apply style effects
                        if (style == STYLE_ABSTRACT) {
                            ApplyAbstractStyle(&currentPolygon);
                        } else if (style == STYLE_GEOMETRIC) {
                            ApplyGeometricStyle(&currentPolygon);
                        }
                        
                        currentLayer->polygons[currentLayer->polygonCount] = currentPolygon;
                        currentLayer->polygonCount++;
                    }
                }
                isDrawing = false;
            }
        } else {
            isDrawing = false;
        }
    }
    
    // Toggle polygon fill with F key
    if (IsKeyPressed(KEY_F) && isDrawing && 
        (currentTool == TOOL_POLYGON || currentTool == TOOL_RECT)) {
        currentPolygon.filled = !currentPolygon.filled;
    }
}

void AddUndo(Canvas* canvas)
{
    // Shift undo buffer to make room for a new state
    for (int i = MAX_UNDO - 1; i > 0; i--) {
        canvas->undoBuffer[i] = canvas->undoBuffer[i - 1];
    }
    
    // Copy current canvas to undo buffer
    for (int l = 0; l < MAX_LAYERS; l++) {
        canvas->undoBuffer[0].layers[l] = canvas->layers[l];
    }
    
    canvas->undoCount++;
    canvas->redoCount = 0;
}

void Undo(Canvas* canvas)
{
    if (canvas->undoCount <= 0) return;
    
    // Save current state for redo
    Layer redoState[MAX_LAYERS];
    for (int l = 0; l < MAX_LAYERS; l++) {
        redoState[l] = canvas->layers[l];
    }
    
    // Restore from undo buffer
    for (int l = 0; l < MAX_LAYERS; l++) {
        canvas->layers[l] = canvas->undoBuffer[0].layers[l];
    }
    
    // Shift undo buffer
    for (int i = 0; i < MAX_UNDO - 1; i++) {
        canvas->undoBuffer[i] = canvas->undoBuffer[i + 1];
    }
    
    // Add to redo buffer
    for (int i = MAX_UNDO - 1; i > 0; i--) {
        canvas->undoBuffer[i] = canvas->undoBuffer[i - 1];
    }
    canvas->undoBuffer[0].layers[0].polygonCount = 0; // Clear the slot we just used
    
    canvas->undoCount--;
    canvas->redoCount++;
}
