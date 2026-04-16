// pool.h — entity-pool helpers built on a `.active` bool convention.
// Caller owns the storage; macros iterate / spawn / count.
#ifndef UTIL_POOL_H
#define UTIL_POOL_H

#include <stdbool.h>

// Find first inactive slot in (arr, N), mark active=true, return its index.
// Returns -1 if pool is full. GNU statement expression (supported by Zig cc / Clang / GCC).
//
// Usage:  int i = POOL_SPAWN(bullets, MAX_BULLETS);
//         if (i >= 0) { bullets[i].pos = ...; }
#define POOL_SPAWN(arr, N) __extension__ ({ \
    int _found_ = -1; \
    for (int _i_ = 0; _i_ < (int)(N); _i_++) { \
        if (!(arr)[_i_].active) { \
            (arr)[_i_].active = true; \
            _found_ = _i_; \
            break; \
        } \
    } \
    _found_; \
})

// Iterate a typed pointer over every slot (active or not). The caller filters
// on `p->active` as needed. Single `for`, so `break` and `continue` behave
// exactly as written.
//
// Usage:  POOL_FOREACH(bullets, MAX_BULLETS, b) { if (!b->active) continue; ... }
#define POOL_FOREACH(arr, N, p) \
    for (__typeof__(&(arr)[0]) p = &(arr)[0]; p < &(arr)[(N)]; p++)

// Ring-buffer push: marks (arr)[idx_var] active, advances idx_var modulo N.
// Use for decals / tracers / oldest-overwrite particle sources.
// Precondition: N >= 1.
//
// Usage:  int decalIdx = 0; ... POOL_RING_PUSH(decals, MAX_DECALS, decalIdx);
#define POOL_RING_PUSH(arr, N, idx_var) do { \
    (arr)[(idx_var)].active = true; \
    (idx_var) = ((idx_var) + 1) % (int)(N); \
} while (0)

// Count active slots.
#define POOL_COUNT_ACTIVE(arr, N) __extension__ ({ \
    int _c_ = 0; \
    for (int _i_ = 0; _i_ < (int)(N); _i_++) if ((arr)[_i_].active) _c_++; \
    _c_; \
})

#endif // UTIL_POOL_H
