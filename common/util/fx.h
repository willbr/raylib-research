// fx.h — 2D + 3D particle pools and screen shake.
// The 3D particle API (SpawnParticleBurst / UpdateParticles3D / DrawParticles3D)
// and ScreenShake originated in common/objects3d.h; this header is now their
// canonical home. objects3d.h re-includes this file so legacy callers are
// unaffected.
#ifndef UTIL_FX_H
#define UTIL_FX_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>
#include <math.h>

// ---- 3D particles ---------------------------------------------------------

typedef struct {
    Vector3 pos, vel;
    float life, maxLife;
    float size;
    Color color;
    bool active;
} Particle3D;

// Spawn a burst of particles at pos. Colors cycle through fire orange, fire
// yellow, gray smoke (same behaviour as the original objects3d.h version).
static inline void SpawnParticleBurst(Particle3D *particles, int maxParticles,
                                      Vector3 pos, int count,
                                      float speedMin, float speedMax,
                                      float lifeMin,  float lifeMax,
                                      float sizeMin,  float sizeMax) {
    for (int i = 0; i < maxParticles && count > 0; i++) {
        if (particles[i].active) continue;
        float a1 = (float)GetRandomValue(0, 628) / 100.0f;
        float a2 = (float)GetRandomValue(-314, 314) / 200.0f;
        float spd = speedMin + (float)GetRandomValue(0, 100) / 100.0f * (speedMax - speedMin);
        particles[i].pos = pos;
        particles[i].vel = (Vector3){
            cosf(a1) * cosf(a2) * spd,
            fabsf(sinf(a2)) * spd + 2.0f,
            sinf(a1) * cosf(a2) * spd,
        };
        int roll = GetRandomValue(0, 2);
        if (roll == 0)      particles[i].color = (Color){255, 200,  50, 255};
        else if (roll == 1) particles[i].color = (Color){255, 100,   0, 255};
        else                particles[i].color = (Color){ 80,  80,  80, 200};
        particles[i].life    = lifeMin + (float)GetRandomValue(0, 100) / 100.0f * (lifeMax - lifeMin);
        particles[i].maxLife = particles[i].life;
        particles[i].size    = sizeMin + (float)GetRandomValue(0, 100) / 100.0f * (sizeMax - sizeMin);
        particles[i].active  = true;
        count--;
    }
}

static inline void UpdateParticles3D(Particle3D *particles, int maxParticles,
                                     float dt, float gravity) {
    for (int i = 0; i < maxParticles; i++) {
        if (!particles[i].active) continue;
        particles[i].pos = Vector3Add(particles[i].pos, Vector3Scale(particles[i].vel, dt));
        particles[i].vel.y -= gravity * dt;
        particles[i].life  -= dt;
        if (particles[i].life <= 0.0f) particles[i].active = false;
    }
}

static inline void DrawParticles3D(Particle3D *particles, int maxParticles) {
    for (int i = 0; i < maxParticles; i++) {
        if (!particles[i].active) continue;
        float a = particles[i].life / particles[i].maxLife;
        Color c = particles[i].color;
        c.a = (unsigned char)(a * c.a);
        DrawSphere(particles[i].pos, particles[i].size * a, c);
    }
}

// ---- 2D particles ---------------------------------------------------------

typedef struct {
    Vector2 pos, vel;
    float life, maxLife;
    float size;
    Color color;
    bool active;
} Particle2D;

// Spawn a burst of 2D particles with user-supplied color.
static inline void SpawnParticleBurst2D(Particle2D *particles, int maxParticles,
                                        Vector2 pos, Color color, int count,
                                        float speedMin, float speedMax,
                                        float lifeMin,  float lifeMax,
                                        float sizeMin,  float sizeMax) {
    for (int i = 0; i < maxParticles && count > 0; i++) {
        if (particles[i].active) continue;
        float a   = (float)GetRandomValue(0, 628) / 100.0f;
        float spd = speedMin + (float)GetRandomValue(0, 100) / 100.0f * (speedMax - speedMin);
        particles[i].pos = pos;
        particles[i].vel = (Vector2){ cosf(a) * spd, sinf(a) * spd };
        particles[i].color   = color;
        particles[i].life    = lifeMin + (float)GetRandomValue(0, 100) / 100.0f * (lifeMax - lifeMin);
        particles[i].maxLife = particles[i].life;
        particles[i].size    = sizeMin + (float)GetRandomValue(0, 100) / 100.0f * (sizeMax - sizeMin);
        particles[i].active  = true;
        count--;
    }
}

// Gravity here is a 2D vector (pixels/sec²). (0, 200) is typical downward.
// Integration order: velocity updated before position (semi-implicit Euler).
// Note: UpdateParticles3D uses explicit Euler (position first); the asymmetry
// is historical — the 3D version is preserved verbatim from objects3d.h.
static inline void UpdateParticles2D(Particle2D *particles, int maxParticles,
                                     float dt, Vector2 gravity) {
    for (int i = 0; i < maxParticles; i++) {
        if (!particles[i].active) continue;
        particles[i].vel.x += gravity.x * dt;
        particles[i].vel.y += gravity.y * dt;
        particles[i].pos.x += particles[i].vel.x * dt;
        particles[i].pos.y += particles[i].vel.y * dt;
        particles[i].life  -= dt;
        if (particles[i].life <= 0.0f) particles[i].active = false;
    }
}

// Draws each active particle as an alpha-faded filled circle. Radius holds
// constant (unlike DrawParticles3D which shrinks the sphere with alpha).
static inline void DrawParticles2D(Particle2D *particles, int maxParticles) {
    for (int i = 0; i < maxParticles; i++) {
        if (!particles[i].active) continue;
        float a = particles[i].life / particles[i].maxLife;
        Color c = particles[i].color;
        c.a = (unsigned char)(a * c.a);
        DrawCircleV(particles[i].pos, particles[i].size, c);
    }
}

// ---- Screen shake ---------------------------------------------------------

typedef struct {
    float amount;
    float timer;
    float decay;   // reserved for future exponential falloff; currently unused (ShakeUpdate decays linearly via timer).
} ScreenShake;

static inline void ShakeTrigger(ScreenShake *s, float amount) {
    s->amount = amount;
    s->timer  = amount;
}

static inline void ShakeUpdate(ScreenShake *s, float dt) {
    if (s->timer > 0.0f) {
        s->timer -= dt;
        if (s->timer < 0.0f) s->timer = 0.0f;
    }
}

static inline Vector2 ShakeOffset(ScreenShake *s) {
    if (s->timer <= 0.0f) return (Vector2){0, 0};
    float intensity = s->timer / s->amount;
    return (Vector2){
        (float)GetRandomValue(-100, 100) / 100.0f * s->amount * intensity,
        (float)GetRandomValue(-100, 100) / 100.0f * s->amount * intensity,
    };
}

#endif // UTIL_FX_H
