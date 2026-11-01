/* Ray-traced lighting for a track, baked ahead of time from what export.py wrote. Rays only, on the CPU, no libraries.

       bake IN.geo OUT.olm sx sy sz [threads]      (sx sy sz: towards the sun, game axes)

   What it writes (zlib, tools/bake/README in DESIGN.md 7d; the game reads it in runtime/light.c), for one sun over one layout:
     sun  16 texels a terrain cell (31 cm): how much of the sun the ground sees (R8)
     amb  4 texels a cell: how much of the sky it sees (R), the light bounced to it from what the sun lights (G)
     grid 2 texels a cell x 12 layers, 2 m apart, draped over the terrain: the same for anything above the ground,
          as sun, sky, bounce, for the objects and cars (RGBA8, the layers one under the other in one 2D image)
     the height field, f32, which puts a pixel on the ground or in the grid */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define CELL 512.0f
#define SUN_TEXELS 16
#define AMB_TEXELS 4
#define GRID_TEXELS 2
#define GRID_LAYERS 12
#define GRID_STEP 200.0f
#define MAX_SIZE 8192
#define SUN_RAYS 12
#define SKY_RAYS 48
#define GRID_POINTS 4
#define SUN_RADIUS 0.0175f       /* a degree: a soft edge, more than the real sun's quarter */
#define T_MAX 1e7f

typedef struct { float x, y, z; } V;
static V add(V a, V b) { return (V){a.x + b.x, a.y + b.y, a.z + b.z}; }
static V sub(V a, V b) { return (V){a.x - b.x, a.y - b.y, a.z - b.z}; }
static V mul(V a, float k) { return (V){a.x * k, a.y * k, a.z * k}; }
static float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V cross(V a, V b) { return (V){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static V norm(V a) { return mul(a, 1.0f / sqrtf(dot(a, a))); }

typedef struct { V a, e1, e2; float uv[6]; uint32_t mat; } Tri;
typedef struct { uint32_t tw, th; float albedo; const uint8_t *alpha; } Mat;
typedef struct { float lo[3], hi[3]; uint32_t left_or_first, count; } Node;

static Tri *tris;
static uint32_t *order;
static uint32_t ntri, nmat, W, H;
static Mat *mats;
static float *height;
static Node *nodes;
static uint32_t nnodes;

/* ---- the tree (binned surface area) ---- */
typedef struct { float lo[3], hi[3]; } Box;
static void box_clear(Box *b) { for (int i = 0; i < 3; i++) { b->lo[i] = 1e30f; b->hi[i] = -1e30f; } }
static void box_grow(Box *b, const float p[3])
{
    for (int i = 0; i < 3; i++) {
        if (p[i] < b->lo[i]) b->lo[i] = p[i];
        if (p[i] > b->hi[i]) b->hi[i] = p[i];
    }
}
static float box_area(const Box *b)
{
    float d[3] = {b->hi[0] - b->lo[0], b->hi[1] - b->lo[1], b->hi[2] - b->lo[2]};
    return d[0] < 0 ? 0 : d[0] * d[1] + d[1] * d[2] + d[2] * d[0];
}
static void tri_box(const Tri *t, Box *b)
{
    V p1 = add(t->a, t->e1), p2 = add(t->a, t->e2);
    box_clear(b);
    box_grow(b, &t->a.x);
    box_grow(b, &p1.x);
    box_grow(b, &p2.x);
}
static void build(uint32_t ni, uint32_t first, uint32_t count)
{
    Node *n = &nodes[ni];
    Box b, cb;
    box_clear(&b);
    box_clear(&cb);
    for (uint32_t i = first; i < first + count; i++) {
        Box t;
        tri_box(&tris[order[i]], &t);
        box_grow(&b, t.lo);
        box_grow(&b, t.hi);
        float c[3] = {(t.lo[0] + t.hi[0]) / 2, (t.lo[1] + t.hi[1]) / 2, (t.lo[2] + t.hi[2]) / 2};
        box_grow(&cb, c);
    }
    memcpy(n->lo, b.lo, sizeof n->lo);
    memcpy(n->hi, b.hi, sizeof n->hi);
    n->left_or_first = first;
    n->count = count;
    if (count <= 4)
        return;
    enum { BINS = 16 };
    float best = 1e30f;
    int axis = -1, split = 0;
    for (int a = 0; a < 3; a++) {
        float ext = cb.hi[a] - cb.lo[a];
        if (ext <= 0)
            continue;
        Box bb[BINS];
        uint32_t cnt[BINS] = {0};
        for (int i = 0; i < BINS; i++)
            box_clear(&bb[i]);
        for (uint32_t i = first; i < first + count; i++) {
            Box t;
            tri_box(&tris[order[i]], &t);
            int k = (int)(((t.lo[a] + t.hi[a]) / 2 - cb.lo[a]) / ext * BINS);
            k = k >= BINS ? BINS - 1 : k;
            cnt[k]++;
            box_grow(&bb[k], t.lo);
            box_grow(&bb[k], t.hi);
        }
        float ra[BINS];
        Box r;
        box_clear(&r);
        uint32_t rc = 0;
        for (int i = BINS - 1; i > 0; i--) {
            rc += cnt[i];
            if (cnt[i]) { box_grow(&r, bb[i].lo); box_grow(&r, bb[i].hi); }
            ra[i] = box_area(&r) * rc;
        }
        Box l;
        box_clear(&l);
        uint32_t lc = 0;
        for (int i = 0; i < BINS - 1; i++) {
            lc += cnt[i];
            if (cnt[i]) { box_grow(&l, bb[i].lo); box_grow(&l, bb[i].hi); }
            float c = box_area(&l) * lc + ra[i + 1];
            if (lc && lc < count && c < best) { best = c; axis = a; split = i + 1; }
        }
    }
    if (axis < 0)
        return;
    float ext = cb.hi[axis] - cb.lo[axis];
    uint32_t i = first, j = first + count;
    while (i < j) {
        Box t;
        tri_box(&tris[order[i]], &t);
        int k = (int)(((t.lo[axis] + t.hi[axis]) / 2 - cb.lo[axis]) / ext * BINS);
        k = k >= BINS ? BINS - 1 : k;
        if (k < split)
            i++;
        else {
            uint32_t s = order[i];
            order[i] = order[--j];
            order[j] = s;
        }
    }
    uint32_t left = nnodes;
    nnodes += 2;
    n->left_or_first = left;
    n->count = 0;
    uint32_t lcount = i - first;
    build(left, first, lcount);
    build(left + 1, i, count - lcount);
}

/* ---- rays ---- */
typedef struct { V o, d, inv; } Ray;
static Ray ray_new(V o, V d)
{
    Ray r = {o, d, {1.0f / d.x, 1.0f / d.y, 1.0f / d.z}};
    return r;
}
static inline int box_hit(const Node *n, const Ray *r, float tmax)
{
    float t0 = (n->lo[0] - r->o.x) * r->inv.x, t1 = (n->hi[0] - r->o.x) * r->inv.x;
    float mn = t0 < t1 ? t0 : t1, mx = t0 < t1 ? t1 : t0;
    t0 = (n->lo[1] - r->o.y) * r->inv.y; t1 = (n->hi[1] - r->o.y) * r->inv.y;
    mn = fmaxf(mn, t0 < t1 ? t0 : t1); mx = fminf(mx, t0 < t1 ? t1 : t0);
    t0 = (n->lo[2] - r->o.z) * r->inv.z; t1 = (n->hi[2] - r->o.z) * r->inv.z;
    mn = fmaxf(mn, t0 < t1 ? t0 : t1); mx = fminf(mx, t0 < t1 ? t1 : t0);
    return mx >= mn && mx > 0 && mn < tmax;
}
/* the nearest hit, or with any set the first found; a cut-out part's clear pixels let the ray through */
static int trace(const Ray *r, float tmax, int any, const Tri **out, float *tout)
{
    uint32_t stack[64];
    int sp = 0, found = 0;
    stack[sp++] = 0;
    while (sp) {
        const Node *n = &nodes[stack[--sp]];
        if (!box_hit(n, r, tmax))
            continue;
        if (n->count == 0) {
            stack[sp++] = n->left_or_first;
            stack[sp++] = n->left_or_first + 1;
            continue;
        }
        for (uint32_t i = 0; i < n->count; i++) {
            const Tri *t = &tris[order[n->left_or_first + i]];
            V p = cross(r->d, t->e2);
            float det = dot(t->e1, p);
            if (det > -1e-6f && det < 1e-6f)
                continue;
            float inv = 1.0f / det;
            V s = sub(r->o, t->a);
            float u = dot(s, p) * inv;
            if (u < 0 || u > 1)
                continue;
            V q = cross(s, t->e1);
            float v = dot(r->d, q) * inv;
            if (v < 0 || u + v > 1)
                continue;
            float d = dot(t->e2, q) * inv;
            if (d < 1.0f || d >= tmax)
                continue;
            const Mat *m = &mats[t->mat & 0x7fffffff];
            if (m->tw) {
                float w = 1 - u - v, tu = w * t->uv[0] + u * t->uv[2] + v * t->uv[4], tv = w * t->uv[1] + u * t->uv[3] + v * t->uv[5];
                tu -= floorf(tu);
                tv -= floorf(tv);
                uint32_t x = (uint32_t)(tu * m->tw), y = (uint32_t)(tv * m->th);
                x = x >= m->tw ? m->tw - 1 : x;
                y = y >= m->th ? m->th - 1 : y;
                if (m->alpha[y * m->tw + x] < 128)
                    continue;
            }
            found = 1;
            if (any)
                return 1;
            tmax = d;
            if (out) { *out = t; *tout = d; }
        }
    }
    return found;
}

/* ---- the terrain ---- */
static inline float hat(int i, int j)
{
    i = i < 0 ? 0 : i >= (int)W ? (int)W - 1 : i;
    j = j < 0 ? 0 : j >= (int)H ? (int)H - 1 : j;
    return height[(uint32_t)j * W + (uint32_t)i];
}
/* the game's two triangles a cell, split from corner (i, j) to (i + 1, j + 1) */
static void ground(float x, float z, float *y, V *n)
{
    float fx = x / CELL, fz = z / CELL;
    int i = (int)floorf(fx), j = (int)floorf(fz);
    fx -= (float)i;
    fz -= (float)j;
    float a = hat(i, j), b = hat(i + 1, j), c = hat(i + 1, j + 1), d = hat(i, j + 1);
    float dx, dz;
    if (fx >= fz) { *y = a + (b - a) * fx + (c - b) * fz; dx = b - a; dz = c - b; }
    else          { *y = a + (c - d) * fx + (d - a) * fz; dx = c - d; dz = d - a; }
    *n = norm((V){-dx / CELL, 1.0f, -dz / CELL});
}

/* ---- sampling ---- */
static V sun_dir, sun_u, sun_v;
static inline float rad_inv(uint32_t b)
{
    b = (b << 16) | (b >> 16);
    b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
    b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
    b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
    b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
    return (float)b * 2.3283064e-10f;
}
static inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static float sun_sees(V o, uint32_t seed, int rays)
{
    float j0 = (float)(hash32(seed) & 0xffff) / 65536.0f, j1 = (float)(hash32(seed + 7) & 0xffff) / 65536.0f;
    int got = 0;
    for (int i = 0; i < rays; i++) {
        float u = (float)i / (float)rays + j0, v = rad_inv((uint32_t)i) + j1;
        u -= floorf(u);
        v -= floorf(v);
        float r = sqrtf(u) * SUN_RADIUS, a = 6.2831853f * v;
        V d = norm(add(sun_dir, add(mul(sun_u, r * cosf(a)), mul(sun_v, r * sinf(a)))));
        Ray ray = ray_new(o, d);
        if (!trace(&ray, T_MAX, 1, NULL, NULL))
            got++;
    }
    return (float)got / (float)rays;
}
/* how much sky a point sees and how much light comes back from what its rays hit, against the normal (cosine weighted), or
   with no normal over the whole upper half */
static void sky_sees(V o, const V *n, uint32_t seed, int rays, float *sky, float *bounce)
{
    V nn = n ? *n : (V){0, 1, 0};
    V t = fabsf(nn.x) > 0.5f ? (V){0, 1, 0} : (V){1, 0, 0};
    V a = norm(cross(nn, t)), b = cross(nn, a);
    float j0 = (float)(hash32(seed + 11) & 0xffff) / 65536.0f, j1 = (float)(hash32(seed + 13) & 0xffff) / 65536.0f;
    float s = 0, l = 0;
    for (int i = 0; i < rays; i++) {
        float u = (float)i / (float)rays + j0, v = rad_inv((uint32_t)i) + j1;
        u -= floorf(u);
        v -= floorf(v);
        float r = sqrtf(u), c = 6.2831853f * v, h = sqrtf(1.0f - u);
        V d = add(add(mul(a, r * cosf(c)), mul(b, r * sinf(c))), mul(nn, h));
        if (fabsf(d.x) < 1e-5f) d.x = 1e-5f;
        if (fabsf(d.y) < 1e-5f) d.y = 1e-5f;
        if (fabsf(d.z) < 1e-5f) d.z = 1e-5f;
        d = norm(d);
        Ray ray = ray_new(o, d);
        const Tri *hit = NULL;
        float at = 0;
        if (!trace(&ray, T_MAX, 0, &hit, &at)) {
            s += 1.0f;
            continue;
        }
        V p = add(o, mul(d, at)), hn = norm(cross(hit->e1, hit->e2));
        if (dot(hn, d) > 0)
            hn = mul(hn, -1.0f);
        float nl = dot(hn, sun_dir);
        if (nl <= 0)
            continue;
        Ray sh = ray_new(add(p, mul(hn, 4.0f)), sun_dir);
        if (!trace(&sh, T_MAX, 1, NULL, NULL))
            l += nl * mats[hit->mat & 0x7fffffff].albedo;
    }
    *sky = s / (float)rays;
    *bounce = l / (float)rays;
}

/* ---- the work ---- */
typedef struct { uint32_t sw, sh, aw, ah, gw, gh; uint8_t *sun, *amb, *grid; } Out;
static Out out;
static volatile uint32_t next_row;
static uint32_t total_rows;
static inline uint8_t u8f(float v) { v = v * 255.0f + 0.5f; return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

static void *worker(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t k = __atomic_fetch_add(&next_row, 1, __ATOMIC_RELAXED);
        if (k >= total_rows)
            return NULL;
        if (k < out.sh) {
            float z = ((float)k + 0.5f) / (float)out.sh * (float)(H - 1) * CELL;
            for (uint32_t i = 0; i < out.sw; i++) {
                float x = ((float)i + 0.5f) / (float)out.sw * (float)(W - 1) * CELL, y;
                V n;
                ground(x, z, &y, &n);
                V o = add((V){x, y, z}, mul(n, 8.0f));
                out.sun[k * out.sw + i] = dot(n, sun_dir) <= 0 ? 0 : u8f(sun_sees(o, k * 8191u + i, SUN_RAYS));
            }
            continue;
        }
        k -= out.sh;
        if (k < out.ah) {
            float z = ((float)k + 0.5f) / (float)out.ah * (float)(H - 1) * CELL;
            for (uint32_t i = 0; i < out.aw; i++) {
                float x = ((float)i + 0.5f) / (float)out.aw * (float)(W - 1) * CELL, y, s, b;
                V n;
                ground(x, z, &y, &n);
                sky_sees(add((V){x, y, z}, mul(n, 8.0f)), &n, k * 131071u + i, SKY_RAYS, &s, &b);
                out.amb[(k * out.aw + i) * 2] = u8f(s);
                out.amb[(k * out.aw + i) * 2 + 1] = u8f(b);
            }
            continue;
        }
        k -= out.ah;
        uint32_t layer = k / out.gh, row = k % out.gh;
        float z0 = ((float)row + 0.5f) / (float)out.gh * (float)(H - 1) * CELL, dz = (float)(H - 1) * CELL / (float)out.gh;
        float dx = (float)(W - 1) * CELL / (float)out.gw;
        for (uint32_t i = 0; i < out.gw; i++) {
            float x0 = ((float)i + 0.5f) / (float)out.gw * (float)(W - 1) * CELL, sn = 0, sk = 0, bo = 0;
            uint32_t seed = (layer * 977u + row) * 65599u + i;
            for (int p = 0; p < GRID_POINTS; p++) {
                float jx = (float)(hash32(seed + (uint32_t)p * 3u) & 0xff) / 256.0f - 0.5f, jz = (float)(hash32(seed + (uint32_t)p * 5u) & 0xff) / 256.0f - 0.5f;
                float x = x0 + jx * dx, z = z0 + jz * dz, y, s, b;
                V n;
                ground(x, z, &y, &n);
                V o = {x, y + ((float)layer + 0.5f) * GRID_STEP, z};
                sn += sun_sees(o, seed + (uint32_t)p, SUN_RAYS / 2);
                sky_sees(o, NULL, seed + (uint32_t)p, SKY_RAYS / 4, &s, &b);
                sk += s;
                bo += b;
            }
            uint8_t *c = out.grid + (((size_t)layer * out.gh + row) * out.gw + i) * 4;
            c[0] = u8f(sn / GRID_POINTS);
            c[1] = u8f(sk / GRID_POINTS);
            c[2] = u8f(bo / GRID_POINTS);
            c[3] = 255;
        }
    }
}

static void put(FILE *f, const void *data, size_t n)
{
    uLongf cap = compressBound((uLong)n);
    uint8_t *z = malloc(cap);
    compress2(z, &cap, data, (uLong)n, 6);
    uint64_t sizes[2] = {n, cap};
    fwrite(sizes, sizeof sizes, 1, f);
    fwrite(z, 1, cap, f);
    free(z);
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "bake IN.geo OUT.olm sx sy sz [threads]\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    uint8_t *d = malloc((size_t)size);
    if (fread(d, 1, (size_t)size, f) != (size_t)size) return 1;
    fclose(f);
    uint32_t *h = (uint32_t *)d;
    if (memcmp(d, "OGEO", 4)) return 1;
    W = h[1]; H = h[2]; nmat = h[3]; ntri = h[4];
    height = (float *)(d + 20);
    uint8_t *p = d + 20 + (size_t)W * H * 4;
    mats = calloc(nmat, sizeof *mats);
    for (uint32_t i = 0; i < nmat; i++) {
        uint32_t *u = (uint32_t *)p;
        float *c = (float *)(p + 8);
        mats[i].tw = u[0];
        mats[i].th = u[1];
        mats[i].albedo = 0.3f * c[0] + 0.6f * c[1] + 0.1f * c[2];
        mats[i].alpha = p + 20;
        p += 20 + (size_t)u[0] * u[1];
    }
    tris = malloc(ntri * sizeof *tris);
    order = malloc(ntri * sizeof *order);
    for (uint32_t i = 0; i < ntri; i++) {
        float *r = (float *)(p + (size_t)i * 64);
        V a = {r[0], r[1], r[2]}, b = {r[3], r[4], r[5]}, c = {r[6], r[7], r[8]};
        tris[i].a = a;
        tris[i].e1 = sub(b, a);
        tris[i].e2 = sub(c, a);
        memcpy(tris[i].uv, r + 9, sizeof tris[i].uv);
        tris[i].mat = ((uint32_t *)r)[15];
        order[i] = i;
    }
    nodes = malloc((size_t)ntri * 2 * sizeof *nodes);
    nnodes = 1;
    build(0, 0, ntri);

    sun_dir = norm((V){(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])});
    V t = fabsf(sun_dir.x) > 0.5f ? (V){0, 1, 0} : (V){1, 0, 0};
    sun_u = norm(cross(sun_dir, t));
    sun_v = cross(sun_dir, sun_u);
    /* a ray straight along an axis would divide by nothing */
    if (fabsf(sun_dir.x) < 1e-5f) sun_dir.x = 1e-5f;
    if (fabsf(sun_dir.y) < 1e-5f) sun_dir.y = 1e-5f;
    if (fabsf(sun_dir.z) < 1e-5f) sun_dir.z = 1e-5f;

#define FIT(n, t) ((n) * (t) > MAX_SIZE ? MAX_SIZE : (n) * (t))
    out.sw = FIT(W - 1, SUN_TEXELS); out.sh = FIT(H - 1, SUN_TEXELS);
    out.aw = FIT(W - 1, AMB_TEXELS); out.ah = FIT(H - 1, AMB_TEXELS);
    out.gw = FIT(W - 1, GRID_TEXELS); out.gh = FIT(H - 1, GRID_TEXELS);
    out.sun = malloc((size_t)out.sw * out.sh);
    out.amb = malloc((size_t)out.aw * out.ah * 2);
    out.grid = malloc((size_t)out.gw * out.gh * GRID_LAYERS * 4);
    total_rows = out.sh + out.ah + out.gh * GRID_LAYERS;

    int nt = argc > 6 ? atoi(argv[6]) : 8;
    pthread_t th[64];
    nt = nt > 64 ? 64 : nt;
    for (int i = 0; i < nt; i++)
        pthread_create(&th[i], NULL, worker, NULL);
    for (int i = 0; i < nt; i++)
        pthread_join(th[i], NULL);

    f = fopen(argv[2], "wb");
    if (!f) { perror(argv[2]); return 1; }
    uint32_t head[10] = {0x314d4c4fu, W, H, out.sw, out.sh, out.aw, out.ah, out.gw, out.gh, GRID_LAYERS};
    float fh[4] = {sun_dir.x, sun_dir.y, sun_dir.z, GRID_STEP};
    fwrite(head, sizeof head, 1, f);
    fwrite(fh, sizeof fh, 1, f);
    put(f, height, (size_t)W * H * 4);
    put(f, out.sun, (size_t)out.sw * out.sh);
    put(f, out.amb, (size_t)out.aw * out.ah * 2);
    put(f, out.grid, (size_t)out.gw * out.gh * GRID_LAYERS * 4);
    fclose(f);
    return 0;
}
