#include "raylib.h"
#include "raymath.h"
#include "../common/map3d.h"
#include "../common/objects3d.h"
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

// Prefab definitions
typedef struct {
    const char *name;
    Part *parts;
    int partCount;
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

static PrefabDef prefabs[MAX_PREFABS] = {
    { "Human",    NULL, 0 },
    { "Car",      NULL, 0 },
    { "Tree",     NULL, 0 },
    { "Crate",    NULL, 0 },
    { "Barrel",   NULL, 0 },
    { "Lamppost", NULL, 0 },
    { "Bush",     NULL, 0 },
};
static int numPrefabs = 7;

// Editor state
static Map3D map;
static PlacedObject placed[MAX_PLACED];
static int numPlaced = 0;

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
typedef enum { MODE_TILES, MODE_OBJECTS, MODE_BUILD } EditorMode;

// Build mode: compose a new object from primitives
#define MAX_BUILD_PARTS 16
typedef struct {
    Part parts[MAX_BUILD_PARTS];
    int count;
    int selected;  // which part is selected (-1 = none)
    char name[32];
    int editingPrefab;  // index of prefab being edited, -1 if new
} BuildObject;
static BuildObject buildObj = { .count = 0, .selected = -1, .name = "Custom", .editingPrefab = -1 };
static int buildPrimitive = 0;  // 0=cube, 1=sphere, 2=cylinder
static Vector3 buildCursor = {0, 0.5f, 0};  // where new parts spawn

static EditorMode mode = MODE_TILES;
static int selectedTile = 1;
static int selectedPrefab = 0;
static int selectedObject = -1;
static GizmoMode gizmoMode = GIZMO_MOVE;
static int draggingAxis = -1;
static bool showExport = false;
static bool showGrid = true;
static bool showMinimap = true;

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
    // Replace spaces with underscores
    for (char *c = path + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
    SaveObject3D(path, prefabs[idx].parts, prefabs[idx].partCount);
}

// Try to load a prefab from file, returns part count or 0 if not found
int LoadPrefabFile(const char *name, Part *destParts, int maxParts) {
    char path[64];
    snprintf(path, sizeof(path), "objects/%s.obj3d", name);
    for (char *c = path + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
    return LoadObject3D(path, destParts, maxParts);
}

void InitEditor(void) {
    // Init built-in prefab part pointers
    prefabs[0].parts = humanParts;  prefabs[0].partCount = sizeof(humanParts)/sizeof(Part);
    prefabs[1].parts = carParts;    prefabs[1].partCount = sizeof(carParts)/sizeof(Part);
    prefabs[2].parts = treeParts;   prefabs[2].partCount = sizeof(treeParts)/sizeof(Part);
    prefabs[3].parts = crateParts;  prefabs[3].partCount = sizeof(crateParts)/sizeof(Part);
    prefabs[4].parts = barrelParts; prefabs[4].partCount = sizeof(barrelParts)/sizeof(Part);
    prefabs[5].parts = lampParts;   prefabs[5].partCount = sizeof(lampParts)/sizeof(Part);
    prefabs[6].parts = bushParts;   prefabs[6].partCount = sizeof(bushParts)/sizeof(Part);

    // Create objects/ directory and save built-in prefabs as files
    MakeDirectory("objects");
    for (int i = 0; i < numPrefabs; i++) {
        char path[64];
        snprintf(path, sizeof(path), "objects/%s.obj3d", prefabs[i].name);
        for (char *c = path + 8; *c && *c != '.'; c++) if (*c == ' ') *c = '_';
        if (!FileExists(path)) SavePrefabFile(i);
    }

    // Try loading the map from file, otherwise start with grass
    if (!LoadMap3DFile("map.m3d", &map, tileDefs, NUM_TILEDEFS)) {
        char layout[EDITOR_MAP_W * EDITOR_MAP_H];
        memset(layout, '1', sizeof(layout));
        Map3DLoad(&map, layout, EDITOR_MAP_W, EDITOR_MAP_H, TILE_SZ, tileDefs, NUM_TILEDEFS);
    }
    camFocus = (Vector3){ EDITOR_MAP_W * TILE_SZ / 2, 0, EDITOR_MAP_H * TILE_SZ / 2 };
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
    SetWindowState(FLAG_WINDOW_RESIZABLE);

    InitEditor();

    Camera3D camera = { 0 };
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 50;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        Vector2 mouse = GetMousePosition();

        // --- Camera mode toggle: V cycles Orbit -> FPS -> Fly ---
        if (IsKeyPressed(KEY_V)) {
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
                if (mode == MODE_OBJECTS && selectedObject >= 0 && selectedObject < numPlaced && placed[selectedObject].active) {
                    orbitTarget = placed[selectedObject].pos;
                    PrefabDef *opf = &prefabs[placed[selectedObject].prefabIdx];
                    orbitTarget.y += PrefabCenterY(opf->parts, opf->partCount, placed[selectedObject].scale);
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

        // --- Mode switch ---
        if (IsKeyPressed(KEY_TAB)) {
            EditorMode prev = mode;
            mode = (mode == MODE_TILES) ? MODE_OBJECTS : (mode == MODE_OBJECTS) ? MODE_BUILD : MODE_TILES;
            draggingAxis = -1;
            if (mode == MODE_BUILD) {
                camFocus = (Vector3){0, 0, 0};
                camDist = 12.0f;
            } else if (prev == MODE_BUILD) {
                camFocus = (Vector3){ EDITOR_MAP_W * TILE_SZ / 2, 0, EDITOR_MAP_H * TILE_SZ / 2 };
                camDist = 30.0f;
            }
        }

        // Toggle grid
        if (IsKeyPressed(KEY_G)) showGrid = !showGrid;
        // Toggle minimap
        if (IsKeyPressed(KEY_M)) showMinimap = !showMinimap;

        // Save map (Ctrl+S)
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER)) && IsKeyPressed(KEY_S)) {
            SaveMap3D("map.m3d", &map);
            ExportMapToConsole();
            showExport = true;
        }

        // --- Mouse interaction (only when not over UI) ---
        bool overUI = (mouse.x < 180);  // left panel
        Vector3 groundHit = MouseToGround(camera);
        int hoverTX, hoverTZ;
        Map3DFromWorld(&map, groundHit, &hoverTX, &hoverTZ);
        bool hoverValid = !overUI && hoverTX >= 0 && hoverTX < map.width &&
                          hoverTZ >= 0 && hoverTZ < map.height;

        if (mode == MODE_TILES) {
            // Tile selection: number keys or scroll in palette
            for (int k = KEY_ZERO; k <= KEY_NINE; k++)
                if (IsKeyPressed(k)) selectedTile = k - KEY_ZERO;

            // Paint tiles with left click
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && hoverValid) {
                map.tiles[hoverTZ][hoverTX] = selectedTile;
            }

            // Eyedropper: pick tile under cursor
            if (IsKeyPressed(KEY_Q) && hoverValid) {
                selectedTile = map.tiles[hoverTZ][hoverTX];
            }
        } else if (mode == MODE_OBJECTS) {
            // Object mode
            // Select prefab
            for (int k = KEY_ONE; k <= KEY_NINE; k++)
                if (IsKeyPressed(k) && (k - KEY_ONE) < numPrefabs) selectedPrefab = k - KEY_ONE;

            // Left-click: check gizmo first, then select object
            bool clickedGizmo = false;
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI &&
                selectedObject >= 0 && placed[selectedObject].active) {
                PlacedObject *sel = &placed[selectedObject];
                PrefabDef *gpf = &prefabs[sel->prefabIdx];
                float avgS = (sel->scale.x + sel->scale.y + sel->scale.z) / 3.0f;
                float objRadius = PrefabCenterY(gpf->parts, gpf->partCount, sel->scale) * 2.0f;
                if (objRadius < 0.5f) objRadius = 0.5f;
                float gLen = objRadius + 0.8f;
                float gy = sel->pos.y + PrefabCenterY(gpf->parts, gpf->partCount, sel->scale);
                Vector2 screenCenter = GetWorldToScreen(
                    (Vector3){sel->pos.x, gy, sel->pos.z}, camera);

                if (gizmoMode == GIZMO_ROTATE) {
                    // Rotate: click anywhere near the object to start rotating
                    if (Vector2Distance(mouse, screenCenter) < 60.0f) {
                        draggingAxis = 0;
                        clickedGizmo = true;
                    }
                } else {
                    // Move/Scale: check axis endpoints
                    Vector3 gizmoEnd[3] = {
                        { sel->pos.x + gLen, gy, sel->pos.z },
                        { sel->pos.x, gy + gLen, sel->pos.z },
                        { sel->pos.x, gy, sel->pos.z + gLen },
                    };
                    for (int ax = 0; ax < 3; ax++) {
                        Vector2 screenPt = GetWorldToScreen(gizmoEnd[ax], camera);
                        if (Vector2Distance(mouse, screenPt) < 20.0f) {
                            draggingAxis = ax;
                            clickedGizmo = true;
                            break;
                        }
                    }
                    // Center point: ground plane drag
                    if (!clickedGizmo && Vector2Distance(mouse, screenCenter) < 15.0f) {
                        draggingAxis = 3;
                        clickedGizmo = true;
                    }
                }
            }

            // If gizmo wasn't clicked, try selecting an object
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hoverValid && !overUI && !clickedGizmo) {
                selectedObject = -1;
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
            }

            // Shift+click or P: place new object
            if (((IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && IsKeyDown(KEY_LEFT_SHIFT) && !clickedGizmo) ||
                 IsKeyPressed(KEY_F)) && hoverValid && !overUI && numPlaced < MAX_PLACED) {
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

            // Escape: deselect
            if (IsKeyPressed(KEY_ESCAPE)) { selectedObject = -1; draggingAxis = -1; }

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
                            sel->rotY += delta.x * 0.02f;
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
        }

        // Load prefab into build mode for editing (from any mode)
        if (IsKeyPressed(KEY_E) && mode == MODE_OBJECTS && selectedPrefab >= 0 && selectedPrefab < numPrefabs) {
            PrefabDef *pf = &prefabs[selectedPrefab];
            buildObj.count = (pf->partCount < MAX_BUILD_PARTS) ? pf->partCount : MAX_BUILD_PARTS;
            for (int i = 0; i < buildObj.count; i++)
                buildObj.parts[i] = pf->parts[i];
            buildObj.selected = -1;
            buildObj.editingPrefab = selectedPrefab;
            snprintf(buildObj.name, sizeof(buildObj.name), "%s", pf->name);
            mode = MODE_BUILD;
            camFocus = (Vector3){0, 0, 0};
            camDist = 12.0f;
            draggingAxis = -1;
        }

        // --- Build mode ---
        if (mode == MODE_BUILD) {
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

            // Select part by clicking
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !overUI) {
                // Check if clicking a gizmo first
                bool hitGizmo = false;
                if (buildObj.selected >= 0) {
                    Part *sp = &buildObj.parts[buildObj.selected];
                    float gy = sp->offset.y + (sp->type == PART_CYLINDER ? sp->size.y * 0.5f : 0);
                    float partR = fmaxf(fmaxf(sp->size.x, sp->size.y), sp->size.z);
                    if (partR < 0.3f) partR = 0.3f;
                    float gLen = partR + 0.5f;
                    Vector3 ends[3] = {
                        {sp->offset.x + gLen, gy, sp->offset.z},
                        {sp->offset.x, gy + gLen, sp->offset.z},
                        {sp->offset.x, gy, sp->offset.z + gLen},
                    };
                    for (int ax = 0; ax < 3; ax++) {
                        Vector2 sp2 = GetWorldToScreen(ends[ax], camera);
                        if (Vector2Distance(mouse, sp2) < 20) { draggingAxis = ax; hitGizmo = true; break; }
                    }
                    if (!hitGizmo) {
                        Vector2 cp = GetWorldToScreen((Vector3){sp->offset.x, gy, sp->offset.z}, camera);
                        if (Vector2Distance(mouse, cp) < 15) { draggingAxis = 3; hitGizmo = true; }
                    }
                }
                if (!hitGizmo) {
                    // Select nearest part using screen-space distance
                    buildObj.selected = -1;
                    float bestD = 40.0f;  // pixels
                    for (int i = 0; i < buildObj.count; i++) {
                        Vector2 partScreen = GetWorldToScreen(buildObj.parts[i].offset, camera);
                        float d = Vector2Distance(mouse, partScreen);
                        if (d < bestD) { bestD = d; buildObj.selected = i; }
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
                    // Orbit the part around the origin (Y axis)
                    float angle = delta.x * 0.02f;
                    float ox = sp->offset.x, oz = sp->offset.z;
                    float rcs = cosf(angle), rsn = sinf(angle);
                    sp->offset.x = ox * rcs - oz * rsn;
                    sp->offset.z = ox * rsn + oz * rcs;
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
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) draggingAxis = -1;

            // Delete selected part
            if (buildObj.selected >= 0 && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE))) {
                for (int i = buildObj.selected; i < buildObj.count - 1; i++)
                    buildObj.parts[i] = buildObj.parts[i + 1];
                buildObj.count--;
                buildObj.selected = -1;
            }

            // Escape deselect
            if (IsKeyPressed(KEY_ESCAPE)) { buildObj.selected = -1; draggingAxis = -1; }

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
                        selectedPrefab = numPrefabs;
                        SavePrefabFile(numPrefabs);
                        numPrefabs++;
                    }

                    customPartsUsed += buildObj.count;

                    // Switch to objects mode
                    buildObj.count = 0;
                    buildObj.selected = -1;
                    buildObj.editingPrefab = -1;
                    mode = MODE_OBJECTS;
                    camFocus = (Vector3){ EDITOR_MAP_W * TILE_SZ / 2, 0, EDITOR_MAP_H * TILE_SZ / 2 };
                    camDist = 30.0f;
                    showExport = true;
                }
            }
        }

        // Undo last placed (Ctrl+Z)
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER)) && IsKeyPressed(KEY_Z)) {
            for (int i = numPlaced - 1; i >= 0; i--) {
                if (placed[i].active) { placed[i].active = false; break; }
            }
        }

        // Clear map
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_N)) {
            InitEditor();
            numPlaced = 0;
            selectedObject = -1;
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){30, 30, 35, 255});

        BeginMode3D(camera);
            if (mode == MODE_BUILD) {
                // Build mode: draw on a work surface
                DrawPlane((Vector3){0, 0, 0}, (Vector2){10, 10}, (Color){50, 50, 55, 255});
                DrawGrid(10, 1.0f);

                // Draw all build parts
                for (int i = 0; i < buildObj.count; i++) {
                    DrawPart(&buildObj.parts[i], (Vector3){0,0,0}, 0);
                }
            } else {
                // Draw map
                Map3DDrawAll(&map);
                if (showGrid) Map3DDrawGrid(&map, (Color){60,60,70,80});

                // Draw placed objects
                for (int i = 0; i < numPlaced; i++) {
                    if (!placed[i].active) continue;
                    PrefabDef *pf = &prefabs[placed[i].prefabIdx];
                    DrawObject3DScaled(pf->parts, pf->partCount,
                        placed[i].pos, placed[i].rotY, placed[i].scale);
                }
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
                } else if (IsKeyDown(KEY_LEFT_SHIFT) && mode == MODE_OBJECTS) {
                    float hy = Map3DHeightAt(&map, groundHit);
                    PrefabDef *pf = &prefabs[selectedPrefab];
                    Object3D previewObj = { pf->parts, pf->partCount, {groundHit.x, hy, groundHit.z}, 0 };
                    DrawObject3D(&previewObj);
                    DrawCircle3D((Vector3){groundHit.x, hy + 0.05f, groundHit.z}, 0.5f,
                        (Vector3){1,0,0}, 90, (Color){255,255,0,150});
                }
            }

        EndMode3D();

        // --- Gizmo overlay (2D screen-space, guaranteed on top) ---
        {
            Vector3 gCenter = {0};
            float gLen = 0;
            bool drawGizmo = false;
            float objRotY = 0;

            if (mode == MODE_OBJECTS && selectedObject >= 0 && selectedObject < numPlaced && placed[selectedObject].active) {
                PlacedObject *sel = &placed[selectedObject];
                PrefabDef *gpf2 = &prefabs[sel->prefabIdx];
                gCenter = sel->pos;
                gCenter.y += PrefabCenterY(gpf2->parts, gpf2->partCount, sel->scale);
                float objR = PrefabCenterY(gpf2->parts, gpf2->partCount, sel->scale) * 2.0f;
                if (objR < 0.5f) objR = 0.5f;
                gLen = objR + 0.8f;
                objRotY = sel->rotY;
                drawGizmo = true;
            } else if (mode == MODE_BUILD && buildObj.selected >= 0 && buildObj.selected < buildObj.count) {
                Part *sp = &buildObj.parts[buildObj.selected];
                gCenter = sp->offset;
                gCenter.y += (sp->type == PART_CYLINDER ? sp->size.y * 0.5f : 0);
                float partR = fmaxf(fmaxf(sp->size.x, sp->size.y), sp->size.z);
                if (partR < 0.3f) partR = 0.3f;
                gLen = partR + 0.5f;
                drawGizmo = true;
            }

            if (drawGizmo) {
                Vector2 sc = GetWorldToScreen(gCenter, camera);
                Vector2 sxEnd = GetWorldToScreen((Vector3){gCenter.x + gLen, gCenter.y, gCenter.z}, camera);
                Vector2 syEnd = GetWorldToScreen((Vector3){gCenter.x, gCenter.y + gLen, gCenter.z}, camera);
                Vector2 szEnd = GetWorldToScreen((Vector3){gCenter.x, gCenter.y, gCenter.z + gLen}, camera);

                bool isMove = (gizmoMode == GIZMO_MOVE);
                bool isRot = (gizmoMode == GIZMO_ROTATE);
                bool isScl = (gizmoMode == GIZMO_SCALE);
                int tipR = 8;

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
                    Color ringCol = (draggingAxis >= 0) ? YELLOW : (Color){255,200,0,220};
                    int segs = 32;
                    for (int s = 0; s < segs; s++) {
                        float a0 = (float)s / segs * 2.0f * PI;
                        float a1 = (float)(s+1) / segs * 2.0f * PI;
                        Vector2 p0 = GetWorldToScreen((Vector3){
                            gCenter.x+cosf(a0)*gLen, gCenter.y, gCenter.z+sinf(a0)*gLen}, camera);
                        Vector2 p1 = GetWorldToScreen((Vector3){
                            gCenter.x+cosf(a1)*gLen, gCenter.y, gCenter.z+sinf(a1)*gLen}, camera);
                        DrawLineEx(p0, p1, 2, ringCol);
                    }
                    for (int t = 0; t < 8; t++) {
                        float a = t * PI / 4.0f;
                        Vector2 pi = GetWorldToScreen((Vector3){
                            gCenter.x+cosf(a)*gLen*0.85f, gCenter.y, gCenter.z+sinf(a)*gLen*0.85f}, camera);
                        Vector2 po = GetWorldToScreen((Vector3){
                            gCenter.x+cosf(a)*gLen, gCenter.y, gCenter.z+sinf(a)*gLen}, camera);
                        DrawLineEx(pi, po, 2, ringCol);
                    }
                    float rcs = cosf(objRotY), rsn = sinf(objRotY);
                    Vector2 arrowEnd = GetWorldToScreen((Vector3){
                        gCenter.x+rcs*gLen, gCenter.y, gCenter.z+rsn*gLen}, camera);
                    DrawLineEx(sc, arrowEnd, 2, RED);
                    DrawCircleV(arrowEnd, tipR, RED);
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
        const char *modeLabel = mode == MODE_TILES ? "TILES" : (mode == MODE_OBJECTS ? "OBJECTS" : "BUILD");
        DrawText(modeLabel, 10, 10, 20, GOLD);
        DrawText("[TAB] switch mode", 10, 32, 10, (Color){120,120,130,255});

        if (mode == MODE_BUILD) {
            DrawText("Build Object:", 10, 55, 12, WHITE);
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
                if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) buildObj.selected = i;
                // Color swatch
                DrawRectangle(12, y + 2, 12, 12, buildObj.parts[i].color);
                DrawText(TextFormat("%s", tname), 28, y + 3, 10, sel ? WHITE : (Color){150,150,150,255});
            }

            // Color palette (when a part is selected)
            if (buildObj.selected >= 0 && buildObj.selected < buildObj.count) {
                int cpY = 200 + buildObj.count * 18 + 10;
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
            }

            DrawText("[F] Add part  [DEL] Remove", 10, sh - 90, 10, (Color){120,120,130,255});
            DrawText("[Z] Move  [X] Rotate  [C] Scale", 10, sh - 75, 10, (Color){120,120,130,255});
            DrawText("[ENTER] Save to palette", 10, sh - 60, 10, GOLD);
            DrawText("[ESC] Deselect part", 10, sh - 45, 10, (Color){120,120,130,255});

            const char *gmNames[] = { "", "MOVE", "ROTATE", "SCALE" };
            Color gmCol = gizmoMode == GIZMO_MOVE ? GREEN : (gizmoMode == GIZMO_ROTATE ? YELLOW : ORANGE);
            DrawText(TextFormat("Gizmo: %s", gmNames[gizmoMode]), 10, sh - 105, 12, gmCol);
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
            DrawText("[Q] Eyedropper", 10, sh - 60, 10, (Color){120,120,130,255});
        } else {
            DrawText("Object Palette:", 10, 55, 12, WHITE);
            for (int i = 0; i < numPrefabs; i++) {
                int y = 72 + i * 26;
                bool sel = (i == selectedPrefab);
                Rectangle itemRect = {8, (float)y, 160, 24};
                bool hover = CheckCollisionPointRec(mouse, itemRect);
                Color bg = sel ? (Color){60,50,30,255} : (hover ? (Color){45,40,35,255} : (Color){30,30,35,255});
                DrawRectangle(8, y, 160, 24, bg);
                if (sel) DrawRectangleLinesEx(itemRect, 1, GOLD);
                else if (hover) DrawRectangleLinesEx(itemRect, 1, (Color){100,90,70,255});

                // Click to select
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    selectedPrefab = i;

                DrawText(TextFormat("%d: %s", i+1, prefabs[i].name), 14, y + 6, 12,
                    sel ? WHITE : (Color){150,150,150,255});
            }
            DrawText("[1-7] Select prefab", 10, sh - 120, 10, (Color){120,120,130,255});
            DrawText("[Click] Select  [Shift+Click/F] Place", 10, sh - 105, 10, (Color){120,120,130,255});
            DrawText("[E] Edit prefab in build mode", 10, sh - 150, 10, (Color){180,160,100,255});
            DrawText("[Z] Move  [X] Rotate  [C] Scale", 10, sh - 90, 10, (Color){120,120,130,255});
            DrawText("Drag gizmo axes with mouse", 10, sh - 75, 10, (Color){120,120,130,255});
            DrawText("[R/Q] Rotate  [DEL] Remove", 10, sh - 60, 10, (Color){120,120,130,255});
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
        if (showMinimap)
            Map3DDraw2D(&map, sw - EDITOR_MAP_W * 6 - 10, 10, 6);

        DrawFPS(sw - 80, sh - 20);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
