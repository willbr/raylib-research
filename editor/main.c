#include "raylib.h"
#include "raymath.h"
#include "../common/map3d.h"
#include "../common/objects3d.h"
#include "../common/sprites2d.h"
#include <stdio.h>
#include "rlgl.h"
#include <string.h>

// Simple 3D map + object editor
// - Paint tiles with mouse
// - Place/move objects
// - Orbit camera
// - Export map as C code

#define EDITOR_MAP_W 20
#define EDITOR_MAP_H 20
#define TILE_SZ      2.0f
#define MAX_PLACED   64

typedef enum { GIZMO_NONE, GIZMO_MOVE, GIZMO_ROTATE, GIZMO_SCALE } GizmoMode;

typedef struct {
    Vector3 pos;
    float rotY;
    Vector3 scale;  // per-axis scale (x, y, z)
    int prefabIdx;
    bool active;
} PlacedObject;

// Attached sprite: a 2D sprite pinned to a 3D object
#define MAX_ATTACHED_SPRITES 8
typedef struct {
    Vector3 offset;       // position relative to object origin
    float scale;
    char filename[32];
    Sprite2DPart parts[32];
    int partCount;
    SpriteDisplayMode displayMode;
} AttachedSprite;

// Prefab definitions
typedef struct {
    const char *name;
    Part *parts;
    int partCount;
    AttachedSprite sprites[MAX_ATTACHED_SPRITES];
    int spriteCount;
} PrefabDef;

// Prefab parts storage
static Part humanParts[] = PREFAB_HUMAN(COL(220,190,150,255), COL(0,121,241,255), COL(50,50,60,255));
static Part carParts[] = PREFAB_CAR(COL(230,41,55,255));
static Part treeParts[] = PREFAB_TREE;
static Part crateParts[] = PREFAB_CRATE;
static Part barrelParts[] = PREFAB_BARREL(COL(100,70,40,255));
static Part lampParts[] = PREFAB_LAMPPOST;
static Part bushParts[] = PREFAB_BUSH;

#define MAX_PREFABS 20
#define MAX_CUSTOM_PARTS 128  // storage for all custom object parts

static Part customPartsPool[MAX_CUSTOM_PARTS];
static int customPartsUsed = 0;
static char customNames[MAX_PREFABS][32];

static PrefabDef prefabs[MAX_PREFABS];
static int numPrefabs = 7;
static const char *builtinNames[] = { "Human", "Car", "Tree", "Crate", "Barrel", "Lamppost", "Bush" };

// Editor state
static Map3D map;
static PlacedObject placed[MAX_PLACED];
static int numPlaced = 0;

// Placed 2D sprites as billboards in the 3D scene
#define MAX_PLACED_SPRITES 32
static PlacedSprite2D placedSprites[MAX_PLACED_SPRITES];
static int numPlacedSprites = 0;
static int selectedSprite = -1;

// Loaded sprite files for the sprite palette
#define MAX_SPRITE_FILES 10
static Sprite2DPart spriteParts[MAX_SPRITE_FILES][32];
static int spritePartCounts[MAX_SPRITE_FILES];
static char spriteNames[MAX_SPRITE_FILES][32];
static int numSpriteFiles = 0;
static int selectedSpriteFile = -1;
static bool placingSprite = false;  // true = Shift+click places sprite, false = places 3D object

void LoadSpriteFiles(void) {
    // Try loading .spr2d files from objects/ directory
    numSpriteFiles = 0;
    FilePathList files = LoadDirectoryFiles("objects");
    for (int i = 0; i < (int)files.count && numSpriteFiles < MAX_SPRITE_FILES; i++) {
        const char *path = files.paths[i];
        if (IsFileExtension(path, ".spr2d")) {
            int count = LoadSprite2D(path, spriteParts[numSpriteFiles], 32);
            if (count > 0) {
                spritePartCounts[numSpriteFiles] = count;
                const char *name = GetFileNameWithoutExt(path);
                strncpy(spriteNames[numSpriteFiles], name, 31);
                numSpriteFiles++;
            }
        }
    }
    UnloadDirectoryFiles(files);
}

static TileDef tileDefs[] = {
    TILEDEF_EMPTY,
    TILEDEF_FLOOR(((Color){100,180,80,255})),           // 1: grass
    TILEDEF_FLOOR(((Color){170,170,175,255})),           // 2: concrete
    TILEDEF_WALL(2.0f, ((Color){80,80,90,255})),         // 3: low wall
    TILEDEF_WALL(4.0f, ((Color){70,60,50,255})),         // 4: tall wall
    TILEDEF_RAMP_N(2.0f, ((Color){200,180,140,255})),    // 5: ramp N
    TILEDEF_RAMP_S(2.0f, ((Color){200,180,140,255})),    // 6: ramp S
    TILEDEF_RAMP_E(2.0f, ((Color){200,180,140,255})),    // 7: ramp E
    TILEDEF_RAMP_W(2.0f, ((Color){200,180,140,255})),    // 8: ramp W
    TILEDEF_WATER(((Color){40,120,200,255})),             // 9: water
    TILEDEF_PIT,                                          // a: pit
    TILEDEF_PLATFORM(1.0f, ((Color){150,140,130,255})),   // b: platform
    TILEDEF_FLOOR(((Color){140,110,70,255})),             // c: dirt
    TILEDEF_FLOOR(((Color){180,160,130,255})),             // d: sand
};
#define NUM_TILEDEFS (sizeof(tileDefs)/sizeof(tileDefs[0]))

static const char *tileNames[] = {
    "Empty", "Grass", "Concrete", "Wall Low", "Wall Tall",
    "Ramp N", "Ramp S", "Ramp E", "Ramp W", "Water",
    "Pit", "Platform", "Dirt", "Sand"
};

// Editor modes
typedef enum { MODE_TILES, MODE_OBJECTS, MODE_BUILD, MODE_BUILD2D } EditorMode;

// Build mode: compose a new object from primitives
#define MAX_BUILD_PARTS 16
typedef struct {
    Part parts[MAX_BUILD_PARTS];
    int count;
    int selected;  // which part is selected (-1 = none)
    char name[32];
    int editingPrefab;  // index of prefab being edited, -1 if new
    AttachedSprite sprites[MAX_ATTACHED_SPRITES];
    int spriteCount;
    int selectedSprite;  // which attached sprite is selected (-1 = none)
} BuildObject;
static BuildObject buildObj = { .count = 0, .selected = -1, .name = "Custom", .editingPrefab = -1, .spriteCount = 0, .selectedSprite = -1 };
static int buildPrimitive = 0;  // 0=cube, 1=sphere, 2=cylinder

// 2D build mode
#define MAX_BUILD2D_PARTS 32
typedef struct {
    Sprite2DPart parts[MAX_BUILD2D_PARTS];
    int count;
    int selected;
    char name[32];
} Build2D;
static Build2D build2d = { .count = 0, .selected = -1, .name = "Sprite" };
static int build2dPrimitive = 0;  // 0=rect, 1=circle, 2=triangle, 3=line, 4=ellipse, 5=polygon
// Freeform polygon drawing state
#define MAX_POLY_VERTS 16
static Vector2 polyVerts[MAX_POLY_VERTS];
static int polyVertCount = 0;
static bool polyDrawing = false;
static float lastLineWidth = 2.0f;  // remembered line thickness
static Color selectedColor = {230, 41, 55, 255};  // current drawing color (default red)
static bool drawing2d = false;     // currently dragging to create a shape
static Vector2 drawStart = {0};    // where the drag started (screen coords)
static int triVertex = 0;          // 0-2: which triangle vertex we're placing next
static Vector3 buildCursor = {0, 0.5f, 0};  // where new parts spawn

// Text input state for naming
static bool textEditing = false;
static char *textTarget = NULL;     // pointer to the char[] being edited
static int textMaxLen = 0;
static int textCursor = 0;
static char textBackup[32] = {0};   // restore on escape

static void StartTextEdit(char *buf, int maxLen) {
    textEditing = true;
    textTarget = buf;
    textMaxLen = maxLen;
    textCursor = (int)strlen(buf);
    strncpy(textBackup, buf, 31);
}

static void StopTextEdit(bool accept) {
    if (!accept && textTarget) strncpy(textTarget, textBackup, textMaxLen - 1);
    // If renaming a prefab, rename the file on disk
    if (accept && textTarget) {
        for (int i = 0; i < MAX_PREFABS; i++) {
            if (textTarget == customNames[i] && strcmp(textBackup, customNames[i]) != 0) {
                char oldPath[64], newPath[64];
                snprintf(oldPath, sizeof(oldPath), "objects/%s.obj3d", textBackup);
                for (char *c = oldPath + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
                snprintf(newPath, sizeof(newPath), "objects/%s.obj3d", customNames[i]);
                for (char *c = newPath + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
                if (FileExists(oldPath)) rename(oldPath, newPath);
                break;
            }
        }
    }
    textEditing = false;
    textTarget = NULL;
}

// Returns true if text input consumed input this frame
static bool UpdateTextEdit(void) {
    if (!textEditing || !textTarget) return false;
    if (IsKeyPressed(KEY_ENTER)) { StopTextEdit(true); return true; }
    if (IsKeyPressed(KEY_ESCAPE)) { StopTextEdit(false); return true; }
    if (IsKeyPressed(KEY_BACKSPACE) && textCursor > 0) {
        for (int i = textCursor - 1; textTarget[i]; i++) textTarget[i] = textTarget[i+1];
        textCursor--;
    }
    if (IsKeyPressed(KEY_LEFT) && textCursor > 0) textCursor--;
    if (IsKeyPressed(KEY_RIGHT) && textCursor < (int)strlen(textTarget)) textCursor++;
    // Character input
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        int len = (int)strlen(textTarget);
        if (len < textMaxLen - 1 && ch >= 32 && ch < 127 && ch != ' ') {
            for (int i = len + 1; i > textCursor; i--) textTarget[i] = textTarget[i-1];
            textTarget[textCursor] = (char)ch;
            textCursor++;
        }
    }
    return true;
}

static EditorMode mode = MODE_TILES;
static int selectedTile = 1;
static int selectedPrefab = 0;
static int selectedObject = -1;
static GizmoMode gizmoMode = GIZMO_MOVE;
static int draggingAxis = -1;

// Compute screen-space gizmo endpoints for a world-space center.
// Returns arm length in pixels and fills axisEnds[3] with screen positions.
static float GizmoScreenEnds(Vector3 center, Camera3D cam, Vector2 *axisEnds) {
    int sw2 = GetScreenWidth(), sh2 = GetScreenHeight();
    float armLen = (float)(sw2 < sh2 ? sw2 : sh2) * 0.08f;
    Vector2 sc = GetWorldToScreen(center, cam);
    Vector3 dirs[3] = {{1,0,0},{0,1,0},{0,0,1}};
    for (int i = 0; i < 3; i++) {
        Vector2 wp = GetWorldToScreen((Vector3){center.x+dirs[i].x, center.y+dirs[i].y, center.z+dirs[i].z}, cam);
        Vector2 d = Vector2Subtract(wp, sc);
        float len = Vector2Length(d);
        if (len > 0) d = Vector2Scale(d, armLen / len);
        axisEnds[i] = Vector2Add(sc, d);
    }
    return armLen;
}
// Determine which rotation ring axis (0=X, 1=Y, 2=Z) the mouse is closest to.
// Returns the axis index, or -1 if not close enough. ringR is the screen-space ring radius.
static int RotateGizmoHitAxis(Vector2 mouse, Vector3 center, Camera3D cam, float ringR) {
    Vector2 sc = GetWorldToScreen(center, cam);
    float mouseDist = Vector2Distance(mouse, sc);
    if (mouseDist > ringR * 1.4f) return -1;  // too far from any ring
    int segs = 16;
    float bestDist = 1e9f;
    int bestAxis = -1;
    for (int ax = 0; ax < 3; ax++) {
        for (int s = 0; s < segs; s++) {
            float a = (float)s / segs * 2.0f * PI;
            Vector3 w = center;
            if (ax == 0) { w.y += cosf(a); w.z += sinf(a); }
            else if (ax == 1) { w.x += cosf(a); w.z += sinf(a); }
            else { w.x += cosf(a); w.y += sinf(a); }
            Vector2 sp = GetWorldToScreen(w, cam);
            Vector2 d = Vector2Subtract(sp, sc);
            float len = Vector2Length(d);
            if (len > 0) d = Vector2Scale(d, ringR / len);
            Vector2 ringPt = Vector2Add(sc, d);
            float dist = Vector2Distance(mouse, ringPt);
            if (dist < bestDist) { bestDist = dist; bestAxis = ax; }
        }
    }
    float hitThresh = ringR * 0.25f;
    if (hitThresh < 12) hitThresh = 12;
    return (bestDist < hitThresh) ? bestAxis : -1;
}

static bool showExport = false;
static bool showGrid = true;
static bool showMinimap = true;
static bool showModeMenu = false;
static Vector2 modeMenuPos = {0};

// Camera modes
typedef enum { CAM_ORBIT, CAM_FPS, CAM_FLY } CamMode;
static CamMode camMode = CAM_ORBIT;

// Camera orbit
static float camYaw = -0.5f;
static float camPitch = 0.8f;
static float camDist = 30.0f;
static Vector3 camFocus = {0};

// FPS / Fly camera
static Vector3 fpsCamPos = {0, 1.7f, 0};
static float fpsCamYaw = 0;
static float fpsCamPitch = 0;
static bool fpsCursorLocked = false;

// Get the vertical center of an object's parts (for gizmo placement)
float PrefabCenterY(Part *parts, int count, Vector3 scale) {
    if (count == 0) return 0.5f;
    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < count; i++) {
        float py = parts[i].offset.y * scale.y;
        float halfH = 0;
        switch (parts[i].type) {
            case PART_CUBE: halfH = parts[i].size.y * scale.y * 0.5f; break;
            case PART_SPHERE: halfH = parts[i].size.x * ((scale.x+scale.y+scale.z)/3.0f); break;
            case PART_CYLINDER: halfH = parts[i].size.y * scale.y * 0.5f; break;
            case PART_CONE: halfH = parts[i].size.y * scale.y * 0.5f; break;
        }
        if (py - halfH < minY) minY = py - halfH;
        if (py + halfH > maxY) maxY = py + halfH;
    }
    return (minY + maxY) / 2.0f;
}

// Raycast ground plane from mouse position
Vector3 MouseToGround(Camera3D camera) {
    Ray ray = GetMouseRay(GetMousePosition(), camera);
    // Intersect with y=0 plane
    if (fabsf(ray.direction.y) < 0.0001f) return (Vector3){0,0,0};
    float t = -ray.position.y / ray.direction.y;
    if (t < 0) return (Vector3){0,0,0};
    return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
}

// Raycast to specific height plane
Vector3 MouseToPlane(Camera3D camera, float planeY) {
    Ray ray = GetMouseRay(GetMousePosition(), camera);
    if (fabsf(ray.direction.y) < 0.0001f) return (Vector3){0,planeY,0};
    float t = (planeY - ray.position.y) / ray.direction.y;
    if (t < 0) return (Vector3){0,planeY,0};
    return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
}

// Save a prefab to a text file in objects/ directory
void SavePrefabFile(int idx) {
    char path[64];
    snprintf(path, sizeof(path), "objects/%s.obj3d", prefabs[idx].name);
    for (char *c = path + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
    SaveObject3D(path, prefabs[idx].parts, prefabs[idx].partCount);
    // Append attached sprites
    if (prefabs[idx].spriteCount > 0) {
        FILE *f = fopen(path, "a");
        if (f) {
            for (int s = 0; s < prefabs[idx].spriteCount; s++) {
                AttachedSprite *as = &prefabs[idx].sprites[s];
                fprintf(f, "sprite %s %.3f %.3f %.3f %.2f %d\n",
                    as->filename, as->offset.x, as->offset.y, as->offset.z,
                    as->scale, (int)as->displayMode);
            }
            fclose(f);
        }
    }
}

// Try to load a prefab from file, returns part count or 0 if not found
int LoadPrefabFile(const char *name, Part *destParts, int maxParts) {
    char path[64];
    snprintf(path, sizeof(path), "objects/%s.obj3d", name);
    for (char *c = path + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
    return LoadObject3D(path, destParts, maxParts);
}

// Load attached sprites from a prefab file
void LoadPrefabSprites(int idx) {
    char path[64];
    snprintf(path, sizeof(path), "objects/%s.obj3d", prefabs[idx].name);
    for (char *c = path + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
    FILE *f = fopen(path, "r");
    if (!f) return;
    prefabs[idx].spriteCount = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char name2[32]; float ox, oy, oz, sc; int dm;
        if (sscanf(line, "sprite %31s %f %f %f %f %d", name2, &ox, &oy, &oz, &sc, &dm) >= 5) {
            if (prefabs[idx].spriteCount < MAX_ATTACHED_SPRITES) {
                AttachedSprite *as = &prefabs[idx].sprites[prefabs[idx].spriteCount];
                as->offset = (Vector3){ox, oy, oz};
                as->scale = sc;
                as->displayMode = (SpriteDisplayMode)dm;
                strncpy(as->filename, name2, 31);
                // Load sprite parts from file
                as->partCount = 0;
                for (int s = 0; s < numSpriteFiles; s++) {
                    if (strcmp(spriteNames[s], name2) == 0) {
                        as->partCount = spritePartCounts[s];
                        for (int p = 0; p < as->partCount; p++)
                            as->parts[p] = spriteParts[s][p];
                        break;
                    }
                }
                if (as->partCount > 0) prefabs[idx].spriteCount++;
            }
        }
    }
    fclose(f);
}

void SavePlacedObjects(void) {
    FILE *f = fopen("editor_placed.txt", "w");
    if (!f) return;
    for (int i = 0; i < numPlaced; i++) {
        if (!placed[i].active) continue;
        fprintf(f, "obj %d %.3f %.3f %.3f %.2f %.3f %.3f %.3f\n",
            placed[i].prefabIdx,
            placed[i].pos.x, placed[i].pos.y, placed[i].pos.z,
            placed[i].rotY,
            placed[i].scale.x, placed[i].scale.y, placed[i].scale.z);
    }
    for (int i = 0; i < numPlacedSprites; i++) {
        if (!placedSprites[i].active) continue;
        fprintf(f, "spr %s %.3f %.3f %.3f %.2f %.2f %d\n",
            placedSprites[i].filename,
            placedSprites[i].pos.x, placedSprites[i].pos.y, placedSprites[i].pos.z,
            placedSprites[i].scale, placedSprites[i].rotY,
            (int)placedSprites[i].displayMode);
    }
    fclose(f);
}

void LoadPlacedObjects(void) {
    FILE *f = fopen("editor_placed.txt", "r");
    if (!f) return;
    char line[256];
    numPlaced = 0;
    numPlacedSprites = 0;
    while (fgets(line, sizeof(line), f)) {
        int idx;
        float px, py, pz, ry, sx, sy, sz, sc;
        char name[32];
        if (sscanf(line, "obj %d %f %f %f %f %f %f %f", &idx, &px, &py, &pz, &ry, &sx, &sy, &sz) == 8) {
            if (numPlaced < MAX_PLACED && idx >= 0 && idx < numPrefabs) {
                placed[numPlaced] = (PlacedObject){
                    .pos = {px, py, pz}, .rotY = ry,
                    .scale = {sx, sy, sz}, .prefabIdx = idx, .active = true
                };
                numPlaced++;
            }
        } else if (sscanf(line, "spr %31s %f %f %f %f", name, &px, &py, &pz, &sc) >= 5) {
            float rotY2 = 0; int dmode = 0;
            sscanf(line, "spr %*s %*f %*f %*f %*f %f %d", &rotY2, &dmode);
            if (numPlacedSprites < MAX_PLACED_SPRITES) {
                PlacedSprite2D *ps = &placedSprites[numPlacedSprites];
                ps->pos = (Vector3){px, py, pz};
                ps->scale = sc;
                ps->rotY = rotY2;
                ps->displayMode = (SpriteDisplayMode)dmode;
                ps->active = true;
                strncpy(ps->filename, name, 31);
                // Find matching sprite file and load parts
                ps->partCount = 0;
                for (int s = 0; s < numSpriteFiles; s++) {
                    if (strcmp(spriteNames[s], name) == 0) {
                        ps->partCount = spritePartCounts[s];
                        for (int p = 0; p < ps->partCount; p++)
                            ps->parts[p] = spriteParts[s][p];
                        break;
                    }
                }
                if (ps->partCount > 0) numPlacedSprites++;
            }
        }
    }
    fclose(f);
}

void SaveEditorState(void) {
    FILE *f = fopen("editor_state.txt", "w");
    if (!f) return;
    fprintf(f, "mode %d\n", (int)mode);
    fprintf(f, "cam %.2f %.2f %.2f %.2f\n", camYaw, camPitch, camDist, 0.0f);
    fprintf(f, "focus %.2f %.2f %.2f\n", camFocus.x, camFocus.y, camFocus.z);
    fprintf(f, "tile %d\n", selectedTile);
    fprintf(f, "prefab %d\n", selectedPrefab);
    fprintf(f, "build3d %d\n", buildObj.count);
    fprintf(f, "build2d %d\n", build2d.count);
    fprintf(f, "prim3d %d\n", buildPrimitive);
    fprintf(f, "prim2d %d\n", build2dPrimitive);
    fprintf(f, "linewidth %.1f\n", lastLineWidth);
    fprintf(f, "spritefile %d\n", selectedSpriteFile);
    fprintf(f, "placingsprite %d\n", placingSprite ? 1 : 0);
    // Save build3d parts if any
    if (buildObj.count > 0) {
        SaveObject3D("editor_build3d.obj3d", buildObj.parts, buildObj.count);
    }
    // Save build2d parts if any
    if (build2d.count > 0) {
        SaveSprite2D("editor_build2d.spr2d", build2d.parts, build2d.count);
    }
    SavePlacedObjects();
    fclose(f);
}

void LoadEditorState(void) {
    FILE *f = fopen("editor_state.txt", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int modeVal;
        float y, p, d, dummy;
        float fx, fy, fz;
        int ival;
        if (sscanf(line, "mode %d", &modeVal) == 1) {
            mode = (EditorMode)modeVal;
        } else if (sscanf(line, "cam %f %f %f %f", &y, &p, &d, &dummy) == 4) {
            camYaw = y; camPitch = p; camDist = d;
        } else if (sscanf(line, "focus %f %f %f", &fx, &fy, &fz) == 3) {
            camFocus = (Vector3){fx, fy, fz};
        } else if (sscanf(line, "tile %d", &ival) == 1) {
            selectedTile = ival;
        } else if (sscanf(line, "prefab %d", &ival) == 1) {
            selectedPrefab = ival;
        } else if (sscanf(line, "build3d %d", &ival) == 1) {
            if (ival > 0) {
                buildObj.count = LoadObject3D("editor_build3d.obj3d", buildObj.parts, MAX_BUILD_PARTS);
                buildObj.selected = -1;
            }
        } else if (sscanf(line, "build2d %d", &ival) == 1) {
            if (ival > 0) {
                build2d.count = LoadSprite2D("editor_build2d.spr2d", build2d.parts, MAX_BUILD2D_PARTS);
                build2d.selected = -1;
            }
        } else if (sscanf(line, "prim3d %d", &ival) == 1) {
            buildPrimitive = ival;
        } else if (sscanf(line, "prim2d %d", &ival) == 1) {
            build2dPrimitive = ival;
        } else if (sscanf(line, "linewidth %f", &fx) == 1) {
            lastLineWidth = fx;
        } else if (sscanf(line, "spritefile %d", &ival) == 1) {
            selectedSpriteFile = ival;
        } else if (sscanf(line, "placingsprite %d", &ival) == 1) {
            placingSprite = (ival != 0);
        }
    }
    fclose(f);
}

void InitEditor(void) {
    // Init built-in prefab part pointers and copy names into mutable storage
    Part *builtinParts[] = { humanParts, carParts, treeParts, crateParts, barrelParts, lampParts, bushParts };
    int builtinCounts[] = {
        sizeof(humanParts)/sizeof(Part), sizeof(carParts)/sizeof(Part), sizeof(treeParts)/sizeof(Part),
        sizeof(crateParts)/sizeof(Part), sizeof(barrelParts)/sizeof(Part), sizeof(lampParts)/sizeof(Part),
        sizeof(bushParts)/sizeof(Part)
    };
    for (int i = 0; i < numPrefabs; i++) {
        strncpy(customNames[i], builtinNames[i], 31);
        prefabs[i].name = customNames[i];
        prefabs[i].parts = builtinParts[i];
        prefabs[i].partCount = builtinCounts[i];
    }

    // Create objects/ directory and save/load built-in prefabs
    MakeDirectory("objects");
    for (int i = 0; i < numPrefabs; i++) {
        char path[64];
        snprintf(path, sizeof(path), "objects/%s.obj3d", prefabs[i].name);
        for (char *c = path + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
        if (!FileExists(path)) {
            SavePrefabFile(i);
        } else {
            // Load edited parts from file, replacing hardcoded defaults
            Part *dest = &customPartsPool[customPartsUsed];
            int loaded = LoadPrefabFile(prefabs[i].name, dest, MAX_CUSTOM_PARTS - customPartsUsed);
            if (loaded > 0) {
                prefabs[i].parts = dest;
                prefabs[i].partCount = loaded;
                customPartsUsed += loaded;
            }
        }
    }

    // Try loading the map from file, otherwise start with grass
    if (!LoadMap3DFile("map.m3d", &map, tileDefs, NUM_TILEDEFS)) {
        char layout[EDITOR_MAP_W * EDITOR_MAP_H];
        memset(layout, '1', sizeof(layout));
        Map3DLoad(&map, layout, EDITOR_MAP_W, EDITOR_MAP_H, TILE_SZ, tileDefs, NUM_TILEDEFS);
    }
    camFocus = (Vector3){ EDITOR_MAP_W * TILE_SZ / 2, 0, EDITOR_MAP_H * TILE_SZ / 2 };
    LoadSpriteFiles();

    // Load attached sprites for all prefabs
    for (int i = 0; i < numPrefabs; i++)
        LoadPrefabSprites(i);

    // Auto-pick next available sprite name
    for (int n = 1; n < 100; n++) {
        char tryName[32], tryPath[64];
        if (n == 1) snprintf(tryName, sizeof(tryName), "Sprite");
        else snprintf(tryName, sizeof(tryName), "Sprite%d", n);
        snprintf(tryPath, sizeof(tryPath), "objects/%s.spr2d", tryName);
        if (!FileExists(tryPath)) {
            strncpy(build2d.name, tryName, 31);
            break;
        }
    }
}

void ExportMapToConsole(void) {
    printf("\n// --- Exported Map ---\n");
    printf("TileDef defs[] = {\n");
    for (int i = 0; i < (int)NUM_TILEDEFS; i++)
        printf("    // %d: %s\n", i, tileNames[i]);
    printf("};\n\n");

    printf("const char *layout =\n");
    for (int z = 0; z < map.height; z++) {
        printf("    \"");
        for (int x = 0; x < map.width; x++) {
            int t = map.tiles[z][x];
            if (t < 10) printf("%c", '0' + t);
            else printf("%c", 'a' + (t - 10));
        }
        printf("\"%s\n", z < map.height - 1 ? "" : ";");
    }
    printf("// Map size: %dx%d, tile size: %.1f\n\n", map.width, map.height, map.tileSize);

    if (numPlaced > 0) {
        printf("// --- Placed Objects ---\n");
        for (int i = 0; i < numPlaced; i++) {
            if (!placed[i].active) continue;
            printf("// %s at (%.1f, %.1f, %.1f) rot=%.2f scale=(%.2f, %.2f, %.2f)\n",
                prefabs[placed[i].prefabIdx].name,
                placed[i].pos.x, placed[i].pos.y, placed[i].pos.z,
                placed[i].rotY, placed[i].scale.x, placed[i].scale.y, placed[i].scale.z);
        }
    }
    printf("// --- End Export ---\n\n");
}

int main(void) {
    InitWindow(1200, 800, "3D Map & Object Editor");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED);
    SetExitKey(0);  // Disable ESC closing the window

    InitEditor();
    LoadEditorState();
    LoadPlacedObjects();

    Camera3D camera = { 0 };
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 50;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        Vector2 mouse = GetMousePosition();

        // Text input consumes keys when active
        bool textActive = UpdateTextEdit();

        // --- Camera mode toggle: V cycles Orbit -> FPS -> Fly ---
        if (IsKeyPressed(KEY_V) && !textActive) {
            if (camMode == CAM_ORBIT) {
                camMode = CAM_FPS;
                fpsCamPos = camera.position;
                fpsCamPos.y = 1.7f;
                fpsCamYaw = camYaw;
                fpsCamPitch = 0;
                if (!fpsCursorLocked) { DisableCursor(); fpsCursorLocked = true; }
            } else if (camMode == CAM_FPS) {
                camMode = CAM_FLY;
                fpsCamYaw = fpsCamYaw;
                fpsCamPitch = fpsCamPitch;
            } else {
                camMode = CAM_ORBIT;
                if (fpsCursorLocked) { EnableCursor(); fpsCursorLocked = false; }
                camFocus = camera.target;
            }
        }
        // Escape returns to orbit from FPS/Fly
        if (IsKeyPressed(KEY_ESCAPE) && camMode != CAM_ORBIT) {
            camMode = CAM_ORBIT;
            if (fpsCursorLocked) { EnableCursor(); fpsCursorLocked = false; }
            camFocus = camera.target;
        }

        if (camMode == CAM_ORBIT) {
            // --- Orbit camera ---
            if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
                Vector2 delta = GetMouseDelta();
                camYaw += delta.x * 0.005f;
                camPitch += delta.y * 0.005f;
                camPitch = Clamp(camPitch, 0.1f, 1.5f);
            }
            float wheel = GetMouseWheelMove();
            camDist -= wheel * 2.0f;
            camDist = Clamp(camDist, 5.0f, 80.0f);

            if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON)) {
                Vector2 delta = GetMouseDelta();
                float panSpeed = camDist * 0.003f;
                Vector3 right = { cosf(camYaw + PI/2), 0, sinf(camYaw + PI/2) };
                Vector3 fwd = { cosf(camYaw), 0, sinf(camYaw) };
                camFocus = Vector3Add(camFocus, Vector3Scale(right, -delta.x * panSpeed));
                camFocus = Vector3Add(camFocus, Vector3Scale(fwd, delta.y * panSpeed));
            }

            float panSpd = 15.0f * dt;
            Vector3 camFwd = { -cosf(camYaw), 0, -sinf(camYaw) };
            Vector3 camRight = { -sinf(camYaw), 0, cosf(camYaw) };
            if (IsKeyDown(KEY_W)) camFocus = Vector3Add(camFocus, Vector3Scale(camFwd, panSpd));
            if (IsKeyDown(KEY_S)) camFocus = Vector3Add(camFocus, Vector3Scale(camFwd, -panSpd));
            if (IsKeyDown(KEY_A)) camFocus = Vector3Add(camFocus, Vector3Scale(camRight, panSpd));
            if (IsKeyDown(KEY_D)) camFocus = Vector3Add(camFocus, Vector3Scale(camRight, -panSpd));

            // If orbiting (right-drag) with something selected, focus on it
            if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
                Vector3 orbitTarget = camFocus;
                bool hasTarget = false;
                if (mode == MODE_OBJECTS && selectedSprite >= 0 && selectedSprite < numPlacedSprites && placedSprites[selectedSprite].active) {
                    orbitTarget = placedSprites[selectedSprite].pos;
                    hasTarget = true;
                } else if (mode == MODE_OBJECTS && selectedObject >= 0 && selectedObject < numPlaced && placed[selectedObject].active) {
                    orbitTarget = placed[selectedObject].pos;
                    PrefabDef *opf = &prefabs[placed[selectedObject].prefabIdx];
                    orbitTarget.y += PrefabCenterY(opf->parts, opf->partCount, placed[selectedObject].scale);
                    hasTarget = true;
                } else if (mode == MODE_BUILD && buildObj.selectedSprite >= 0 && buildObj.selectedSprite < buildObj.spriteCount) {
                    orbitTarget = buildObj.sprites[buildObj.selectedSprite].offset;
                    hasTarget = true;
                } else if (mode == MODE_BUILD && buildObj.selected >= 0 && buildObj.selected < buildObj.count) {
                    orbitTarget = buildObj.parts[buildObj.selected].offset;
                    hasTarget = true;
                }
                if (hasTarget) camFocus = Vector3Lerp(camFocus, orbitTarget, 8.0f * dt);
            }

            camera.position = (Vector3){
                camFocus.x + cosf(camYaw) * cosf(camPitch) * camDist,
                camFocus.y + sinf(camPitch) * camDist,
                camFocus.z + sinf(camYaw) * cosf(camPitch) * camDist
            };
            camera.target = camFocus;
        } else {
            // --- FPS / Fly camera ---
            Vector2 delta = GetMouseDelta();
            fpsCamYaw += delta.x * 0.003f;
            fpsCamPitch -= delta.y * 0.003f;
            fpsCamPitch = Clamp(fpsCamPitch, -1.4f, 1.4f);

            // Forward/right vectors
            Vector3 fpsForward = {
                cosf(fpsCamYaw) * cosf(fpsCamPitch),
                sinf(fpsCamPitch),
                sinf(fpsCamYaw) * cosf(fpsCamPitch)
            };
            Vector3 fpsRight = { -sinf(fpsCamYaw), 0, cosf(fpsCamYaw) };
            Vector3 fpsForwardFlat = { cosf(fpsCamYaw), 0, sinf(fpsCamYaw) };

            float moveSpd = 10.0f * dt;
            if (IsKeyDown(KEY_LEFT_SHIFT)) moveSpd *= 3.0f;

            if (camMode == CAM_FLY) {
                // Fly: move in look direction
                if (IsKeyDown(KEY_W)) fpsCamPos = Vector3Add(fpsCamPos, Vector3Scale(fpsForward, moveSpd));
                if (IsKeyDown(KEY_S)) fpsCamPos = Vector3Add(fpsCamPos, Vector3Scale(fpsForward, -moveSpd));
            } else {
                // FPS: move on ground plane, fixed height
                if (IsKeyDown(KEY_W)) fpsCamPos = Vector3Add(fpsCamPos, Vector3Scale(fpsForwardFlat, moveSpd));
                if (IsKeyDown(KEY_S)) fpsCamPos = Vector3Add(fpsCamPos, Vector3Scale(fpsForwardFlat, -moveSpd));
                fpsCamPos.y = Map3DHeightAt(&map, fpsCamPos) + 1.7f;
            }
            if (IsKeyDown(KEY_A)) fpsCamPos = Vector3Add(fpsCamPos, Vector3Scale(fpsRight, -moveSpd));
            if (IsKeyDown(KEY_D)) fpsCamPos = Vector3Add(fpsCamPos, Vector3Scale(fpsRight, moveSpd));
            // Fly: up/down
            if (camMode == CAM_FLY) {
                if (IsKeyDown(KEY_SPACE)) fpsCamPos.y += moveSpd;
                if (IsKeyDown(KEY_LEFT_CONTROL)) fpsCamPos.y -= moveSpd;
            }

            camera.position = fpsCamPos;
            camera.target = Vector3Add(fpsCamPos, fpsForward);
        }

        // --- Mode switch (TAB opens popup menu at cursor) ---
        if (IsKeyPressed(KEY_TAB) && !textActive) {
            showModeMenu = !showModeMenu;
            if (showModeMenu) modeMenuPos = GetMousePosition();
        }
        if (showModeMenu) {
            const char *modeOpts[] = { "Tiles", "Objects", "Build 3D", "Build 2D" };
            EditorMode modes[] = { MODE_TILES, MODE_OBJECTS, MODE_BUILD, MODE_BUILD2D };
            // Block other input while menu is open
        }

        // Toggle grid
        if (IsKeyPressed(KEY_G)) showGrid = !showGrid;
        // Toggle minimap
        if (IsKeyPressed(KEY_M)) showMinimap = !showMinimap;

        // Save map (Ctrl+S)
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER)) && IsKeyPressed(KEY_S)) {
            SaveMap3D("map.m3d", &map);
            SavePlacedObjects();
            ExportMapToConsole();
            showExport = true;
        }

        // --- Mouse interaction (only when not over UI) ---
        bool overUI = (mouse.x < 180);  // left panel
        // Color palette on right side in 2D build mode
        if (mode == MODE_BUILD2D && mouse.x > sw - 150 && mouse.y > sh - 210 && mouse.y < sh - 100)
            overUI = true;
        Vector3 groundHit = MouseToGround(camera);
        int hoverTX, hoverTZ;
        Map3DFromWorld(&map, groundHit, &hoverTX, &hoverTZ);
        bool hoverValid = !overUI && hoverTX >= 0 && hoverTX < map.width &&
                          hoverTZ >= 0 && hoverTZ < map.height;

        if (mode == MODE_TILES && !textActive) {
            // Tile selection: number keys or scroll in palette
            for (int k = KEY_ZERO; k <= KEY_NINE; k++)
                if (IsKeyPressed(k)) selectedTile = k - KEY_ZERO;

            // Shift+click: place sprite billboard if sprite selected
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && IsKeyDown(KEY_LEFT_SHIFT) &&
                hoverValid && !overUI && placingSprite &&
                selectedSpriteFile >= 0 && selectedSpriteFile < numSpriteFiles &&
                numPlacedSprites < MAX_PLACED_SPRITES) {
                float h = Map3DHeightAt(&map, groundHit);
                PlacedSprite2D *ps = &placedSprites[numPlacedSprites];
                ps->pos = (Vector3){groundHit.x, h, groundHit.z};
                ps->scale = 1.0f;
                ps->rotY = 0;
                ps->partCount = spritePartCounts[selectedSpriteFile];
                for (int p = 0; p < ps->partCount; p++)
                    ps->parts[p] = spriteParts[selectedSpriteFile][p];
                strncpy(ps->filename, spriteNames[selectedSpriteFile], 31);
                ps->active = true;
                selectedSprite = numPlacedSprites;
                numPlacedSprites++;
            }
            // Paint tiles with left click (not when shift-placing sprites)
            else if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && hoverValid && !overUI) {
                map.tiles[hoverTZ][hoverTX] = selectedTile;
            }

            // Eyedropper: pick tile under cursor
            if (IsKeyPressed(KEY_Q) && hoverValid) {
                selectedTile = map.tiles[hoverTZ][hoverTX];
            }
        } else if (mode == MODE_OBJECTS && !textActive) {
            // Object mode
            // Select prefab
            for (int k = KEY_ONE; k <= KEY_NINE; k++)
                if (IsKeyPressed(k) && (k - KEY_ONE) < numPrefabs) { selectedPrefab = k - KEY_ONE; placingSprite = false; selectedSpriteFile = -1; }

            // Left-click: check gizmo first (sprite or object), then select
            bool clickedGizmo = false;
            // Check sprite gizmo
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI &&
                selectedSprite >= 0 && selectedSprite < numPlacedSprites && placedSprites[selectedSprite].active) {
                PlacedSprite2D *ss = &placedSprites[selectedSprite];
                Vector2 screenCenter = GetWorldToScreen(ss->pos, camera);
                Vector2 gEnds[3];
                float gArm = GizmoScreenEnds(ss->pos, camera, gEnds);
                float hitR = gArm * 0.25f;
                if (hitR < 12) hitR = 12;
                if (gizmoMode == GIZMO_ROTATE) {
                    int rax = RotateGizmoHitAxis(mouse, ss->pos, camera, gArm);
                    if (rax >= 0) { draggingAxis = rax; clickedGizmo = true; }
                } else {
                    for (int ax = 0; ax < 3; ax++) {
                        if (Vector2Distance(mouse, gEnds[ax]) < hitR) {
                            draggingAxis = ax; clickedGizmo = true; break;
                        }
                    }
                    if (!clickedGizmo && Vector2Distance(mouse, screenCenter) < hitR) {
                        draggingAxis = 3; clickedGizmo = true;
                    }
                }
            }
            // Check object gizmo
            if (!clickedGizmo && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI &&
                selectedObject >= 0 && placed[selectedObject].active) {
                PlacedObject *sel = &placed[selectedObject];
                PrefabDef *gpf = &prefabs[sel->prefabIdx];
                float gy = sel->pos.y + PrefabCenterY(gpf->parts, gpf->partCount, sel->scale);
                Vector3 gCtr = {sel->pos.x, gy, sel->pos.z};
                Vector2 screenCenter = GetWorldToScreen(gCtr, camera);
                Vector2 gEnds[3];
                float gArm = GizmoScreenEnds(gCtr, camera, gEnds);
                float hitR = gArm * 0.25f;
                if (hitR < 12) hitR = 12;

                if (gizmoMode == GIZMO_ROTATE) {
                    int rax = RotateGizmoHitAxis(mouse, gCtr, camera, gArm);
                    if (rax >= 0) { draggingAxis = rax; clickedGizmo = true; }
                } else {
                    for (int ax = 0; ax < 3; ax++) {
                        if (Vector2Distance(mouse, gEnds[ax]) < hitR) {
                            draggingAxis = ax;
                            clickedGizmo = true;
                            break;
                        }
                    }
                    // Center point: ground plane drag
                    if (!clickedGizmo && Vector2Distance(mouse, screenCenter) < hitR) {
                        draggingAxis = 3;
                        clickedGizmo = true;
                    }
                }
            }

            // If gizmo wasn't clicked, try selecting an object or placed sprite
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hoverValid && !overUI && !clickedGizmo) {
                selectedObject = -1;
                selectedSprite = -1;
                float bestDist = 2.5f;
                for (int i = 0; i < numPlaced; i++) {
                    if (!placed[i].active) continue;
                    Vector3 diff = Vector3Subtract(groundHit, placed[i].pos);
                    diff.y = 0;
                    float d = Vector3Length(diff);
                    if (d < bestDist) {
                        bestDist = d;
                        selectedObject = i;
                    }
                }
                // Also check placed sprites (screen-space bounding box)
                float bestSpriteDist = 1e9f;
                for (int i = 0; i < numPlacedSprites; i++) {
                    if (!placedSprites[i].active) continue;
                    Rectangle sr = GetSprite2DScreenRect(placedSprites[i].parts, placedSprites[i].partCount,
                        placedSprites[i].pos, placedSprites[i].scale, camera);
                    if (CheckCollisionPointRec(mouse, sr)) {
                        float d = Vector3Length(Vector3Subtract(placedSprites[i].pos, camera.position));
                        if (d < bestSpriteDist) {
                            bestSpriteDist = d;
                            selectedSprite = i;
                        }
                    }
                }
                // If both matched, pick the closer one in world space
                if (selectedObject >= 0 && selectedSprite >= 0) {
                    float objD = Vector3Length(Vector3Subtract(groundHit, placed[selectedObject].pos));
                    float sprD = Vector3Length(Vector3Subtract(groundHit, placedSprites[selectedSprite].pos));
                    if (sprD < objD) selectedObject = -1;
                    else selectedSprite = -1;
                }
            }

            // Shift+click or F: place object or sprite depending on what's selected
            if (((IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && IsKeyDown(KEY_LEFT_SHIFT) && !clickedGizmo) ||
                 IsKeyPressed(KEY_F)) && hoverValid && !overUI) {
                if (placingSprite && selectedSpriteFile >= 0 && selectedSpriteFile < numSpriteFiles &&
                    numPlacedSprites < MAX_PLACED_SPRITES) {
                    float h = Map3DHeightAt(&map, groundHit);
                    PlacedSprite2D *ps = &placedSprites[numPlacedSprites];
                    ps->pos = (Vector3){groundHit.x, h, groundHit.z};
                    ps->scale = 1.0f;
                    ps->rotY = 0;
                    ps->partCount = spritePartCounts[selectedSpriteFile];
                    for (int p = 0; p < ps->partCount; p++)
                        ps->parts[p] = spriteParts[selectedSpriteFile][p];
                    strncpy(ps->filename, spriteNames[selectedSpriteFile], 31);
                    ps->active = true;
                    selectedSprite = numPlacedSprites;
                    numPlacedSprites++;
                } else if (!placingSprite && numPlaced < MAX_PLACED) {
                    float h = Map3DHeightAt(&map, groundHit);
                    placed[numPlaced] = (PlacedObject){
                        .pos = {groundHit.x, h, groundHit.z},
                        .rotY = 0,
                        .scale = {1.0f, 1.0f, 1.0f},
                        .prefabIdx = selectedPrefab,
                        .active = true
                    };
                    selectedObject = numPlaced;
                    numPlaced++;
                }
            }

            // Escape: deselect
            if (IsKeyPressed(KEY_ESCAPE)) { selectedObject = -1; selectedSprite = -1; draggingAxis = -1; }

            // Selected sprite: delete or toggle display mode
            if (selectedSprite >= 0 && selectedSprite < numPlacedSprites &&
                placedSprites[selectedSprite].active) {
                if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
                    placedSprites[selectedSprite].active = false;
                    selectedSprite = -1;
                }
                if (IsKeyPressed(KEY_T)) {
                    placedSprites[selectedSprite].displayMode =
                        (placedSprites[selectedSprite].displayMode == SPRITE_BILLBOARD)
                        ? SPRITE_PLANE : SPRITE_BILLBOARD;
                }
            }

            // Gizmo mode switching: Z/X/C
            if (IsKeyPressed(KEY_Z)) gizmoMode = GIZMO_MOVE;
            if (IsKeyPressed(KEY_X)) gizmoMode = GIZMO_ROTATE;
            if (IsKeyPressed(KEY_C)) gizmoMode = GIZMO_SCALE;

            // Selected object interaction
            if (selectedObject >= 0 && placed[selectedObject].active) {
                PlacedObject *sel = &placed[selectedObject];

                // Delete
                if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
                    sel->active = false;
                    selectedObject = -1;
                    draggingAxis = -1;
                } else {
                    // Keyboard shortcuts still work
                    if (IsKeyDown(KEY_R)) sel->rotY += 2.0f * dt;
                    if (IsKeyDown(KEY_Q)) sel->rotY -= 2.0f * dt;

                    // Continue gizmo drag (hit detection now handled above)
                    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && draggingAxis >= 0) {
                        Vector2 delta = GetMouseDelta();
                        float dragSpeed = 0.02f * (camDist / 15.0f);

                        if (gizmoMode == GIZMO_MOVE) {
                            if (draggingAxis == 0)      sel->pos.x += delta.x * dragSpeed;
                            else if (draggingAxis == 1)  sel->pos.y -= delta.y * dragSpeed;
                            else if (draggingAxis == 2)  sel->pos.z += delta.x * dragSpeed;
                            else if (draggingAxis == 3) {
                                // Move on ground plane
                                Vector3 hit = MouseToGround(camera);
                                sel->pos.x = hit.x;
                                sel->pos.z = hit.z;
                                sel->pos.y = Map3DHeightAt(&map, sel->pos);
                            }
                        } else if (gizmoMode == GIZMO_ROTATE) {
                            // Only Y rotation supported for placed objects
                            if (draggingAxis == 1) sel->rotY += delta.x * 0.02f;
                        } else if (gizmoMode == GIZMO_SCALE) {
                            float sd = (delta.x - delta.y) * 0.005f;
                            if (draggingAxis == 0)      sel->scale.x += sd;
                            else if (draggingAxis == 1) sel->scale.y += sd;
                            else if (draggingAxis == 2) sel->scale.z += sd;
                            else { sel->scale.x += sd; sel->scale.y += sd; sel->scale.z += sd; }
                            sel->scale.x = Clamp(sel->scale.x, 0.1f, 10.0f);
                            sel->scale.y = Clamp(sel->scale.y, 0.1f, 10.0f);
                            sel->scale.z = Clamp(sel->scale.z, 0.1f, 10.0f);
                        }
                    }

                    // End drag
                    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) draggingAxis = -1;
                }
            }

            // Selected sprite interaction (move/scale via gizmo)
            if (selectedSprite >= 0 && selectedSprite < numPlacedSprites && placedSprites[selectedSprite].active) {
                PlacedSprite2D *ss = &placedSprites[selectedSprite];

                if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && draggingAxis >= 0) {
                    Vector2 delta = GetMouseDelta();
                    float dragSpeed = 0.02f * (camDist / 15.0f);

                    if (gizmoMode == GIZMO_MOVE) {
                        if (draggingAxis == 0)      ss->pos.x += delta.x * dragSpeed;
                        else if (draggingAxis == 1)  ss->pos.y -= delta.y * dragSpeed;
                        else if (draggingAxis == 2)  ss->pos.z += delta.x * dragSpeed;
                        else if (draggingAxis == 3) {
                            Vector3 hit = MouseToGround(camera);
                            ss->pos.x = hit.x;
                            ss->pos.z = hit.z;
                            ss->pos.y = Map3DHeightAt(&map, ss->pos);
                        }
                    } else if (gizmoMode == GIZMO_SCALE) {
                        float sd = (delta.x - delta.y) * 0.005f;
                        ss->scale += sd;
                        ss->scale = Clamp(ss->scale, 0.1f, 10.0f);
                    }
                }
                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) draggingAxis = -1;
            }
        }

        // Load prefab into build mode for editing
        // If a placed object is selected, edit its prefab; otherwise edit the palette selection
        if (IsKeyPressed(KEY_E) && mode == MODE_OBJECTS) {
            int editIdx = selectedPrefab;
            if (selectedObject >= 0 && selectedObject < numPlaced && placed[selectedObject].active)
                editIdx = placed[selectedObject].prefabIdx;
            if (editIdx >= 0 && editIdx < numPrefabs) {
            selectedPrefab = editIdx;
            PrefabDef *pf = &prefabs[editIdx];
            buildObj.count = (pf->partCount < MAX_BUILD_PARTS) ? pf->partCount : MAX_BUILD_PARTS;
            for (int i = 0; i < buildObj.count; i++)
                buildObj.parts[i] = pf->parts[i];
            buildObj.selected = -1;
            buildObj.editingPrefab = selectedPrefab;
            buildObj.spriteCount = pf->spriteCount;
            for (int s = 0; s < pf->spriteCount; s++)
                buildObj.sprites[s] = pf->sprites[s];
            buildObj.selectedSprite = -1;
            snprintf(buildObj.name, sizeof(buildObj.name), "%s", pf->name);
            mode = MODE_BUILD;
            camFocus = (Vector3){0, 0, 0};
            camDist = 12.0f;
            draggingAxis = -1;
        }}

        // --- Build mode ---
        if (mode == MODE_BUILD && !textActive) {
            // Select primitive type
            if (IsKeyPressed(KEY_ONE)) buildPrimitive = 0;  // cube
            if (IsKeyPressed(KEY_TWO)) buildPrimitive = 1;  // sphere
            if (IsKeyPressed(KEY_THREE)) buildPrimitive = 2; // cylinder

            // Gizmo mode
            if (IsKeyPressed(KEY_Z)) gizmoMode = GIZMO_MOVE;
            if (IsKeyPressed(KEY_X)) gizmoMode = GIZMO_ROTATE;
            if (IsKeyPressed(KEY_C)) gizmoMode = GIZMO_SCALE;

            // Add new part
            if (IsKeyPressed(KEY_P) && buildObj.count < MAX_BUILD_PARTS) {
                Part *np = &buildObj.parts[buildObj.count];
                Color defaultCols[] = { RED, GREEN, BLUE, YELLOW, PURPLE, ORANGE,
                    SKYBLUE, PINK, LIME, GOLD, MAROON, DARKBLUE };
                Color col = defaultCols[buildObj.count % 12];
                switch (buildPrimitive) {
                    case 0: *np = (Part)CUBE(0, 0.5f, 0, 1,1,1, col); break;
                    case 1: *np = (Part)SPHERE(0, 0.5f, 0, 0.5f, col); break;
                    case 2: *np = (Part){ PART_CYLINDER, {0, 0, 0}, {0.3f, 0.8f, 0}, col, false }; break;
                }
                buildObj.selected = buildObj.count;
                buildObj.count++;
            }

            // Shift+click: attach sprite to the build object
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && IsKeyDown(KEY_LEFT_SHIFT) && !overUI &&
                placingSprite && selectedSpriteFile >= 0 && selectedSpriteFile < numSpriteFiles &&
                buildObj.spriteCount < MAX_ATTACHED_SPRITES) {
                Vector3 hit = MouseToGround(camera);
                AttachedSprite *as = &buildObj.sprites[buildObj.spriteCount];
                as->offset = (Vector3){hit.x, 0, hit.z};
                as->scale = 1.0f;
                as->displayMode = SPRITE_BILLBOARD;
                as->partCount = spritePartCounts[selectedSpriteFile];
                for (int p = 0; p < as->partCount; p++)
                    as->parts[p] = spriteParts[selectedSpriteFile][p];
                strncpy(as->filename, spriteNames[selectedSpriteFile], 31);
                buildObj.selectedSprite = buildObj.spriteCount;
                buildObj.spriteCount++;
            }

            // Select part by clicking
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI) {
                // Check if clicking a gizmo first
                bool hitGizmo = false;
                if (buildObj.selected >= 0) {
                    Part *sp = &buildObj.parts[buildObj.selected];
                    float gy = sp->offset.y + (sp->type == PART_CYLINDER ? sp->size.y * 0.5f : 0);
                    Vector3 gCtr = {sp->offset.x, gy, sp->offset.z};
                    Vector2 gCenter2 = GetWorldToScreen(gCtr, camera);
                    Vector2 gEnds[3];
                    float gArm = GizmoScreenEnds(gCtr, camera, gEnds);
                    float hitR = gArm * 0.25f;
                    if (hitR < 12) hitR = 12;
                    if (gizmoMode == GIZMO_ROTATE) {
                        int rax = RotateGizmoHitAxis(mouse, gCtr, camera, gArm);
                        if (rax >= 0) { draggingAxis = rax; hitGizmo = true; }
                    } else {
                        for (int ax = 0; ax < 3; ax++) {
                            if (Vector2Distance(mouse, gEnds[ax]) < hitR) { draggingAxis = ax; hitGizmo = true; break; }
                        }
                        if (!hitGizmo && Vector2Distance(mouse, gCenter2) < hitR) {
                            draggingAxis = 3; hitGizmo = true;
                        }
                    }
                }
                if (!hitGizmo && buildObj.selectedSprite >= 0) {
                    // Check gizmo on selected attached sprite
                    AttachedSprite *as = &buildObj.sprites[buildObj.selectedSprite];
                    Vector2 sc2 = GetWorldToScreen(as->offset, camera);
                    Vector2 gEnds[3];
                    float gArm = GizmoScreenEnds(as->offset, camera, gEnds);
                    float hitR = gArm * 0.25f;
                    if (hitR < 12) hitR = 12;
                    if (gizmoMode == GIZMO_ROTATE) {
                        int rax = RotateGizmoHitAxis(mouse, as->offset, camera, gArm);
                        if (rax >= 0) { draggingAxis = rax; hitGizmo = true; }
                    } else {
                        for (int ax = 0; ax < 3; ax++) {
                            if (Vector2Distance(mouse, gEnds[ax]) < hitR) { draggingAxis = ax; hitGizmo = true; break; }
                        }
                        if (!hitGizmo && Vector2Distance(mouse, sc2) < hitR) { draggingAxis = 3; hitGizmo = true; }
                    }
                }
                if (!hitGizmo) {
                    // Select nearest part or attached sprite using screen-space distance
                    buildObj.selected = -1;
                    buildObj.selectedSprite = -1;
                    float bestPartD = 40.0f;  // pixels
                    for (int i = 0; i < buildObj.count; i++) {
                        Vector2 partScreen = GetWorldToScreen(buildObj.parts[i].offset, camera);
                        float d = Vector2Distance(mouse, partScreen);
                        if (d < bestPartD) { bestPartD = d; buildObj.selected = i; }
                    }
                    // Check attached sprites (screen-space bounding box)
                    float bestSpriteD = 1e9f;
                    int bestSpriteIdx = -1;
                    for (int i = 0; i < buildObj.spriteCount; i++) {
                        Rectangle sr = GetSprite2DScreenRect(buildObj.sprites[i].parts, buildObj.sprites[i].partCount,
                            buildObj.sprites[i].offset, buildObj.sprites[i].scale, camera);
                        if (CheckCollisionPointRec(mouse, sr)) {
                            float d = Vector3Length(Vector3Subtract(buildObj.sprites[i].offset, camera.position));
                            if (d < bestSpriteD) { bestSpriteD = d; bestSpriteIdx = i; }
                        }
                    }
                    // If both matched, prefer sprite (bounding box hit is more precise than 40px proximity)
                    if (bestSpriteIdx >= 0) {
                        buildObj.selected = -1;
                        buildObj.selectedSprite = bestSpriteIdx;
                    }
                }
            }

            // Drag selected part's gizmo
            if (buildObj.selected >= 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && draggingAxis >= 0) {
                Part *sp = &buildObj.parts[buildObj.selected];
                Vector2 delta = GetMouseDelta();
                float dragSpeed = 0.02f * (camDist / 15.0f);
                if (gizmoMode == GIZMO_MOVE) {
                    if (draggingAxis == 0)      sp->offset.x += delta.x * dragSpeed;
                    else if (draggingAxis == 1)  sp->offset.y -= delta.y * dragSpeed;
                    else if (draggingAxis == 2)  sp->offset.z += delta.x * dragSpeed;
                    else {
                        Vector3 hit = MouseToGround(camera);
                        sp->offset.x = hit.x;
                        sp->offset.z = hit.z;
                    }
                } else if (gizmoMode == GIZMO_ROTATE) {
                    // Orbit the part around the origin on the selected axis
                    float angle = delta.x * 0.02f;
                    float c = cosf(angle), s = sinf(angle);
                    if (draggingAxis == 0) { // X axis: rotate in YZ plane
                        float oy = sp->offset.y, oz = sp->offset.z;
                        sp->offset.y = oy * c - oz * s;
                        sp->offset.z = oy * s + oz * c;
                    } else if (draggingAxis == 1) { // Y axis: rotate in XZ plane
                        float ox = sp->offset.x, oz = sp->offset.z;
                        sp->offset.x = ox * c - oz * s;
                        sp->offset.z = ox * s + oz * c;
                    } else if (draggingAxis == 2) { // Z axis: rotate in XY plane
                        float ox = sp->offset.x, oy = sp->offset.y;
                        sp->offset.x = ox * c - oy * s;
                        sp->offset.y = ox * s + oy * c;
                    }
                } else if (gizmoMode == GIZMO_SCALE) {
                    float sd = (delta.x - delta.y) * 0.005f;
                    if (draggingAxis == 0)      sp->size.x = Clamp(sp->size.x + sd, 0.05f, 5.0f);
                    else if (draggingAxis == 1) sp->size.y = Clamp(sp->size.y + sd, 0.05f, 5.0f);
                    else if (draggingAxis == 2) sp->size.z = Clamp(sp->size.z + sd, 0.05f, 5.0f);
                    else {
                        sp->size.x = Clamp(sp->size.x + sd, 0.05f, 5.0f);
                        sp->size.y = Clamp(sp->size.y + sd, 0.05f, 5.0f);
                        sp->size.z = Clamp(sp->size.z + sd, 0.05f, 5.0f);
                    }
                }
            }
            // Drag selected attached sprite's gizmo
            if (buildObj.selectedSprite >= 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && draggingAxis >= 0) {
                AttachedSprite *as = &buildObj.sprites[buildObj.selectedSprite];
                Vector2 delta = GetMouseDelta();
                float dragSpeed = 0.02f * (camDist / 15.0f);
                if (gizmoMode == GIZMO_MOVE) {
                    if (draggingAxis == 0)      as->offset.x += delta.x * dragSpeed;
                    else if (draggingAxis == 1)  as->offset.y -= delta.y * dragSpeed;
                    else if (draggingAxis == 2)  as->offset.z += delta.x * dragSpeed;
                    else {
                        Vector3 hit = MouseToGround(camera);
                        as->offset.x = hit.x;
                        as->offset.z = hit.z;
                    }
                } else if (gizmoMode == GIZMO_SCALE) {
                    float sd = (delta.x - delta.y) * 0.005f;
                    as->scale = Clamp(as->scale + sd, 0.1f, 10.0f);
                }
            }
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) draggingAxis = -1;

            // Toggle billboard/plane on selected attached sprite
            if (buildObj.selectedSprite >= 0 && IsKeyPressed(KEY_T)) {
                buildObj.sprites[buildObj.selectedSprite].displayMode =
                    (buildObj.sprites[buildObj.selectedSprite].displayMode == SPRITE_BILLBOARD)
                    ? SPRITE_PLANE : SPRITE_BILLBOARD;
            }

            // Delete selected part or attached sprite
            if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
                if (buildObj.selectedSprite >= 0) {
                    for (int i = buildObj.selectedSprite; i < buildObj.spriteCount - 1; i++)
                        buildObj.sprites[i] = buildObj.sprites[i + 1];
                    buildObj.spriteCount--;
                    buildObj.selectedSprite = -1;
                } else if (buildObj.selected >= 0) {
                    for (int i = buildObj.selected; i < buildObj.count - 1; i++)
                        buildObj.parts[i] = buildObj.parts[i + 1];
                    buildObj.count--;
                    buildObj.selected = -1;
                }
            }

            // Escape deselect
            if (IsKeyPressed(KEY_ESCAPE)) { buildObj.selected = -1; buildObj.selectedSprite = -1; draggingAxis = -1; }

            // Save built object to palette + file
            if (IsKeyPressed(KEY_ENTER) && buildObj.count > 0) {
                if (customPartsUsed + buildObj.count <= MAX_CUSTOM_PARTS) {
                    Part *dest = &customPartsPool[customPartsUsed];
                    for (int i = 0; i < buildObj.count; i++)
                        dest[i] = buildObj.parts[i];

                    int targetIdx = buildObj.editingPrefab;

                    if (targetIdx >= 0 && targetIdx < numPrefabs) {
                        // Update existing prefab
                        prefabs[targetIdx].parts = dest;
                        prefabs[targetIdx].partCount = buildObj.count;
                        prefabs[targetIdx].spriteCount = buildObj.spriteCount;
                        for (int s = 0; s < buildObj.spriteCount; s++)
                            prefabs[targetIdx].sprites[s] = buildObj.sprites[s];
                        selectedPrefab = targetIdx;
                        SavePrefabFile(targetIdx);
                    } else if (numPrefabs < MAX_PREFABS) {
                        // Create new prefab
                        if (buildObj.name[0] == '\0' || strcmp(buildObj.name, "Custom") == 0)
                            snprintf(buildObj.name, 32, "Custom_%d", numPrefabs - 6);
                        snprintf(customNames[numPrefabs], 32, "%s", buildObj.name);
                        prefabs[numPrefabs].name = customNames[numPrefabs];
                        prefabs[numPrefabs].parts = dest;
                        prefabs[numPrefabs].partCount = buildObj.count;
                        prefabs[numPrefabs].spriteCount = buildObj.spriteCount;
                        for (int s = 0; s < buildObj.spriteCount; s++)
                            prefabs[numPrefabs].sprites[s] = buildObj.sprites[s];
                        selectedPrefab = numPrefabs;
                        SavePrefabFile(numPrefabs);
                        numPrefabs++;
                    }

                    customPartsUsed += buildObj.count;

                    // Switch to objects mode
                    buildObj.count = 0;
                    buildObj.selected = -1;
                    buildObj.spriteCount = 0;
                    buildObj.selectedSprite = -1;
                    buildObj.editingPrefab = -1;
                    mode = MODE_OBJECTS;
                    camFocus = (Vector3){ EDITOR_MAP_W * TILE_SZ / 2, 0, EDITOR_MAP_H * TILE_SZ / 2 };
                    camDist = 30.0f;
                    showExport = true;
                }
            }
        }

        // --- 2D Build mode ---
        if (mode == MODE_BUILD2D) {
            // Primitive selection
            if (IsKeyPressed(KEY_ONE)) build2dPrimitive = 0;  // rect
            if (IsKeyPressed(KEY_TWO)) build2dPrimitive = 1;  // circle
            if (IsKeyPressed(KEY_THREE)) build2dPrimitive = 2; // triangle
            if (IsKeyPressed(KEY_FOUR)) build2dPrimitive = 3;  // line
            if (IsKeyPressed(KEY_FIVE)) build2dPrimitive = 4;  // ellipse
            if (IsKeyPressed(KEY_SIX)) build2dPrimitive = 5;   // polygon

            int cx = sw / 2, cy = sh / 2;
            bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT);
            if (shiftHeld) {
                // --- Shift: select and move existing parts ---
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI) {
                    build2d.selected = -1;
                    float bestD = 30.0f;
                    for (int i = 0; i < build2d.count; i++) {
                        Sprite2DPart *sp = &build2d.parts[i];
                        float d;
                        if (sp->type == SP_LINE) {
                            // Distance to line segment
                            Vector2 a = {cx + sp->x, cy + sp->y};
                            Vector2 b = {cx + sp->w, cy + sp->h};
                            Vector2 ab = {b.x - a.x, b.y - a.y};
                            float t = ((mouse.x - a.x)*ab.x + (mouse.y - a.y)*ab.y) /
                                      (ab.x*ab.x + ab.y*ab.y + 0.001f);
                            if (t < 0) t = 0; if (t > 1) t = 1;
                            Vector2 closest = {a.x + t*ab.x, a.y + t*ab.y};
                            d = Vector2Distance(mouse, closest);
                        } else if (sp->type == SP_TRIANGLE) {
                            // Check distance to centroid of triangle
                            float tx = (sp->x + sp->w + sp->extra1) / 3.0f;
                            float ty = (sp->y + sp->h + sp->extra2) / 3.0f;
                            d = Vector2Distance(mouse, (Vector2){cx + tx, cy + ty});
                        } else {
                            d = Vector2Distance(mouse, (Vector2){cx + sp->x, cy + sp->y});
                        }
                        if (d < bestD) { bestD = d; build2d.selected = i; }
                    }
                }
                if (build2d.selected >= 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && !overUI) {
                    Vector2 delta = GetMouseDelta();
                    build2d.parts[build2d.selected].x += delta.x;
                    build2d.parts[build2d.selected].y += delta.y;
                }
            } else if (build2dPrimitive == 2 && triVertex > 0) {
                // --- Triangle: third vertex tracks mouse until clicked ---
                Sprite2DPart *sp = &build2d.parts[build2d.selected];
                float mx = mouse.x - cx, my = mouse.y - cy;
                sp->extra1 = mx; sp->extra2 = my;  // preview third vertex at cursor

                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI) {
                    triVertex = 0;  // finalize
                }
                if (IsKeyPressed(KEY_ESCAPE)) {
                    build2d.count--; build2d.selected = -1; triVertex = 0;
                }
            } else if (polyDrawing) {
                // --- Polygon: click to place vertices, Enter/double-click to finish ---
                bool finishPoly = IsKeyPressed(KEY_ENTER);

                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI) {
                    // Check double-click on last vertex
                    if (polyVertCount >= 3) {
                        float distToLast = Vector2Distance(mouse,
                            (Vector2){cx + polyVerts[polyVertCount-1].x, cy + polyVerts[polyVertCount-1].y});
                        float distToFirst = Vector2Distance(mouse,
                            (Vector2){cx + polyVerts[0].x, cy + polyVerts[0].y});
                        if (distToLast < 15 || distToFirst < 15) {
                            finishPoly = true;
                        } else if (polyVertCount < MAX_POLY_VERTS) {
                            polyVerts[polyVertCount++] = (Vector2){ mouse.x - cx, mouse.y - cy };
                        }
                    } else if (polyVertCount < MAX_POLY_VERTS) {
                        polyVerts[polyVertCount++] = (Vector2){ mouse.x - cx, mouse.y - cy };
                    }
                }

                if (finishPoly && polyVertCount >= 3) {
                    // Convert polygon to triangle fan
                    Color col = selectedColor;

                    for (int t = 1; t < polyVertCount - 1 && build2d.count < MAX_BUILD2D_PARTS; t++) {
                        build2d.parts[build2d.count++] = (Sprite2DPart)STRIANGLE(
                            polyVerts[0].x, polyVerts[0].y,
                            polyVerts[t].x, polyVerts[t].y,
                            polyVerts[t+1].x, polyVerts[t+1].y, col);
                    }
                    build2d.selected = build2d.count - 1;
                    polyDrawing = false;
                    polyVertCount = 0;
                }

                if (IsKeyPressed(KEY_ESCAPE)) {
                    polyDrawing = false;
                    polyVertCount = 0;
                }
            } else {
                // --- Click and drag to draw shapes ---
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI && build2d.count < MAX_BUILD2D_PARTS) {
                    Color col = selectedColor;
                    float sx = mouse.x - cx, sy = mouse.y - cy;

                    if (build2dPrimitive == 5) {
                        // Polygon: start placing vertices
                        polyDrawing = true;
                        polyVertCount = 1;
                        polyVerts[0] = (Vector2){ sx, sy };
                    } else if (build2dPrimitive == 2) {
                        // Triangle: drag for first two points, click for third
                        drawing2d = true;
                        drawStart = mouse;
                        Sprite2DPart *np = &build2d.parts[build2d.count];
                        *np = (Sprite2DPart)STRIANGLE(sx, sy, sx, sy, sx, sy, col);
                        build2d.selected = build2d.count;
                        build2d.count++;
                    } else {
                        drawing2d = true;
                        drawStart = mouse;
                        Sprite2DPart *np = &build2d.parts[build2d.count];
                        switch (build2dPrimitive) {
                            case 0: *np = (Sprite2DPart)SRECT(sx, sy, 1, 1, col); break;
                            case 1: *np = (Sprite2DPart)SCIRCLE(sx, sy, 1, col); break;
                            case 3: *np = (Sprite2DPart)SLINE(sx, sy, sx, sy, lastLineWidth, col); break;
                            case 4: *np = (Sprite2DPart)SELLIPSE(sx, sy, 1, 1, col); break;
                            default: break;
                        }
                        build2d.selected = build2d.count;
                        build2d.count++;
                    }
                }

                if (drawing2d && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                    Sprite2DPart *sp = &build2d.parts[build2d.selected];
                    float dx = mouse.x - drawStart.x;
                    float dy = mouse.y - drawStart.y;
                    float sx = drawStart.x - cx, sy = drawStart.y - cy;
                    float ex = mouse.x - cx, ey = mouse.y - cy;

                    switch (build2dPrimitive) {
                        case 0: // Rect
                            sp->x = (sx + ex) / 2;
                            sp->y = (sy + ey) / 2;
                            sp->w = fabsf(dx);
                            sp->h = fabsf(dy);
                            break;
                        case 1: // Circle: fixed corner at start, radius from drag distance
                            sp->w = fabsf(dx) / 2;
                            sp->x = sx + (dx > 0 ? sp->w : -sp->w);
                            sp->y = sy + (dy > 0 ? sp->w : -sp->w);
                            break;
                        case 2: // Triangle: drag sets first two vertices
                            sp->w = ex; sp->h = ey;
                            sp->extra1 = ex; sp->extra2 = ey;  // third follows mouse until clicked
                            break;
                        case 3: // Line
                            sp->w = ex; sp->h = ey;
                            break;
                        case 4: // Ellipse: fixed corner at start
                            sp->w = fabsf(dx) / 2;
                            sp->extra2 = fabsf(dy) / 2;
                            sp->x = sx + (dx > 0 ? sp->w : -sp->w);
                            sp->y = sy + (dy > 0 ? sp->extra2 : -sp->extra2);
                            break;
                        // case 5 (polygon) handled separately via vertex clicking
                        default: break;
                    }
                }

                if (drawing2d && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                    drawing2d = false;
                    if (build2dPrimitive == 2) {
                        // Triangle: release sets vertex 2, now wait for click to set vertex 3
                        triVertex = 1;
                    } else {
                        Sprite2DPart *sp = &build2d.parts[build2d.selected];
                        bool tooSmall = false;
                        if (sp->type == SP_RECT && sp->w < 3 && sp->h < 3) tooSmall = true;
                        if (sp->type == SP_CIRCLE && sp->w < 2) tooSmall = true;
                        if (sp->type == SP_ELLIPSE && sp->w < 2 && sp->extra2 < 2) tooSmall = true;
                        if (tooSmall) { build2d.count--; build2d.selected = -1; }
                    }
                }
            }

            // Resize selected with scroll
            if (build2d.selected >= 0 && !drawing2d) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0) {
                    Sprite2DPart *sp = &build2d.parts[build2d.selected];
                    float delta = wheel * 2.0f;
                    if (sp->type == SP_LINE) {
                        sp->extra1 += delta * 0.5f;
                        if (sp->extra1 < 1) sp->extra1 = 1;
                        if (sp->extra1 > 20) sp->extra1 = 20;
                        lastLineWidth = sp->extra1;
                    } else {
                        sp->w += delta;
                        if (sp->type == SP_RECT) sp->h += delta;
                        if (sp->type == SP_ELLIPSE) sp->extra2 += delta * 0.7f;
                        if (sp->w < 1) sp->w = 1;
                    }
                }
            }

            // Delete selected
            if (build2d.selected >= 0 && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE))) {
                for (int i = build2d.selected; i < build2d.count - 1; i++)
                    build2d.parts[i] = build2d.parts[i + 1];
                build2d.count--;
                build2d.selected = -1;
            }

            // Escape deselect
            if (IsKeyPressed(KEY_ESCAPE)) build2d.selected = -1;

            // Color palette for selected part
            // (handled in the draw section)

            // Save sprite to file
            if (IsKeyPressed(KEY_ENTER) && build2d.count > 0) {
                char path[64];
                snprintf(path, sizeof(path), "objects/%s.spr2d", build2d.name);
                for (char *c = path + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
                MakeDirectory("objects");
                SaveSprite2D(path, build2d.parts, build2d.count);

                // Also print to console
                printf("\n// --- 2D Sprite: %s ---\n", build2d.name);
                printf("static Sprite2DPart %sParts[] = {\n", build2d.name);
                for (int i = 0; i < build2d.count; i++) {
                    Sprite2DPart *sp = &build2d.parts[i];
                    switch (sp->type) {
                        case SP_RECT: printf("    SRECT(%.0f, %.0f, %.0f, %.0f, COL(%d,%d,%d,%d)),\n",
                            sp->x, sp->y, sp->w, sp->h, sp->color.r, sp->color.g, sp->color.b, sp->color.a); break;
                        case SP_CIRCLE: printf("    SCIRCLE(%.0f, %.0f, %.0f, COL(%d,%d,%d,%d)),\n",
                            sp->x, sp->y, sp->w, sp->color.r, sp->color.g, sp->color.b, sp->color.a); break;
                        case SP_ELLIPSE: printf("    SELLIPSE(%.0f, %.0f, %.0f, %.0f, COL(%d,%d,%d,%d)),\n",
                            sp->x, sp->y, sp->w, sp->extra2, sp->color.r, sp->color.g, sp->color.b, sp->color.a); break;
                        case SP_TRIANGLE: printf("    STRIANGLE(%.0f, %.0f, %.0f, %.0f, %.0f, %.0f, COL(%d,%d,%d,%d)),\n",
                            sp->x, sp->y, sp->w, sp->h, sp->extra1, sp->extra2,
                            sp->color.r, sp->color.g, sp->color.b, sp->color.a); break;
                        case SP_LINE: printf("    SLINE(%.0f, %.0f, %.0f, %.0f, %.0f, COL(%d,%d,%d,%d)),\n",
                            sp->x, sp->y, sp->w, sp->h, sp->extra1,
                            sp->color.r, sp->color.g, sp->color.b, sp->color.a); break;
                        case SP_POLYGON: printf("    SPOLYGON(%.0f, %.0f, %.0f, %d, %.2f, COL(%d,%d,%d,%d)),\n",
                            sp->x, sp->y, sp->w, (int)sp->h, sp->extra1,
                            sp->color.r, sp->color.g, sp->color.b, sp->color.a); break;
                    }
                }
                printf("};\n// ---\n\n");

                // Reload sprite files so it appears in palette
                LoadSpriteFiles();

                // Clear canvas and pick next available name
                build2d.count = 0;
                build2d.selected = -1;
                // Auto-increment name: Sprite, Sprite2, Sprite3, ...
                for (int n = 1; n < 100; n++) {
                    char tryName[32], tryPath[64];
                    if (n == 1) snprintf(tryName, sizeof(tryName), "Sprite");
                    else snprintf(tryName, sizeof(tryName), "Sprite%d", n);
                    snprintf(tryPath, sizeof(tryPath), "objects/%s.spr2d", tryName);
                    if (!FileExists(tryPath)) {
                        strncpy(build2d.name, tryName, 31);
                        break;
                    }
                }

                showExport = true;
            }
        }

        // Undo last placed (Ctrl+Z)
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER)) && IsKeyPressed(KEY_Z)) {
            for (int i = numPlaced - 1; i >= 0; i--) {
                if (placed[i].active) { placed[i].active = false; break; }
            }
        }

        // Clear (Ctrl+N)
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER)) && IsKeyPressed(KEY_N)) {
            if (mode == MODE_BUILD2D) {
                build2d.count = 0;
                build2d.selected = -1;
                triVertex = 0;
                drawing2d = false;
            } else if (mode == MODE_BUILD) {
                buildObj.count = 0;
                buildObj.selected = -1;
            } else {
                // Reset map to all grass
                char layout[EDITOR_MAP_W * EDITOR_MAP_H];
                memset(layout, '1', sizeof(layout));
                Map3DLoad(&map, layout, EDITOR_MAP_W, EDITOR_MAP_H, TILE_SZ, tileDefs, NUM_TILEDEFS);
                // Clear all placed objects and sprites
                numPlaced = 0;
                selectedObject = -1;
                numPlacedSprites = 0;
                selectedSprite = -1;
            }
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){30, 30, 35, 255});

        if (mode == MODE_BUILD2D) {
            // --- 2D Build workspace ---
            int cx = sw / 2, cy = sh / 2;
            // Grid
            for (int gx = -200; gx <= 200; gx += 20) {
                Color gc = (gx == 0) ? (Color){80,80,90,255} : (Color){45,45,50,255};
                DrawLine(cx + gx, cy - 200, cx + gx, cy + 200, gc);
            }
            for (int gy = -200; gy <= 200; gy += 20) {
                Color gc = (gy == 0) ? (Color){80,80,90,255} : (Color){45,45,50,255};
                DrawLine(cx - 200, cy + gy, cx + 200, cy + gy, gc);
            }
            // Origin crosshair
            DrawLine(cx - 8, cy, cx + 8, cy, RED);
            DrawLine(cx, cy - 8, cx, cy + 8, GREEN);

            // Draw all 2D parts
            for (int i = 0; i < build2d.count; i++) {
                DrawSprite2DPart(&build2d.parts[i], cx, cy, 1.0f);
                // Selected highlight: draw corner handles instead of full overlay
                if (i == build2d.selected) {
                    Sprite2DPart *sp = &build2d.parts[i];
                    switch (sp->type) {
                        case SP_LINE: {
                            // Draw small squares at endpoints
                            DrawRectangle((int)(cx + sp->x) - 3, (int)(cy + sp->y) - 3, 6, 6, YELLOW);
                            DrawRectangle((int)(cx + sp->w) - 3, (int)(cy + sp->h) - 3, 6, 6, YELLOW);
                            break;
                        }
                        default:
                            DrawSprite2DPartOutline(sp, cx, cy, 1.0f, YELLOW);
                            break;
                    }
                }
            }

            // Polygon vertex preview
            if (polyDrawing && polyVertCount > 0) {
                for (int v = 0; v < polyVertCount; v++) {
                    DrawCircle(cx + polyVerts[v].x, cy + polyVerts[v].y, 4, YELLOW);
                    if (v > 0)
                        DrawLineEx(
                            (Vector2){cx + polyVerts[v-1].x, cy + polyVerts[v-1].y},
                            (Vector2){cx + polyVerts[v].x, cy + polyVerts[v].y}, 2, YELLOW);
                }
                DrawLineEx(
                    (Vector2){cx + polyVerts[polyVertCount-1].x, cy + polyVerts[polyVertCount-1].y},
                    mouse, 2, (Color){255,255,0,120});
                if (polyVertCount >= 2) {
                    DrawLineEx(mouse,
                        (Vector2){cx + polyVerts[0].x, cy + polyVerts[0].y}, 1, (Color){255,255,0,60});
                    for (int t = 0; t < polyVertCount - 1; t++) {
                        DrawTriangle(
                            (Vector2){cx+polyVerts[0].x, cy+polyVerts[0].y},
                            (Vector2){cx+polyVerts[t].x, cy+polyVerts[t].y},
                            (Vector2){cx+polyVerts[t+1].x, cy+polyVerts[t+1].y}, (Color){255,200,0,40});
                        DrawTriangle(
                            (Vector2){cx+polyVerts[0].x, cy+polyVerts[0].y},
                            (Vector2){cx+polyVerts[t+1].x, cy+polyVerts[t+1].y},
                            (Vector2){cx+polyVerts[t].x, cy+polyVerts[t].y}, (Color){255,200,0,40});
                    }
                    DrawTriangle(
                        (Vector2){cx+polyVerts[0].x, cy+polyVerts[0].y},
                        (Vector2){cx+polyVerts[polyVertCount-1].x, cy+polyVerts[polyVertCount-1].y},
                        mouse, (Color){255,200,0,30});
                    DrawTriangle(
                        (Vector2){cx+polyVerts[0].x, cy+polyVerts[0].y}, mouse,
                        (Vector2){cx+polyVerts[polyVertCount-1].x, cy+polyVerts[polyVertCount-1].y},
                        (Color){255,200,0,30});
                }
                float dFirst = Vector2Distance(mouse, (Vector2){cx+polyVerts[0].x, cy+polyVerts[0].y});
                if (polyVertCount >= 3 && dFirst < 15)
                    DrawCircle(cx+polyVerts[0].x, cy+polyVerts[0].y, 8, (Color){255,255,0,80});
                DrawText(TextFormat("Vertices: %d  [Enter] finish  Click near start to close", polyVertCount),
                    sw/2-160, sh-130, 14, YELLOW);
            }

            // Color palette (always visible, sets drawing color + selected part color)
            {
                int cpY = sh - 200;
                Color palette[] = {
                    {230,41,55,255},   {255,161,0,255},   {253,249,0,255},  {0,228,48,255},
                    {0,121,241,255},   {200,122,255,255}, {255,109,194,255},{255,255,255,255},
                    {130,130,130,255}, {80,80,80,255},    {40,40,40,255},   {0,0,0,255},
                    {127,106,79,255},  {220,190,150,255}, {160,120,60,255}, {100,70,40,255},
                    {30,120,30,255},   {80,160,80,255},   {40,130,200,255}, {200,60,60,255},
                    {180,140,50,255},  {100,100,110,255}, {60,40,25,255},   {200,180,140,255},
                };
                int numColors = sizeof(palette)/sizeof(palette[0]);
                int swatchSize = 18;
                int colsPerRow = 6;
                int px = sw - 140;
                DrawRectangle(px - 5, cpY - 5, 125, 100, (Color){20,20,25,220});
                // Current color preview
                DrawRectangle(px, cpY - 3, 20, 12, selectedColor);
                DrawRectangleLines(px, cpY - 3, 20, 12, WHITE);
                DrawText("Color", px + 24, cpY - 2, 10, WHITE);
                cpY += 14;
                for (int ci = 0; ci < numColors; ci++) {
                    int col2 = ci % colsPerRow, row = ci / colsPerRow;
                    int sx = px + col2 * (swatchSize + 2);
                    int sy = cpY + row * (swatchSize + 2);
                    Rectangle sr = {(float)sx, (float)sy, (float)swatchSize, (float)swatchSize};
                    DrawRectangleRec(sr, palette[ci]);
                    // Highlight current selected color
                    if (selectedColor.r == palette[ci].r && selectedColor.g == palette[ci].g &&
                        selectedColor.b == palette[ci].b)
                        DrawRectangleLinesEx(sr, 2, WHITE);
                    if (CheckCollisionPointRec(mouse, sr) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                        selectedColor = palette[ci];
                        // Also update selected part if one is selected
                        if (build2d.selected >= 0 && build2d.selected < build2d.count)
                            build2d.parts[build2d.selected].color = palette[ci];
                    }
                }
            }
        }

        if (mode != MODE_BUILD2D) {
        BeginMode3D(camera);
            if (mode == MODE_BUILD) {
                // Build mode: draw on a work surface
                DrawPlane((Vector3){0, 0, 0}, (Vector2){10, 10}, (Color){50, 50, 55, 255});
                DrawGrid(10, 1.0f);

                // Draw all build parts
                for (int i = 0; i < buildObj.count; i++) {
                    DrawPart(&buildObj.parts[i], (Vector3){0,0,0}, 0);
                }
                // Draw attached sprites in build mode
                for (int s = 0; s < buildObj.spriteCount; s++) {
                    AttachedSprite *as = &buildObj.sprites[s];
                    float rotY2 = (as->displayMode == SPRITE_BILLBOARD)
                        ? atan2f(camera.position.z - as->offset.z, camera.position.x - as->offset.x) - PI * 0.5f
                        : 0;
                    DrawSprite2DAsPlane(as->parts, as->partCount, as->offset, as->scale, rotY2, camera);
                }
            } else {
                // Draw map
                Map3DDrawAll(&map);
                if (showGrid) Map3DDrawGrid(&map, (Color){60,60,70,80});

                // Draw placed objects and their attached sprites
                for (int i = 0; i < numPlaced; i++) {
                    if (!placed[i].active) continue;
                    PrefabDef *pf = &prefabs[placed[i].prefabIdx];
                    DrawObject3DScaled(pf->parts, pf->partCount,
                        placed[i].pos, placed[i].rotY, placed[i].scale);
                    // Draw attached sprites
                    for (int s = 0; s < pf->spriteCount; s++) {
                        AttachedSprite *as = &pf->sprites[s];
                        // Transform sprite offset by object position, rotation, and scale
                        float cosR = cosf(placed[i].rotY), sinR = sinf(placed[i].rotY);
                        float sx = as->offset.x * placed[i].scale.x;
                        float sy = as->offset.y * placed[i].scale.y;
                        float sz = as->offset.z * placed[i].scale.z;
                        Vector3 sprPos = {
                            placed[i].pos.x + sx * cosR - sz * sinR,
                            placed[i].pos.y + sy,
                            placed[i].pos.z + sx * sinR + sz * cosR
                        };
                        float avgScale = (placed[i].scale.x + placed[i].scale.y + placed[i].scale.z) / 3.0f;
                        float rotY2 = (as->displayMode == SPRITE_BILLBOARD)
                            ? atan2f(camera.position.z - sprPos.z, camera.position.x - sprPos.x) - PI * 0.5f
                            : placed[i].rotY;
                        DrawSprite2DAsPlane(as->parts, as->partCount, sprPos, as->scale * avgScale, rotY2, camera);
                    }
                }
            }

            // Draw all placed sprites as 3D geometry
            for (int i = 0; i < numPlacedSprites; i++) {
                if (!placedSprites[i].active) continue;
                float rotY2 = placedSprites[i].rotY;
                if (placedSprites[i].displayMode == SPRITE_BILLBOARD) {
                    float dx = camera.position.x - placedSprites[i].pos.x;
                    float dz = camera.position.z - placedSprites[i].pos.z;
                    rotY2 = atan2f(dz, dx) - PI * 0.5f;
                }
                DrawSprite2DAsPlane(placedSprites[i].parts, placedSprites[i].partCount,
                    placedSprites[i].pos, placedSprites[i].scale, rotY2, camera);
                DrawCircle3D(placedSprites[i].pos, 0.5f, (Vector3){1,0,0}, 90, (Color){0,0,0,30});
            }

            // Hover indicator (drawn before gizmos so gizmos stay on top)
            if (hoverValid && !overUI) {
                float hx = hoverTX * TILE_SZ + TILE_SZ/2;
                float hz = hoverTZ * TILE_SZ + TILE_SZ/2;

                if (mode == MODE_TILES) {
                    int origTile = map.tiles[hoverTZ][hoverTX];
                    map.tiles[hoverTZ][hoverTX] = selectedTile;
                    Map3DDrawTile(&map, hoverTX, hoverTZ);
                    map.tiles[hoverTZ][hoverTX] = origTile;
                    float hy = 0.05f;
                    TileDef *preview = &map.defs[selectedTile];
                    if (preview->type == TILE_WALL || preview->type == TILE_PLATFORM)
                        hy = preview->height;
                    DrawCubeWires((Vector3){hx, hy / 2.0f, hz}, TILE_SZ, hy + 0.1f, TILE_SZ,
                        (Color){255,255,255,120});
                } else if (IsKeyDown(KEY_LEFT_SHIFT) && (mode == MODE_OBJECTS || mode == MODE_TILES)) {
                    float hy = Map3DHeightAt(&map, groundHit);
                    if (placingSprite && selectedSpriteFile >= 0) {
                        // Shadow circle for sprite preview (sprite drawn in 2D after EndMode3D)
                        DrawCircle3D((Vector3){groundHit.x, hy + 0.05f, groundHit.z}, 0.5f,
                            (Vector3){1,0,0}, 90, (Color){180,150,255,100});
                    } else if (!placingSprite && mode == MODE_OBJECTS && selectedPrefab >= 0) {
                        PrefabDef *pf = &prefabs[selectedPrefab];
                        Vector3 previewPos = {groundHit.x, hy, groundHit.z};
                        Object3D previewObj = { pf->parts, pf->partCount, previewPos, 0 };
                        DrawObject3D(&previewObj);
                        // Draw attached sprites on preview
                        for (int s = 0; s < pf->spriteCount; s++) {
                            AttachedSprite *as = &pf->sprites[s];
                            Vector3 sprPos = {
                                previewPos.x + as->offset.x,
                                previewPos.y + as->offset.y,
                                previewPos.z + as->offset.z
                            };
                            float rotY2 = (as->displayMode == SPRITE_BILLBOARD)
                                ? atan2f(camera.position.z - sprPos.z, camera.position.x - sprPos.x) - PI * 0.5f
                                : 0;
                            DrawSprite2DAsPlane(as->parts, as->partCount, sprPos, as->scale, rotY2, camera);
                        }
                        DrawCircle3D((Vector3){groundHit.x, hy + 0.05f, groundHit.z}, 0.5f,
                            (Vector3){1,0,0}, 90, (Color){255,255,0,150});
                    }
                }
            }

        EndMode3D();
        } // end if (mode != MODE_BUILD2D)

        // Sprite preview when holding shift with sprite selected
        if (mode != MODE_BUILD2D && IsKeyDown(KEY_LEFT_SHIFT) && placingSprite &&
            selectedSpriteFile >= 0 && selectedSpriteFile < numSpriteFiles &&
            hoverValid && !overUI) {
            float hy = Map3DHeightAt(&map, groundHit) + 1.0f;
            Vector3 previewPos = {groundHit.x, hy, groundHit.z};
            DrawSprite2DAt3D(spriteParts[selectedSpriteFile], spritePartCounts[selectedSpriteFile],
                previewPos, 1.0f, camera);
        }

        // Selected sprite indicator (2D overlay)
        if ((mode == MODE_TILES || mode == MODE_OBJECTS) && selectedSprite >= 0 && selectedSprite < numPlacedSprites &&
            placedSprites[selectedSprite].active) {
            Vector2 sp = GetWorldToScreen(placedSprites[selectedSprite].pos, camera);
            DrawCircleLines(sp.x, sp.y, 20, YELLOW);
        }

        // --- Gizmo overlay (2D screen-space, guaranteed on top) ---
        {
            Vector3 gCenter = {0};
            bool drawGizmo = false;
            float objRotY = 0;

            if (mode == MODE_OBJECTS && selectedSprite >= 0 && selectedSprite < numPlacedSprites && placedSprites[selectedSprite].active) {
                gCenter = placedSprites[selectedSprite].pos;
                drawGizmo = true;
            } else if (mode == MODE_OBJECTS && selectedObject >= 0 && selectedObject < numPlaced && placed[selectedObject].active) {
                PlacedObject *sel = &placed[selectedObject];
                PrefabDef *gpf2 = &prefabs[sel->prefabIdx];
                gCenter = sel->pos;
                gCenter.y += PrefabCenterY(gpf2->parts, gpf2->partCount, sel->scale);
                objRotY = sel->rotY;
                drawGizmo = true;
            } else if (mode == MODE_BUILD && buildObj.selectedSprite >= 0 && buildObj.selectedSprite < buildObj.spriteCount) {
                gCenter = buildObj.sprites[buildObj.selectedSprite].offset;
                drawGizmo = true;
            } else if (mode == MODE_BUILD && buildObj.selected >= 0 && buildObj.selected < buildObj.count) {
                Part *sp = &buildObj.parts[buildObj.selected];
                gCenter = sp->offset;
                gCenter.y += (sp->type == PART_CYLINDER ? sp->size.y * 0.5f : 0);
                drawGizmo = true;
            }

            if (drawGizmo) {
                // Skip if gizmo center is behind the camera
                Vector3 camFwd2 = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                Vector3 toCenter = Vector3Subtract(gCenter, camera.position);
                if (Vector3DotProduct(camFwd2, toCenter) <= 0) drawGizmo = false;
            }
            if (drawGizmo) {
                Vector2 sc = GetWorldToScreen(gCenter, camera);
                Vector2 axisEnds[3];
                float gArmLen = GizmoScreenEnds(gCenter, camera, axisEnds);
                Vector2 sxEnd = axisEnds[0], syEnd = axisEnds[1], szEnd = axisEnds[2];
                int tipR = (int)(gArmLen * 0.1f);
                if (tipR < 4) tipR = 4;

                bool isMove = (gizmoMode == GIZMO_MOVE);
                bool isRot = (gizmoMode == GIZMO_ROTATE);
                bool isScl = (gizmoMode == GIZMO_SCALE);

                if (isMove || isScl) {
                    Color xCol = (draggingAxis == 0) ? YELLOW : RED;
                    DrawLineEx(sc, sxEnd, 3, xCol);
                    if (isMove) DrawCircleV(sxEnd, tipR, xCol);
                    else DrawRectangle(sxEnd.x-tipR, sxEnd.y-tipR, tipR*2, tipR*2, xCol);

                    Color yCol = (draggingAxis == 1) ? YELLOW : GREEN;
                    DrawLineEx(sc, syEnd, 3, yCol);
                    if (isMove) DrawCircleV(syEnd, tipR, yCol);
                    else DrawRectangle(syEnd.x-tipR, syEnd.y-tipR, tipR*2, tipR*2, yCol);

                    Color zCol = (draggingAxis == 2) ? YELLOW : BLUE;
                    DrawLineEx(sc, szEnd, 3, zCol);
                    if (isMove) DrawCircleV(szEnd, tipR, zCol);
                    else DrawRectangle(szEnd.x-tipR, szEnd.y-tipR, tipR*2, tipR*2, zCol);

                    DrawCircleV(sc, tipR+2, (draggingAxis == 3) ? YELLOW : WHITE);
                }

                if (isRot) {
                    float ringR = gArmLen;
                    int segs = 32;
                    // Draw 3 rings: X (red, YZ plane), Y (green, XZ plane), Z (blue, XY plane)
                    Color ringColors[3] = { RED, GREEN, BLUE };
                    for (int ax = 0; ax < 3; ax++) {
                        Color col = (draggingAxis == ax) ? YELLOW : ringColors[ax];
                        for (int s = 0; s < segs; s++) {
                            float a0 = (float)s / segs * 2.0f * PI;
                            float a1 = (float)(s+1) / segs * 2.0f * PI;
                            Vector3 w0 = gCenter, w1 = gCenter;
                            if (ax == 0) { // X: ring in YZ plane
                                w0.y += cosf(a0) * 1.0f; w0.z += sinf(a0) * 1.0f;
                                w1.y += cosf(a1) * 1.0f; w1.z += sinf(a1) * 1.0f;
                            } else if (ax == 1) { // Y: ring in XZ plane
                                w0.x += cosf(a0) * 1.0f; w0.z += sinf(a0) * 1.0f;
                                w1.x += cosf(a1) * 1.0f; w1.z += sinf(a1) * 1.0f;
                            } else { // Z: ring in XY plane
                                w0.x += cosf(a0) * 1.0f; w0.y += sinf(a0) * 1.0f;
                                w1.x += cosf(a1) * 1.0f; w1.y += sinf(a1) * 1.0f;
                            }
                            // Project to screen and normalize to ringR
                            Vector2 s0 = GetWorldToScreen(w0, camera);
                            Vector2 s1 = GetWorldToScreen(w1, camera);
                            Vector2 d0 = Vector2Subtract(s0, sc);
                            Vector2 d1 = Vector2Subtract(s1, sc);
                            float l0 = Vector2Length(d0), l1 = Vector2Length(d1);
                            if (l0 > 0) d0 = Vector2Scale(d0, ringR / l0);
                            if (l1 > 0) d1 = Vector2Scale(d1, ringR / l1);
                            Vector2 p0 = Vector2Add(sc, d0);
                            Vector2 p1 = Vector2Add(sc, d1);
                            DrawLineEx(p0, p1, 2, col);
                        }
                    }
                    // Direction arrow for Y rotation
                    float rcs = cosf(objRotY), rsn = sinf(objRotY);
                    Vector2 wDir = GetWorldToScreen((Vector3){gCenter.x+rcs, gCenter.y, gCenter.z+rsn}, camera);
                    Vector2 aDir = Vector2Subtract(wDir, sc);
                    float aLen = Vector2Length(aDir);
                    if (aLen > 0) aDir = Vector2Scale(aDir, ringR / aLen);
                    Vector2 arrowEnd = Vector2Add(sc, aDir);
                    DrawLineEx(sc, arrowEnd, 2, (Color){255,100,100,255});
                    DrawCircleV(arrowEnd, tipR, (Color){255,100,100,255});
                    DrawCircleV(sc, tipR, WHITE);
                }

                if (isMove || isScl) {
                    DrawText("X", sxEnd.x+10, sxEnd.y-5, 12, RED);
                    DrawText("Y", syEnd.x+10, syEnd.y-5, 12, GREEN);
                    DrawText("Z", szEnd.x+10, szEnd.y-5, 12, BLUE);
                }
            }
        }

        // (old 3D gizmo passes replaced by 2D above)

        // --- UI ---
        // Left panel background
        DrawRectangle(0, 0, 175, sh, (Color){20,20,25,230});
        DrawLine(175, 0, 175, sh, (Color){60,60,70,255});

        // Mode indicator
        const char *modeLabel = mode == MODE_TILES ? "TILES" : (mode == MODE_OBJECTS ? "OBJECTS" :
            (mode == MODE_BUILD ? "BUILD 3D" : "BUILD 2D"));
        DrawText(modeLabel, 10, 10, 20, GOLD);
        DrawText("[TAB] switch mode", 10, 32, 10, (Color){120,120,130,255});

        if (mode == MODE_BUILD) {
            // Editable name field
            {
                DrawText("Name:", 10, 55, 10, (Color){150,150,150,255});
                Rectangle nameRect = {50, 52, 118, 18};
                bool nameHov = CheckCollisionPointRec(mouse, nameRect);
                bool nameActive = (textEditing && textTarget == buildObj.name);
                DrawRectangleRec(nameRect, nameActive ? (Color){50,50,60,255} : (nameHov ? (Color){40,40,45,255} : (Color){30,30,35,255}));
                DrawRectangleLinesEx(nameRect, 1, nameActive ? GOLD : (nameHov ? (Color){100,100,110,255} : (Color){60,60,65,255}));
                if (nameActive) {
                    // Draw text with cursor
                    char before[32] = {0};
                    strncpy(before, buildObj.name, textCursor);
                    before[textCursor] = '\0';
                    int cx = 54 + MeasureText(before, 11);
                    DrawText(buildObj.name, 54, 56, 11, WHITE);
                    if ((int)(GetTime() * 2) % 2 == 0) DrawLine(cx, 54, cx, 68, GOLD);
                } else {
                    DrawText(buildObj.name, 54, 56, 11, WHITE);
                }
                if (nameHov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !nameActive)
                    StartTextEdit(buildObj.name, 32);
            }
            DrawText(TextFormat("Parts: %d/%d", buildObj.count, MAX_BUILD_PARTS), 10, 72, 10, (Color){180,180,180,255});

            // Primitive selector
            const char *primNames[] = { "Cube", "Sphere", "Cylinder" };
            DrawText("Add primitive:", 10, 92, 10, WHITE);
            for (int i = 0; i < 3; i++) {
                int y = 107 + i * 22;
                bool sel = (i == buildPrimitive);
                Rectangle ir = {8, (float)y, 160, 20};
                bool hov = CheckCollisionPointRec(mouse, ir);
                DrawRectangle(8, y, 160, 20, sel ? (Color){60,50,30,255} : (hov ? (Color){45,40,35,255} : (Color){30,30,35,255}));
                if (sel) DrawRectangleLinesEx(ir, 1, GOLD);
                if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) buildPrimitive = i;
                DrawText(TextFormat("%d: %s", i+1, primNames[i]), 14, y + 4, 11, sel ? WHITE : (Color){150,150,150,255});
            }

            // Parts list
            DrawText("Parts:", 10, 180, 10, WHITE);
            for (int i = 0; i < buildObj.count; i++) {
                int y = 195 + i * 18;
                bool sel = (i == buildObj.selected);
                const char *tname = buildObj.parts[i].type == PART_CUBE ? "Cube" :
                    buildObj.parts[i].type == PART_SPHERE ? "Sphere" : "Cyl";
                Rectangle ir = {8, (float)y, 160, 16};
                bool hov = CheckCollisionPointRec(mouse, ir);
                DrawRectangle(8, y, 160, 16, sel ? (Color){60,50,30,255} : (hov ? (Color){40,38,33,255} : (Color){30,30,35,255}));
                if (sel) DrawRectangleLinesEx(ir, 1, GOLD);
                if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { buildObj.selected = i; buildObj.selectedSprite = -1; }
                // Color swatch
                DrawRectangle(12, y + 2, 12, 12, buildObj.parts[i].color);
                DrawText(TextFormat("%s", tname), 28, y + 3, 10, sel ? WHITE : (Color){150,150,150,255});
            }
            // Attached sprites in parts list
            for (int i = 0; i < buildObj.spriteCount; i++) {
                int y = 195 + (buildObj.count + i) * 18;
                bool sel = (i == buildObj.selectedSprite);
                Rectangle ir = {8, (float)y, 160, 16};
                bool hov = CheckCollisionPointRec(mouse, ir);
                DrawRectangle(8, y, 160, 16, sel ? (Color){50,40,60,255} : (hov ? (Color){40,35,45,255} : (Color){30,30,35,255}));
                if (sel) DrawRectangleLinesEx(ir, 1, (Color){180,150,255,255});
                if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { buildObj.selectedSprite = i; buildObj.selected = -1; }
                DrawText(TextFormat("Spr: %s", buildObj.sprites[i].filename), 14, y + 3, 10, sel ? WHITE : (Color){150,150,170,255});
            }

            // Color palette (when a part is selected)
            int totalListItems = buildObj.count + buildObj.spriteCount;
            int nextY = 195 + totalListItems * 18 + 10;
            if (buildObj.selected >= 0 && buildObj.selected < buildObj.count) {
                int cpY = nextY;
                DrawText("Color:", 10, cpY, 10, WHITE);
                cpY += 14;
                Color palette[] = {
                    {230,41,55,255},   {255,161,0,255},   {253,249,0,255},  {0,228,48,255},
                    {0,121,241,255},   {200,122,255,255}, {255,109,194,255},{255,255,255,255},
                    {130,130,130,255}, {80,80,80,255},    {40,40,40,255},   {0,0,0,255},
                    {127,106,79,255},  {220,190,150,255}, {160,120,60,255}, {100,70,40,255},
                    {30,120,30,255},   {80,160,80,255},   {40,130,200,255}, {200,60,60,255},
                    {180,140,50,255},  {100,100,110,255}, {60,40,25,255},   {200,180,140,255},
                };
                int numColors = sizeof(palette)/sizeof(palette[0]);
                int colsPerRow = 6;
                int swatchSize = 22;
                int padding = 4;
                int numRows = (numColors + colsPerRow - 1) / colsPerRow;
                for (int ci = 0; ci < numColors; ci++) {
                    int col = ci % colsPerRow;
                    int row = ci / colsPerRow;
                    int sx = 10 + col * (swatchSize + padding);
                    int sy = cpY + row * (swatchSize + padding);
                    Rectangle sr = {(float)sx, (float)sy, (float)swatchSize, (float)swatchSize};
                    DrawRectangleRec(sr, palette[ci]);
                    // Highlight if matches current part color
                    Part *sp = &buildObj.parts[buildObj.selected];
                    if (sp->color.r == palette[ci].r && sp->color.g == palette[ci].g &&
                        sp->color.b == palette[ci].b)
                        DrawRectangleLinesEx(sr, 2, WHITE);
                    else
                        DrawRectangleLines(sx, sy, swatchSize, swatchSize, (Color){60,60,60,255});
                    // Click to set color
                    if (CheckCollisionPointRec(mouse, sr) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                        sp->color = palette[ci];
                }
                nextY = cpY + numRows * (swatchSize + padding) + 10;
            }

            // Sprite billboard palette
            if (numSpriteFiles > 0) {
                int sprY = nextY;
                DrawText("2D Sprites:", 10, sprY, 12, (Color){200,180,255,255});
                for (int i = 0; i < numSpriteFiles; i++) {
                    int y = sprY + 16 + i * 22;
                    bool sel = (i == selectedSpriteFile);
                    Rectangle ir = {8, (float)y, 160, 20};
                    bool hov = CheckCollisionPointRec(mouse, ir);
                    DrawRectangle(8, y, 160, 20, sel ? (Color){50,40,60,255} : (hov ? (Color){40,35,45,255} : (Color){30,30,35,255}));
                    if (sel) DrawRectangleLinesEx(ir, 1, (Color){180,150,255,255});
                    if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                        { selectedSpriteFile = i; placingSprite = true; }
                    DrawText(spriteNames[i], 14, y + 4, 11, sel ? WHITE : (Color){150,150,170,255});
                }
            }

            DrawText("[F] Add part  [DEL] Remove", 10, sh - 90, 10, (Color){120,120,130,255});
            DrawText("[Z] Move  [X] Rotate  [C] Scale", 10, sh - 75, 10, (Color){120,120,130,255});
            DrawText("Shift+Click: Place sprite", 10, sh - 60, 10, (Color){120,120,130,255});
            DrawText(TextFormat("[ENTER] Save as \"%s\"", buildObj.name), 10, sh - 45, 10, GOLD);

            const char *gmNames[] = { "", "MOVE", "ROTATE", "SCALE" };
            Color gmCol = gizmoMode == GIZMO_MOVE ? GREEN : (gizmoMode == GIZMO_ROTATE ? YELLOW : ORANGE);
            DrawText(TextFormat("Gizmo: %s", gmNames[gizmoMode]), 10, sh - 105, 12, gmCol);
        } else if (mode == MODE_BUILD2D) {
            DrawText("Build 2D Sprite:", 10, 55, 12, WHITE);
            DrawText(TextFormat("Parts: %d/%d", build2d.count, MAX_BUILD2D_PARTS), 10, 72, 10, (Color){180,180,180,255});

            // Primitive selector
            const char *primNames2d[] = { "Rect", "Circle", "Triangle", "Line", "Ellipse", "Polygon" };
            DrawText("Add shape:", 10, 92, 10, WHITE);
            for (int i = 0; i < 6; i++) {
                int y = 107 + i * 22;
                bool sel = (i == build2dPrimitive);
                Rectangle ir = {8, (float)y, 160, 20};
                bool hov = CheckCollisionPointRec(mouse, ir);
                DrawRectangle(8, y, 160, 20, sel ? (Color){60,50,30,255} : (hov ? (Color){45,40,35,255} : (Color){30,30,35,255}));
                if (sel) DrawRectangleLinesEx(ir, 1, GOLD);
                if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) build2dPrimitive = i;
                DrawText(TextFormat("%d: %s", i+1, primNames2d[i]), 14, y + 4, 11, sel ? WHITE : (Color){150,150,150,255});
            }

            // Parts list
            int partsListY = 107 + 6 * 22 + 10;  // below shape menu
            DrawText("Parts:", 10, partsListY, 10, WHITE);
            for (int i = 0; i < build2d.count; i++) {
                int y = partsListY + 15 + i * 18;
                bool sel = (i == build2d.selected);
                const char *tname = build2d.parts[i].type == SP_RECT ? "Rect" :
                    build2d.parts[i].type == SP_CIRCLE ? "Circle" :
                    build2d.parts[i].type == SP_TRIANGLE ? "Tri" :
                    build2d.parts[i].type == SP_LINE ? "Line" :
                    build2d.parts[i].type == SP_POLYGON ? "Poly" : "Ellipse";
                Rectangle ir = {8, (float)y, 160, 16};
                bool hov = CheckCollisionPointRec(mouse, ir);
                DrawRectangle(8, y, 160, 16, sel ? (Color){60,50,30,255} : (hov ? (Color){40,38,33,255} : (Color){30,30,35,255}));
                if (sel) DrawRectangleLinesEx(ir, 1, GOLD);
                if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) build2d.selected = i;
                DrawRectangle(12, y + 2, 12, 12, build2d.parts[i].color);
                DrawText(tname, 28, y + 3, 10, sel ? WHITE : (Color){150,150,150,255});
            }

            DrawText("Click+drag: Draw shape", 10, sh - 105, 10, (Color){120,120,130,255});
            DrawText("Shift+click: Select/move", 10, sh - 90, 10, (Color){120,120,130,255});
            DrawText("Scroll: Resize selected", 10, sh - 75, 10, (Color){120,120,130,255});
            DrawText("[DEL] Remove  [ESC] Deselect", 10, sh - 60, 10, (Color){120,120,130,255});
            DrawText(TextFormat("[ENTER] Save as \"%s\"", build2d.name), 10, sh - 45, 10, GOLD);

        } else if (mode == MODE_TILES) {
            DrawText("Tile Palette:", 10, 55, 12, WHITE);
            for (int i = 0; i < (int)NUM_TILEDEFS; i++) {
                int y = 72 + i * 22;
                bool sel = (i == selectedTile);
                Rectangle itemRect = {8, (float)y, 160, 20};
                bool hover = CheckCollisionPointRec(mouse, itemRect);
                Color bg = sel ? (Color){60,50,30,255} : (hover ? (Color){45,40,35,255} : (Color){30,30,35,255});
                DrawRectangle(8, y, 160, 20, bg);
                if (sel) DrawRectangleLinesEx(itemRect, 1, GOLD);
                else if (hover) DrawRectangleLinesEx(itemRect, 1, (Color){100,90,70,255});

                // Click to select
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    selectedTile = i;

                // Color swatch
                DrawRectangle(12, y + 3, 14, 14, tileDefs[i].topColor);
                DrawRectangleLines(12, y + 3, 14, 14, (Color){80,80,80,255});

                // Label
                char label[32];
                snprintf(label, sizeof(label), "%c %s", i < 10 ? '0'+i : 'a'+(i-10), tileNames[i]);
                DrawText(label, 30, y + 4, 10, sel ? WHITE : (Color){150,150,150,255});
            }
            // Sprite billboard palette
            if (numSpriteFiles > 0) {
                int sprY = 72 + (int)NUM_TILEDEFS * 22 + 10;
                DrawText("2D Sprites:", 10, sprY, 12, (Color){200,180,255,255});
                for (int i = 0; i < numSpriteFiles; i++) {
                    int y = sprY + 16 + i * 22;
                    bool sel = (i == selectedSpriteFile);
                    Rectangle ir = {8, (float)y, 160, 20};
                    bool hov = CheckCollisionPointRec(mouse, ir);
                    DrawRectangle(8, y, 160, 20, sel ? (Color){50,40,60,255} : (hov ? (Color){40,35,45,255} : (Color){30,30,35,255}));
                    if (sel) DrawRectangleLinesEx(ir, 1, (Color){180,150,255,255});
                    if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                        { selectedSpriteFile = i; placingSprite = true; selectedPrefab = -1; }
                    DrawText(spriteNames[i], 14, y + 4, 11, sel ? WHITE : (Color){150,150,170,255});
                }
            }
            DrawText("[Q] Eyedropper  Sprite: Shift+S", 10, sh - 60, 10, (Color){120,120,130,255});
        } else {
            DrawText("Object Palette:", 10, 55, 12, WHITE);
            for (int i = 0; i < numPrefabs; i++) {
                int y = 72 + i * 26;
                bool sel = (i == selectedPrefab);
                bool isRenaming = (textEditing && textTarget == customNames[i]);
                Rectangle itemRect = {8, (float)y, 160, 24};
                bool hover = CheckCollisionPointRec(mouse, itemRect);
                Color bg = isRenaming ? (Color){50,50,60,255} : (sel ? (Color){60,50,30,255} : (hover ? (Color){45,40,35,255} : (Color){30,30,35,255}));
                DrawRectangle(8, y, 160, 24, bg);
                if (isRenaming) DrawRectangleLinesEx(itemRect, 1, GOLD);
                else if (sel) DrawRectangleLinesEx(itemRect, 1, GOLD);
                else if (hover) DrawRectangleLinesEx(itemRect, 1, (Color){100,90,70,255});

                // Click to select, double-click to rename
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !isRenaming) {
                    if (sel && !textEditing) {
                        // Already selected — start renaming
                        StartTextEdit(customNames[i], 32);
                    } else {
                        selectedPrefab = i; placingSprite = false; selectedSpriteFile = -1;
                    }
                }

                if (isRenaming) {
                    char before[32] = {0};
                    strncpy(before, customNames[i], textCursor);
                    before[textCursor] = '\0';
                    int cx = 14 + MeasureText(before, 12);
                    DrawText(customNames[i], 14, y + 6, 12, WHITE);
                    if ((int)(GetTime() * 2) % 2 == 0) DrawLine(cx, y + 4, cx, y + 20, GOLD);
                } else {
                    DrawText(TextFormat("%d: %s", i+1, prefabs[i].name), 14, y + 6, 12,
                        sel ? WHITE : (Color){150,150,150,255});
                }
            }
            // Sprite billboard palette
            if (numSpriteFiles > 0) {
                int sprY = 72 + numPrefabs * 26 + 10;
                DrawText("2D Sprites:", 10, sprY, 12, (Color){200,180,255,255});
                for (int i = 0; i < numSpriteFiles; i++) {
                    int y = sprY + 16 + i * 22;
                    bool sel = (i == selectedSpriteFile);
                    Rectangle ir = {8, (float)y, 160, 20};
                    bool hov = CheckCollisionPointRec(mouse, ir);
                    DrawRectangle(8, y, 160, 20, sel ? (Color){50,40,60,255} : (hov ? (Color){40,35,45,255} : (Color){30,30,35,255}));
                    if (sel) DrawRectangleLinesEx(ir, 1, (Color){180,150,255,255});
                    if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                        { selectedSpriteFile = i; placingSprite = true; selectedPrefab = -1; }
                    DrawText(spriteNames[i], 14, y + 4, 11, sel ? WHITE : (Color){150,150,170,255});
                }
            }
            // Placed objects list
            {
                int listY = 72 + numPrefabs * 26 + 10;
                if (numSpriteFiles > 0) listY += 16 + numSpriteFiles * 22 + 10;
                int activeCount = 0;
                for (int i = 0; i < numPlaced; i++) if (placed[i].active) activeCount++;
                for (int i = 0; i < numPlacedSprites; i++) if (placedSprites[i].active) activeCount++;
                if (activeCount > 0) {
                    DrawText(TextFormat("Placed (%d):", activeCount), 10, listY, 12, (Color){180,200,180,255});
                    listY += 16;
                    for (int i = 0; i < numPlaced; i++) {
                        if (!placed[i].active) continue;
                        int y = listY;
                        bool sel = (i == selectedObject);
                        Rectangle ir = {8, (float)y, 160, 18};
                        bool hov = CheckCollisionPointRec(mouse, ir);
                        DrawRectangle(8, y, 160, 18, sel ? (Color){40,60,40,255} : (hov ? (Color){35,45,35,255} : (Color){30,30,35,255}));
                        if (sel) DrawRectangleLinesEx(ir, 1, GREEN);
                        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            selectedObject = i; selectedSprite = -1;
                        }
                        DrawText(TextFormat("%s (%.0f,%.0f)", prefabs[placed[i].prefabIdx].name,
                            placed[i].pos.x, placed[i].pos.z), 14, y + 3, 10,
                            sel ? WHITE : (Color){150,170,150,255});
                        listY += 20;
                    }
                    for (int i = 0; i < numPlacedSprites; i++) {
                        if (!placedSprites[i].active) continue;
                        int y = listY;
                        bool sel = (i == selectedSprite);
                        Rectangle ir = {8, (float)y, 160, 18};
                        bool hov = CheckCollisionPointRec(mouse, ir);
                        DrawRectangle(8, y, 160, 18, sel ? (Color){40,40,60,255} : (hov ? (Color){35,35,45,255} : (Color){30,30,35,255}));
                        if (sel) DrawRectangleLinesEx(ir, 1, (Color){180,150,255,255});
                        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            selectedSprite = i; selectedObject = -1;
                        }
                        const char *dm = placedSprites[i].displayMode == SPRITE_BILLBOARD ? "B" : "P";
                        DrawText(TextFormat("[%s] %s (%.0f,%.0f)", dm, placedSprites[i].filename,
                            placedSprites[i].pos.x, placedSprites[i].pos.z), 14, y + 3, 10,
                            sel ? WHITE : (Color){150,150,170,255});
                        listY += 20;
                    }
                }
            }

            DrawText("[1-9] Prefab  [Shift+Click/F] Place", 10, sh - 120, 10, (Color){120,120,130,255});
            DrawText("[Click] Select  Sprite: Shift+S", 10, sh - 105, 10, (Color){120,120,130,255});
            DrawText("[E] Edit prefab in build mode", 10, sh - 150, 10, (Color){180,160,100,255});
            DrawText("[Z] Move  [X] Rotate  [C] Scale", 10, sh - 90, 10, (Color){120,120,130,255});
            DrawText("Drag gizmo axes with mouse", 10, sh - 75, 10, (Color){120,120,130,255});
            DrawText("[R/Q] Rotate  [T] Billboard/Plane  [DEL] Remove", 10, sh - 60, 10, (Color){120,120,130,255});
            DrawText("[ESC] Deselect", 10, sh - 45, 10, (Color){120,120,130,255});

            // Gizmo mode indicator
            const char *modeNames[] = { "", "MOVE", "ROTATE", "SCALE" };
            Color modeCol = gizmoMode == GIZMO_MOVE ? GREEN : (gizmoMode == GIZMO_ROTATE ? YELLOW : ORANGE);
            DrawText(TextFormat("Gizmo: %s", modeNames[gizmoMode]), 10, sh - 135, 12, modeCol);
        }

        // Bottom status
        const char *camLabel = camMode == CAM_ORBIT ? "ORBIT" : (camMode == CAM_FPS ? "FPS" : "FLY");
        Color camLabelCol = camMode == CAM_ORBIT ? WHITE : (camMode == CAM_FPS ? GREEN : SKYBLUE);
        DrawText(TextFormat("[V] Cycle camera: %s    [ESC] Back to Orbit", camLabel), 185, sh - 46, 11, camLabelCol);
        DrawText("[G] Grid  [M] Minimap  [Ctrl+Z] Undo  [Ctrl+N] Clear", 185, sh - 18, 11,
            (Color){100,100,110,200});
        if (camMode == CAM_ORBIT)
            DrawText("Right-drag: Orbit  Scroll: Zoom  Middle-drag: Pan  WASD: Move", 185, sh - 32, 11,
                (Color){100,100,110,200});
        else
            DrawText("Mouse: Look  WASD: Move  Shift: Fast  Space/Ctrl: Up/Down  ESC: Back", 185, sh - 32, 11,
                (Color){100,100,110,200});

        // Hover info
        if (hoverValid && mode == MODE_TILES) {
            int tileIdx = map.tiles[hoverTZ][hoverTX];
            DrawText(TextFormat("(%d,%d) %s", hoverTX, hoverTZ, tileNames[tileIdx]),
                     185, 10, 14, WHITE);
        }

        // Export notification
        if (showExport) {
            DrawRectangle(sw/2 - 100, 10, 200, 30, (Color){0,80,0,220});
            DrawText("Exported to console!", sw/2 - 75, 18, 14, WHITE);
            showExport = false;  // one frame flash
        }

        // Minimap
        if (showMinimap && mode != MODE_BUILD && mode != MODE_BUILD2D)
            Map3DDraw2D(&map, sw - EDITOR_MAP_W * 6 - 10, 10, 6);

        // Mode selection popup menu
        if (showModeMenu) {
            const char *modeOpts[] = { "Tiles", "Objects", "Build 3D", "Build 2D" };
            EditorMode modes[] = { MODE_TILES, MODE_OBJECTS, MODE_BUILD, MODE_BUILD2D };
            int menuW = 120, itemH = 28, menuH = 4 * itemH + 8;
            int mx = (int)modeMenuPos.x, my = (int)modeMenuPos.y;
            // Keep on screen
            if (mx + menuW > sw) mx = sw - menuW;
            if (my + menuH > sh) my = sh - menuH;

            DrawRectangle(mx, my, menuW, menuH, (Color){25, 25, 35, 240});
            DrawRectangleLinesEx((Rectangle){mx, my, menuW, menuH}, 1, (Color){100, 90, 70, 255});

            for (int i = 0; i < 4; i++) {
                int iy = my + 4 + i * itemH;
                Rectangle itemRect = { mx + 2, iy, menuW - 4, itemH - 2 };
                bool hov = CheckCollisionPointRec(mouse, itemRect);
                bool isCurrent = (modes[i] == mode);

                if (hov) DrawRectangleRec(itemRect, (Color){60, 50, 40, 200});
                else if (isCurrent) DrawRectangleRec(itemRect, (Color){40, 35, 30, 200});

                Color textCol = isCurrent ? GOLD : (hov ? WHITE : (Color){180, 180, 180, 255});
                DrawText(modeOpts[i], mx + 12, iy + 6, 16, textCol);

                if (isCurrent) DrawText("*", mx + menuW - 18, iy + 6, 16, GOLD);

                if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    EditorMode prev = mode;
                    mode = modes[i];
                    showModeMenu = false;
                    draggingAxis = -1;
                    if (mode == MODE_BUILD) {
                        camFocus = (Vector3){0, 0, 0};
                        camDist = 12.0f;
                    } else if (mode == MODE_BUILD2D) {
                        // No camera changes needed for 2D
                    } else if (prev == MODE_BUILD || prev == MODE_BUILD2D) {
                        camFocus = (Vector3){ EDITOR_MAP_W * TILE_SZ / 2, 0, EDITOR_MAP_H * TILE_SZ / 2 };
                        camDist = 30.0f;
                    }
                }
            }

            // Close on ESC or clicking outside
            if (IsKeyPressed(KEY_ESCAPE)) showModeMenu = false;
            Rectangle menuBounds = { mx, my, menuW, menuH };
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !CheckCollisionPointRec(mouse, menuBounds))
                showModeMenu = false;
        }

        DrawFPS(sw - 80, sh - 20);
        EndDrawing();
    }

    SaveEditorState();
    SaveMap3D("map.m3d", &map);
    SavePlacedObjects();
    CloseWindow();
    return 0;
}
