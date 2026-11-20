/* 訳の表と日本語フォント */
#include "loljp.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t *g_tbl;
static char g_tblpath[MAX_PATH];
static FILETIME g_tbltime;
static DWORD g_lastcheck;

static void FileTimeOf(const char *path, FILETIME *ft)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    memset(ft, 0, sizeof *ft);
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &a)) *ft = a.ftLastWriteTime;
}
static uint32_t g_count;
static uint8_t *g_font;

static uint8_t *ReadAll(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc(n + 1);
    if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[n] = 0; if (size) *size = (uint32_t)n; }
    return b;
}

static uint32_t Fnv(const char *s, int len)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

/* 書式: "LOLJ" u32 ver, u32 count, {u32 hash, u32 en, u32 ja}*count（hash 順）, 文字列 */
int Text_Load(const char *path)
{
    uint32_t size;
    uint8_t *t;
    lstrcpynA(g_tblpath, path, MAX_PATH);
    FileTimeOf(path, &g_tbltime);
    t = ReadAll(path, &size);
    if (!t || size < 12 || memcmp(t, "LOLJ", 4)) { free(t); return 0; }
    g_tbl = t;
    g_count = *(uint32_t *)(g_tbl + 8);
    LogF("訳 %u 件", g_count);
    return 1;
}

const char *Text_Find(const char *en, int len)
{
    uint32_t h, lo = 0, hi;
    const uint32_t *ent;
    if (g_reload && GetTickCount() - g_lastcheck > 1000) {
        FILETIME ft;
        g_lastcheck = GetTickCount();
        FileTimeOf(g_tblpath, &ft);
        if (CompareFileTime(&ft, &g_tbltime)) Text_Load(g_tblpath);
    }
    if (!g_tbl) return NULL;
    h = Fnv(en, len);
    ent = (const uint32_t *)(g_tbl + 12);
    hi = g_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (ent[mid*3] < h) lo = mid + 1; else hi = mid;
    }
    for (; lo < g_count && ent[lo*3] == h; lo++) {
        const char *e = (const char *)g_tbl + ent[lo*3+1];
        if ((int)strlen(e) == len && !memcmp(e, en, len))
            return (const char *)g_tbl + ent[lo*3+2];
    }
    return NULL;
}

int Font_Load(const char *path)
{
    uint32_t size;
    g_font = ReadAll(path, &size);
    return g_font && size >= 94*94*8;
}

const uint8_t *Font_Glyph(uint8_t c1, uint8_t c2)
{
    if (c1 < 0xA1 || c1 > 0xFE || c2 < 0xA1 || c2 > 0xFE) return NULL;
    return g_font + (((c1 - 0xA1) * 94) + (c2 - 0xA1)) * 8;
}
