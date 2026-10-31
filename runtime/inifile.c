/* KERNEL32's .ini file functions (from li2-native's files.c): read from the user folder or the game
   folder, written to the user folder. Hard Truck 2 keeps its settings in TRUCK.INI (and
   kmjin.ini, WEATHER.INI), with comments after values ("xpyramid=1.2 ; 1.15"): as on Windows the
   value is returned whole, with only the spaces at its ends removed. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"

/* The program's .ini files: read from the game or user folder, written to the user folder. */
static int ini_read(const char *file, char **text)
{
    char host[2048];
    *text = NULL;
    char name[600];
    if (!strchr(file, '\\') && !strchr(file, '/'))
        snprintf(name, sizeof name, "C:\\WINDOWS\\%s", file);
    else
        snprintf(name, sizeof name, "%s", file);
    if (!path_to_host(name, host, sizeof host, 0))
        return 0;
    FILE *f = fopen(host, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *text = malloc(n + 1);
    n = fread(*text, 1, n, f);
    (*text)[n] = 0;
    fclose(f);
    return 1;
}

/* the value of key in section (NULL if absent), into out */
static int ini_get(const char *text, const char *sect, const char *key, char *out, size_t n)
{
    int in = 0;
    for (const char *l = text; l && *l; ) {
        const char *e = strpbrk(l, "\r\n");
        size_t len = e ? (size_t)(e - l) : strlen(l);
        char line[1024];
        snprintf(line, sizeof line, "%.*s", (int)(len < 1023 ? len : 1023), l);
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            char *q = strchr(p, ']');
            if (q) *q = 0;
            in = !strcasecmp(p + 1, sect);
        } else if (in && *p && *p != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = 0;
                char *k = p, *v = eq + 1;
                size_t kl = strlen(k);
                while (kl && (k[kl - 1] == ' ' || k[kl - 1] == '\t')) k[--kl] = 0;
                while (*v == ' ' || *v == '\t') v++;
                if (!strcasecmp(k, key)) {
                    size_t vl = strlen(v);
                    while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) v[--vl] = 0;
                    if (vl >= 2 && (v[0] == '"' || v[0] == '\'') && v[vl - 1] == v[0]) {
                        v[vl - 1] = 0;          /* quotes round the whole value are removed */
                        v++;
                    }
                    snprintf(out, n, "%s", v);
                    return 1;
                }
            }
        }
        l = e ? e + 1 : NULL;
        while (l && (*l == '\r' || *l == '\n')) l++;
    }
    return 0;
}

/* the names of a section's keys (sect set) or of all sections (sect NULL), each ended by a NUL,
   into out; returns the length used */
static size_t ini_list(const char *text, const char *sect, char *out, size_t cap)
{
    size_t n = 0;
    int in = 0;
    for (const char *l = text; l && *l; ) {
        const char *e = strpbrk(l, "\r\n");
        size_t len = e ? (size_t)(e - l) : strlen(l);
        char line[1024];
        snprintf(line, sizeof line, "%.*s", (int)(len < 1023 ? len : 1023), l);
        char *p = line, *name = NULL;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            char *q = strchr(p, ']');
            if (q) *q = 0;
            in = sect && !strcasecmp(p + 1, sect);
            if (!sect) name = p + 1;
        } else if (in && *p && *p != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = 0;
                size_t kl = strlen(p);
                while (kl && (p[kl - 1] == ' ' || p[kl - 1] == '\t')) p[--kl] = 0;
                name = p;
            }
        }
        if (name) {
            size_t k = strlen(name) + 1;
            if (n + k > cap) k = cap > n ? cap - n : 0;
            memcpy(out + n, name, k);
            n += k;
        }
        l = e ? e + 1 : NULL;
        while (l && (*l == '\r' || *l == '\n')) l++;
    }
    return n;
}

API(KERNEL32, GetPrivateProfileStringA, 6)
{
    const char *sect = gstr(ARG(0)), *key = gstr(ARG(1)), *def = gstr(ARG(2)), *file = gstr(ARG(5));
    uint32_t out = ARG(3), cap = ARG(4);
    char *text, v[1024];
    const char *r = def ? def : "";
    if ((!sect || !key) && cap) {
        /* a list: names ended by NULs, one more NUL at the end; when it does not fit, cut to
           cap - 2 with the two NULs */
        char *list = malloc(cap + 2);
        size_t n = 0;
        if (ini_read(file, &text)) {
            n = ini_list(text, sect, list, cap + 2);
            free(text);
        }
        uint32_t ret;
        if (n + 1 > cap) {
            n = cap >= 2 ? cap - 2 : 0;
            ret = n;
            memcpy(gptr(out), list, n);
            if (cap >= 2) { W8(out + n, 0); W8(out + n + 1, 0); }
            else W8(out, 0);
        } else {
            memcpy(gptr(out), list, n);
            W8(out + n, 0);
            ret = n;                    /* the length without the last NUL */
        }
        free(list);
        LOG(LOG_FILE, "GetPrivateProfileStringA([%s] list, %s) -> %u bytes", sect ? sect : "sections", file, ret);
        return ret;
    }
    if (sect && key && ini_read(file, &text)) {
        if (ini_get(text, sect, key, v, sizeof v))
            r = v;
        free(text);
    }
    LOG(LOG_FILE, "GetPrivateProfileStringA([%s] %s, %s) -> \"%s\"", sect, key, file, r);
    if (!cap) return 0;
    gstrncpy(out, r, cap);
    return strlen(gptr(out));
}

API(KERNEL32, GetPrivateProfileIntA, 4)
{
    const char *sect = gstr(ARG(0)), *key = gstr(ARG(1)), *file = gstr(ARG(3));
    char *text, v[1024];
    uint32_t r = ARG(2);
    if (ini_read(file, &text)) {
        if (ini_get(text, sect, key, v, sizeof v))
            r = (uint32_t)strtol(v, NULL, 10);
        free(text);
    }
    LOG(LOG_FILE, "GetPrivateProfileIntA([%s] %s, %s) -> %d", sect, key, file, (int)r);
    return r;
}

API(KERNEL32, GetProfileIntA, 3) { return ARG(2); }

API(KERNEL32, WritePrivateProfileStringA, 4)
{
    const char *sect = gstr(ARG(0)), *key = gstr(ARG(1)), *val = gstr(ARG(2)), *file = gstr(ARG(3));
    char *text = NULL;
    ini_read(file, &text);
    /* rebuild: copy lines, replacing or adding key in section */
    size_t cap = (text ? strlen(text) : 0) + 4096, n = 0;
    char *out = malloc(cap);
    int in = 0, done = 0, seen = 0;
    out[0] = 0;
#define PUT(...) n += snprintf(out + n, cap - n, __VA_ARGS__)
    for (const char *l = text; l && *l; ) {
        const char *e = strpbrk(l, "\r\n");
        size_t len = e ? (size_t)(e - l) : strlen(l);
        char line[1024];
        snprintf(line, sizeof line, "%.*s", (int)(len < 1023 ? len : 1023), l);
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            if (in && !done && key && val) { PUT("%s=%s\r\n", key, val); done = 1; }
            char s[256];
            snprintf(s, sizeof s, "%s", p + 1);
            char *q = strchr(s, ']');
            if (q) *q = 0;
            in = !strcasecmp(s, sect);
            seen |= in;
            if (!(in && !key)) PUT("%s\r\n", line);
        } else if (in) {
            const char *eq = strchr(p, '=');
            if (!key)
                ;                                               /* the section is deleted */
            else if (eq && (size_t)(eq - p) == strlen(key) && !strncasecmp(p, key, eq - p)) {
                if (val && !done) PUT("%s=%s\r\n", key, val);
                done = 1;
            } else
                PUT("%s\r\n", line);
        } else if (*p)
            PUT("%s\r\n", line);
        l = e ? e + 1 : NULL;
        while (l && (*l == '\r' || *l == '\n')) l++;
    }
    if (key && val && !done) {
        if (!in) PUT("[%s]\r\n", sect);
        PUT("%s=%s\r\n", key, val);
    }
#undef PUT
    (void)seen;
    char name[600], host[2048];
    if (!strchr(file, '\\') && !strchr(file, '/'))
        snprintf(name, sizeof name, "C:\\WINDOWS\\%s", file);
    else
        snprintf(name, sizeof name, "%s", file);
    path_to_host(name, host, sizeof host, 1);
    FILE *f = fopen(host, "wb");
    if (f) { fwrite(out, 1, n, f); fclose(f); }
    LOG(LOG_FILE, "WritePrivateProfileStringA([%s] %s=%s, %s)", sect, key ? key : "-", val ? val : "-", file);
    free(out);
    free(text);
    return f != NULL;
}
