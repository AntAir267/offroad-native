/* The terrain's hottest drawing, by hand: the walk over the cells (0x54f230, 0x53f680, 0x53f070, 0x53eb50), the land pass
   (0x540260, 0x53fc20, 0x53fe80, the writers 0x540a60, 0x541150, 0x5420c0, 0x5430e0, 0x543860, 0x544070) and the fog pass
   (0x544da0, 0x5450a0). Option native_terrain, on by default; ORR_NATIVE_TERRAIN=0 keeps the translated code. Never in
   test runs or the lockstep check (game_init: widescreen_allowed()).

   Same bytes as the translated code: guest memory is read and written directly, and arithmetic the original does on the x87
   stays x87, in long double, in the original's order, with the program's own control word (the host's: fpu.c). So nothing
   differs by a last unit; the check below shows it. float only where the original stores to a float.

   Still guest_call, as they are not hot (sampled at Extreme, Dead Man's Drop: all of them together 2 % of the CPU):
   the lights' slots +0x30, +0x14 (0x50bcb0 is 0.2 %), 0x52aeb0, the camera's matrix 0x4f0cb0, the polygon clipper
   0x4f5240 (0.9 %), the render state setters, an object's slot +0x28 of a class other than the three in mark_object, and the growth of
   any array (0x54cb10, or the whole original vertex or cell when one would overflow).
   The unusual paths go to the original whole: DAT_5d3e01 == 0 (no per-corner alpha), renderer +0x5a1 set (batching).

   ORR_TERRAIN_AB=<N>: at every Nth view, each of walk, land and fog runs twice on the same input: the original, then with all
   program memory put back, ours; then all program memory (image data, every region, without dead stacks) and the Direct3D calls
   each made, with their vertices, are compared, and the first differences logged ("terrain ab:"). */
#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "options.h"
#include "funcs.h"
#include "native_land.h"

typedef uint32_t u32;
typedef long double f80x;           /* the x87's own */

#define M32(a) (*(u32 *)(uintptr_t)(a))
#define MF(a) (*(float *)(uintptr_t)(a))
#define M16(a) (*(uint16_t *)(uintptr_t)(a))
#define MS16(a) (*(int16_t *)(uintptr_t)(a))
#define M8(a) (*(uint8_t *)(uintptr_t)(a))

/* a vertex [verified: 0x53eb50, 0x53fc20, 0x53fe80] */
typedef struct {
    float x, y, z, pad0;
    float sx, sy, sz, rhw;
    u32 outcode;
    float nx, ny, nz;
    float extra;
    float r, g, b;
    u32 unset;
    float fog;
    u32 shade;
} Vtx;
_Static_assert(sizeof(Vtx) == 0x4c, "vertex");

/* a grid record [verified: 0x53eb50, 0x53f680] */
typedef struct {
    u32 stamp;
    int16_t land, water;
    float depth;
    uint8_t tex, rot, pad[2];
    uint16_t flags;
    uint16_t bits;
} Rec;
_Static_assert(sizeof(Rec) == 0x14, "record");

/* a cell [verified: 0x402b10, 0x402bc0] */
typedef struct {
    u32 list;
    float h;
    uint16_t normal, flags;
    uint8_t tex, rot, shade, water;
} Cell;

/* a polygon as it goes to the clipper: stride 0x28 */
typedef struct {
    u32 pos[4];
    u32 diffuse, specular;
    u32 u, v;
    u32 pad[2];
} Out;

#define VP(a) ((Vtx *)(uintptr_t)(a))
#define RP(a) ((Rec *)(uintptr_t)(a))
#define CP(a) ((Cell *)(uintptr_t)(a))

static inline u32 fbits(float f) { u32 u; memcpy(&u, &f, 4); return u; }
static inline float bitsf(u32 u) { float f; memcpy(&f, &u, 4); return f; }

/* constants the code reads from the image [verified: disassembly of each] */
#define K_ONE 0x57a458u
#define K_ZERO 0x57a40cu
#define K_P01 0x57a454u     /* 0.01 */
#define K_SINK 0x57a418u    /* 12582912 */
#define K_4096 0x57a758u
#define K_SIX 0x57b3bcu
#define K_NINTH 0x581490u
#define K_WATER 0x57afa8u   /* a water byte to its phase */
#define K_300 0x57b3b8u
#define K_P04 0x57ae20u
#define K_1024 0x57ca58u
#define K_P02 0x57ae28u
#define K_FOUR 0x57af14u
#define K_255 0x58057cu

/* what each run went through, for the check's report */
enum { C_V_LAND, C_V_WATER, C_V_SEA, C_V_NOFOG, C_V_FOG, C_OPAQUE, C_OPAQUE_DARK, C_BLEND, C_BLEND_DARK, C_WATER, C_WATER_DARK, C_WATER2, C_FOGQ, C_LIGHT, C_MARK_OTHER, C_ORIG_V, C_ORIG_CELL, C_N };
static unsigned long cov[C_N];
static const char *const cov_name[C_N] = {"land vertices", "water vertices", "sea vertices", "without height mist", "with height mist", "opaque quads",
    "dark opaque", "blends", "dark blends", "water", "dark water", "water 2", "fog quads", "extra lights", "other marks", "original vertex", "original cell"};

/* ------------------------------------------------------------------ the walk */

/* An object's slot +0x28, the mark that lets it draw this view. The three the classes use [verified: disassembly]: 0x52b580
   the stamp; 0x52ddd0 the same, its attachment at +0x190 first; 0x4f8ae0 (cars) 0x52ddd0, then the list of children at +0x4ec.
   Any other class's goes through the program. */
static void mark_object(CPU *c, u32 o, u32 view)
{
    u32 fn = M32(M32(o) + 0x28);
    if (fn == 0x52b580u) {
        M32(o + 8) = M32(view + 0x4c);
    } else if (fn == 0x52ddd0u) {
        u32 at = M32(o + 0x190);
        if (at)
            mark_object(c, at, view);
        M32(o + 8) = M32(view + 0x4c);
    } else if (fn == 0x4f8ae0u) {
        u32 at = M32(o + 0x190);
        if (at)
            mark_object(c, at, view);
        M32(o + 8) = M32(view + 0x4c);
        for (u32 n = M32(o + 0x4ec); n; n = M32(n))
            mark_object(c, M32(n + 4), view);
    } else {
        cov[C_MARK_OTHER]++;
        c->ecx = o;
        guest_call(c, fn, 1, view);
    }
}

/* the wave's tables: DAT_5d3d1c, 4096 floats */
#define SINES M32(0x5d3d1cu)

static inline u32 sine_index(f80x v)
{
    float f = (float)(v * MF(K_4096) + MF(K_SINK));
    return fbits(f) & 0xfff;
}

/* 0x550ee0: y of a water cell's vertex, returns the sum of the three waves */
static inline f80x wave_water(u32 w, float h, Vtx *v)
{
    u32 s = SINES;
    float t = MF(w + 0x60);
    f80x s0 = MF(s + sine_index(((f80x)h * MF(0x57acacu) - t) * MF(0x57bd3cu)) * 4);
    v->y = (float)(s0 * MF(0x57aff4u));
    f80x s1 = MF(s + sine_index(((f80x)v->x * MF(0x5814e8u) + t) * MF(0x5814e4u)) * 4);
    float low = (float)(s1 * MF(0x57b534u));
    f80x sum = s0 + s1;
    f80x s2 = MF(s + sine_index((((f80x)v->z * MF(0x5814e0u) + (f80x)v->x * MF(0x5814dcu)) + t) * MF(0x57bd3cu)) * 4);
    low = (float)(s2 * MF(0x5814d8u) + low);
    sum = sum + s2;
    if (low > v->y)
        v->y = low;
    return sum;
}

/* 0x550fe0: y of a vertex off the map */
static inline f80x wave_sea(u32 w, Vtx *v)
{
    u32 s = SINES;
    float t = MF(w + 0x60);
    f80x s1 = MF(s + sine_index(((f80x)v->x * MF(0x5814e8u) + t) * MF(0x5814e4u)) * 4);
    v->y = (float)(s1 * MF(0x57b534u));
    f80x s2 = MF(s + sine_index((((f80x)v->z * MF(0x5814e0u) + (f80x)v->x * MF(0x5814dcu)) + t) * MF(0x57bd3cu)) * 4);
    v->y = (float)(s2 * MF(0x5814d8u) + v->y);
    return s1 + s2;
}

static inline void wave_colour(u32 t, Vtx *v, f80x sum)
{
    f80x k = (sum + MF(K_SIX)) * MF(K_NINTH);
    v->r = (float)(k * MF(t + 0x24));
    v->g = (float)(k * MF(t + 0x28));
    v->b = (float)(k * MF(t + 0x2c));
    /* +0x40: the original copies a stale stack slot here. Nothing reads it (see DESIGN.md) */
    v->unset = 0;
}

static inline void clamp_fog(Vtx *v, float f)
{
    v->fog = f;
    if (fbits(f) & 0x80000000u)
        v->fog = 0.0f;
    if (v->fog > MF(K_ONE))
        v->fog = 1.0f;
}

/* 0x53eb50: a corner's vertices. cell 0 is off the map */
static void make_vertex(CPU *c, u32 t, u32 view, u32 cell, u32 rec, float x, float z)
{
    u32 vf = M32(view + 0x38);
    Rec *r = RP(rec);
    int water_view = (vf & 2) != 0;
    if (!cell) {
        if (!water_view)
            return;
        int i = MS16(t + 0x52);
        if (i >= (int)M32(t + 0x4c))
            goto original;
        u32 l = M32(0x5e1450u);
        cov[C_V_SEA]++;
        Vtx *v = VP(M32(t + 0x48) + (u32)i * 0x4c);
        v->z = z;
        v->nx = 0.0f;
        v->ny = 1.0f;
        v->nz = 0.0f;
        v->extra = 0.0f;
        v->x = x;
        r->water = MS16(t + 0x52);
        r->stamp = M32(t + 0x40098);
        MS16(t + 0x52) = (int16_t)(MS16(t + 0x52) + 1);
        r->flags = 0x4a;
        M32(rec + 8) = M32(t + 0x20);
        v->fog = 0.0f;
        v->shade = M32(l + 0x41288);
        wave_colour(t, v, wave_sea(l + 0x412a4, v));
        return;
    }
    Cell *ce = CP(cell);
    uint16_t cf = ce->flags;
    r->flags = cf;
    int want_land = !water_view || (cf & 4);
    int want_water = (cf & 8) && water_view;
    if ((want_land && (int)MS16(t + 0x50) >= (int)M32(t + 0x40)) || (want_water && (int)MS16(t + 0x52) >= (int)M32(t + 0x4c)))
        goto original;
    u32 l = M32(0x5e1450u);
    if (want_land) {
        int i = MS16(t + 0x50);
        cov[C_V_LAND]++;
        Vtx *v = VP(M32(t + 0x3c) + (u32)i * 0x4c);
        u32 n = M32(0x5d3d2cu) + (u32)ce->normal * 0x10;
        v->x = x;
        v->y = ce->h;
        v->z = z;
        v->nx = MF(n);
        v->ny = MF(n + 4);
        v->nz = MF(n + 8);
        v->b = 0.0f;
        v->g = 0.0f;
        v->r = 0.0f;
        v->shade = (u32)ce->shade * 0x10 + M32(l + 0x41288);
        u32 fo = M32(view + 0x20);
        if (!fo || !(M8(fo + 0x24) & 1)) {
            v->fog = 0.0f;
            cov[C_V_NOFOG]++;
        } else {
            clamp_fog(v, (float)-(((f80x)ce->h - MF(fo + 0x40)) * MF(fo + 0x44)));
            cov[C_V_FOG]++;
        }
        r->land = MS16(t + 0x50);
        r->tex = ce->tex;
        r->rot = ce->rot;
        r->stamp = M32(t + 0x40098);
        MS16(t + 0x50) = (int16_t)(MS16(t + 0x50) + 1);
    }
    if (want_water) {
        int i = MS16(t + 0x52);
        cov[C_V_WATER]++;
        Vtx *v = VP(M32(t + 0x48) + (u32)i * 0x4c);
        v->x = x;
        v->z = z;
        r->water = MS16(t + 0x52);
        r->stamp = M32(t + 0x40098);
        MS16(t + 0x52) = (int16_t)(MS16(t + 0x52) + 1);
        v->shade = (u32)ce->shade * 0x10 + M32(l + 0x41288);
        if (cf & 0x100) {
            v->fog = 0.0f;
            v->y = 0.0f;
            return;
        }
        f80x d = -((f80x)MF(t + 0x10) * ce->h);
        r->depth = (float)d;
        if (fbits((float)(d - MF(K_P01))) & 0x80000000u)
            M32(rec + 8) = 0x3c23d70au;
        v->nx = 0.0f;
        v->nz = 0.0f;
        v->ny = 1.0f;
        v->extra = (float)((f80x)ce->water * MF(K_WATER));
        wave_colour(t, v, wave_water(l + 0x412a4, ce->h, v));
        u32 fo = M32(view + 0x20);
        if (!fo || !(M8(fo + 0x24) & 1))
            v->fog = 0.0f;
        else
            clamp_fog(v, (float)-((-(f80x)ce->h - MF(fo + 0x40)) * MF(fo + 0x44)));
    }
    return;
original:
    /* an array is full: the original grows it */
    cov[C_ORIG_V]++;
    c->ecx = t;
    guest_call(c, 0x53eb50u, 5, view, cell, rec, fbits(x), fbits(z));
}

/* 0x53f2a0: the textures of a polygon, the lowest first */
typedef struct {
    int16_t n;
    uint16_t first, rest[4];
} TexList;

static inline void tex_add(TexList *l, uint16_t tex)
{
    if (l->n < 0) {
        l->first = tex;
        l->n = 0;
        return;
    }
    for (int i = 0; i < l->n; i++)
        if (l->rest[i] == tex)
            return;
    if (l->first <= tex) {
        l->rest[l->n++] = tex;
        return;
    }
    l->rest[l->n++] = l->first;
    l->first = tex;
}

/* an append to one of the object's lists of polygons: count at cnt, the array object at arr ({ptr at +4, capacity at +8}) */
static inline void list_add(CPU *c, u32 cnt, u32 arr, u32 poly)
{
    int i = (int)M32(cnt);
    M32(cnt) = (u32)(i + 1);
    if (i < (int)M32(arr + 8)) {
        M32(M32(arr + 4) + (u32)i * 4) = poly;
        return;
    }
    c->ecx = arr;
    M32(guest_call(c, 0x54cb10u, 1, (u32)i)) = poly;
}

/* 0x53f680 with 0x53f070 inside */
static void walk_cell(CPU *c, u32 t, u32 view, float x, float z, int xi, int zi)
{
    u32 l = M32(0x5e1450u), w = M32(t + 0x34);
    int rx = xi - (int)M32(l + 0x1180), rz = zi - (int)M32(l + 0x1188);
    /* any of the row table or the grid about to grow: the original does the cell */
    if (rz + 1 >= (int)M32(t + 0x40094))
        goto original;
    u32 rows = M32(t + 0x40090);
    int row0 = (int)M32(rows + (u32)rz * 4), row1 = (int)M32(rows + (u32)rz * 4 + 4);
    int cap = (int)M32(t + 0x40088);
    if (row0 + rx + 1 >= cap || row1 + rx + 1 >= cap || row0 + rx >= cap || row1 + rx >= cap)
        goto original;
    {
        u32 grid = M32(t + 0x40084);
        Rec *r0 = RP(grid + (u32)(row0 + rx) * 0x14);
        float cs = MF(w + 0x6c);
        r0->depth = 0.0f;
        u32 cell = 0;
        if (xi >= 0 && xi < (int)M32(w + 0x58) - 1 && zi >= 0 && zi < (int)M32(w + 0x5c) - 1)
            cell = M32(M32(w + 0x48) + (u32)zi * 4) + (u32)xi * 0x10;
        int land = 0, water = 0, onmap = 0;
        if (cell) {
            for (u32 n = CP(cell)->list; n; n = M32(n)) {
                mark_object(c, M32(n + 4), view);
            }
            if (M8(cell + 10) & 0x80)
                return;
            onmap = 1;
        }
        make_vertex(c, t, view, cell, (u32)(uintptr_t)r0, x, z);
        if (!cell)
            water = 1;
        else {
            u32 no_w = (~M32(view + 0x38) >> 1) & 1;
            uint16_t f = CP(cell)->flags;
            if (((f & 4) || no_w) && ((f & 1) || no_w))
                land = 1;
            if ((f & 8) && (M8(view + 0x38) & 2) && (f & 2))
                water = 1;
        }
        r0->bits = 0;
        TexList tl;
        tl.n = -1;
        tl.first = 0;
        u32 sel = (r0->flags & 0x600u) >> 9;
        int blended = !(r0->flags & 0x40) && M8(0x5d3e79u);
        if (blended || sel == 0)
            tex_add(&tl, r0->tex);
        u32 poly = M32(t + 0x40054);
        M32(poly) = (u32)(uintptr_t)r0;

        Rec *r;
        r = RP(grid + (u32)(row1 + rx) * 0x14);
        if (r->stamp != M32(t + 0x40098))
            make_vertex(c, t, view, onmap ? M32(M32(w + 0x48) + 4 + (u32)zi * 4) + (u32)xi * 0x10 : 0, (u32)(uintptr_t)r, x, (float)((f80x)cs + z));
        if (blended) {
            if (r->tex != tl.first)
                tex_add(&tl, r->tex);
        } else if (sel == 1)
            tex_add(&tl, r->tex);
        M32(poly + 4) = (u32)(uintptr_t)r;

        r = RP(grid + (u32)(row0 + 1 + rx) * 0x14);
        if (r->stamp != M32(t + 0x40098))
            make_vertex(c, t, view, onmap ? M32(M32(w + 0x48) + (u32)zi * 4) + (u32)(xi + 1) * 0x10 : 0, (u32)(uintptr_t)r, (float)((f80x)cs + x), z);
        if (blended) {
            if (r->tex != tl.first)
                tex_add(&tl, r->tex);
        } else if (sel == 2)
            tex_add(&tl, r->tex);
        M32(poly + 8) = (u32)(uintptr_t)r;

        r = RP(grid + (u32)(row1 + 1 + rx) * 0x14);
        if (r->stamp != M32(t + 0x40098))
            make_vertex(c, t, view, onmap ? M32(M32(w + 0x48) + 4 + (u32)zi * 4) + (u32)(xi + 1) * 0x10 : 0, (u32)(uintptr_t)r, (float)((f80x)cs + x), (float)((f80x)cs + z));
        if (blended) {
            if (r->tex != tl.first)
                tex_add(&tl, r->tex);
        } else if (sel == 3)
            tex_add(&tl, r->tex);
        M32(poly + 12) = (u32)(uintptr_t)r;

        if (water)
            list_add(c, t + 0x40074, t + 0x4005c, poly);
        if (land) {
            u32 bins = M32(t + 0x40058);
            list_add(c, bins + (u32)tl.first * 0x20 + 0x18, bins + (u32)tl.first * 0x20, poly);
            for (int i = 0; i < tl.n; i++) {
                bins = M32(t + 0x40058);
                list_add(c, bins + (u32)tl.rest[i] * 0x20 + 0x1c, bins + (u32)tl.rest[i] * 0x20 + 0xc, poly);
            }
        }
        M32(t + 0x40054) = poly + 0x10;
        return;
    }
original:
    cov[C_ORIG_CELL]++;
    c->ecx = t;
    guest_call(c, 0x53f680u, 5, view, fbits(x), fbits(z), (u32)xi, (u32)zi);
}

/* 0x54f230: rows high to low, x high to low (ECX the level, first argument the view) */
static void native_walk(CPU *c)
{
    u32 l = c->ecx, view = R32(c->esp + 4);
    int rows = (int)M32(l + 0x117c);
    u32 t = l + 0x1194;
    for (int row = rows - 1; row >= 0; row--) {
        u32 pair = l + (u32)row * 8 + 0x17c;
        int z0 = (int)M32(l + 0x1188), first = (int)M32(pair), xi = (int)M32(pair + 4);
        float cell = MF(l + 0x41310);
        float x = (float)((f80x)xi * cell);
        if (first > xi)
            continue;
        float z = (float)(((f80x)z0 + (f80x)row) * cell);
        for (; xi >= first; xi--) {
            walk_cell(c, t, view, x, z, xi, (int)M32(l + 0x1188) + row);
            x = (float)((f80x)x - MF(l + 0x41310));
            first = (int)M32(pair);
        }
    }
    api_return(c, c->eax, 1);
}

/* ------------------------------------------------------------------ transform */

/* the clip codes of 0x53fc20 and 0x417300 [verified: disassembly; the comparisons as the x87's condition bits read,
   a NaN taking the branch C0 and C3 take] */
static inline u32 clip_code(u32 ren, const Vtx *v)
{
    u32 code = 0;
    float sx = v->sx, sy = v->sy, sz = v->sz;
    if (!(fbits(v->rhw) & 0x80000000u)) {
        if (!(sx >= MF(ren + 0x3ec)))
            code = 1;
        else if (sx > MF(ren + 0x3f4))
            code = 2;
        if (!(sy >= MF(ren + 0x3f0)))
            code |= 4;
        else if (sy > MF(ren + 0x3f8))
            code |= 8;
        if (!(sz > MF(ren + 0x3fc)))
            code |= 0x10;
        else if (sz >= MF(ren + 0x400))
            code |= 0x20;
    } else {
        if (!(sx >= MF(ren + 0x3ec)))
            code = 2;
        else if (sx > MF(ren + 0x3f4))
            code = 1;
        if (!(sy >= MF(ren + 0x3f0)))
            code |= 8;
        else if (sy > MF(ren + 0x3f8))
            code |= 4;
        code |= 0x10;
    }
    return code;
}

/* 0x53fc20 */
static void transform(CPU *c, u32 t, u32 view)
{
    /* the camera's matrix into a frame of our own on the program's stack, as the original's local */
    c->esp -= 64;
    u32 buf = c->esp;
    c->ecx = M32(view + 4);
    u32 mp = guest_call(c, 0x4f0cb0u, 1, buf);
    float m[16];
    memcpy(m, (const void *)(uintptr_t)mp, sizeof m);
    c->esp += 64;
    u32 ren = M32(view + 8);
    int n = MS16(t + 0x50);
    Vtx *v = VP(M32(t + 0x3c));
    for (int i = 0; i < n; i++, v++) {
        f80x rhw = 1.0f / ((f80x)m[3] * v->x + (f80x)m[7] * v->y + (f80x)m[11] * v->z + m[15]);
        v->sx = (float)(((f80x)m[0] * v->x + (f80x)m[4] * v->y + (f80x)m[8] * v->z + m[12]) * rhw);
        v->sy = (float)(((f80x)m[1] * v->x + (f80x)m[5] * v->y + (f80x)m[9] * v->z + m[13]) * rhw);
        v->sz = (float)(((f80x)m[2] * v->x + (f80x)m[6] * v->y + (f80x)m[10] * v->z + m[14]) * rhw);
        v->rhw = (float)rhw;
        v->outcode = clip_code(ren, v);
    }
    n = MS16(t + 0x52);
    v = VP(M32(t + 0x48));
    for (int i = 0; i < n; i++, v++) {
        f80x rhw = 1.0f / ((f80x)m[7] * v->y + (f80x)m[11] * v->z + (f80x)m[3] * v->x + m[15]);
        v->sx = (float)(((f80x)m[4] * v->y + (f80x)m[8] * v->z + (f80x)m[0] * v->x + m[12]) * rhw);
        v->sy = (float)(((f80x)m[5] * v->y + (f80x)m[9] * v->z + (f80x)m[1] * v->x + m[13]) * rhw);
        v->sz = (float)(((f80x)m[6] * v->y + (f80x)m[10] * v->z + (f80x)m[2] * v->x + m[14]) * rhw);
        v->rhw = (float)rhw;
        v->outcode = clip_code(ren, v);
    }
}

/* ------------------------------------------------------------------ light */

static inline void light_apply(CPU *c, u32 light, u32 verts, int n)
{
    c->ecx = light;
    guest_call(c, M32(M32(light) + 0x14), 3, verts, (u32)n, 0u);
}

/* The end of 0x53fe80 for one array */
static void finish_light(u32 view, Vtx *v, int n)
{
    u32 fo = M32(view + 0x20);
    int mist = fo && (M8(fo + 0x24) & 2);
    u32 cam = M32(view + 4);
    for (int i = 0; i < n; i++, v++) {
        const float *s = (const float *)(uintptr_t)v->shade;
        v->r = (float)((f80x)v->r * s[0]);
        v->g = (float)((f80x)s[1] * v->g);
        v->b = (float)((f80x)s[2] * v->b);
        if (mist) {
            f80x d = (((f80x)1.0f / v->rhw - MF(cam + 0x270)) * MF(cam + 0x274) - MF(fo)) * MF(fo + 0xc) + v->fog;
            float f = (float)d;
            if (fbits(f) & 0x80000000u)
                v->fog = 0.0f;
            else if ((fbits(f) & 0x7fffffffu) > 0x3f800000u)
                v->fog = 1.0f;
            else
                v->fog = f;
        }
        u32 br = fbits(v->r), bg = fbits(v->g), bb = fbits(v->b);
        if (((br & 0x7fffffffu) > 0x3f800000u && !(br >> 31)) || ((bg & 0x7fffffffu) > 0x3f800000u && !(bg >> 31)) ||
            ((bb & 0x7fffffffu) > 0x3f800000u && !(bb >> 31))) {
            int have = 0;
            float big = 0.0f;
            if ((br & 0x7fffffffu) > 0x3f800000u && !(br >> 31)) {
                big = v->r;
                have = 1;
            }
            if ((bg & 0x7fffffffu) > 0x3f800000u && !(bg >> 31)) {
                if (!have || !(big >= v->g))
                    big = v->g;
                have = 1;
            }
            if ((bb & 0x7fffffffu) > 0x3f800000u && !(bb >> 31)) {
                if (!have || !(big >= v->b))
                    big = v->b;
            }
            f80x k = 1.0f / (f80x)big;
            v->r = (float)(k * v->r);
            v->g = (float)(k * v->g);
            v->b = (float)(k * v->b);
        }
    }
}

/* 0x53fe80 */
static void light(CPU *c, u32 t, u32 view)
{
    u32 sun = M32(view + 0xc);
    c->ecx = sun;
    guest_call(c, M32(M32(sun) + 0x30), 0);
    light_apply(c, sun, M32(t + 0x3c), MS16(t + 0x50));
    u32 scene = M32(view + 0x14);
    if (scene) {
        for (u32 li = M32(scene + 0x30); li; li = M32(li + 0x2c)) {
            if (!M8(li + 0x18) || !M8(li + 0x1a))
                continue;
            if (M8(li + 0x24) & 2) {
                c->ecx = li;
                if (!(guest_call(c, 0x52aeb0u, 0) & 0xff))
                    continue;
            }
            cov[C_LIGHT]++;
            c->ecx = li;
            guest_call(c, M32(M32(li) + 0x30), 0);
            light_apply(c, li, M32(t + 0x3c), MS16(t + 0x50));
            light_apply(c, li, M32(t + 0x48), MS16(t + 0x52));
        }
    }
    finish_light(view, VP(M32(t + 0x3c)), MS16(t + 0x50));
    finish_light(view, VP(M32(t + 0x48)), MS16(t + 0x52));
}

/* ------------------------------------------------------------------ writers */

static inline int rnd255(float v) { return fpu_ist32((f80x)v * MF(K_255)); }

/* colour with the alpha already in place at 0xff or 0 (blends, 0x5420c0: their expression is this modulo 2^32) */
static inline u32 argb(u32 a, float r, float g, float b)
{
    return (a << 24) + ((u32)rnd255(r) << 16) + ((u32)rnd255(g) << 8) + (u32)rnd255(b);
}

static inline void put_pos(Out *o, const Vtx *v)
{
    memcpy(o->pos, &v->sx, 16);
    o->specular = v->outcode | 0xff000000u;
}

#define UVT(rot, k) (0x5f9b00u + (u32)(rot) * 0x20 + (k) * 8u)

/* the order a polygon's corners go to the clipper: flag 0x10 picks the diagonal */
static const uint8_t corner_order[2][4] = {{1, 3, 0, 2}, {0, 1, 2, 3}};

static inline void clip(CPU *c, u32 ren, u32 kind)
{
    c->ecx = ren;
    guest_call(c, 0x4f5240u, 1, kind);
}

/* the depth darkening of a cell in water: 1 - depth, at least 0 */
static inline f80x darken_x87(float depth)
{
    f80x d = (f80x)MF(K_ONE) - depth;
    return (fbits((float)d) & 0x80000000u) ? (f80x)MF(K_ZERO) : d;
}

/* 0x564c80 four times with the 0x4f5240 after it; 0x540a60 when the cell is in water */
static void write_opaque(CPU *c, u32 ren, const u32 *rec, Vtx *const *vs)
{
    Out *out = (Out *)(uintptr_t)M32(ren + 0x3d4);
    Rec *r0 = RP(rec[0]);
    u32 rot = r0->rot;
    const uint8_t *order = corner_order[(r0->flags & 0x10) != 0];
    int dark = (r0->flags & 0x20) != 0;
    cov[dark ? C_OPAQUE_DARK : C_OPAQUE]++;
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        const Vtx *v = vs[k];
        Out *o = out + i;
        put_pos(o, v);
        o->u = M32(UVT(rot, k));
        o->v = M32(UVT(rot, k) + 4);
        if (dark) {
            f80x d = darken_x87(RP(rec[k])->depth);
            /* (b, g, r: their products to floats, then each times 255) */
            float fb = (float)(d * v->b), fg = (float)(d * v->g), fr = (float)(d * v->r);
            o->diffuse = argb(0xff, fr, fg, fb);
        } else
            o->diffuse = argb(0xff, v->r, v->g, v->b);
    }
    clip(c, ren, 6);
}

/* 0x5420c0; 0x541150 when the cell is in water. Alpha per corner: 0xff where the corner's texture is this one */
static void write_blend(CPU *c, u32 ren, const u32 *rec, Vtx *const *vs, u32 tex)
{
    Out *out = (Out *)(uintptr_t)M32(ren + 0x3d4);
    Rec *r0 = RP(rec[0]);
    u32 rot = r0->rot;
    const uint8_t *order = corner_order[(r0->flags & 0x10) != 0];
    int dark = (r0->flags & 0x20) != 0;
    cov[dark ? C_BLEND_DARK : C_BLEND]++;
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        const Vtx *v = vs[k];
        Out *o = out + i;
        u32 a = RP(rec[k])->tex == tex ? 0xff : 0;
        put_pos(o, v);
        o->u = M32(UVT(rot, k));
        o->v = M32(UVT(rot, k) + 4);
        if (dark) {
            float d = (float)((f80x)MF(K_ONE) - RP(rec[k])->depth);
            if (fbits(d) & 0x80000000u)
                d = MF(K_ZERO);
            float fb = (float)((f80x)d * v->b), fg = (float)((f80x)d * v->g), fr = (float)((f80x)d * v->r);
            o->diffuse = argb(a, fr, fg, fb);
        } else
            o->diffuse = argb(a, v->r, v->g, v->b);
    }
    clip(c, ren, 6);
}

/* water's four corners: their texture coordinates, 0x5430e0 and 0x544070 alike */
typedef struct { float u[4], v[4]; } WaterUV;

static inline void water_phase(float extra, u32 *s, u32 *co)
{
    u32 b = fbits((float)((f80x)extra * MF(K_1024) + MF(K_SINK))) & 0x7fffffu;
    b -= 0x400000u;
    *s = (b + 0x400u) & 0xfffu;
    *co = b & 0xfffu;
}

static void water_uv(u32 t, Vtx *const *vs, const u32 *rec, WaterUV *w)
{
    u32 tab = SINES, si, co;
    float tick = MF(t + 8);
    /* the first stays on the x87, the others go through a float [verified: 0x543223..0x543292] */
    f80x a0 = (((f80x)vs[0]->y + MF(K_300)) / RP(rec[0])->depth) * MF(K_P04);
    float a[4];
    a[1] = (float)((((f80x)vs[1]->y + MF(K_300)) / RP(rec[1])->depth) * MF(K_P04));
    a[2] = (float)((((f80x)vs[2]->y + MF(K_300)) / RP(rec[2])->depth) * MF(K_P04));
    a[3] = (float)((((f80x)vs[3]->y + MF(K_300)) / RP(rec[3])->depth) * MF(K_P04));
    water_phase(vs[0]->extra, &si, &co);
    w->u[0] = (float)(a0 * MF(tab + si * 4) + tick);
    w->v[0] = (float)(-(a0 * MF(tab + co * 4)) + tick);
    for (int k = 1; k < 4; k++) {
        water_phase(vs[k]->extra, &si, &co);
        w->u[k] = (float)((f80x)a[k] * MF(tab + si * 4) + tick);
        w->v[k] = (float)(-((f80x)a[k] * MF(tab + co * 4)) + tick);
    }
}

/* which corners add 1 to which coordinate (0x5430e0) */
static const uint8_t water_u1[4] = {0, 1, 0, 1}, water_v1[4] = {1, 1, 0, 0};

static inline void water_queue(CPU *c, u32 t, Vtx *const *vs, u32 poly)
{
    if (!M8(0x5d3eb4u))
        return;
    int high = 0;
    for (int k = 0; k < 4; k++) {
        u32 y = fbits(vs[k]->y);
        if ((y & 0x7fffffffu) > 0x3f800000u && !(y >> 31))
            high = 1;
    }
    if (high)
        list_add(c, t + 0x40078, t + 0x40068, poly);
}

/* 0x5430e0; 0x544070 when the cell is flagged 0x20: alpha from the depth */
static void write_water(CPU *c, u32 t, u32 ren, Vtx *const *vs, u32 poly)
{
    Out *out = (Out *)(uintptr_t)M32(ren + 0x3d4);
    const u32 *rec = (const u32 *)(uintptr_t)poly;
    water_queue(c, t, vs, poly);
    Rec *r0 = RP(rec[0]);
    int dark = (r0->flags & 0x20) != 0;
    cov[dark ? C_WATER_DARK : C_WATER]++;
    float al[4] = {0, 0, 0, 0};
    if (dark)
        for (int k = 0; k < 4; k++) {
            float a = (float)(((f80x)RP(rec[k])->depth + MF(K_ONE)) * 0.5f);
            if ((fbits(a) & 0x7fffffffu) > 0x3f800000u && !(fbits(a) >> 31))
                a = 1.0f;
            al[k] = a;
        }
    WaterUV w;
    water_uv(t, vs, rec, &w);
    const uint8_t *order = corner_order[(r0->flags & 0x10) != 0];
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        const Vtx *v = vs[k];
        Out *o = out + i;
        put_pos(o, v);
        o->u = fbits(water_u1[k] ? (float)((f80x)w.u[k] + MF(K_ONE)) : w.u[k]);
        o->v = fbits(water_v1[k] ? (float)((f80x)w.v[k] + MF(K_ONE)) : w.v[k]);
        if (dark)
            o->diffuse = ((u32)rnd255(al[k]) << 24) + ((u32)rnd255(v->r) << 16) + ((u32)rnd255(v->g) << 8) + (u32)rnd255(v->b);
        else
            o->diffuse = argb(0xff, v->r, v->g, v->b);
    }
    clip(c, ren, 6);
}

/* 0x543860: water's second layer, additive */
static void write_water2(CPU *c, u32 t, u32 ren, Vtx *const *vs, u32 poly)
{
    const u32 *rec = (const u32 *)(uintptr_t)poly;
    float s[4], sc = MF(t + 0x18);
    for (int k = 0; k < 4; k++) {
        s[k] = (float)((f80x)vs[k]->y * MF(K_P02) - (f80x)RP(rec[k])->depth * sc);
        if (fbits(s[k]) & 0x80000000u)
            s[k] = MF(K_ZERO);
    }
    if (!((f80x)s[3] + s[2] + s[1] + s[0] != (f80x)MF(K_ZERO)))
        return;
    cov[C_WATER2]++;
    float d[4], ph[4];
    for (int k = 0; k < 4; k++) {
        d[k] = (float)((f80x)RP(rec[k])->depth * MF(t + 0x1c));
        ph[k] = vs[k]->extra;
    }
    for (int k = 1; k < 4; k++) {
        float diff = fabsf((float)((f80x)ph[k] - ph[0]));
        if (diff > MF(K_ONE))
            ph[k] = (float)(ph[k] <= ph[0] ? (f80x)ph[k] + MF(K_FOUR) : (f80x)ph[k] - MF(K_FOUR));
    }
    Out *out = (Out *)(uintptr_t)M32(ren + 0x3d4);
    const uint8_t *order = corner_order[(RP(rec[0])->flags & 0x10) != 0];
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        const Vtx *v = vs[k];
        Out *o = out + i;
        float fb = (float)((f80x)s[k] * v->b), fg = (float)((f80x)s[k] * v->g), fr = (float)((f80x)s[k] * v->r);
        put_pos(o, v);
        o->u = fbits(k == 0 || k == 2 ? (float)((f80x)ph[k] + MF(K_ONE)) : ph[k]);
        o->v = fbits((float)((f80x)d[k] - MF(t + 8)));
        o->diffuse = argb(0xff, fr, fg, fb);
    }
    clip(c, ren, 6);
}

/* 0x5450a0: the fog over a quad */
static void write_fog(CPU *c, u32 ren, Vtx *const *vs, const u32 *rec)
{
    int any = 0;
    for (int k = 0; k < 4; k++) {
        u32 b = fbits(vs[k]->fog);
        if (!(b >> 31) && (b & 0x7fffffffu))
            any = 1;
    }
    if (!any)
        return;
    cov[C_FOGQ]++;
    static const u32 fog_u[4] = {0, 0, 0x3f800000u, 0x3f800000u}, fog_v[4] = {0x3f800000u, 0, 0x3f800000u, 0};
    Out *out = (Out *)(uintptr_t)M32(ren + 0x3d4);
    const uint8_t *order = corner_order[(RP(rec[0])->flags & 0x10) != 0];
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        const Vtx *v = vs[k];
        Out *o = out + i;
        put_pos(o, v);
        o->u = fog_u[k];
        o->v = fog_v[k];
        o->diffuse = fbits((float)((f80x)v->fog * MF(K_255) + MF(K_SINK))) << 24 | M32(0x5f9c00u);
    }
    clip(c, ren, 4);
}

/* ------------------------------------------------------------------ the passes */

/* whether a quad is dropped: 0x540260's test and 0x540930. Both triangles' signed areas, each times its three rhw */
static inline int quad_kept(Vtx *const *v, int flip)
{
    float a;
    if (!flip) {
        f80x dx = (f80x)v[0]->sx - v[3]->sx, dy = (f80x)v[0]->sy - v[3]->sy;
        a = (float)(((((f80x)v[1]->sx - v[3]->sx) * dy - ((f80x)v[1]->sy - v[3]->sy) * dx)) * v[1]->rhw * v[0]->rhw * v[3]->rhw);
        if (!(fbits(a) >> 31))
            a = (float)((((f80x)v[2]->sy - v[3]->sy) * dx - ((f80x)v[2]->sx - v[3]->sx) * dy) * v[2]->rhw * v[0]->rhw * v[3]->rhw);
    } else {
        f80x dx = (f80x)v[2]->sx - v[1]->sx, dy = (f80x)v[2]->sy - v[1]->sy;
        a = (float)(((((f80x)v[0]->sx - v[1]->sx) * dy - ((f80x)v[0]->sy - v[1]->sy) * dx)) * v[0]->rhw * v[2]->rhw * v[1]->rhw);
        if (!(fbits(a) >> 31))
            a = (float)((((f80x)v[3]->sy - v[1]->sy) * dx - ((f80x)v[3]->sx - v[1]->sx) * dy) * v[3]->rhw * v[2]->rhw * v[1]->rhw);
    }
    int culled_front = !(fbits(a) >> 31);
    uint8_t mirror = M8(0x5d18a8u);
    return culled_front ? mirror != 0 : mirror == 0;
}

static inline void flush_state(CPU *c, u32 ren)
{
    c->ecx = ren;
    guest_call(c, M32(ren + 0x5d0), 0);
}

static inline void device_state(CPU *c, u32 dev, u32 state, u32 value)
{
    if ((int)guest_call(c, M32(M32(dev) + 0x50), 3, dev, state, value) < 0)
        guest_call(c, 0x422580u, 0);
}

/* key test off, as 0x540260 and 0x544da0 both begin */
static void key_off(CPU *c, u32 ren)
{
    if (!M8(ren + 0x5b1))
        return;
    flush_state(c, ren);
    M8(ren + 0x5b1) = 0;
    u32 dev = M32(ren + 0x5cc);
    if (!M8(0x5d3e03u)) {
        device_state(c, dev, 0x29, 0);
        device_state(c, dev, 0x90, M8(ren + 0x5b1));
    } else
        device_state(c, dev, 0xf, 0);
}

static inline void set_blend(CPU *c, u32 ren, u32 on)
{
    if (M8(ren + 0x5b0) == on)
        return;
    c->ecx = ren;
    guest_call(c, 0x4a11e0u, 1, on);
}

static inline void set_texture(CPU *c, u32 ren, u32 tex)
{
    c->ecx = ren;
    guest_call(c, 0x4f2ae0u, 1, tex);
}

static inline void set_address(CPU *c, u32 ren, u32 wrap)
{
    if (M8(ren + 0x5bc) != wrap) {
        c->ecx = ren;
        guest_call(c, 0x4f30c0u, 1, wrap);
    }
}

static inline void set_mode(CPU *c, u32 ren, u32 mode)
{
    if (M32(ren + 0x5ac) != mode) {
        c->ecx = ren;
        guest_call(c, 0x4f26d0u, 1, mode);
    }
}

static inline void land_verts(u32 base, const u32 *rec, Vtx **v)
{
    for (int k = 0; k < 4; k++)
        v[k] = VP(base + (u32)(int)RP(rec[k])->land * 0x4c);
}

static inline void water_verts(u32 base, const u32 *rec, Vtx **v)
{
    for (int k = 0; k < 4; k++)
        v[k] = VP(base + (u32)(int)RP(rec[k])->water * 0x4c);
}

/* the unusual cases stay with the original */
static inline int native_usable(u32 view)
{
    return M8(0x5d3e01u) && !M8(M32(view + 8) + 0x5a1);
}

/* 0x540260 (ECX the terrain, first argument the view; returns 1 in al) */
static void native_land(CPU *c)
{
    u32 t = c->ecx, view = R32(c->esp + 4), l = M32(0x5e1450u);
    transform(c, t, view);
    light(c, t, view);
    u32 ren = M32(view + 8);
    set_address(c, ren, 0);
    set_mode(c, ren, 2);
    key_off(c, ren);
    int ntex = MS16(t + 0x4007e);
    for (int i = 0; i < ntex; i++) {
        u32 bin = M32(t + 0x40058) + (u32)i * 0x20;
        if (M32(bin + 0x18)) {
            set_texture(c, ren, M32(M32(M32(l + 0x4128c) + (u32)i * 4) + 4));
            set_blend(c, ren, 0);
            bin = M32(t + 0x40058) + (u32)i * 0x20;
            const u32 *list = (const u32 *)(uintptr_t)M32(bin + 4);
            int n = (int)M32(bin + 0x18);
            u32 base = M32(t + 0x3c);
            for (int q = 0; q < n; q++) {
                const u32 *rec = (const u32 *)(uintptr_t)list[q];
                Vtx *v[4];
                land_verts(base, rec, v);
                if (M8((u32)(uintptr_t)v[0] + 0x20) & M8((u32)(uintptr_t)v[1] + 0x20) & M8((u32)(uintptr_t)v[2] + 0x20) & M8((u32)(uintptr_t)v[3] + 0x20))
                    continue;
                Rec *r0 = RP(rec[0]);
                if (!quad_kept(v, (r0->flags & 0x10) != 0))
                    continue;
                *((uint8_t *)&r0->bits) |= 1;
                write_opaque(c, ren, rec, v);
            }
        }
        bin = M32(t + 0x40058) + (u32)i * 0x20;
        if (M32(bin + 0x1c)) {
            set_texture(c, ren, M32(M32(M32(l + 0x4128c) + (u32)i * 4) + 4));
            set_blend(c, ren, 1);
            bin = M32(t + 0x40058) + (u32)i * 0x20;
            const u32 *list = (const u32 *)(uintptr_t)M32(bin + 0x10);
            int n = (int)M32(bin + 0x1c);
            u32 base = M32(t + 0x3c);
            for (int q = 0; q < n; q++) {
                const u32 *rec = (const u32 *)(uintptr_t)list[q];
                if (!(*(uint8_t *)&RP(rec[0])->bits & 1))
                    continue;
                Vtx *v[4];
                land_verts(base, rec, v);
                write_blend(c, ren, rec, v, (u32)i);
            }
        }
    }
    set_address(c, ren, 1);
    if (M8(view + 0x38) & 2) {
        if (M32(t + 0x40074)) {
            set_texture(c, ren, M32(M32(M32(l + 0x41290)) + 4));
            set_blend(c, ren, 1);
            int n = (int)M32(t + 0x40074);
            const u32 *list = (const u32 *)(uintptr_t)M32(t + 0x40060);
            for (int q = 0; q < n; q++) {
                u32 poly = list[q];
                const u32 *rec = (const u32 *)(uintptr_t)poly;
                Vtx *v[4];
                water_verts(M32(t + 0x48), rec, v);
                if (M8((u32)(uintptr_t)v[0] + 0x20) & M8((u32)(uintptr_t)v[1] + 0x20) & M8((u32)(uintptr_t)v[2] + 0x20) & M8((u32)(uintptr_t)v[3] + 0x20))
                    continue;
                Rec *r0 = RP(rec[0]);
                if (!quad_kept(v, (r0->flags & 0x10) != 0))
                    continue;
                *((uint8_t *)&r0->bits) |= 2;
                write_water(c, t, ren, v, poly);
            }
        }
        set_mode(c, ren, 3);
        if (M32(t + 0x40078)) {
            set_texture(c, ren, M32(M32(M32(l + 0x41290) + 4) + 4));
            c->ecx = ren;
            guest_call(c, 0x4a11e0u, 1, 1u);
            int n = (int)M32(t + 0x40078);
            const u32 *list = (const u32 *)(uintptr_t)M32(t + 0x4006c);
            for (int q = 0; q < n; q++) {
                u32 poly = list[q];
                const u32 *rec = (const u32 *)(uintptr_t)poly;
                Vtx *v[4];
                water_verts(M32(t + 0x48), rec, v);
                write_water2(c, t, ren, v, poly);
            }
        }
    }
    api_return(c, 1, 1);
}

/* 0x544da0 */
static void native_fog(CPU *c)
{
    u32 t = c->ecx, view = R32(c->esp + 4);
    u32 ren = M32(view + 8);
    key_off(c, ren);
    if (M8(ren + 0x5b2)) {
        c->ecx = ren;
        guest_call(c, 0x4f2a70u, 1, 0u);
    }
    set_blend(c, ren, 1);
    set_mode(c, ren, 2);
    if (M32(ren + 0x5b8) != 4) {
        c->ecx = ren;
        guest_call(c, 0x4f3080u, 1, 4u);
    }
    set_address(c, ren, 1);
    if (M32(view + 0x20)) {
        set_texture(c, ren, 0);
        set_blend(c, ren, 1);
        int ntex = MS16(t + 0x4007e);
        for (int i = 0; i < ntex; i++) {
            u32 bin = M32(t + 0x40058) + (u32)i * 0x20;
            int n = (int)M32(bin + 0x18);
            const u32 *list = (const u32 *)(uintptr_t)M32(bin + 4);
            u32 base = M32(t + 0x3c);
            for (int q = 0; q < n; q++) {
                const u32 *rec = (const u32 *)(uintptr_t)list[q];
                if (!(*(uint8_t *)&RP(rec[0])->bits & 1))
                    continue;
                Vtx *v[4];
                land_verts(base, rec, v);
                write_fog(c, ren, v, rec);
            }
        }
        if (M8(view + 0x38) & 2) {
            int n = (int)M32(t + 0x40074);
            const u32 *list = (const u32 *)(uintptr_t)M32(t + 0x40060);
            for (int q = 0; q < n; q++) {
                const u32 *rec = (const u32 *)(uintptr_t)list[q];
                if (!(*(uint8_t *)&RP(rec[0])->bits & 2))
                    continue;
                Vtx *v[4];
                water_verts(M32(t + 0x48), rec, v);
                write_fog(c, ren, v, rec);
            }
        }
    }
    api_return(c, 1, 1);
}

/* ------------------------------------------------------------------ the check, ORR_TERRAIN_AB */

int native_ab_mode;     /* d3d.c: 1 the original's run, its draws only recorded; 2 ours, recorded and drawn */
static unsigned char *ab_log[2];
static size_t ab_len[2], ab_cap[2];
static int ab_every, ab_ran;
static unsigned long ab_views, ab_last_frame = ~0ul;
static unsigned long ab_compares[3], ab_diffs[3], ab_skips[3], ab_known[3], ab_draws[3];
static u32 ab_t, ab_view;

void native_ab_record(u32 kind, const void *p, size_t n)
{
    int w = native_ab_mode - 1;
    size_t need = ab_len[w] + n + 8;
    if (need > ab_cap[w]) {
        ab_cap[w] = need * 2;
        ab_log[w] = realloc(ab_log[w], ab_cap[w]);
    }
    u32 h[2] = {kind, (u32)n};
    memcpy(ab_log[w] + ab_len[w], h, 8);
    if (n)
        memcpy(ab_log[w] + ab_len[w] + 8, p, n);
    ab_len[w] += n + 8;
}

/* a place in program memory, by what's there */
static void ab_where(u32 a, char *out, size_t cap)
{
    u32 t = ab_t;
    u32 lv = M32(t + 0x3c), wv = M32(t + 0x48), gr = M32(t + 0x40084);
    if (a >= t && a < t + 0x400a0)
        snprintf(out, cap, "terrain +0x%x", a - t);
    else if (a >= lv && a < lv + M32(t + 0x40) * 0x4c)
        snprintf(out, cap, "land vertex %u +0x%x", (a - lv) / 0x4c, (a - lv) % 0x4c);
    else if (a >= wv && a < wv + M32(t + 0x4c) * 0x4c)
        snprintf(out, cap, "water vertex %u +0x%x", (a - wv) / 0x4c, (a - wv) % 0x4c);
    else if (a >= gr && a < gr + M32(t + 0x40088) * 0x14)
        snprintf(out, cap, "grid record %u +0x%x", (a - gr) / 0x14, (a - gr) % 0x14);
    else {
        u32 ren = M32(ab_view + 8);
        if (a >= ren && a < ren + 0x800)
            snprintf(out, cap, "renderer +0x%x", a - ren);
        else
            snprintf(out, cap, "%08x", a);
    }
}

static void ab_compare_logs(int which, const char *name)
{
    if (ab_len[0] == ab_len[1] && !memcmp(ab_log[0], ab_log[1], ab_len[0]))
        return;
    size_t i = 0, n = ab_len[0] < ab_len[1] ? ab_len[0] : ab_len[1];
    /* to the first differing record */
    while (i + 8 <= n) {
        u32 h0[2], h1[2];
        memcpy(h0, ab_log[0] + i, 8);
        memcpy(h1, ab_log[1] + i, 8);
        if (memcmp(h0, h1, 8) || memcmp(ab_log[0] + i + 8, ab_log[1] + i + 8, h0[1])) {
            size_t k = 0;
            if (!memcmp(h0, h1, 8))
                while (k < h0[1] && ab_log[0][i + 8 + k] == ab_log[1][i + 8 + k])
                    k++;
            LOG(LOG_ALWAYS, "terrain ab: %s frame %lu: Direct3D calls differ, record at %zu kinds %x %x sizes %u %u, first byte %zu", name, frame_count, i, h0[0], h1[0], h0[1], h1[1], k);
            ab_diffs[which]++;
            return;
        }
        i += 8 + h0[1];
    }
    LOG(LOG_ALWAYS, "terrain ab: %s frame %lu: Direct3D call lists differ in length, %zu and %zu bytes", name, frame_count, ab_len[0], ab_len[1]);
    ab_diffs[which]++;
}

static unsigned char *ab_snap, *ab_after;
static size_t ab_snap_cap;

/* the ranges to keep: from mem.c */
static int ab_ranges(u32 *base, u32 *size, int max)
{
    return mem_ranges(base, size, max);
}

static void ab_run(CPU *c, int which, const char *name, u32 t, u32 view, void (*orig)(CPU *), void (*ours)(CPU *))
{
    enum { MAXR = 4096 };
    static u32 base[MAXR], size[MAXR];
    int nr = ab_ranges(base, size, MAXR);
    size_t total = 0;
    for (int i = 0; i < nr; i++)
        total += size[i];
    if (total > ab_snap_cap) {
        free(ab_snap);
        free(ab_after);
        ab_snap_cap = total + (64u << 20);
        ab_snap = malloc(ab_snap_cap);
        ab_after = malloc(ab_snap_cap);
        if (!ab_snap || !ab_after)
            fatal("terrain ab: no memory for %zu bytes", ab_snap_cap);
        LOG(LOG_ALWAYS, "terrain ab: %zu MB of program memory in %d ranges", total >> 20, nr);
    }
    ab_t = t;
    ab_view = view;
    size_t off = 0;
    for (int i = 0; i < nr; i++) {
        memcpy(ab_snap + off, (const void *)(uintptr_t)base[i], size[i]);
        off += size[i];
    }
    CPU keep = *c;
    unsigned long heaps = mem_heap_ops;
    ab_len[0] = ab_len[1] = 0;
    native_ab_mode = 1;
    orig(c);
    native_ab_mode = 0;
    if (mem_heap_ops != heaps) {
        /* the runtime's allocator keeps its books outside program memory, so an array grown can't be put back: this
           view stays the original's */
        ab_skips[which]++;
        return;
    }
    CPU after_orig = *c;
    off = 0;
    for (int i = 0; i < nr; i++) {
        memcpy(ab_after + off, (const void *)(uintptr_t)base[i], size[i]);
        off += size[i];
    }
    /* (only the pages it changed: another thread's live stack stays as it is) */
    off = 0;
    for (int i = 0; i < nr; i++) {
        for (u32 k = 0; k < size[i]; k += 4096) {
            u32 n = size[i] - k < 4096 ? size[i] - k : 4096;
            if (memcmp((const void *)(uintptr_t)(base[i] + k), ab_snap + off + k, n))
                memcpy((void *)(uintptr_t)(base[i] + k), ab_snap + off + k, n);
        }
        off += size[i];
    }
    *c = keep;
    native_ab_mode = 2;
    ours(c);
    native_ab_mode = 0;
    /* ORR_TERRAIN_AB_BREAK=1: that the check sees one last unit: a run with this must report it */
    if (getenv("ORR_TERRAIN_AB_BREAK") && M32(t + 0x3c))
        VP(M32(t + 0x3c))[3].r += 1e-6f;
    ab_compares[which]++;
    ab_compare_logs(which, name);
    if (ab_len[0] > 8)
        ab_draws[which] += ab_len[0];

    if (c->ebx != after_orig.ebx || c->ebp != after_orig.ebp || c->esi != after_orig.esi || c->edi != after_orig.edi ||
        c->esp != after_orig.esp || c->ret != after_orig.ret || c->ftop != after_orig.ftop || (c->eax & 0xff) != (after_orig.eax & 0xff)) {
        if (which != 0 || c->ebx != after_orig.ebx || c->ebp != after_orig.ebp || c->esi != after_orig.esi || c->edi != after_orig.edi || c->esp != after_orig.esp || c->ret != after_orig.ret || c->ftop != after_orig.ftop) {
            LOG(LOG_ALWAYS, "terrain ab: %s frame %lu: registers differ: ebx %x %x ebp %x %x esi %x %x edi %x %x esp %x %x ret %x %x ftop %d %d eax %x %x", name, frame_count,
                after_orig.ebx, c->ebx, after_orig.ebp, c->ebp, after_orig.esi, c->esi, after_orig.edi, c->edi, after_orig.esp, c->esp, after_orig.ret, c->ret, after_orig.ftop, c->ftop, after_orig.eax, c->eax);
            ab_diffs[which]++;
        }
    }

    /* every byte, without the stacks below each thread's esp */
    extern int cpu_dead_stacks(uint32_t *lo, uint32_t *hi, int max);
    uint32_t lo[64], hi[64];
    int nd = cpu_dead_stacks(lo, hi, 64);
    int said = 0;
    unsigned long diffs = 0, known = 0;
    u32 lv = M32(t + 0x3c), wv = M32(t + 0x48), wcap = M32(t + 0x4c);
    off = 0;
    for (int i = 0; i < nr; i++) {
        const unsigned char *ga = ab_after + off, *gb = (const unsigned char *)(uintptr_t)base[i];
        off += size[i];
        if (!memcmp(ga, gb, size[i]))
            continue;
        for (u32 k = 0; k + 4 <= size[i]; k += 4) {
            u32 a, b;
            memcpy(&a, ga + k, 4);
            memcpy(&b, gb + k, 4);
            if (a == b)
                continue;
            u32 at = base[i] + k;
            int dead = 0;
            for (int j = 0; j < nd; j++)
                if (at >= lo[j] && at < hi[j])
                    dead = 1;
            if (dead)
                continue;
            /* the stale +0x40 of a water vertex (make_vertex) */
            if (at >= wv && at < wv + wcap * 0x4c && (at - wv) % 0x4c == 0x40) {
                known++;
                continue;
            }
            (void)lv;
            diffs++;
            if (said < 12) {
                char w[64];
                ab_where(at, w, sizeof w);
                LOG(LOG_ALWAYS, "terrain ab: %s frame %lu: %s (%08x): original %08x ours %08x", name, frame_count, w, at, a, b);
                said++;
            }
        }
    }
    ab_diffs[which] += diffs;
    ab_known[which] += known;
}

/* ORR_TERRAIN_AB_FORCE=water,dark: paths no track reaches by itself, for the check only. The view flag for water (nothing sets
   it in a normal race, [verified: 0 water vertices in every run]) and, before a compared land pass, the water-darkened flag and
   depths on the blended quads and water quads. Both runs see the same changed input; the picture is not meant to look right. */
static int force_water, force_dark;

static void force_dark_input(u32 t)
{
    int ntex = MS16(t + 0x4007e);
    u32 mix = 0x9e3779b9u;
    for (int i = 0; i < ntex; i++) {
        u32 bin = M32(t + 0x40058) + (u32)i * 0x20;
        int n = (int)M32(bin + 0x1c);
        const u32 *list = (const u32 *)(uintptr_t)M32(bin + 0x10);
        for (int q = 0; q < n; q++) {
            const u32 *rec = (const u32 *)(uintptr_t)list[q];
            RP(rec[0])->flags |= 0x20;
            for (int k = 0; k < 4; k++) {
                mix = mix * 0x01000193u + rec[k];
                RP(rec[k])->depth = (float)(mix >> 12) * (1.2f / 1048576.0f);
            }
        }
    }
    int n = (int)M32(t + 0x40074);
    const u32 *list = (const u32 *)(uintptr_t)M32(t + 0x40060);
    for (int q = 0; q < n; q++)
        RP(*(const u32 *)(uintptr_t)list[q])->flags |= 0x20;
}

static void ab_report(void)
{
    static const char *const nm[3] = {"walk", "land", "fog"};
    char line[600];
    size_t k = 0;
    for (int i = 0; i < C_N; i++)
        k += (size_t)snprintf(line + k, sizeof line - k, "%s %lu, ", cov_name[i], cov[i]);
    LOG(LOG_ALWAYS, "terrain ab: went through %s", line);
    for (int i = 0; i < 3; i++)
        LOG(LOG_ALWAYS, "terrain ab: %s: %lu views compared, %lu skipped (an array grew), %lu dwords different, %lu known (water vertex +0x40), %lu bytes of Direct3D calls compared",
            nm[i], ab_compares[i], ab_skips[i], ab_diffs[i], ab_known[i], ab_draws[i]);
}

/* A view's first piece decides for all three that it runs on */
static int ab_this_view(void)
{
    static int due;
    if (ab_last_frame != frame_count) {
        ab_last_frame = frame_count;
        due = ab_every && (++ab_views % (unsigned long)ab_every) == 0;
    }
    return due;
}

/* ------------------------------------------------------------------ the hooks */

static int terrain_on;

void native_land_init(void)
{
    const char *e = getenv("ORR_NATIVE_TERRAIN");
    terrain_on = e ? atoi(e) != 0 : option_bool("native_terrain", 1);
    e = getenv("ORR_TERRAIN_AB");
    ab_every = e ? atoi(e) : 0;
    e = getenv("ORR_TERRAIN_AB_FORCE");
    force_water = e && strstr(e, "water");
    force_dark = e && strstr(e, "dark");
    if (ab_every > 0) {
        atexit(ab_report);
        LOG(LOG_ALWAYS, "terrain ab: every %d frames", ab_every);
    }
}

void native_terrain_report(void)
{
    if (ab_every > 0)
        ab_report();
}

void native_walk_call(CPU *c)
{
    if (!terrain_on) {
        F_0054f230_orig(c);
        return;
    }
    {
        extern int water_enabled(void);
        if (force_water || water_enabled())
            M32(R32(c->esp + 4) + 0x38) |= 2;
    }
    if (ab_every > 0 && ab_this_view()) {
        ab_run(c, 0, "walk", c->ecx + 0x1194, R32(c->esp + 4), F_0054f230_orig, native_walk);
        return;
    }
    native_walk(c);
}

void native_land_call(CPU *c)
{
    u32 view = R32(c->esp + 4);
    if (!terrain_on || !native_usable(view)) {
        if (terrain_on && !ab_ran) {
            ab_ran = 1;
            LOG(LOG_ALWAYS, "native terrain: the original draws the land (5d3e01 %d, renderer +0x5a1 %d)", M8(0x5d3e01u), M8(M32(view + 8) + 0x5a1));
        }
        F_00540260_orig(c);
        return;
    }
    if (ab_every > 0 && ab_this_view()) {
        if (force_dark)
            force_dark_input(c->ecx);
        ab_run(c, 1, "land", c->ecx, view, F_00540260_orig, native_land);
        return;
    }
    native_land(c);
}

void native_fog_call(CPU *c)
{
    u32 view = R32(c->esp + 4);
    if (!terrain_on || !native_usable(view)) {
        F_00544da0_orig(c);
        return;
    }
    if (ab_every > 0 && ab_this_view()) {
        ab_run(c, 2, "fog", c->ecx, view, F_00544da0_orig, native_fog);
        return;
    }
    native_fog(c);
}
