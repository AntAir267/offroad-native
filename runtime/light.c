/* Baked lighting: the ray-traced maps for the race being driven (tools/bake; DESIGN.md 7d). The game asks for its level's
   files by name, loose first, also when they are in Levels.mng [verified: --log file]: <env>\track_<n>\[redneck_]<env>.ob3. That
   and the race's weather and time of day (0x5cea00, +4: 1 snow, 3 rain; 2 sunset, 3 night [verified: 0x4d9c30, 0x54e370]) pick the
   bake from index.txt in the maps' folder; each frame's camera comes
   from the start of each view's drawing (0x4daec0 calls 0x4f0770 first). No maps for a race, or the option off: the game's own light. */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"
#include "gfx.h"
#include "vk.h"

static char want_env[32], want_look[32], want_layout[32];
static int want_changed;
static uint8_t *m_sun, *m_amb, *m_grid, *m_lamp, *m_lampg;
static uint16_t *m_height;
static float *m_rt;
static float light_dir[3];
#define MAX_CARS 12
static uint32_t cars[MAX_CARS];
static int ncars;

/* (case folded, either slash) */
static void lower(char *o, size_t n, const char *s)
{
    size_t i = 0;
    for (; s[i] && i + 1 < n; i++)
        o[i] = s[i] == '/' ? '\\' : (char)tolower((unsigned char)s[i]);
    o[i] = 0;
}

void light_file_opened(const char *name)
{
    char s[256];
    if (!name)
        return;
    lower(s, sizeof s, name);
    size_t n = strlen(s);
    if (n < 5 || !strncmp(s, "c:", 2))
        return;
    char *sl = strchr(s, '\\');
    if (!sl)
        return;
    if (!strcmp(s + n - 4, ".ob3")) {
        *sl = 0;
        const char *rest = sl + 1;
        int track = 0;
        if (!strncmp(rest, "track_", 6))
            track = atoi(rest + 6);
        int red = strstr(rest, "redneck") != NULL;
        if (!track)
            return;
        char l[32];
        snprintf(l, sizeof l, "%d%s", track, red ? "r" : "");
        if (strcmp(s, want_env) || strcmp(l, want_layout)) {
            snprintf(want_env, sizeof want_env, "%s", s);
            snprintf(want_layout, sizeof want_layout, "%s", l);
            want_changed = 1;
        }
    }
}

static const char *maps_dir(void)
{
    static char dir[512];
    if (!dir[0]) {
        const char *e = getenv("ORR_LIGHTMAPS");
        if (e)
            snprintf(dir, sizeof dir, "%s", e);
        else
            snprintf(dir, sizeof dir, "%s/.local/share/offroad-native/lightmaps", getenv("HOME") ? getenv("HOME") : ".");
    }
    return dir;
}

static uint8_t *section(FILE *f, size_t want)
{
    uint64_t sizes[2];
    if (fread(sizes, sizeof sizes, 1, f) != 1 || sizes[0] != want)
        return NULL;
    uint8_t *z = malloc(sizes[1]), *o = malloc(want);
    uLongf got = want;
    int ok = z && o && fread(z, 1, sizes[1], f) == sizes[1] && uncompress(o, &got, z, sizes[1]) == Z_OK && got == want;
    free(z);
    if (!ok) {
        free(o);
        return NULL;
    }
    return o;
}

/* (kept until the next load: the render thread reads them later) */
static void load(void)
{
    static uint8_t *old[5];
    static uint16_t *oldh;
    VkLightMaps m;
    memset(&m, 0, sizeof m);
    char path[640], file[128] = "", line[256];
    float sd[3] = {0}, sc[3] = {0}, ac[3] = {0}, tint[3] = {0};
    snprintf(path, sizeof path, "%s/index.txt", maps_dir());
    FILE *f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f)) {
        char env[32], lay[32], look[32], name[128];
        float v[12];
        if (sscanf(line, "%31s %31s %31s %127s %f %f %f %f %f %f %f %f %f %f %f %f", env, lay, look, name, &v[0], &v[1], &v[2], &v[3], &v[4],
                   &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]) == 16 &&
            !strcasecmp(env, want_env) && !strcasecmp(lay, want_layout) && !strcasecmp(look, want_look)) {
            snprintf(file, sizeof file, "%s", name);
            memcpy(sd, v, sizeof sd);
            memcpy(sc, v + 3, sizeof sc);
            memcpy(ac, v + 6, sizeof ac);
            memcpy(tint, v + 9, sizeof tint);
            break;
        }
    }
    if (f)
        fclose(f);
    if (file[0]) {
        snprintf(path, sizeof path, "%s/%s", maps_dir(), file);
        f = fopen(path, "rb");
        uint32_t h[10];
        float fh[4];
        if (f && fread(h, sizeof h, 1, f) == 1 && h[0] == 0x314d4c4fu && fread(fh, sizeof fh, 1, f) == 1) {
            float *hf = (float *)section(f, (size_t)h[1] * h[2] * 4);
            m_sun = section(f, (size_t)h[3] * h[4]);
            m_amb = section(f, (size_t)h[5] * h[6] * 2);
            m_grid = section(f, (size_t)h[7] * h[8] * h[9] * 4);
            /* the layers one under the other, or for a long narrow track, side by side (an image's size is limited) */
            int across = h[8] * h[9] > 8192;
            if (m_grid && across) {
                uint8_t *g = malloc((size_t)h[7] * h[8] * h[9] * 4);
                for (uint32_t l = 0; l < h[9]; l++)
                    for (uint32_t r = 0; r < h[8]; r++)
                        memcpy(g + ((size_t)r * h[7] * h[9] + (size_t)l * h[7]) * 4, m_grid + ((size_t)(l * h[8] + r) * h[7]) * 4, (size_t)h[7] * 4);
                free(m_grid);
                m_grid = g;
            }
            if (hf && m_sun && m_amb && m_grid) {
                float lo = 1e30f, hi = -1e30f;
                for (size_t i = 0; i < (size_t)h[1] * h[2]; i++) {
                    lo = hf[i] < lo ? hf[i] : lo;
                    hi = hf[i] > hi ? hf[i] : hi;
                }
                float range = hi > lo ? hi - lo : 1.0f;
                m_height = malloc((size_t)h[1] * h[2] * 2);
                for (size_t i = 0; i < (size_t)h[1] * h[2]; i++)
                    m_height[i] = (uint16_t)((hf[i] - lo) / range * 65535.0f + 0.5f);
                m.sun = m_sun; m.amb = m_amb; m.grid = m_grid; m.height = m_height;
                m.W = h[1]; m.H = h[2]; m.sw = h[3]; m.sh = h[4]; m.aw = h[5]; m.ah = h[6]; m.gw = h[7]; m.gh = h[8]; m.layers = h[9]; m.across = across;
                const float cell = 512.0f;
                m.size[0] = 1.0f / ((float)(h[1] - 1) * cell);
                m.size[1] = 1.0f / ((float)(h[2] - 1) * cell);
                m.size[2] = (float)h[9];
                m.size[3] = cell;
                m.hmin = lo;
                m.hrange = range;
                for (int i = 0; i < 3; i++) {
                    m.sun_dir[i] = sd[i];
                    m.sun_col[i] = sc[i];
                    m.amb_col[i] = ac[i];
                    m.bounce_col[i] = sc[i] * tint[i];
                }
                /* a darker night, at no cost: the bake's light all scaled, the fog and sky with it (draw.frag) */
                const char *nv = option_str("night", "original");
                m.bounce_col[3] = !strcmp(want_look, "night") || !strncmp(want_look, "night_", 6)
                                      ? (!strcasecmp(nv, "darkest") ? 0.3f : !strcasecmp(nv, "dark") ? 0.55f : 1.0f) : 1.0f;
                m.sun_dir[3] = fh[3];
                memcpy(light_dir, sd, sizeof light_dir);
                /* how much of the game's flat ambient really comes from the sky (the rest always arrives) */
                const char *e = getenv("ORR_LIGHT_SKY");
                m.amb_col[3] = e ? (float)atof(e) : 0.65f;
            }
            free(hf);
        }
        if (f)
            fclose(f);
        /* what the car lights' rays hit */
        if (m.sun && !strcasecmp(option_str("car_light_shadows", "on"), "on")) {
            char rtg[512];
            snprintf(rtg, sizeof rtg, "%s/%s", maps_dir(), file);
            char *us = strrchr(rtg, '_');
            if (us)
                strcpy(us, ".rtg");
            FILE *rf = fopen(rtg, "rb");
            uint32_t rh[2];
            if (rf && fread(rh, sizeof rh, 1, rf) == 1 && rh[0] == 0x4754524fu) {
                fseek(rf, 0, SEEK_END);
                long end = ftell(rf);
                fseek(rf, 8, SEEK_SET);
                uint8_t *z = malloc((size_t)end - 8);
                uLongf got = (uLongf)rh[1] * 36;
                free(m_rt);
                m_rt = malloc(got);
                if (z && m_rt && fread(z, 1, (size_t)end - 8, rf) == (size_t)end - 8 && uncompress((uint8_t *)m_rt, &got, z, (uLong)end - 8) == Z_OK) {
                    m.rt = m_rt;
                    m.nrt = rh[1];
                }
                free(z);
            }
            if (rf)
                fclose(rf);
        }
        /* the map's own lamps, for the layout (not every map has any): at night and dusk */
        if (m.sun) {
            char oll[512];
            snprintf(oll, sizeof oll, "%s/%s", maps_dir(), file);
            char *us = strrchr(oll, '_');
            if (us)
                strcpy(us, ".oll");
            FILE *lf = fopen(oll, "rb");
            uint32_t lh_[6];
            if (lf && fread(lh_, sizeof lh_, 1, lf) == 1 && lh_[0] == 0x314c4c4fu && lh_[3] == m.gw && lh_[4] == m.gh && lh_[5] == m.layers) {
                m_lamp = section(lf, (size_t)lh_[1] * lh_[2]);
                m_lampg = section(lf, (size_t)lh_[3] * lh_[4] * lh_[5]);
                if (m_lamp && m_lampg) {
                    if (m.across) {
                        uint8_t *g = malloc((size_t)m.gw * m.gh * m.layers);
                        for (uint32_t l = 0; l < m.layers; l++)
                            for (uint32_t r = 0; r < m.gh; r++)
                                memcpy(g + (size_t)r * m.gw * m.layers + (size_t)l * m.gw, m_lampg + (size_t)(l * m.gh + r) * m.gw, m.gw);
                        free(m_lampg);
                        m_lampg = g;
                    }
                    m.lamp = m_lamp; m.lampg = m_lampg; m.lw = lh_[1]; m.lh = lh_[2];
                    m.lamp_strength = strcasecmp(option_str("map_lights", "on"), "on") ? 0.0f : !strncmp(want_look, "night", 5) ? 1.0f : !strncmp(want_look, "sunset", 6) ? 0.35f : 0.0f;
                }
            }
            if (lf)
                fclose(lf);
        }
    }
    ncars = 0;
    LOG(LOG_ALWAYS, "baked lighting: %s track %s %s: %s", want_env, want_layout, want_look, m.sun ? file : "no maps");
    vk_light_load(&m);
    /* the last ones: the render thread has had its load command before it can reach this point again */
    free(old[0]); free(old[1]); free(old[2]); free(old[3]); free(old[4]); free(oldh);
    old[0] = m.sun ? m_sun : NULL; old[1] = m.sun ? m_amb : NULL; old[2] = m.sun ? m_grid : NULL; oldh = m.sun ? m_height : NULL;
    old[3] = m_lamp; old[4] = m_lampg;
    m_sun = m_amb = m_grid = m_lamp = m_lampg = NULL;
    m_height = NULL;
}

/* Car lights: the cars the race has drawn, whose matrices are read at the start of each view (the terrain draws before them).
   The object's matrix: three rows of four floats, then the position [verified: 0x52e300, 0x49b300]. */
int light_car_hook(CPU *c)
{
    uint32_t o = c->ecx;
    for (int i = 0; i < ncars; i++)
        if (cars[i] == o)
            return 0;
    if (ncars < MAX_CARS)
        cars[ncars++] = o;
    return 0;
}

static int on = -1;
void light_options(void) { on = -1; want_changed = 1; }

int light_terrain_hook(CPU *c)
{
    static int mode;
    if (on < 0) {
        on = widescreen_allowed() && !strcasecmp(option_str("baked_lighting", "on"), "on") && vk_on();
        mode = getenv("ORR_LIGHT_DEBUG") ? atoi(getenv("ORR_LIGHT_DEBUG")) : 1;
    }
    /* (a race's view only: the menus' car previews have cameras too) */
    uint32_t from = R32(c->esp);
    if (!on || from < 0x4daec0u || from > 0x4daf00u)
        return 0;
    {
        uint32_t wea = R32(0x5cea00), tod = R32(0x5cea04);
        char l[32];
        snprintf(l, sizeof l, "%s%s", tod == 2 ? "sunset" : tod == 3 ? "night" : "day", wea == 1 ? "_snow" : wea == 3 ? "_rain" : "");
        if (strcmp(l, want_look)) {
            snprintf(want_look, sizeof want_look, "%s", l);
            want_changed = 1;
        }
    }
    if (want_changed && want_env[0]) {
        want_changed = 0;
        load();
    }
    uint32_t cam = c->ecx, keep = cam;
    F_004f0770_orig(c);
    static uint32_t buf;
    if (!buf)
        buf = galloc(64);
    c->ecx = cam;
    uint32_t mp = guest_call(c, 0x4f0cb0, 1, buf);
    c->ecx = keep;
    float m[16];
    for (int i = 0; i < 16; i++)
        m[i] = RF32(mp + 4 * (uint32_t)i);
    /* screen x * w, y * w, w = A world + t: rows m[0,4,8], m[1,5,9], m[3,7,11], offset m[12], m[13], m[15] [verified: 0x53c2ab] */
    float a = m[0], b = m[4], d = m[8], e = m[1], f = m[5], g = m[9], h = m[3], i = m[7], k = m[11];
    float det = a * (f * k - g * i) - b * (e * k - g * h) + d * (e * i - f * h);
    if (det == 0.0f)
        return 1;
    float s = 1.0f / det;
    float inv[16] = {(f * k - g * i) * s, (d * i - b * k) * s, (b * g - d * f) * s, 0,
                     (g * h - e * k) * s, (a * k - d * h) * s, (d * e - a * g) * s, 0,
                     (e * i - f * h) * s, (b * h - a * i) * s, (a * f - b * e) * s, 0,
                     m[12], m[13], m[15], 0};
    float eye[3] = {-(inv[0] * inv[12] + inv[1] * inv[13] + inv[2] * inv[14]), -(inv[4] * inv[12] + inv[5] * inv[13] + inv[6] * inv[14]),
                    -(inv[8] * inv[12] + inv[9] * inv[13] + inv[10] * inv[14])};
    if (getenv("ORR_LIGHT_LOG") && frame_count % 300 == 0) {
        float l[3] = {light_dir[0] * 1e6f + eye[0], light_dir[1] * 1e6f + eye[1], light_dir[2] * 1e6f + eye[2]};
        float W_ = m[3] * l[0] + m[7] * l[1] + m[11] * l[2] + m[15];
        LOG(LOG_ALWAYS, "light: frame %lu eye %.0f %.0f %.0f sun on screen %.0f %.0f (w %.0f)", frame_count, eye[0], eye[1], eye[2],
            (m[0] * l[0] + m[4] * l[1] + m[8] * l[2] + m[12]) / W_, (m[1] * l[0] + m[5] * l[1] + m[9] * l[2] + m[13]) / W_, W_);
    }
    vk_light_view(inv, eye, (uint32_t)mode);
    /* how strong a car's lamps are for the look (ORR_CAR_LIGHTS or the option): night, dusk, rain; not by day */
    float strength = !strncmp(want_look, "night", 5) ? 1.0f : !strncmp(want_look, "sunset", 6) ? 0.3f : strstr(want_look, "rain") ? 0.3f : 0.0f;
    if (strcasecmp(option_str("car_lights", "on"), "on") || getenv("ORR_NO_CAR_LIGHTS"))
        strength = 0.0f;
    float L[MAX_CARS * 8];
    int n = 0;
    if (strength > 0.0f)
        for (int k2 = 0; k2 < ncars && n < MAX_CARS; k2++, n++) {
            uint32_t mo = cars[k2] + 0x24;
            for (int q = 0; q < 3; q++) {
                L[n * 8 + q] = RF32(mo + 48 + 4 * (uint32_t)q);
                L[n * 8 + 4 + q] = RF32(mo + 32 + 4 * (uint32_t)q);   /* the car's z: forwards */
            }
            L[n * 8 + 3] = strength;
            L[n * 8 + 7] = 0.0f;
        }
    vk_light_cars(L, n);
    return 1;
}
