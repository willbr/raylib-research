#include "raylib.h"
#include "raymath.h"
#include "../common/sprites2d.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Street Fighter style 2D fighting game

// Design resolution — everything is in these coords, then scaled to window
#define DESIGN_W     1280.0f
#define DESIGN_H      720.0f
#define GROUND_Y      500.0f
#define GRAVITY       1800.0f
#define STAGE_LEFT     50.0f
#define STAGE_RIGHT  1230.0f
#define WALK_SPEED    250.0f
#define JUMP_FORCE   -700.0f
#define PUSHBACK     300.0f
#define ROUND_TIME     99

// Fighter states
typedef enum {
    FS_IDLE, FS_WALK_FWD, FS_WALK_BACK, FS_CROUCH,
    FS_JUMP, FS_PUNCH, FS_KICK, FS_HADOUKEN,
    FS_BLOCK, FS_HIT, FS_KNOCKDOWN, FS_WIN
} FighterState;

// Attack properties
typedef struct {
    int damage;
    float hitstun;    // how long defender is stunned
    float pushback;   // knockback force
    bool launched;    // was the hitbox active this attack
} AttackInfo;

typedef struct {
    float x, y;
    float velX, velY;
    bool facingRight;
    FighterState state;
    int hp, maxHp;
    float stateTimer;
    float hitstunTimer;
    bool grounded;
    bool hitConnected;  // prevent multi-hit per attack
    int wins;

    // Puppet animations (new system)
    PuppetRig rig;
    PuppetAnim pIdle, pWalk, pPunch, pKick;
    PuppetAnim pCrouch, pJump, pHit, pBlock;
    PuppetAnim pHadouken;
    PuppetState puppet;
    bool usePuppet;  // true if rig loaded successfully

    // Frame-based animations (fallback)
    SpriteAnim animIdle, animWalk, animPunch, animKick;
    SpriteAnim animCrouch, animJump, animHit, animBlock;
    SpriteAnim animHadouken;
    SpriteAnimState animState;

    AttackInfo currentAttack;

    // Input (for both player and AI)
    bool keyLeft, keyRight, keyUp, keyDown;
    bool keyPunch, keyKick, keySpecial;

    // Combo tracking
    int comboCount;
    float comboTimer;
} Fighter;

// Projectile (hadouken)
#define MAX_PROJECTILES 4
typedef struct {
    float x, y;
    float velX;
    int damage;
    bool active;
    int owner;   // 0 or 1
    float life;
    SpriteAnimState animState;
} Projectile;

// Hit spark effect
#define MAX_SPARKS 16
typedef struct {
    float x, y;
    float life;
    float size;
    bool active;
} HitSpark;

static Fighter fighters[2];
static Projectile projectiles[MAX_PROJECTILES];
static HitSpark sparks[MAX_SPARKS];
static int roundTimer;
static float roundTimerAccum;
static int currentRound;
static float roundStartTimer;
static float roundEndTimer;
static bool roundActive;
static const char *roundMessage;
static float shakeTimer;
static float shakeAmount;
static bool showHitboxes;
static float gameTime;

// Hadouken sprite (shared)
static SpriteAnim hadoukenBall;

void LoadFighterAnims(Fighter *f, const char *name) {
    char path[128];

    // Try puppet system first
    snprintf(path, sizeof(path), "fighter/sprites/%s/%s.rig2d", name, name);
    f->usePuppet = (LoadPuppetRig(path, &f->rig) > 0);
    if (f->usePuppet) {
        snprintf(path, sizeof(path), "fighter/sprites/%s/idle.anim2d", name);
        LoadPuppetAnim(path, &f->pIdle, &f->rig);
        snprintf(path, sizeof(path), "fighter/sprites/%s/walk.anim2d", name);
        LoadPuppetAnim(path, &f->pWalk, &f->rig);
        snprintf(path, sizeof(path), "fighter/sprites/%s/punch.anim2d", name);
        LoadPuppetAnim(path, &f->pPunch, &f->rig);
        snprintf(path, sizeof(path), "fighter/sprites/%s/kick.anim2d", name);
        LoadPuppetAnim(path, &f->pKick, &f->rig);
        snprintf(path, sizeof(path), "fighter/sprites/%s/crouch.anim2d", name);
        LoadPuppetAnim(path, &f->pCrouch, &f->rig);
        snprintf(path, sizeof(path), "fighter/sprites/%s/jump.anim2d", name);
        LoadPuppetAnim(path, &f->pJump, &f->rig);
        snprintf(path, sizeof(path), "fighter/sprites/%s/hit.anim2d", name);
        LoadPuppetAnim(path, &f->pHit, &f->rig);
        snprintf(path, sizeof(path), "fighter/sprites/%s/block.anim2d", name);
        LoadPuppetAnim(path, &f->pBlock, &f->rig);
        snprintf(path, sizeof(path), "fighter/sprites/%s/hadouken.anim2d", name);
        LoadPuppetAnim(path, &f->pHadouken, &f->rig);
    }

    // Also load frame-based as fallback
    snprintf(path, sizeof(path), "fighter/sprites/%s/idle", name);
    LoadSpriteAnim(path, &f->animIdle, true);
    snprintf(path, sizeof(path), "fighter/sprites/%s/walk", name);
    LoadSpriteAnim(path, &f->animWalk, true);
    snprintf(path, sizeof(path), "fighter/sprites/%s/punch", name);
    LoadSpriteAnim(path, &f->animPunch, false);
    snprintf(path, sizeof(path), "fighter/sprites/%s/kick", name);
    LoadSpriteAnim(path, &f->animKick, false);
    snprintf(path, sizeof(path), "fighter/sprites/%s/crouch", name);
    LoadSpriteAnim(path, &f->animCrouch, false);
    snprintf(path, sizeof(path), "fighter/sprites/%s/jump", name);
    LoadSpriteAnim(path, &f->animJump, false);
    snprintf(path, sizeof(path), "fighter/sprites/%s/hit", name);
    LoadSpriteAnim(path, &f->animHit, false);
    snprintf(path, sizeof(path), "fighter/sprites/%s/block", name);
    LoadSpriteAnim(path, &f->animBlock, false);
    snprintf(path, sizeof(path), "fighter/sprites/%s/hadouken", name);
    LoadSpriteAnim(path, &f->animHadouken, false);
}

void SpawnSpark(float x, float y) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (!sparks[i].active) {
            sparks[i] = (HitSpark){x, y, 0.3f, 30.0f, true};
            break;
        }
    }
}

void InitFighter(Fighter *f, float x, bool facingRight) {
    f->x = x;
    f->y = GROUND_Y;
    f->velX = 0;
    f->velY = 0;
    f->facingRight = facingRight;
    f->state = FS_IDLE;
    f->hp = f->maxHp;
    f->stateTimer = 0;
    f->hitstunTimer = 0;
    f->grounded = true;
    f->hitConnected = false;
    f->comboCount = 0;
    f->comboTimer = 0;
    memset(&f->currentAttack, 0, sizeof(AttackInfo));
    SpriteAnimForcePlay(&f->animState, &f->animIdle, !facingRight);
    if (f->usePuppet && f->pIdle.frameCount > 0)
        PuppetForcePlay(&f->puppet, &f->rig, &f->pIdle, !facingRight);
}

void InitRound(void) {
    InitFighter(&fighters[0], 350.0f, true);
    InitFighter(&fighters[1], 930.0f, false);
    for (int i = 0; i < MAX_PROJECTILES; i++) projectiles[i].active = false;
    for (int i = 0; i < MAX_SPARKS; i++) sparks[i].active = false;
    roundTimer = ROUND_TIME;
    roundTimerAccum = 0;
    roundActive = false;
    roundStartTimer = 2.0f;
    roundEndTimer = 0;
    roundMessage = TextFormat("ROUND %d", currentRound);
    shakeTimer = 0;
}

void SetFighterState(Fighter *f, FighterState newState) {
    f->state = newState;
    f->stateTimer = 0;
    f->hitConnected = false;

    // Map state to puppet anim and frame-based anim
    PuppetAnim *panim = NULL;
    SpriteAnim *anim = NULL;
    switch (newState) {
        case FS_IDLE:      panim = &f->pIdle;     anim = &f->animIdle; break;
        case FS_WALK_FWD:
        case FS_WALK_BACK: panim = &f->pWalk;     anim = &f->animWalk; break;
        case FS_CROUCH:    panim = &f->pCrouch;   anim = &f->animCrouch; break;
        case FS_JUMP:      panim = &f->pJump;     anim = &f->animJump; break;
        case FS_PUNCH:
            panim = &f->pPunch; anim = &f->animPunch;
            f->currentAttack = (AttackInfo){10, 0.3f, PUSHBACK, false};
            break;
        case FS_KICK:
            panim = &f->pKick; anim = &f->animKick;
            f->currentAttack = (AttackInfo){15, 0.4f, PUSHBACK * 1.3f, false};
            break;
        case FS_HADOUKEN:
            panim = &f->pHadouken; anim = &f->animHadouken;
            f->currentAttack = (AttackInfo){0, 0, 0, false};
            break;
        case FS_BLOCK:     panim = &f->pBlock;    anim = &f->animBlock; break;
        case FS_HIT:       panim = &f->pHit;      anim = &f->animHit; break;
        case FS_KNOCKDOWN: panim = &f->pHit;      anim = &f->animHit; break;
        case FS_WIN:       panim = &f->pIdle;     anim = &f->animIdle; break;
    }

    if (f->usePuppet && panim && panim->frameCount > 0)
        PuppetForcePlay(&f->puppet, &f->rig, panim, !f->facingRight);
    if (anim) SpriteAnimForcePlay(&f->animState, anim, !f->facingRight);
}

bool CanAct(Fighter *f) {
    return f->state == FS_IDLE || f->state == FS_WALK_FWD || f->state == FS_WALK_BACK;
}

bool BoxOverlap(SpriteBox a, SpriteBox b) {
    return fabsf(a.x - b.x) < (a.w + b.w) / 2.0f &&
           fabsf(a.y - b.y) < (a.h + b.h) / 2.0f;
}

void UpdateFighter(Fighter *f, Fighter *other, int idx, float dt) {
    f->stateTimer += dt;
    f->comboTimer -= dt;
    if (f->comboTimer <= 0) f->comboCount = 0;
    SpriteAnimUpdate(&f->animState, dt);
    if (f->usePuppet) PuppetUpdate(&f->puppet, dt);

    // Face opponent
    if (f->state != FS_HIT && f->state != FS_KNOCKDOWN) {
        f->facingRight = (f->x < other->x);
        f->animState.flipped = !f->facingRight;
        if (f->usePuppet) f->puppet.flipped = !f->facingRight;
    }

    // Gravity
    if (!f->grounded) {
        f->velY += GRAVITY * dt;
        f->y += f->velY * dt;
        if (f->y >= GROUND_Y) {
            f->y = GROUND_Y;
            f->velY = 0;
            f->grounded = true;
            if (f->state == FS_JUMP) SetFighterState(f, FS_IDLE);
        }
    }

    // Horizontal movement
    f->x += f->velX * dt;
    if (f->x < STAGE_LEFT) f->x = STAGE_LEFT;
    if (f->x > STAGE_RIGHT) f->x = STAGE_RIGHT;

    // Push apart if overlapping
    float dist = fabsf(f->x - other->x);
    if (dist < 50.0f && f->grounded) {
        float push = (50.0f - dist) * 0.5f;
        if (f->x < other->x) { f->x -= push; other->x += push; }
        else { f->x += push; other->x -= push; }
    }

    // State machine
    switch (f->state) {
        case FS_IDLE:
        case FS_WALK_FWD:
        case FS_WALK_BACK:
            f->velX = 0;
            if (f->keyDown) { SetFighterState(f, FS_CROUCH); break; }
            if (f->keyUp && f->grounded) {
                SetFighterState(f, FS_JUMP);
                f->velY = JUMP_FORCE;
                f->grounded = false;
                break;
            }
            if (f->keyPunch) { SetFighterState(f, FS_PUNCH); break; }
            if (f->keyKick) { SetFighterState(f, FS_KICK); break; }
            if (f->keySpecial) { SetFighterState(f, FS_HADOUKEN); break; }
            // Movement
            if (f->keyRight) {
                f->velX = f->facingRight ? WALK_SPEED : -WALK_SPEED;
                FighterState ws = f->facingRight ? FS_WALK_FWD : FS_WALK_BACK;
                if (f->state != ws) SetFighterState(f, ws);
            } else if (f->keyLeft) {
                f->velX = f->facingRight ? -WALK_SPEED : WALK_SPEED;
                FighterState ws = f->facingRight ? FS_WALK_BACK : FS_WALK_FWD;
                if (f->state != ws) SetFighterState(f, ws);
            } else if (f->state != FS_IDLE) {
                SetFighterState(f, FS_IDLE);
            }
            // Block (hold back while being attacked)
            if (other->state == FS_PUNCH || other->state == FS_KICK) {
                bool holdingBack = (f->facingRight && f->keyLeft) || (!f->facingRight && f->keyRight);
                if (holdingBack && dist < 120.0f) {
                    SetFighterState(f, FS_BLOCK);
                }
            }
            break;

        case FS_CROUCH:
            f->velX = 0;
            if (!f->keyDown) SetFighterState(f, FS_IDLE);
            if (f->keyPunch) SetFighterState(f, FS_PUNCH);
            if (f->keyKick) SetFighterState(f, FS_KICK);
            break;

        case FS_JUMP:
            if (f->keyPunch && !f->hitConnected) {
                // Air attack - just set attack info
                f->currentAttack = (AttackInfo){8, 0.2f, PUSHBACK * 0.5f, false};
            }
            break;

        case FS_PUNCH:
        case FS_KICK:
            f->velX *= 0.9f;
            if (f->animState.finished) SetFighterState(f, FS_IDLE);
            break;

        case FS_HADOUKEN:
            f->velX = 0;
            // Spawn projectile on frame 1
            if (f->animState.currentFrame >= 1 && !f->currentAttack.launched) {
                f->currentAttack.launched = true;
                for (int i = 0; i < MAX_PROJECTILES; i++) {
                    if (!projectiles[i].active) {
                        float dir = f->facingRight ? 1.0f : -1.0f;
                        projectiles[i] = (Projectile){
                            f->x + dir * 40.0f, f->y - 30.0f,
                            dir * 400.0f, 12, true, idx, 2.0f
                        };
                        if (hadoukenBall.frameCount > 0)
                            SpriteAnimForcePlay(&projectiles[i].animState, &hadoukenBall, !f->facingRight);
                        break;
                    }
                }
            }
            if (f->animState.finished) SetFighterState(f, FS_IDLE);
            break;

        case FS_BLOCK:
            f->velX = 0;
            {
                bool holdingBack = (f->facingRight && f->keyLeft) || (!f->facingRight && f->keyRight);
                if (!holdingBack) SetFighterState(f, FS_IDLE);
            }
            break;

        case FS_HIT:
            f->hitstunTimer -= dt;
            f->velX *= 0.92f;
            if (f->hitstunTimer <= 0) SetFighterState(f, FS_IDLE);
            break;

        case FS_KNOCKDOWN:
            f->velX *= 0.95f;
            if (f->stateTimer > 1.0f) {
                if (f->hp > 0) SetFighterState(f, FS_IDLE);
            }
            break;

        case FS_WIN:
            f->velX = 0;
            break;
    }
}

// Get hitboxes/hurtboxes from whichever animation system is active
void GetBoxes(Fighter *f, SpriteBox **hitboxes, int *hitCount, SpriteBox **hurtboxes, int *hurtCount) {
    if (f->usePuppet && f->puppet.anim && f->puppet.anim->frameCount > 0) {
        PuppetKeyframe *kf = &f->puppet.anim->frames[f->puppet.currentFrame];
        *hitboxes = kf->hitboxes; *hitCount = kf->hitboxCount;
        *hurtboxes = kf->hurtboxes; *hurtCount = kf->hurtboxCount;
    } else if (f->animState.anim && f->animState.anim->frameCount > 0) {
        SpriteFrame *sf = &f->animState.anim->frames[f->animState.currentFrame];
        *hitboxes = sf->hitboxes; *hitCount = sf->hitboxCount;
        *hurtboxes = sf->hurtboxes; *hurtCount = sf->hurtboxCount;
    } else {
        *hitboxes = NULL; *hitCount = 0;
        *hurtboxes = NULL; *hurtCount = 0;
    }
}

void CheckHit(Fighter *attacker, Fighter *defender, float scale) {
    if (attacker->hitConnected) return;
    if (defender->state == FS_KNOCKDOWN) return;
    if (attacker->state != FS_PUNCH && attacker->state != FS_KICK && attacker->state != FS_JUMP) return;

    SpriteBox *aHit, *dHurt;
    int aHitCount, dHurtCount;
    GetBoxes(attacker, &aHit, &aHitCount, NULL, NULL);
    SpriteBox *dummy; int dummyN;
    GetBoxes(defender, &dummy, &dummyN, &dHurt, &dHurtCount);
    if (!aHit || !dHurt) return;

    for (int h = 0; h < aHitCount; h++) {
        SpriteBox hitbox = SpriteBoxTransform(aHit[h], attacker->x, attacker->y, scale, !attacker->facingRight);
        for (int u = 0; u < dHurtCount; u++) {
            SpriteBox hurtbox = SpriteBoxTransform(dHurt[u], defender->x, defender->y, scale, !defender->facingRight);
            if (BoxOverlap(hitbox, hurtbox)) {
                attacker->hitConnected = true;

                if (defender->state == FS_BLOCK) {
                    // Chip damage
                    defender->hp -= 2;
                    float pushDir = (attacker->x < defender->x) ? 1.0f : -1.0f;
                    defender->velX = pushDir * attacker->currentAttack.pushback * 0.3f;
                    SpawnSpark((attacker->x + defender->x) / 2, hitbox.y);
                } else {
                    // Full hit
                    defender->hp -= attacker->currentAttack.damage;
                    defender->hitstunTimer = attacker->currentAttack.hitstun;
                    float pushDir = (attacker->x < defender->x) ? 1.0f : -1.0f;
                    defender->velX = pushDir * attacker->currentAttack.pushback;

                    attacker->comboCount++;
                    attacker->comboTimer = 1.0f;

                    if (defender->hp <= 0) {
                        defender->hp = 0;
                        SetFighterState(defender, FS_KNOCKDOWN);
                        defender->velX = pushDir * attacker->currentAttack.pushback * 1.5f;
                    } else {
                        SetFighterState(defender, FS_HIT);
                    }

                    SpawnSpark((attacker->x + defender->x) / 2, hitbox.y);
                    shakeTimer = 0.15f;
                    shakeAmount = 4.0f;
                }
                return;
            }
        }
    }
}

// Simple AI for player 2
void UpdateAI(Fighter *ai, Fighter *player, float dt) {
    float dist = fabsf(ai->x - player->x);
    ai->keyLeft = ai->keyRight = ai->keyUp = ai->keyDown = false;
    ai->keyPunch = ai->keyKick = ai->keySpecial = false;

    if (ai->state == FS_HIT || ai->state == FS_KNOCKDOWN || ai->state == FS_WIN) return;

    // Approach
    if (dist > 120.0f) {
        if (ai->x < player->x) ai->keyRight = true;
        else ai->keyLeft = true;
        // Occasionally jump forward
        if (GetRandomValue(0, 200) == 0) ai->keyUp = true;
    }
    // Close range attacks
    else if (dist < 100.0f) {
        int r = GetRandomValue(0, 60);
        if (r == 0) ai->keyPunch = true;
        else if (r == 1) ai->keyKick = true;
        else if (r == 2 && dist > 60.0f) ai->keySpecial = true;
        // Block sometimes
        if (player->state == FS_PUNCH || player->state == FS_KICK) {
            if (GetRandomValue(0, 3) == 0) {
                ai->keyLeft = ai->facingRight ? true : false;
                ai->keyRight = ai->facingRight ? false : true;
            }
        }
    }
    // Mid range - hadouken
    else if (dist > 200.0f && GetRandomValue(0, 120) == 0) {
        ai->keySpecial = true;
    }
}

void DrawHPBar(float x, float y, float w, float h, int hp, int maxHp, bool mirror) {
    float pct = (float)hp / maxHp;
    Color hpCol = pct > 0.5f ? (Color){50, 220, 80, 255} : pct > 0.25f ? (Color){240, 200, 40, 255} : (Color){220, 40, 40, 255};
    Color hpDark = pct > 0.5f ? (Color){30, 140, 50, 255} : pct > 0.25f ? (Color){180, 140, 20, 255} : (Color){160, 20, 20, 255};
    // Background
    DrawRectangle(x - 2, y - 2, w + 4, h + 4, (Color){15, 15, 25, 255});
    DrawRectangle(x, y, w, h, (Color){40, 35, 50, 255});
    // Damage flash (slightly wider than current HP, darker color, to show recent damage)
    int hpW = (int)(w * pct);
    if (mirror) {
        DrawRectangle(x + w - hpW, y, hpW, h, hpDark);
        // Bright bar on top half for shine
        DrawRectangle(x + w - hpW, y, hpW, h / 2, hpCol);
        // Highlight line at top
        DrawRectangle(x + w - hpW, y, hpW, 2, (Color){hpCol.r + 30 > 255 ? 255 : hpCol.r + 30,
            hpCol.g + 30 > 255 ? 255 : hpCol.g + 30, hpCol.b + 30 > 255 ? 255 : hpCol.b + 30, 200});
    } else {
        DrawRectangle(x, y, hpW, h, hpDark);
        DrawRectangle(x, y, hpW, h / 2, hpCol);
        DrawRectangle(x, y, hpW, 2, (Color){hpCol.r + 30 > 255 ? 255 : hpCol.r + 30,
            hpCol.g + 30 > 255 ? 255 : hpCol.g + 30, hpCol.b + 30 > 255 ? 255 : hpCol.b + 30, 200});
    }
    // Border
    DrawRectangleLinesEx((Rectangle){x - 2, y - 2, w + 4, h + 4}, 2, (Color){100, 90, 120, 255});
    // Tick marks
    for (int t = 1; t < 4; t++) {
        int tx2 = x + (int)(w * t / 4.0f);
        DrawLine(tx2, y, tx2, y + h, (Color){20, 20, 30, 100});
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Street Fighter");
    MaximizeWindow();
    SetTargetFPS(60);
    SetExitKey(0);

    // Load animations
    LoadFighterAnims(&fighters[0], "ryu");
    LoadFighterAnims(&fighters[1], "ken");
    fighters[0].maxHp = fighters[1].maxHp = 100;
    fighters[0].hp = fighters[1].hp = 100;

    // Load hadouken ball anim
    LoadSpriteAnim("fighter/sprites/ryu/hadouken_ball", &hadoukenBall, true);

    currentRound = 1;
    InitRound();

    float scale = 1.0f;
    float sprScale = 2.0f;  // make fighters bigger
    showHitboxes = false;
    gameTime = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        gameTime += dt;
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        // Scale everything to fit window, maintaining aspect ratio
        float scaleX = (float)sw / DESIGN_W;
        float scaleY = (float)sh / DESIGN_H;
        scale = (scaleX < scaleY) ? scaleX : scaleY;
        float totalSprScale = scale * sprScale;

        // Toggle hitbox display
        if (IsKeyPressed(KEY_H)) showHitboxes = !showHitboxes;

        // Round start countdown
        if (roundStartTimer > 0) {
            roundStartTimer -= dt;
            if (roundStartTimer <= 0) {
                roundActive = true;
                roundMessage = "FIGHT!";
            }
        }

        if (roundActive) {
            // Timer
            roundTimerAccum += dt;
            if (roundTimerAccum >= 1.0f) {
                roundTimerAccum -= 1.0f;
                roundTimer--;
                if (roundTimer <= 0) {
                    roundTimer = 0;
                    roundActive = false;
                    roundEndTimer = 3.0f;
                    // Time up — whoever has more HP wins
                    if (fighters[0].hp >= fighters[1].hp) {
                        roundMessage = "TIME! P1 WINS";
                        fighters[0].wins++;
                        SetFighterState(&fighters[0], FS_WIN);
                    } else {
                        roundMessage = "TIME! P2 WINS";
                        fighters[1].wins++;
                        SetFighterState(&fighters[1], FS_WIN);
                    }
                }
            }

            // Player 1 input
            fighters[0].keyLeft = IsKeyDown(KEY_A);
            fighters[0].keyRight = IsKeyDown(KEY_D);
            fighters[0].keyUp = IsKeyPressed(KEY_W);
            fighters[0].keyDown = IsKeyDown(KEY_S);
            fighters[0].keyPunch = IsKeyPressed(KEY_J);
            fighters[0].keyKick = IsKeyPressed(KEY_K);
            fighters[0].keySpecial = IsKeyPressed(KEY_L);

            // AI for player 2
            UpdateAI(&fighters[1], &fighters[0], dt);

            // Update fighters
            for (int i = 0; i < 2; i++)
                UpdateFighter(&fighters[i], &fighters[1 - i], i, dt);

            // Check hits
            CheckHit(&fighters[0], &fighters[1], totalSprScale);
            CheckHit(&fighters[1], &fighters[0], totalSprScale);

            // Update projectiles
            for (int i = 0; i < MAX_PROJECTILES; i++) {
                if (!projectiles[i].active) continue;
                projectiles[i].x += projectiles[i].velX * dt;
                projectiles[i].life -= dt;
                SpriteAnimUpdate(&projectiles[i].animState, dt);
                if (projectiles[i].life <= 0 || projectiles[i].x < 0 || projectiles[i].x > 1280) {
                    projectiles[i].active = false;
                    continue;
                }
                // Hit check vs opponent
                int target = 1 - projectiles[i].owner;
                Fighter *def = &fighters[target];
                if (def->state != FS_KNOCKDOWN && fabsf(projectiles[i].x - def->x) < 30 &&
                    fabsf(projectiles[i].y - (def->y - 30)) < 40) {
                    if (def->state == FS_BLOCK) {
                        def->hp -= 2;
                    } else {
                        def->hp -= projectiles[i].damage;
                        def->hitstunTimer = 0.3f;
                        float pushDir = (projectiles[i].velX > 0) ? 1.0f : -1.0f;
                        def->velX = pushDir * 200.0f;
                        if (def->hp <= 0) {
                            def->hp = 0;
                            SetFighterState(def, FS_KNOCKDOWN);
                        } else {
                            SetFighterState(def, FS_HIT);
                        }
                        fighters[projectiles[i].owner].comboCount++;
                        fighters[projectiles[i].owner].comboTimer = 1.0f;
                    }
                    SpawnSpark(projectiles[i].x, projectiles[i].y);
                    shakeTimer = 0.1f;
                    shakeAmount = 3.0f;
                    projectiles[i].active = false;
                }
            }

            // Check for KO
            for (int i = 0; i < 2; i++) {
                if (fighters[i].hp <= 0 && fighters[i].state == FS_KNOCKDOWN && fighters[i].stateTimer > 1.0f) {
                    roundActive = false;
                    roundEndTimer = 3.0f;
                    int winner = 1 - i;
                    fighters[winner].wins++;
                    SetFighterState(&fighters[winner], FS_WIN);
                    roundMessage = (winner == 0) ? "K.O.! P1 WINS" : "K.O.! P2 WINS";
                }
            }
        }

        // Round end
        if (roundEndTimer > 0) {
            roundEndTimer -= dt;
            if (roundEndTimer <= 0) {
                currentRound++;
                if (currentRound > 5) currentRound = 1;
                InitRound();
            }
        }

        // Shake decay
        if (shakeTimer > 0) shakeTimer -= dt;

        // Update sparks
        for (int i = 0; i < MAX_SPARKS; i++) {
            if (!sparks[i].active) continue;
            sparks[i].life -= dt;
            if (sparks[i].life <= 0) sparks[i].active = false;
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground((Color){20, 20, 35, 255});

        float shakeX = 0, shakeY = 0;
        if (shakeTimer > 0) {
            shakeX = (float)GetRandomValue(-3, 3) * shakeAmount * (shakeTimer / 0.15f);
            shakeY = (float)GetRandomValue(-3, 3) * shakeAmount * (shakeTimer / 0.15f);
        }

        // Stage background
        float groundScreen = GROUND_Y * scale + shakeY;
        // Sky gradient
        for (int y2 = 0; y2 < (int)groundScreen; y2++) {
            float t = (float)y2 / groundScreen;
            Color c = {(unsigned char)(30 + t * 40), (unsigned char)(30 + t * 60), (unsigned char)(80 + t * 40), 255};
            DrawRectangle(0, y2, sw, 1, c);
        }
        // Ground
        DrawRectangle(0, (int)groundScreen, sw, sh - (int)groundScreen, (Color){60, 55, 50, 255});
        DrawRectangle(0, (int)groundScreen, sw, 3, (Color){80, 75, 65, 255});
        // Stage markers
        DrawRectangle((int)(STAGE_LEFT * scale + shakeX), (int)groundScreen + 3, 3, 20, (Color){100, 80, 40, 255});
        DrawRectangle((int)(STAGE_RIGHT * scale + shakeX), (int)groundScreen + 3, 3, 20, (Color){100, 80, 40, 255});

        // Draw projectiles
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (!projectiles[i].active) continue;
            float px = projectiles[i].x * scale + shakeX;
            float py = projectiles[i].y * scale + shakeY;
            if (projectiles[i].animState.anim && projectiles[i].animState.anim->frameCount > 0) {
                SpriteAnimDraw(&projectiles[i].animState, px, py, totalSprScale);
            } else {
                // Fallback circle
                float glow = sinf(gameTime * 20) * 3;
                DrawCircle(px, py, (12 + glow) * totalSprScale, (Color){80, 140, 255, 200});
                DrawCircle(px, py, (8 + glow) * totalSprScale, (Color){200, 230, 255, 255});
            }
        }

        // Draw fighters
        for (int i = 0; i < 2; i++) {
            float fx = fighters[i].x * scale + shakeX;
            // Offset sprite up so bottom (y=25 in sprite space) aligns with ground
            float fy = fighters[i].y * scale - 25 * totalSprScale + shakeY;

            // Shadow on ground
            float shadowY = GROUND_Y * scale + shakeY;
            DrawEllipse(fx, shadowY, 20 * totalSprScale, 4 * totalSprScale, (Color){0, 0, 0, 50});

            // Fighter sprite
            if (fighters[i].usePuppet)
                PuppetDraw(&fighters[i].puppet, fx, fy, totalSprScale);
            else
                SpriteAnimDraw(&fighters[i].animState, fx, fy, totalSprScale);

            // Debug hitboxes
            if (showHitboxes) {
                if (fighters[i].usePuppet)
                    PuppetDrawBoxes(&fighters[i].puppet, fx, fy, totalSprScale);
                else
                    SpriteAnimDrawBoxes(&fighters[i].animState, fx, fy, totalSprScale);
            }
        }

        // Draw sparks
        for (int i = 0; i < MAX_SPARKS; i++) {
            if (!sparks[i].active) continue;
            float t = sparks[i].life / 0.3f;
            float sz = sparks[i].size * t * scale;
            DrawCircle(sparks[i].x * scale + shakeX, sparks[i].y * scale + shakeY, sz, (Color){255, 255, 200, (unsigned char)(t * 255)});
            DrawCircle(sparks[i].x * scale + shakeX, sparks[i].y * scale + shakeY, sz * 0.5f, (Color){255, 255, 255, (unsigned char)(t * 200)});
            // Spark lines
            for (int s = 0; s < 6; s++) {
                float a = (float)s / 6 * 2.0f * PI;
                float r = sz * 1.5f * (1.0f - t);
                DrawLine(sparks[i].x * scale + shakeX, sparks[i].y * scale + shakeY,
                         sparks[i].x * scale + cosf(a) * r + shakeX,
                         sparks[i].y * scale + sinf(a) * r + shakeY,
                         (Color){255, 255, 100, (unsigned char)(t * 200)});
            }
        }

        // --- HUD ---
        float ui = (float)sh / 720.0f;

        // Timer box in center
        int timerSize = (int)(40 * ui);
        int timerBoxW = (int)(70 * ui);
        int timerBoxH = (int)(50 * ui);
        int timerX = sw / 2 - timerBoxW / 2;
        int timerY = (int)(10 * ui);
        DrawRectangle(timerX, timerY, timerBoxW, timerBoxH, (Color){15, 15, 25, 240});
        DrawRectangleLinesEx((Rectangle){timerX, timerY, timerBoxW, timerBoxH}, 2, (Color){120, 100, 60, 255});
        const char *timerTxt = TextFormat("%02d", roundTimer);
        int timerTxtW = MeasureText(timerTxt, timerSize);
        DrawText(timerTxt, sw/2 - timerTxtW/2, timerY + (timerBoxH - timerSize)/2, timerSize, GOLD);

        // HP bars on each side of timer
        int barGap = (int)(8 * ui);
        int barH = (int)(24 * ui);
        int barY = timerY + (timerBoxH - barH) / 2;
        int barW = timerX - barGap - (int)(20 * ui);

        // P1 bar (right-aligned, drains from left)
        int p1BarX = timerX - barGap - barW;
        DrawHPBar(p1BarX, barY, barW, barH, fighters[0].hp, fighters[0].maxHp, true);

        // P2 bar (left-aligned, drains from right)
        int p2BarX = timerX + timerBoxW + barGap;
        DrawHPBar(p2BarX, barY, barW, barH, fighters[1].hp, fighters[1].maxHp, false);

        // Names under bars
        int nameY = barY + barH + (int)(6 * ui);
        int nameSize = (int)(18 * ui);
        DrawText("RYU", p1BarX, nameY, nameSize, (Color){220,220,230,255});
        int kenW = MeasureText("KEN", nameSize);
        DrawText("KEN", p2BarX + barW - kenW, nameY, nameSize, (Color){220,220,230,255});

        // Win marks (gold dots near names)
        int markY = nameY + (int)(4 * ui);
        for (int i = 0; i < fighters[0].wins; i++)
            DrawCircle(p1BarX + barW - (int)(10*ui) - i * (int)(16*ui), markY, (int)(5*ui), GOLD);
        for (int i = 0; i < fighters[1].wins; i++)
            DrawCircle(p2BarX + (int)(10*ui) + i * (int)(16*ui), markY, (int)(5*ui), GOLD);

        // Combo counter
        for (int i = 0; i < 2; i++) {
            if (fighters[i].comboCount > 1 && fighters[i].comboTimer > 0) {
                int cx = (i == 0) ? (int)(100*ui) : sw - (int)(200*ui);
                DrawText(TextFormat("%d HIT COMBO!", fighters[i].comboCount), cx, sh/2, (int)(24*ui), GOLD);
            }
        }

        // Round message
        if (roundStartTimer > 0 || (roundEndTimer > 0 && !roundActive)) {
            int msgSize = (int)(48 * ui);
            int msgW = MeasureText(roundMessage, msgSize);
            DrawText(roundMessage, sw/2 - msgW/2, sh/2 - (int)(60*ui), msgSize, WHITE);
            if (roundStartTimer <= 0.5f && roundStartTimer > 0) {
                int fightW = MeasureText("FIGHT!", (int)(64*ui));
                DrawText("FIGHT!", sw/2 - fightW/2, sh/2 - (int)(40*ui), (int)(64*ui), GOLD);
            }
        }

        // Controls
        DrawText("P1: WASD move, J punch, K kick, L special", (int)(10*ui), sh - (int)(40*ui), (int)(12*ui), (Color){80,80,100,255});
        DrawText("[H] Toggle hitboxes", (int)(10*ui), sh - (int)(22*ui), (int)(12*ui), (Color){80,80,100,255});

        DrawFPS(sw - 80, sh - 20);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
