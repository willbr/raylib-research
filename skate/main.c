#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Isometric projection helpers
// GBA THPS used a ~2:1 iso ratio
#define ISO_SCALE   2.0f
#define TILE_W     32
#define TILE_H     16
#define ZOOM        2.5f

// World
#define WORLD_W    24
#define WORLD_H    24

// Physics
#define SKATE_ACCEL      200.0f
#define SKATE_MAX_SPEED  180.0f
#define SKATE_PUSH_SPEED 120.0f
#define SKATE_FRICTION    0.985f
#define SKATE_BRAKE       0.95f
#define TURN_SPEED        3.5f
#define OLLIE_VEL        220.0f
#define GRAVITY          600.0f
#define GRIND_SPEED      140.0f
#define MANUAL_BALANCE_SPEED 1.2f
#define MANUAL_FALL_THRESH   1.0f

// Trick timing
#define TRICK_LAND_WINDOW  0.15f
#define COMBO_TIMEOUT      2.0f
#define MAX_COMBO_TRICKS  16

// Tile types
#define T_GROUND    0
#define T_RAMP_N    1  // ramp facing north (launches up-screen)
#define T_RAMP_S    2
#define T_RAMP_E    3
#define T_RAMP_W    4
#define T_RAIL_H    5  // horizontal rail
#define T_RAIL_V    6  // vertical rail
#define T_RAIL_D1   7  // diagonal rail /
#define T_RAIL_D2   8  // diagonal rail backslash
#define T_QUARTER   9  // quarter pipe (big air)
#define T_GAP      10  // gap to jump over (scores if cleared)
#define T_WALL     11
#define T_POOL     12  // bowl/pool (curved ramp area)

// Trick types
typedef enum {
    TRICK_NONE = 0,
    TRICK_OLLIE,
    TRICK_KICKFLIP,
    TRICK_HEELFLIP,
    TRICK_SHUVIT,
    TRICK_360FLIP,
    TRICK_GRIND_50,
    TRICK_GRIND_NOSE,
    TRICK_GRIND_TAIL,
    TRICK_MANUAL,
    TRICK_NOSE_MANUAL,
    TRICK_GRAB_MELON,
    TRICK_GRAB_INDY,
    TRICK_GAP_BONUS,
    TRICK_COUNT
} TrickType;

static const char *trickNames[] = {
    "", "Ollie", "Kickflip", "Heelflip", "Pop Shove-It",
    "360 Flip", "50-50 Grind", "Nosegrind", "Tailslide",
    "Manual", "Nose Manual", "Melon Grab", "Indy Grab", "GAP!"
};

static const int trickPoints[] = {
    0, 100, 200, 200, 250,
    500, 300, 350, 350,
    100, 150, 400, 400, 1000
};

typedef struct {
    TrickType type;
    int multiplier;
} ComboTrick;

typedef struct {
    // Position in world space (flat 2D, z = height)
    Vector2 pos;
    float z;         // height above ground
    float velZ;      // vertical velocity
    Vector2 vel;     // horizontal velocity
    float rotation;  // facing angle in radians
    float speed;

    // State
    bool airborne;
    bool grinding;
    bool manualing;
    float manualBalance;  // -1 to 1, fall if |balance| > thresh
    int grindRailType;
    float grindDir;       // direction along rail

    // Tricks
    ComboTrick combo[MAX_COMBO_TRICKS];
    int comboLen;
    int comboMult;
    float comboTimer;
    int totalScore;
    int comboScore;       // current combo value
    TrickType lastAirTrick;
    float trickRotation;  // spin during air tricks
    bool trickLanded;

    // Display
    TrickType displayTrick;
    float displayTimer;
    char displayText[64];
    int bestCombo;

    // Timer
    float runTimer;
    bool runOver;
} Skater;

static int world[WORLD_H][WORLD_W];

// Convert world coords to isometric screen coords
Vector2 WorldToIso(float wx, float wy, float wz) {
    return (Vector2){
        (wx - wy) * (TILE_W / 2.0f),
        (wx + wy) * (TILE_H / 2.0f) - wz
    };
}

// Draw a filled quad with correct winding (auto-detects CCW)
void DrawQuad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    // Cross product to check winding of first triangle
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0) {
        DrawTriangle(a, b, c, col);
        DrawTriangle(a, c, d, col);
    } else {
        DrawTriangle(a, c, b, col);
        DrawTriangle(a, d, c, col);
    }
}

// Convert screen coords back to world (z=0 plane)
Vector2 IsoToWorld(float sx, float sy) {
    float wx = (sx / (TILE_W / 2.0f) + sy / (TILE_H / 2.0f)) / 2.0f;
    float wy = (sy / (TILE_H / 2.0f) - sx / (TILE_W / 2.0f)) / 2.0f;
    return (Vector2){ wx, wy };
}

void BuildPark(void) {
    // Fill with ground
    for (int y = 0; y < WORLD_H; y++)
        for (int x = 0; x < WORLD_W; x++)
            world[y][x] = T_GROUND;

    // Walls around edge
    for (int x = 0; x < WORLD_W; x++) {
        world[0][x] = T_WALL; world[WORLD_H-1][x] = T_WALL;
    }
    for (int y = 0; y < WORLD_H; y++) {
        world[y][0] = T_WALL; world[y][WORLD_W-1] = T_WALL;
    }

    // --- Area 1: Street section (top-left) ---
    // Horizontal rail
    for (int x = 3; x <= 8; x++) world[3][x] = T_RAIL_H;
    // Vertical rail
    for (int y = 5; y <= 9; y++) world[y][5] = T_RAIL_V;
    // Ramps at ends
    world[3][2] = T_RAMP_E;
    world[3][9] = T_RAMP_W;
    // Gap
    world[6][3] = T_GAP; world[6][4] = T_GAP;

    // --- Area 2: Half-pipe / pool (top-right) ---
    for (int x = 14; x <= 19; x++) {
        world[2][x] = T_RAMP_S;
        world[7][x] = T_RAMP_N;
    }
    for (int y = 2; y <= 7; y++) {
        world[y][14] = T_RAMP_E;
        world[y][19] = T_RAMP_W;
    }
    // Pool interior
    for (int y = 3; y <= 6; y++)
        for (int x = 15; x <= 18; x++)
            world[y][x] = T_POOL;

    // --- Area 3: Stairset with handrail (center) ---
    // Launch ramp at top
    world[10][8] = T_RAMP_S;
    world[10][9] = T_RAMP_S;
    world[10][10] = T_RAMP_S;
    // Gap to clear
    world[11][8] = T_GAP;
    world[11][9] = T_GAP;
    world[11][10] = T_GAP;
    // Handrail running alongside
    for (int y = 11; y <= 14; y++) world[y][7] = T_RAIL_V;
    // Landing area ramp at bottom
    world[14][8] = T_RAMP_N;
    world[14][9] = T_RAMP_N;
    world[14][10] = T_RAMP_N;

    // --- Area 4: Quarter pipes (bottom) ---
    for (int x = 3; x <= 8; x++) world[18][x] = T_QUARTER;
    for (int x = 3; x <= 8; x++) world[21][x] = T_QUARTER;
    // Connecting ramps
    world[18][2] = T_RAMP_E; world[18][9] = T_RAMP_W;
    world[21][2] = T_RAMP_E; world[21][9] = T_RAMP_W;

    // --- Area 5: Funbox / manual pad (center-right) ---
    world[15][16] = T_RAMP_N;
    world[15][17] = T_RAMP_N;
    world[15][18] = T_RAMP_N;
    world[12][16] = T_RAMP_S;
    world[12][17] = T_RAMP_S;
    world[12][18] = T_RAMP_S;
    // Rails on funbox
    for (int y = 13; y <= 14; y++) {
        world[y][16] = T_RAIL_V;
        world[y][18] = T_RAIL_V;
    }

    // --- Area 6: Long grind line (bottom-right) ---
    for (int x = 13; x <= 21; x++) world[20][x] = T_RAIL_H;
    world[20][12] = T_RAMP_E;
    world[20][22] = T_RAMP_W;
    // Gap in the middle
    world[20][17] = T_GAP;

    // --- Scattered ramps ---
    world[10][3] = T_RAMP_S;
    world[10][4] = T_RAMP_S;
    world[16][7] = T_RAMP_N;
    world[9][20] = T_RAMP_W;
    world[9][21] = T_RAMP_W;
}

int GetTile(int tx, int ty) {
    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) return T_WALL;
    return world[ty][tx];
}

bool TileIsRamp(int t) {
    return t >= T_RAMP_N && t <= T_RAMP_W;
}

bool TileIsRail(int t) {
    return t >= T_RAIL_H && t <= T_RAIL_D2;
}

bool TileIsQuarter(int t) {
    return t == T_QUARTER;
}

float RampLaunchHeight(int t) {
    if (t == T_QUARTER) return 1.8f;
    if (TileIsRamp(t)) return 1.2f;
    return 0.0f;
}

Vector2 RampDirection(int t) {
    switch (t) {
        case T_RAMP_N: return (Vector2){ 0, -1 };
        case T_RAMP_S: return (Vector2){ 0,  1 };
        case T_RAMP_E: return (Vector2){ 1,  0 };
        case T_RAMP_W: return (Vector2){-1,  0 };
        case T_QUARTER: return (Vector2){ 0, -1 };
    }
    return (Vector2){ 0, 0 };
}

void AddTrickToCombo(Skater *sk, TrickType trick) {
    if (sk->comboLen < MAX_COMBO_TRICKS) {
        // Check for repeat (less points)
        int repeatCount = 0;
        for (int i = 0; i < sk->comboLen; i++)
            if (sk->combo[i].type == trick) repeatCount++;

        sk->combo[sk->comboLen].type = trick;
        sk->combo[sk->comboLen].multiplier = repeatCount + 1;
        sk->comboLen++;
    }
    sk->comboMult++;
    sk->comboTimer = COMBO_TIMEOUT;

    // Calculate combo score
    sk->comboScore = 0;
    for (int i = 0; i < sk->comboLen; i++) {
        int pts = trickPoints[sk->combo[i].type];
        // Diminishing returns for repeats
        pts /= sk->combo[i].multiplier;
        sk->comboScore += pts;
    }
    sk->comboScore *= sk->comboMult;

    // Display
    sk->displayTrick = trick;
    sk->displayTimer = 1.0f;
    snprintf(sk->displayText, sizeof(sk->displayText), "%s x%d",
             trickNames[trick], sk->comboMult);
}

void LandCombo(Skater *sk) {
    if (sk->comboLen > 0) {
        sk->totalScore += sk->comboScore;
        if (sk->comboScore > sk->bestCombo) sk->bestCombo = sk->comboScore;
    }
    sk->comboLen = 0;
    sk->comboMult = 0;
    sk->comboScore = 0;
    sk->comboTimer = 0;
    sk->lastAirTrick = TRICK_NONE;
}

void BailCombo(Skater *sk) {
    // Lose combo on bail
    sk->comboLen = 0;
    sk->comboMult = 0;
    sk->comboScore = 0;
    sk->comboTimer = 0;
    sk->lastAirTrick = TRICK_NONE;

    sk->displayTimer = 1.5f;
    snprintf(sk->displayText, sizeof(sk->displayText), "BAIL!");
}

void DrawTileIso(int tx, int ty) {
    Vector2 top = WorldToIso(tx, ty, 0);
    Vector2 right = WorldToIso(tx + 1, ty, 0);
    Vector2 bottom = WorldToIso(tx + 1, ty + 1, 0);
    Vector2 left = WorldToIso(tx, ty + 1, 0);

    int t = GetTile(tx, ty);

    Color col1, col2;  // top face, side shade

    switch (t) {
        case T_GROUND:
            // Concrete tile
            DrawQuad(top, right, bottom, left, (Color){170, 170, 175, 255});
            // Subtle crack lines
            if ((tx + ty * 7) % 11 == 0) {
                Vector2 mid = Vector2Lerp(top, bottom, 0.5f);
                DrawLineV(Vector2Lerp(top, left, 0.3f), mid, (Color){150,150,155,255});
            }
            break;

        case T_RAMP_N: case T_RAMP_S: case T_RAMP_E: case T_RAMP_W: {
            float h = 10.0f;
            Color rampCol = (Color){210, 190, 150, 255};
            Color sideL = (Color){160, 140, 110, 255};  // left-facing side
            Color sideR = (Color){130, 115, 90, 255};   // right-facing side

            // 4 world corners, each with a height: ground(0) or raised(h)
            // tl=(tx,ty) tr=(tx+1,ty) br=(tx+1,ty+1) bl=(tx,ty+1)
            float htl, htr, hbr, hbl;
            if (t == T_RAMP_N)      { htl = h; htr = h; hbr = 0; hbl = 0; }
            else if (t == T_RAMP_S) { htl = 0; htr = 0; hbr = h; hbl = h; }
            else if (t == T_RAMP_E) { htl = 0; htr = h; hbr = h; hbl = 0; }
            else                    { htl = h; htr = 0; hbr = 0; hbl = h; }

            Vector2 vtl = WorldToIso(tx,   ty,   htl);
            Vector2 vtr = WorldToIso(tx+1, ty,   htr);
            Vector2 vbr = WorldToIso(tx+1, ty+1, hbr);
            Vector2 vbl = WorldToIso(tx,   ty+1, hbl);

            // Sloped top surface
            DrawQuad(vtl, vtr, vbr, vbl, rampCol);

            // Side walls: only draw camera-facing sides (south and east in iso)
            // Right/east edge (tx+1, ty..ty+1) — faces right on screen
            if (htr != 0 || hbr != 0) {
                Vector2 gtr = WorldToIso(tx+1, ty, 0);
                Vector2 gbr = WorldToIso(tx+1, ty+1, 0);
                DrawQuad(vtr, gtr, gbr, vbr, sideR);
            }
            // Bottom/south edge (ty+1, tx..tx+1) — faces down-left on screen
            if (hbl != 0 || hbr != 0) {
                Vector2 gbl = WorldToIso(tx, ty+1, 0);
                Vector2 gbr = WorldToIso(tx+1, ty+1, 0);
                DrawQuad(vbl, vbr, gbr, gbl, sideL);
            }

            // Lip edge highlight
            Vector2 lip0, lip1;
            if (t == T_RAMP_N)      { lip0 = vtl; lip1 = vtr; }
            else if (t == T_RAMP_S) { lip0 = vbl; lip1 = vbr; }
            else if (t == T_RAMP_E) { lip0 = vtr; lip1 = vbr; }
            else                    { lip0 = vtl; lip1 = vbl; }
            DrawLineEx(lip0, lip1, 2.5f, (Color){230, 210, 170, 255});

            break;
        }

        case T_QUARTER: {
            float h = 14.0f;
            Color qSideL = (Color){120, 140, 160, 255};
            Color qSideR = (Color){100, 120, 140, 255};

            // Draw curved surface strips (high at north, ground at south)
            int strips = 5;
            for (int s = 0; s < strips; s++) {
                float t0 = (float)s / strips;
                float t1 = (float)(s + 1) / strips;
                float h0 = h * cosf(t0 * PI / 2.0f);
                float h1 = h * cosf(t1 * PI / 2.0f);
                Vector2 sl = WorldToIso(tx, ty + t0, h0);
                Vector2 sr = WorldToIso(tx+1, ty + t0, h0);
                Vector2 el = WorldToIso(tx, ty + t1, h1);
                Vector2 er = WorldToIso(tx+1, ty + t1, h1);

                Color stripCol = (Color){
                    (unsigned char)(170 + s * 8),
                    (unsigned char)(190 + s * 6),
                    (unsigned char)(210 + s * 4), 255};
                DrawQuad(sl, sr, er, el, stripCol);
            }

            // Only draw camera-facing side walls (east side)
            Vector2 trH = WorldToIso(tx+1, ty, h);
            Vector2 trG = WorldToIso(tx+1, ty, 0);
            Vector2 brG = WorldToIso(tx+1, ty+1, 0);
            DrawQuad(trH, trG, brG, WorldToIso(tx+1, ty+1, 0), qSideR);

            // Coping
            Vector2 tlH = WorldToIso(tx, ty, h);
            DrawLineEx(tlH, trH, 3, ORANGE);
            DrawLineEx(tlH, trH, 1.5f, (Color){255, 200, 100, 255});
            break;
        }

        case T_RAIL_H: case T_RAIL_V: case T_RAIL_D1: case T_RAIL_D2: {
            // Ground underneath
            DrawQuad(top, right, bottom, left, (Color){170,170,175,255});
            // Rail
            float rh = 5.0f;
            Vector2 rs, re;
            if (t == T_RAIL_H) {
                rs = WorldToIso(tx, ty + 0.5f, rh);
                re = WorldToIso(tx + 1, ty + 0.5f, rh);
            } else if (t == T_RAIL_V) {
                rs = WorldToIso(tx + 0.5f, ty, rh);
                re = WorldToIso(tx + 0.5f, ty + 1, rh);
            } else if (t == T_RAIL_D1) {
                rs = WorldToIso(tx, ty + 1, rh);
                re = WorldToIso(tx + 1, ty, rh);
            } else {
                rs = WorldToIso(tx, ty, rh);
                re = WorldToIso(tx + 1, ty + 1, rh);
            }
            DrawLineEx(rs, re, 2.5f, (Color){180, 180, 190, 255});
            DrawLineEx(rs, re, 1.5f, (Color){220, 220, 230, 255});
            // Support posts
            DrawLineV(WorldToIso(tx + 0.3f, ty + 0.5f, 0), WorldToIso(tx + 0.3f, ty + 0.5f, rh),
                      (Color){150,150,160,255});
            DrawLineV(WorldToIso(tx + 0.7f, ty + 0.5f, 0), WorldToIso(tx + 0.7f, ty + 0.5f, rh),
                      (Color){150,150,160,255});
            break;
        }

        case T_GAP:
            DrawQuad(top, right, bottom, left, (Color){50,50,55,255});
            DrawLineEx(top, right, 1, YELLOW);
            DrawLineEx(left, bottom, 1, YELLOW);
            break;

        case T_WALL: {
            float wh = 12.0f;
            Vector2 topH = WorldToIso(tx, ty, wh);
            Vector2 rightH = WorldToIso(tx+1, ty, wh);
            Vector2 bottomH = WorldToIso(tx+1, ty+1, wh);
            Vector2 leftH = WorldToIso(tx, ty+1, wh);

            // Top face
            DrawQuad(topH, rightH, bottomH, leftH, (Color){100, 90, 80, 255});
            // Left side wall (south-west facing, visible)
            DrawQuad(leftH, left, bottom, bottomH, (Color){80, 70, 60, 255});
            // Right side wall (south-east facing, visible)
            DrawQuad(bottomH, bottom, right, rightH, (Color){90, 80, 70, 255});
            break;
        }

        case T_POOL: {
            float depth = 6.0f;
            // Rim at ground level
            DrawQuad(top, right, bottom, left, (Color){170, 170, 175, 255});

            // Sunken interior
            float inset = 0.15f;
            Vector2 iTop = WorldToIso(tx + inset, ty + inset, depth);
            Vector2 iRight = WorldToIso(tx + 1 - inset, ty + inset, depth);
            Vector2 iBottom = WorldToIso(tx + 1 - inset, ty + 1 - inset, depth);
            Vector2 iLeft = WorldToIso(tx + inset, ty + 1 - inset, depth);

            // Pool bottom
            DrawQuad(iTop, iRight, iBottom, iLeft, (Color){100, 160, 200, 255});

            // Inner walls
            DrawQuad(top, iTop, iRight, right, (Color){120, 170, 200, 255});
            DrawQuad(left, top, iTop, iLeft, (Color){110, 160, 190, 255});
            DrawQuad(iLeft, iBottom, bottom, left, (Color){90, 145, 175, 255});
            DrawQuad(iRight, right, bottom, iBottom, (Color){100, 155, 185, 255});

            // Shimmer
            float wave = sinf((float)GetTime() * 2.5f + tx + ty) * 0.5f;
            Vector2 center = {(iTop.x + iBottom.x)/2, (iTop.y + iBottom.y)/2 + wave};
            DrawCircleV(center, 2, (Color){140,190,220,150});
            break;
        }
    }
}

void DrawSkater(Skater *sk) {
    Vector2 screenPos = WorldToIso(sk->pos.x, sk->pos.y, sk->z);

    // Shadow
    Vector2 shadowPos = WorldToIso(sk->pos.x, sk->pos.y, 0);
    float shadowScale = 1.0f - Clamp(sk->z / 100.0f, 0, 0.5f);
    DrawEllipse(shadowPos.x, shadowPos.y, 6 * shadowScale, 3 * shadowScale, (Color){0,0,0,60});

    // Board: project world-space direction into iso screen space
    float bAngle = sk->rotation;
    if (sk->grinding) bAngle = sk->grindDir;
    float bcos = cosf(bAngle), bsin = sinf(bAngle);

    // Convert world-space board direction to iso screen offset
    // WorldToIso: sx = (wx - wy) * TILE_W/2, sy = (wx + wy) * TILE_H/2
    float boardLen = 6.0f;
    float isoX = (bcos - bsin) * (TILE_W / 2.0f) * boardLen / TILE_W;
    float isoY = (bcos + bsin) * (TILE_H / 2.0f) * boardLen / TILE_H;
    // Normalize and scale
    float isoLen = sqrtf(isoX * isoX + isoY * isoY);
    if (isoLen > 0.001f) { isoX = isoX / isoLen * boardLen; isoY = isoY / isoLen * boardLen; }

    Vector2 boardFront = { screenPos.x + isoX, screenPos.y + isoY };
    Vector2 boardBack = { screenPos.x - isoX, screenPos.y - isoY };

    // Trick rotation visual
    if (sk->airborne && sk->trickRotation != 0) {
        float flip = sinf(sk->trickRotation);
        boardFront = (Vector2){ screenPos.x + isoX * flip, screenPos.y + isoY };
        boardBack = (Vector2){ screenPos.x - isoX * flip, screenPos.y - isoY };
    }

    Color boardCol = (Color){160, 120, 60, 255};
    if (sk->grinding) boardCol = (Color){200, 160, 80, 255};
    DrawLineEx(boardBack, boardFront, 3, boardCol);

    // Wheels
    DrawCircleV(boardFront, 1.5f, DARKGRAY);
    DrawCircleV(boardBack, 1.5f, DARKGRAY);

    // Body
    Color bodyCol = (Color){60, 60, 180, 255};  // blue shirt
    Color pantsCol = (Color){80, 70, 60, 255};  // dark pants

    // Legs
    DrawLineEx(screenPos, (Vector2){screenPos.x, screenPos.y + 4}, 2, pantsCol);

    // Torso
    DrawRectangle(screenPos.x - 3, screenPos.y - 10, 6, 8, bodyCol);

    // Head
    DrawCircleV((Vector2){screenPos.x, screenPos.y - 13}, 3, (Color){240,200,160,255});
    // Cap
    float capDir = (sk->vel.x > 0) ? 1 : -1;
    DrawRectangle(screenPos.x - 2, screenPos.y - 16, 5, 2, RED);
    DrawRectangle(screenPos.x + capDir * 2, screenPos.y - 15, 3 * capDir, 1, RED);

    // Arms - change pose based on state
    if (sk->airborne && sk->lastAirTrick >= TRICK_GRAB_MELON) {
        // Grab pose: arms down to board
        DrawLineEx(screenPos, (Vector2){screenPos.x - 3, screenPos.y + 2}, 1.5f, bodyCol);
        DrawLineEx(screenPos, (Vector2){screenPos.x + 3, screenPos.y + 2}, 1.5f, bodyCol);
    } else if (sk->manualing) {
        // Balance arms out
        DrawLineEx((Vector2){screenPos.x, screenPos.y - 6},
                   (Vector2){screenPos.x - 8, screenPos.y - 8}, 1.5f, bodyCol);
        DrawLineEx((Vector2){screenPos.x, screenPos.y - 6},
                   (Vector2){screenPos.x + 8, screenPos.y - 8}, 1.5f, bodyCol);
    } else {
        // Normal pose
        DrawLineEx((Vector2){screenPos.x, screenPos.y - 6},
                   (Vector2){screenPos.x - 4, screenPos.y - 2}, 1.5f, bodyCol);
        DrawLineEx((Vector2){screenPos.x, screenPos.y - 6},
                   (Vector2){screenPos.x + 4, screenPos.y - 2}, 1.5f, bodyCol);
    }

    // Grind sparks
    if (sk->grinding) {
        for (int i = 0; i < 3; i++) {
            float ox = (float)GetRandomValue(-4, 4);
            float oy = (float)GetRandomValue(0, 4);
            DrawCircleV((Vector2){screenPos.x + ox, screenPos.y + oy}, 1,
                       (Color){255, 200 + GetRandomValue(0,55), 0, 200});
        }
    }
}

int main(void) {
    InitWindow(800, 600, "Skate - Tony Hawk GBA Style");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    BuildPark();

    Skater sk = { 0 };
    sk.pos = (Vector2){ 4, 10 };
    sk.rotation = 0;
    sk.runTimer = 120.0f;  // 2 minute run

    Camera2D camera = { 0 };
    camera.zoom = ZOOM;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;

        float screenW = (float)GetScreenWidth();
        float screenH = (float)GetScreenHeight();

        // Timer
        if (!sk.runOver) {
            sk.runTimer -= dt;
            if (sk.runTimer <= 0) {
                sk.runTimer = 0;
                sk.runOver = true;
                LandCombo(&sk);
            }
        }

        // Combo timeout
        if (sk.comboTimer > 0 && !sk.airborne && !sk.grinding && !sk.manualing) {
            sk.comboTimer -= dt;
            if (sk.comboTimer <= 0) {
                LandCombo(&sk);
            }
        }

        // Display timer
        if (sk.displayTimer > 0) sk.displayTimer -= dt;

        if (sk.runOver) goto draw;

        // --- Input ---
        int inputX = 0, inputY = 0;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))   inputX = -1;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))  inputX =  1;
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))     inputY = -1;
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))    inputY =  1;

        // --- Grinding ---
        if (sk.grinding) {
            // Move along rail
            float gcos = cosf(sk.grindDir), gsin = sinf(sk.grindDir);
            sk.pos.x += gcos * GRIND_SPEED * dt;
            sk.pos.y += gsin * GRIND_SPEED * dt;
            sk.speed = GRIND_SPEED;

            // Check if still on rail
            int tx = (int)sk.pos.x, ty = (int)sk.pos.y;
            if (!TileIsRail(GetTile(tx, ty))) {
                sk.grinding = false;
                sk.z = 8;  // pop off rail
                sk.velZ = 60;
                sk.airborne = true;
                // Keep momentum
                sk.vel = (Vector2){ gcos * GRIND_SPEED * 0.8f, gsin * GRIND_SPEED * 0.8f };
            }

            // Ollie off rail
            if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z)) {
                sk.grinding = false;
                sk.z = 8;
                sk.velZ = OLLIE_VEL * 0.7f;
                sk.airborne = true;
                sk.vel = (Vector2){ gcos * GRIND_SPEED * 0.8f, gsin * GRIND_SPEED * 0.8f };
                AddTrickToCombo(&sk, TRICK_OLLIE);
            }

            // Balance (steer to stay on)
            sk.rotation = sk.grindDir;
            goto physics_done;
        }

        // --- Manual ---
        if (sk.manualing) {
            // Balance mechanic
            sk.manualBalance += (float)GetRandomValue(-10, 10) / 100.0f * MANUAL_BALANCE_SPEED * dt * 60.0f;
            // Player can counter-steer
            if (inputX != 0) sk.manualBalance -= inputX * 2.0f * dt;

            if (fabsf(sk.manualBalance) > MANUAL_FALL_THRESH) {
                // Bail!
                sk.manualing = false;
                sk.speed *= 0.3f;
                BailCombo(&sk);
            }

            // End manual
            if (IsKeyReleased(KEY_DOWN) && !IsKeyDown(KEY_S)) {
                sk.manualing = false;
                LandCombo(&sk);
            }
            if (IsKeyReleased(KEY_S) && !IsKeyDown(KEY_DOWN)) {
                sk.manualing = false;
                LandCombo(&sk);
            }

            // Can still steer and ollie during manual
            if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z)) {
                sk.manualing = false;
                sk.velZ = OLLIE_VEL;
                sk.airborne = true;
                AddTrickToCombo(&sk, TRICK_OLLIE);
            }
        }

        // --- Steering ---
        if (inputX != 0 && !sk.grinding) {
            sk.rotation += inputX * TURN_SPEED * dt * (sk.airborne ? 0.5f : 1.0f);
        }

        // --- Push / accelerate ---
        if (!sk.airborne && !sk.grinding) {
            float fcos = cosf(sk.rotation), fsin = sinf(sk.rotation);

            if (inputY < 0) {
                // Push forward
                sk.speed += SKATE_ACCEL * dt;
                if (sk.speed > SKATE_MAX_SPEED) sk.speed = SKATE_MAX_SPEED;
            } else if (inputY > 0 && !sk.manualing) {
                // Brake
                sk.speed *= SKATE_BRAKE;
            }

            sk.vel.x = fcos * sk.speed;
            sk.vel.y = fsin * sk.speed;
            sk.speed *= SKATE_FRICTION;
        }

        // --- Ollie / Jump ---
        if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z)) && !sk.airborne && !sk.grinding && !sk.manualing) {
            sk.velZ = OLLIE_VEL;
            sk.airborne = true;
            AddTrickToCombo(&sk, TRICK_OLLIE);
        }

        // --- Air tricks ---
        if (sk.airborne && !sk.grinding) {
            // Kickflip
            if (IsKeyPressed(KEY_X) || IsKeyPressed(KEY_LEFT_SHIFT)) {
                TrickType trick;
                if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) trick = TRICK_HEELFLIP;
                else if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) trick = TRICK_SHUVIT;
                else if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) trick = TRICK_360FLIP;
                else trick = TRICK_KICKFLIP;

                sk.lastAirTrick = trick;
                sk.trickRotation = 0;
                AddTrickToCombo(&sk, trick);
            }

            // Grabs
            if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_LEFT_CONTROL)) {
                TrickType grab = (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) ? TRICK_GRAB_INDY : TRICK_GRAB_MELON;
                sk.lastAirTrick = grab;
                AddTrickToCombo(&sk, grab);
            }

            // Spin trick rotation
            if (sk.lastAirTrick >= TRICK_KICKFLIP && sk.lastAirTrick <= TRICK_360FLIP) {
                sk.trickRotation += 15.0f * dt;
            }
        }

        // --- Physics ---
        // Horizontal movement
        sk.pos.x += sk.vel.x * dt / TILE_W;
        sk.pos.y += sk.vel.y * dt / TILE_H;

        // Vertical (height)
        if (sk.airborne) {
            sk.velZ -= GRAVITY * dt;
            sk.z += sk.velZ * dt;

            if (sk.z <= 0) {
                sk.z = 0;
                sk.velZ = 0;
                sk.airborne = false;
                sk.trickRotation = 0;
                sk.lastAirTrick = TRICK_NONE;

                // Check what we landed on
                int tx = (int)sk.pos.x, ty = (int)sk.pos.y;
                int tile = GetTile(tx, ty);

                if (TileIsRail(tile)) {
                    // Snap to grind
                    sk.grinding = true;
                    // Determine grind direction from rail type
                    switch (tile) {
                        case T_RAIL_H:  sk.grindDir = (sk.vel.x > 0) ? 0 : PI; break;
                        case T_RAIL_V:  sk.grindDir = (sk.vel.y > 0) ? PI/2.0f : -PI/2.0f; break;
                        case T_RAIL_D1: sk.grindDir = (sk.vel.x > 0) ? -PI/4 : PI*3/4; break;
                        case T_RAIL_D2: sk.grindDir = (sk.vel.x > 0) ? PI/4 : -PI*3/4; break;
                    }
                    sk.z = 5;  // rail height

                    TrickType grindTrick = TRICK_GRIND_50;
                    if (inputY < 0) grindTrick = TRICK_GRIND_NOSE;
                    else if (inputY > 0) grindTrick = TRICK_GRIND_TAIL;
                    AddTrickToCombo(&sk, grindTrick);
                } else if (tile == T_GAP) {
                    // Cleared a gap!
                    AddTrickToCombo(&sk, TRICK_GAP_BONUS);
                } else if (tile == T_WALL) {
                    // Bail on wall
                    sk.speed *= 0.2f;
                    sk.vel = (Vector2){ -sk.vel.x * 0.3f, -sk.vel.y * 0.3f };
                    BailCombo(&sk);
                }
            }
        }

        // Ramp launch
        if (!sk.airborne && !sk.grinding) {
            int tx = (int)sk.pos.x, ty = (int)sk.pos.y;
            int tile = GetTile(tx, ty);

            if (TileIsRamp(tile) || TileIsQuarter(tile)) {
                Vector2 rdir = RampDirection(tile);
                // Check if moving in ramp direction
                float dot = sk.vel.x * rdir.x + sk.vel.y * rdir.y;
                if (dot > 30.0f) {
                    float launchMult = RampLaunchHeight(tile);
                    sk.velZ = OLLIE_VEL * launchMult;
                    sk.airborne = true;
                    // Boost in ramp direction
                    sk.vel.x += rdir.x * 40.0f;
                    sk.vel.y += rdir.y * 40.0f;
                }
            }

            // Manual input (down while on ground moving)
            if ((IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) && sk.speed > 30.0f && !sk.manualing) {
                sk.manualing = true;
                sk.manualBalance = 0;
                TrickType mt = (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) ? TRICK_NOSE_MANUAL : TRICK_MANUAL;
                AddTrickToCombo(&sk, mt);
            }
        }

        // Wall collision
        {
            int tx = (int)sk.pos.x, ty = (int)sk.pos.y;
            if (GetTile(tx, ty) == T_WALL && !sk.airborne) {
                // Bounce back
                sk.pos.x -= sk.vel.x * dt / TILE_W * 2;
                sk.pos.y -= sk.vel.y * dt / TILE_H * 2;
                sk.speed *= 0.3f;
                sk.vel = (Vector2){ -sk.vel.x * 0.3f, -sk.vel.y * 0.3f };
                if (sk.comboLen > 0) BailCombo(&sk);
            }
        }

        // Clamp to world
        sk.pos.x = Clamp(sk.pos.x, 1.0f, WORLD_W - 1.5f);
        sk.pos.y = Clamp(sk.pos.y, 1.0f, WORLD_H - 1.5f);

physics_done:

        // --- Camera ---
        {
            Vector2 isoPos = WorldToIso(sk.pos.x, sk.pos.y, sk.z);
            // Lead camera slightly in movement direction
            float lead = Clamp(sk.speed / SKATE_MAX_SPEED, 0, 1) * 20.0f;
            Vector2 target = {
                isoPos.x + cosf(sk.rotation) * lead,
                isoPos.y + sinf(sk.rotation) * lead * 0.5f
            };
            camera.target = Vector2Lerp(camera.target, target, 6.0f * dt);
            camera.offset = (Vector2){ screenW / 2.0f, screenH / 2.0f };
        }

draw:
        BeginDrawing();
        ClearBackground((Color){40, 40, 50, 255});

        BeginMode2D(camera);
            // Draw tiles back to front (painter's order for iso)
            for (int y = 0; y < WORLD_H; y++) {
                for (int x = 0; x < WORLD_W; x++) {
                    DrawTileIso(x, y);
                }

                // Draw skater at correct depth
                int skTileY = (int)sk.pos.y;
                if (y == skTileY) {
                    DrawSkater(&sk);
                }
            }
            // Draw skater if at bottom edge
            if ((int)sk.pos.y >= WORLD_H - 1) DrawSkater(&sk);

        EndMode2D();

        // --- HUD ---
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();

        // Score
        DrawText(TextFormat("SCORE: %d", sk.totalScore), 10, 10, 24, WHITE);

        // Timer
        int mins = (int)sk.runTimer / 60;
        int secs = (int)sk.runTimer % 60;
        Color timerCol = (sk.runTimer < 10) ? RED : WHITE;
        DrawText(TextFormat("%d:%02d", mins, secs), sw - 80, 10, 28, timerCol);

        // Current combo
        if (sk.comboLen > 0) {
            DrawRectangle(sw/2 - 100, 8, 200, 50, (Color){0,0,0,150});

            // Combo score
            DrawText(TextFormat("%d", sk.comboScore), sw/2 - 30, 12, 24, YELLOW);

            // Multiplier
            DrawText(TextFormat("x%d", sk.comboMult), sw/2 + 30, 12, 20, ORANGE);

            // Last trick name
            if (sk.displayTimer > 0) {
                int tw = MeasureText(sk.displayText, 16);
                DrawText(sk.displayText, sw/2 - tw/2, 36, 16, WHITE);
            }

            // Combo timer bar
            if (!sk.airborne && !sk.grinding && !sk.manualing && sk.comboTimer > 0) {
                float pct = sk.comboTimer / COMBO_TIMEOUT;
                DrawRectangle(sw/2 - 50, 55, (int)(100 * pct), 4, GREEN);
            }
        }

        // Bail text
        if (sk.displayTimer > 0 && sk.comboLen == 0 && strcmp(sk.displayText, "BAIL!") == 0) {
            int bw = MeasureText("BAIL!", 36);
            DrawText("BAIL!", sw/2 - bw/2, sh/2 - 18, 36, RED);
        }

        // Manual balance bar
        if (sk.manualing) {
            int barX = sw/2 - 50, barY = sh - 50;
            DrawRectangle(barX, barY, 100, 8, (Color){40,40,40,180});
            DrawRectangle(barX + 49, barY, 2, 8, WHITE); // center mark
            float balPx = 50 + sk.manualBalance * 50;
            Color balCol = (fabsf(sk.manualBalance) > 0.7f) ? RED : GREEN;
            DrawRectangle(barX + (int)balPx - 2, barY - 1, 4, 10, balCol);
        }

        // Grind indicator
        if (sk.grinding) {
            DrawText("GRINDING", sw/2 - 40, sh - 50, 18, (Color){255, 200, 50, 255});
        }

        // Speed indicator
        {
            float speedPct = Clamp(sk.speed / SKATE_MAX_SPEED, 0, 1);
            DrawRectangle(10, sh - 25, 80, 8, (Color){40,40,40,180});
            DrawRectangle(11, sh - 24, (int)(78 * speedPct), 6, GREEN);
        }

        // Best combo
        if (sk.bestCombo > 0) {
            DrawText(TextFormat("BEST: %d", sk.bestCombo), 10, 38, 16, (Color){150,150,150,255});
        }

        // Controls
        DrawText("WASD: Move  Space/Z: Ollie  X/Shift: Flip tricks  C/Ctrl: Grabs  Down: Manual", 10, sh - 14, 11, (Color){120,120,140,200});

        // Run over
        if (sk.runOver) {
            DrawRectangle(0, 0, sw, sh, (Color){0,0,0,160});
            const char *overText = "TIME'S UP!";
            int ow = MeasureText(overText, 50);
            DrawText(overText, sw/2 - ow/2, sh/2 - 60, 50, WHITE);
            DrawText(TextFormat("Final Score: %d", sk.totalScore), sw/2 - 90, sh/2, 28, YELLOW);
            DrawText(TextFormat("Best Combo: %d", sk.bestCombo), sw/2 - 85, sh/2 + 35, 22, ORANGE);
        }

        DrawFPS(sw - 80, sh - 25);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
