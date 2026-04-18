#include "raylib.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "paint_common.h"

// This file contains utilities for exporting stylized images
// It works with the main paint program

// Export the current canvas to a stylized image
StylizedImage* CreateStylizedImage(Canvas* canvas, int width, int height, bool applyEffects)
{
    StylizedImage* img = (StylizedImage*)malloc(sizeof(StylizedImage));
    img->width = width;
    img->height = height;
    img->data = (Color*)malloc(width * height * sizeof(Color));
    
    // Fill with white background
    for (int i = 0; i < width * height; i++) {
        img->data[i] = WHITE;
    }
    
    // Create a RenderTexture2D to draw the canvas
    RenderTexture2D target = LoadRenderTexture(width, height);
    
    // Draw the canvas to the render texture
    BeginTextureMode(target);
        ClearBackground(WHITE);
        
        // Scale factor to fit the canvas to the image
        Rectangle canvasBounds = { 220, 10, GetScreenWidth() - 230, GetScreenHeight() - 20 };
        float scaleX = (float)width / canvasBounds.width;
        float scaleY = (float)height / canvasBounds.height;
        
        // Draw each layer
        for (int l = 0; l < MAX_LAYERS; l++) {
            if (!canvas->layers[l].visible) continue;
            
            Layer* layer = &canvas->layers[l];
            
            for (int i = 0; i < layer->polygonCount; i++) {
                Polygon* poly = &layer->polygons[i];
                
                // Scale points to fit the image
                Vector2 scaledPoints[MAX_POINTS];
                for (int j = 0; j < poly->pointCount; j++) {
                    scaledPoints[j].x = (poly->points[j].x - canvasBounds.x) * scaleX;
                    scaledPoints[j].y = (poly->points[j].y - canvasBounds.y) * scaleY;
                }
                
                // Draw based on point count
                if (poly->pointCount == 1) {
                    // Single point (brush stroke)
                    DrawCircleV(scaledPoints[0], poly->thickness / 2 * scaleX, poly->color);
                } else if (poly->pointCount == 2) {
                    // Line
                    DrawLineEx(scaledPoints[0], scaledPoints[1], poly->thickness * scaleX, poly->color);
                } else if (poly->pointCount >= 3) {
                    if (poly->filled) {
                        // Draw filled polygon
                        DrawTriangleFan(scaledPoints, poly->pointCount, poly->color);
                    } else {
                        // Draw polygon outline
                        for (int j = 0; j < poly->pointCount; j++) {
                            int nextIdx = (j + 1) % poly->pointCount;
                            DrawLineEx(scaledPoints[j], scaledPoints[nextIdx], poly->thickness * scaleX, poly->color);
                        }
                    }
                }
            }
        }
    EndTextureMode();
    
    // Get image data from render texture
    Image tempImage = GetTextureData(target.texture);
    ImageFlipVertical(&tempImage); // Flip because RenderTextures are inverted
    
    // Copy image data to our stylized image
    Color* pixels = GetImageData(tempImage);
    for (int i = 0; i < width * height; i++) {
        img->data[i] = pixels[i];
    }
    
    // Apply stylization effects if requested
    if (applyEffects) {
        ApplyStylizationEffects(img);
    }
    
    // Clean up
    free(pixels);
    UnloadImage(tempImage);
    UnloadRenderTexture(target);
    
    return img;
}

// Apply various stylization effects to the image
void ApplyStylizationEffects(StylizedImage* img)
{
    // Create a copy of the image for processing
    Color* original = (Color*)malloc(img->width * img->height * sizeof(Color));
    memcpy(original, img->data, img->width * img->height * sizeof(Color));
    
    // 1. Apply subtle edge detection for line emphasis
    for (int y = 1; y < img->height - 1; y++) {
        for (int x = 1; x < img->width - 1; x++) {
            int idx = y * img->width + x;
            Color c = original[idx];
            Color up = original[(y - 1) * img->width + x];
            Color down = original[(y + 1) * img->width + x];
            Color left = original[y * img->width + (x - 1)];
            Color right = original[y * img->width + (x + 1)];
            
            // Simple edge detection
            int edgeIntensity = abs((int)c.r - (int)up.r) + 
                               abs((int)c.r - (int)down.r) + 
                               abs((int)c.r - (int)left.r) + 
                               abs((int)c.r - (int)right.r);
            
            edgeIntensity = (edgeIntensity > 100) ? 255 : 0;
            
            // If this is an edge, darken it slightly
            if (edgeIntensity > 0) {
                img->data[idx].r = (unsigned char)fmax(0, c.r - 40);
                img->data[idx].g = (unsigned char)fmax(0, c.g - 40);
                img->data[idx].b = (unsigned char)fmax(0, c.b - 40);
            }
        }
    }
    
    // 2. Add subtle noise texture for a more natural look
    for (int i = 0; i < img->width * img->height; i++) {
        if (rand() % 100 < 20) { // 20% chance for each pixel
            int noise = (rand() % 30) - 15; // -15 to +15 variation
            
            img->data[i].r = (unsigned char)Clamp(img->data[i].r + noise, 0, 255);
            img->data[i].g = (unsigned char)Clamp(img->data[i].g + noise, 0, 255);
            img->data[i].b = (unsigned char)Clamp(img->data[i].b + noise, 0, 255);
        }
    }
    
    // 3. Apply soft color quantization for a more stylized look
    for (int i = 0; i < img->width * img->height; i++) {
        // Quantize colors to create a more limited palette
        img->data[i].r = (img->data[i].r / 20) * 20;
        img->data[i].g = (img->data[i].g / 20) * 20;
        img->data[i].b = (img->data[i].b / 20) * 20;
    }
    
    // Clean up
    free(original);
}

// Export the stylized image to a PNG file
void ExportStylizedImage(StylizedImage* img, const char* filename)
{
    if (!img || !img->data) return;
    
    // Convert our image data to raylib Image format
    Image output = {
        .data = img->data,
        .width = img->width,
        .height = img->height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    
    // Export to PNG
    ExportImage(output, filename);
    
    // Note: We don't unload the image because we're using our own data buffer
}

// Free the stylized image memory
void FreeStylizedImage(StylizedImage* img)
{
    if (!img) return;
    
    if (img->data) {
        free(img->data);
    }
    
    free(img);
}

// Add export button to the menu
void AddExportButton(Rectangle bounds, Canvas* canvas)
{
    DrawRectangleRec(bounds, LIGHTGRAY);
    DrawRectangleLinesEx(bounds, 1, BLACK);
    DrawText("Export PNG", bounds.x + 10, bounds.y + 5, 10, BLACK);
    
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && 
        CheckCollisionPointRec(GetMousePosition(), bounds)) {
        // Create a 1920x1080 stylized image
        StylizedImage* img = CreateStylizedImage(canvas, 1920, 1080, true);
        
        // Export to PNG
        ExportStylizedImage(img, "stylized_painting.png");
        
        // Clean up
        FreeStylizedImage(img);
    }
}

// Add this to the main program's UI
// In the main function, add these lines:
// Rectangle exportBounds = { 10, screenHeight - 40, 120, 30 };
// AddExportButton(exportBounds, &canvas);
