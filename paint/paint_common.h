#ifndef PAINT_COMMON_H
#define PAINT_COMMON_H

#include "raylib.h"

#define MAX_BRUSH_SIZE 50
#define MIN_BRUSH_SIZE 1
#define MAX_LAYERS 5
#define MAX_UNDO 20
#define MAX_POLYGONS 1000
#define MAX_POINTS 10

// Utility functions
static inline float Clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static inline float Vector2Distance(Vector2 v1, Vector2 v2) {
    return sqrtf((v1.x - v2.x)*(v1.x - v2.x) + (v1.y - v2.y)*(v1.y - v2.y));
}

typedef enum {
    TOOL_BRUSH,
    TOOL_POLYGON,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_ERASER,
    TOOL_EYEDROPPER
} Tool;

typedef enum {
    STYLE_NORMAL,
    STYLE_ABSTRACT,
    STYLE_GEOMETRIC
} PaintStyle;

typedef struct {
    Vector2 points[MAX_POINTS];
    int pointCount;
    Color color;
    float thickness;
    bool filled;
    PaintStyle style;
} Polygon;

typedef struct {
    Polygon polygons[MAX_POLYGONS];
    int polygonCount;
    bool visible;
} Layer;

typedef struct {
    Layer layers[MAX_LAYERS];
    int currentLayer;
    Layer* undoBuffer[MAX_UNDO];  // Changed to pointers for easier copying
    int undoCount;
    int redoCount;
} Canvas;

// Function prototypes for main.c
void DrawColorPalette(Rectangle bounds, Color* selectedColor);
void DrawToolbar(Rectangle bounds, Tool* currentTool, float* brushSize, PaintStyle* style);
void DrawLayerPanel(Rectangle bounds, Canvas* canvas);
void DrawCanvas(Rectangle bounds, Canvas* canvas);
void HandleInput(Rectangle canvasBounds, Canvas* canvas, Tool currentTool, Color selectedColor, float brushSize, PaintStyle style);
void SaveCanvas(Canvas* canvas, const char* filename);
void LoadCanvas(Canvas* canvas, const char* filename);
void AddUndo(Canvas* canvas);
void Undo(Canvas* canvas);
void Redo(Canvas* canvas);
Color GetRandomColorVariation(Color baseColor, int range);
void ApplyAbstractStyle(Polygon* polygon);
void ApplyGeometricStyle(Polygon* polygon);

// Function prototypes from export_utils.c
typedef struct {
    Color* data;
    int width;
    int height;
} StylizedImage;

StylizedImage* CreateStylizedImage(Canvas* canvas, int width, int height, bool applyEffects);
void ApplyStylizationEffects(StylizedImage* img);
void ExportStylizedImage(StylizedImage* img, const char* filename);
void FreeStylizedImage(StylizedImage* img);
void AddExportButton(Rectangle bounds, Canvas* canvas);

#endif // PAINT_COMMON_H
