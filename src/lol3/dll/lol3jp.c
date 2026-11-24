/* Lands of Lore III 日本語化（winmm.dll 代理）
   - 文章: ゲームが TRR を開くとき、LOL3JP\ の訳した TRR へ振り向ける
   - 字: Buffer_Print と String_Pixel_Width を横取りし、EUC-JP の字を LOL3JP\JPFONT.BIN で描く
     日本語の字と字の間の空白（折り返し用）は幅 0 で描かない */
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/* ---- LOL3.DAT の番地 ---- */
#define A_BUFFER_PRINT   0x50c754   /* (vp, str, x, y, fg, bg) cdecl */
#define A_STR_WIDTH      0x506c40   /* (str) cdecl */
#define A_CHAR_WIDTH     0x506c20   /* (c) cdecl */
#define A_CONV_ACCENT    0x5086b0   /* (str, len) cdecl: 0x5539b0 の字を 0x5539c0 の番号へ */
#define A_CONV_DOS       0x508710   /* (str, len) cdecl: 0x5539d0 の字を 0x553a10 の字へ */
#define V_FONT           (*(u8 **)0x7f486c)
#define V_FONT_HEIGHT    (*(u8 *)0x5538a0)
#define V_CHAR_SPACE     (*(int *)0x7f4864)
#define V_LINE_SPACE     (*(int *)0x7f4868)

static char g_dir[MAX_PATH];
static FILE *g_log;
static int g_logon;

static void LOG(const char *fmt, ...)
{
    va_list ap;
    if (!g_logon) return;
    if (!g_log) {
        char p[MAX_PATH];
        wsprintfA(p, "%s\\log.txt", g_dir);
        g_log = fopen(p, "w");
        if (!g_log) { g_logon = 0; return; }
    }
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void put_bytes(u32 addr, const void *src, int n)
{
    DWORD old;
    VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)addr, src, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
}

static void put_jmp(u32 from, void *to)
{
    u8 b[5];
    b[0] = 0xE9;
    *(u32 *)(b + 1) = (u32)to - (from + 5);
    put_bytes(from, b, 5);
}

/* 先頭 n バイトを写して元へ戻る踏み台（写す命令に相対番地が無いこと） */
static void *make_tramp(u32 addr, int n)
{
    u8 *t = (u8 *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(t, (void *)addr, n);
    t[n] = 0xE9;
    *(u32 *)(t + n + 1) = (addr + n) - ((u32)t + n + 5);
    return t;
}

/* ===================== 字体 ===================== */
typedef struct { int w, h, bpr; const u8 *data; } JFont;
static JFont g_jf[4];
static int g_njf;
static u8 *g_fontbin;

static int load_font(void)
{
    char p[MAX_PATH];
    HANDLE h;
    DWORD sz, rd;
    int i;
    wsprintfA(p, "%s\\JPFONT.BIN", g_dir);
    h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(h, NULL);
    g_fontbin = (u8 *)malloc(sz);
    if (!g_fontbin || !ReadFile(h, g_fontbin, sz, &rd, NULL) || rd != sz || memcmp(g_fontbin, "L3JF", 4)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    g_njf = *(u32 *)(g_fontbin + 4);
    if (g_njf > 4) g_njf = 4;
    for (i = 0; i < g_njf; i++) {
        u8 *e = g_fontbin + 8 + 8 * i;
        g_jf[i].w = e[0]; g_jf[i].h = e[1]; g_jf[i].bpr = e[2];
        g_jf[i].data = g_fontbin + *(u32 *)(e + 4);
    }
    return g_njf > 0;
}

/* 今の字体の高さに合う日本語の字体 */
static const JFont *cur_jfont(void)
{
    int fh = V_FONT ? V_FONT_HEIGHT : 9, i, best = 0;
    for (i = 0; i < g_njf; i++)
        if (g_jf[i].h <= (fh < 9 ? 9 : fh)) best = i;   /* 高さ以下で一番大きいもの（小さい字体でも 8 は使う） */
    return &g_jf[best];
}

#define IS_LEAD(c) ((u8)(c) >= 0xA1 && (u8)(c) <= 0xFE)

static int has_jp(const char *s)
{
    for (; *s; s++) if (IS_LEAD(*s)) return 1;
    return 0;
}

/* 日本語の字 1 字の送り幅 */
static int jp_adv(const JFont *f) { return f->w + V_CHAR_SPACE; }

/* 日本語に挟まれた空白か（s[i] == ' '） */
static int zw_space(const u8 *s, int i, int start)
{
    return i > start && i >= 1 && (u8)s[i - 1] >= 0xA1 && IS_LEAD(s[i + 1]);
}

/* ===================== 幅 ===================== */
typedef int (__cdecl *print_fn)(void *, const char *, int, int, int, int);
typedef int (__cdecl *strw_fn)(const char *);
typedef int (__cdecl *cw_fn)(int);
static print_fn o_print;
static strw_fn o_strw;
#define CHAR_W(c) (((cw_fn)A_CHAR_WIDTH)((u8)(c)))

/* 折り返しの処理は 1 バイトだけの文字列で 1 字ずつ幅を測る。
   日本語の 1 バイト目は 0、2 バイト目で 1 字分を返すので、どこで切られても字の途中では切れない。
   日本語の直後の空白（折り返し用）は 0。 */
static int g_half, g_after_jp;

static int __cdecl my_strw(const char *str)
{
    const u8 *s = (const u8 *)str;
    const JFont *f;
    int i, w = 0, mx = 0, a;
    if (!s || !g_njf) return o_strw(str);
    if (s[0] && !s[1]) {
        u8 c = s[0];
        if (IS_LEAD(c)) {
            if (!g_half) { g_half = 1; return 0; }
            g_half = 0; g_after_jp = 1;
            return jp_adv(cur_jfont());
        }
        g_half = 0;
        if (c == ' ' && g_after_jp) return 0;
        g_after_jp = 0;
        return o_strw(str);
    }
    g_half = 0; g_after_jp = 0;
    if (!has_jp(str)) return o_strw(str);
    f = cur_jfont();
    a = jp_adv(f);
    for (i = 0; s[i]; i++) {
        u8 c = s[i];
        if (c == '\r') { if (w > mx) mx = w; w = 0; continue; }
        if (IS_LEAD(c)) {
            if (IS_LEAD(s[i + 1])) { w += a; i++; }
            else w += a;              /* 途中で切れた 1 バイト目（続きは次の呼び出しで描く） */
            continue;
        }
        if (c == ' ' && zw_space(s, i, 0)) continue;
        w += CHAR_W(c);
    }
    if (w > mx) mx = w;
    return mx;
}

/* ===================== 描画 ===================== */
typedef struct { u8 *off; int width, height, xadd, xpos, ypos; void *gb; int pitch; } VP;
static int font_main_color(void);

static void draw_glyph(VP *vp, const JFont *f, int hi, int lo, int x, int y, int fg, int bg)
{
    int mc = font_main_color();
    int idx = (hi - 0xA1) * 94 + (lo - 0xA1), r, c;
    int stride = vp->width + vp->xadd + vp->pitch;
    const u8 *g = f->data + idx * f->h * f->bpr;
    int fh = V_FONT_HEIGHT, top = 0;
    if (fh > f->h) top = (fh - f->h) / 2;
    for (r = 0; r < f->h; r++) {
        int yy = y + top + r;
        u8 *row;
        if (yy < 0 || yy >= vp->height) continue;
        row = vp->off + yy * stride;
        for (c = 0; c < f->w; c++) {
            int xx = x + c;
            int on = (g[r * f->bpr + (c >> 3)] >> (7 - (c & 7))) & 1;
            if (xx < 0 || xx >= vp->width) continue;
            if (on) row[xx] = mc == 1 ? (u8)fg : ((u8 *)0x553b58)[mc];
            else if (bg) row[xx] = (u8)bg;
        }
    }
}

/* 字体の点に一番多く使われている色の番号（1 なら前景色そのもの） */
static u8 *g_cf_font;
static int g_cf_main;
static int font_main_color(void)
{
    u8 *F = V_FONT;
    int cnt[16] = { 0 }, ch, k, best = 1;
    if (!F) return 1;
    if (F == g_cf_font) return g_cf_main;
    {
        u16 offt = *(u16 *)(F + 6), wt = *(u16 *)(F + 8), ht = *(u16 *)(F + 0xc);
        for (ch = 'A'; ch <= 'Z'; ch++) {
            u16 go = *(u16 *)(F + offt + 2 * ch);
            int w = F[wt + ch], h = F[ht + 2 * ch + 1], bpr = (w + 1) / 2;
            for (k = 0; k < bpr * h && k < 512; k++) {
                u8 b = F[go + k];
                cnt[b & 15]++; cnt[b >> 4]++;
            }
        }
    }
    for (k = 2; k < 16; k++) if (cnt[k] > cnt[best]) best = k;
    g_cf_font = F; g_cf_main = best;
    LOG("字体 %p の主な色 %d", F, best);
    return best;
}

/* 1 バイト目で途切れた文字の持ち越し */
static u8 g_pend;
static void *g_pend_vp;
static int g_pend_x, g_pend_y;

static int __cdecl my_print(void *vpv, const char *str, int x, int y, int fg, int bg)
{
    VP *vp = (VP *)vpv;
    const u8 *s = (const u8 *)str;
    const JFont *f;
    int i = 0, cx = x, fh, ret = 0;
    char tmp[1024];
    if (!s || !g_njf) return o_print(vpv, str, x, y, fg, bg);
    /* 持ち越しの続き */
#ifdef LOL3JP_TEST
    if (s[0] && !s[1]) { static int nl1; if (nl1 < 600) { nl1++; LOG("1字 %02x ←%p ←%08x", s[0], _ReturnAddress(), *(u32 *)((char *)_AddressOfReturnAddress() + 36)); } }
#endif
    if (g_pend) {
        u8 p = g_pend;
        g_pend = 0;
        if (g_pend_vp == vpv && g_pend_y == y && IS_LEAD(s[0]) && x >= g_pend_x - 2 && x <= g_pend_x + 24) {
            f = cur_jfont();
            draw_glyph(vp, f, p, s[0], g_pend_x, y, fg, bg);
            cx = x = g_pend_x + jp_adv(f);
            s++;
            if (!*s) return 1;
        }
    }
    if (!has_jp((const char *)s)) return o_print(vpv, (const char *)s, x, y, fg, bg);
#ifdef LOL3JP_TEST
    {   /* 試験: 描く文字列を一度ずつ記録 */
        static u32 seen[512]; static int nseen;
        u32 h = 2166136261u; int k; const u8 *q = s; char hx[400]; int n = 0;
        for (; *q; q++) h = (h ^ *q) * 16777619u;
        for (k = 0; k < nseen && seen[k] != h; k++);
        if (k == nseen && nseen < 512) {
            seen[nseen++] = h;
            for (q = s; *q && n < 390; q++) n += wsprintfA(hx + n, "%02x", *q);
            LOG("描く %s ←%p", hx, _ReturnAddress());
        }
    }
#endif
    f = cur_jfont();
    fh = V_FONT_HEIGHT;
    while (s[i]) {
        u8 c = s[i];
        if (c == '\r' || c == '\n') {
            y += fh + V_LINE_SPACE;
            if (y + fh > vp->height) return 0;
            cx = (c == '\n') ? 0 : x;
            i++;
            continue;
        }
        if (IS_LEAD(c)) {
            if (!s[i + 1]) {             /* 途切れ: 次の呼び出しへ */
                g_pend = c; g_pend_vp = vpv; g_pend_x = cx; g_pend_y = y;
                return 1;
            }
            if (IS_LEAD(s[i + 1])) {
                if (cx + f->w > vp->width) return 0;
                draw_glyph(vp, f, c, s[i + 1], cx, y, fg, bg);
                cx += jp_adv(f);
                ret = 1;
            }
            i += 2;
            continue;
        }
        if (c == ' ' && zw_space(s, i, 0)) { i++; continue; }
        /* 英字などの続き: 元の関数で描く */
        {
            int n = 0;
            while (s[i + n] && s[i + n] != '\r' && s[i + n] != '\n' && !IS_LEAD(s[i + n]) &&
                   !(s[i + n] == ' ' && zw_space(s, i + n, 0)) && n < (int)sizeof tmp - 1)
                n++;
            memcpy(tmp, s + i, n);
            tmp[n] = 0;
            ret = o_print(vpv, tmp, cx, y, fg, bg);
            cx += o_strw(tmp);
            i += n;
        }
    }
    return ret ? ret : 1;
}

/* ===================== 訳した TRR の組み立てと振り向け =====================
   LOL3JP\LOL3_JA.DAT（日本語と英文の指紋）と、ゲームの DAT\locallng.wsx の元の TRR から、
   訳した TRR を組み立てて書き出し、ゲームが開くときにそちらへ振り向ける。 */
#define MAXF 32
/* 日本語の DTEXT（字幕に使う。build_trr が残す） */
static u8 *g_dt_buf;
static u32 g_dt_n;
static u32 *g_dt_off;     /* 各行の位置（g_dt_buf の先頭から） */
static char g_trr_name[MAXF][16];
static char g_trr_path[MAXF][MAX_PATH];
static int g_ntrr;
static char g_game[MAX_PATH];

static u8 *read_all(const char *path, DWORD *size)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, rd;
    u8 *b;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    b = (u8 *)malloc(sz + 1);
    if (!b || !ReadFile(h, b, sz, &rd, NULL) || rd != sz) { CloseHandle(h); free(b); return NULL; }
    CloseHandle(h);
    if (size) *size = sz;
    return b;
}

static u32 fnv(const u8 *p, int n)
{
    u32 h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

/* 書庫から 1 項目を読む */
static u8 *wsx_get(HANDLE h, u32 id, DWORD *size)
{
    u8 hd[6];
    DWORD rd;
    u16 cnt;
    u8 *ix, *b = NULL;
    int i;
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    if (!ReadFile(h, hd, 6, &rd, NULL) || rd != 6) return NULL;
    cnt = *(u16 *)hd;
    ix = (u8 *)malloc(cnt * 12);
    if (!ix || !ReadFile(h, ix, cnt * 12, &rd, NULL) || rd != (DWORD)cnt * 12) { free(ix); return NULL; }
    for (i = 0; i < cnt; i++) {
        u32 *e = (u32 *)(ix + i * 12);
        if (e[0] == id) {
            b = (u8 *)malloc(e[2] + 1);
            SetFilePointer(h, 6 + cnt * 12 + e[1], NULL, FILE_BEGIN);
            if (!b || !ReadFile(h, b, e[2], &rd, NULL) || rd != e[2]) { free(b); b = NULL; }
            else *size = e[2];
            break;
        }
    }
    free(ix);
    return b;
}

static int write_file(const char *path, const u8 *b, DWORD n)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD wr;
    int ok;
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(h, b, n, &wr, NULL) && wr == n;
    CloseHandle(h);
    return ok;
}

/* 書き出し先: LOL3JP\cache、だめなら %LOCALAPPDATA%\LOL3JP */
static int cache_dir(char *out)
{
    char t[MAX_PATH], q[MAX_PATH];
    int k;
    for (k = 0; k < 2; k++) {
        if (k == 0) wsprintfA(out, "%s\\cache", g_dir);
        else {
            if (!GetEnvironmentVariableA("LOCALAPPDATA", t, MAX_PATH)) return 0;
            wsprintfA(out, "%s\\LOL3JP", t);
        }
        CreateDirectoryA(out, NULL);
        wsprintfA(q, "%s\\_test.tmp", out);
        if (write_file(q, (const u8 *)"x", 1)) { DeleteFileA(q); return 1; }
    }
    return 0;
}

static void build_trr(void)
{
    char p[MAX_PATH], cd[MAX_PATH];
    DWORD dsz, osz;
    u8 *dat, *q, *end;
    HANDLE wh;
    u32 nfile, f;
    int hit = 0, miss = 0;
    wsprintfA(p, "%s\\LOL3_JA.DAT", g_dir);
    dat = read_all(p, &dsz);
    if (!dat || dsz < 12 || memcmp(dat, "L3JD", 4)) { LOG("LOL3_JA.DAT が読めない"); free(dat); return; }
    wsprintfA(p, "%s\\DAT\\locallng.wsx", g_game);
    wh = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (wh == INVALID_HANDLE_VALUE) {
        wsprintfA(p, "%s\\locallng.wsx", g_game);
        wh = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    }
    if (wh == INVALID_HANDLE_VALUE) { LOG("locallng.wsx が開けない"); free(dat); return; }
    if (!cache_dir(cd)) { LOG("書き出し先が無い"); CloseHandle(wh); free(dat); return; }
    nfile = *(u32 *)(dat + 8);
    q = dat + 12; end = dat + dsz;
    for (f = 0; f < nfile && q + 24 <= end; f++) {
        char name[17];
        u32 wid = *(u32 *)(q + 16), cnt = *(u32 *)(q + 20), n, k;
        u8 *org, *outb, *body;
        u32 *ids, *offs;
        u8 **str; int *len;
        DWORD olen = 0;
        memcpy(name, q, 16); name[16] = 0;
        q += 24;
        org = wsx_get(wh, wid, &osz);
        if (!org) {   /* 元が無い: 訳を飛ばす */
            for (k = 0; k < cnt && q + 10 <= end; k++) q += 10 + *(u16 *)(q + 8);
            continue;
        }
        n = *(u32 *)org;
        ids = (u32 *)(org + 4); offs = (u32 *)(org + 4 + 4 * n);
        str = (u8 **)calloc(n, sizeof(u8 *)); len = (int *)calloc(n, sizeof(int));
        for (k = 0; k < n; k++) {
            str[k] = org + 4 + offs[k];
            len[k] = lstrlenA((char *)str[k]);
        }
        for (k = 0; k < cnt && q + 10 <= end; k++) {
            u32 idx = *(u32 *)q, h = *(u32 *)(q + 4);
            u16 l = *(u16 *)(q + 8);
            if (idx < n && fnv(str[idx], len[idx]) == h) { str[idx] = q + 10; len[idx] = -l - 1; hit++; }
            else miss++;
            q += 10 + l;
        }
        /* 組み立て: 件数、番号、位置（先頭+4 から）、全体-4、文 */
        for (k = 0; k < n; k++) olen += (len[k] < 0 ? -len[k] - 1 : len[k]) + 1;
        olen += 4 + 8 * n + 4;
        outb = (u8 *)malloc(olen);
        *(u32 *)outb = n;
        memcpy(outb + 4, ids, 4 * n);
        *(u32 *)(outb + 4 + 8 * n) = olen - 4;
        body = outb + 4 + 8 * n + 4;
        for (k = 0; k < n; k++) {
            int l = len[k] < 0 ? -len[k] - 1 : len[k];
            *(u32 *)(outb + 4 + 4 * n + 4 * k) = (u32)(body - outb) - 4;
            memcpy(body, str[k], l); body[l] = 0;
            body += l + 1;
        }
        wsprintfA(p, "%s\\%s", cd, name);
        if (write_file(p, outb, olen) && g_ntrr < MAXF) {
            lstrcpyA(g_trr_name[g_ntrr], name);
            lstrcpyA(g_trr_path[g_ntrr], p);
            g_ntrr++;
        }
        if (!lstrcmpiA(name, "DTEXT.TRR") && !g_dt_buf) {
            /* 字幕のために日本語の会話の文を残す */
            g_dt_buf = outb; g_dt_n = n;
            g_dt_off = (u32 *)malloc(4 * n);
            for (k = 0; k < n; k++) g_dt_off[k] = 4 + *(u32 *)(outb + 4 + 4 * n + 4 * k);
        } else free(outb);
        free(str); free(len); free(org);
    }
    CloseHandle(wh);
    free(dat);
    LOG("訳の組み立て %d ファイル、当たり %d、外れ %d（書き出し先 %s）", g_ntrr, hit, miss, cd);
}

typedef HANDLE (WINAPI *cfa_fn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static cfa_fn o_cfa;
static HANDLE WINAPI my_cfa(LPCSTR n, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t)
{
    if (n && c == OPEN_EXISTING && !(a & GENERIC_WRITE)) {
        const char *b = strrchr(n, '\\'), *b2 = strrchr(n, '/');
        int i;
        if (b2 > b) b = b2;
        b = b ? b + 1 : n;
        for (i = 0; i < g_ntrr; i++) {
            if (!lstrcmpiA(b, g_trr_name[i])) {
                HANDLE h = o_cfa(g_trr_path[i], a, s, sa, c, f, t);
                if (h != INVALID_HANDLE_VALUE) {
                    LOG("訳を読む %s", g_trr_name[i]);
                    return h;
                }
            }
        }
    }
    return o_cfa(n, a, s, sa, c, f, t);
}

/* ===================== IAT の差し替え ===================== */
static void *patch_iat(HMODULE m, const char *dll, const char *fn, void *to)
{
    u8 *base = (u8 *)m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[1].VirtualAddress);
    for (; d->Name; d++) {
        IMAGE_THUNK_DATA *on, *ft;
        if (lstrcmpiA((char *)(base + d->Name), dll)) continue;
        on = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        ft = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; on->u1.AddressOfData; on++, ft++) {
            IMAGE_IMPORT_BY_NAME *ib;
            if (on->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            ib = (IMAGE_IMPORT_BY_NAME *)(base + on->u1.AddressOfData);
            if (!lstrcmpA((char *)ib->Name, fn)) {
                void *old = (void *)ft->u1.Function;
                DWORD op;
                VirtualProtect(&ft->u1.Function, 4, PAGE_READWRITE, &op);
                ft->u1.Function = (u32)to;
                VirtualProtect(&ft->u1.Function, 4, op, &op);
                return old;
            }
        }
    }
    return NULL;
}

#include "subs.inc"

#ifdef LOL3JP_TEST
#include "testctl.inc"
#include "drawtest.inc"
#endif

typedef FARPROC (WINAPI *gpa_fn)(HMODULE, LPCSTR);
static gpa_fn o_gpa;
static FARPROC WINAPI my_gpa(HMODULE m, LPCSTR n)
{
    FARPROC p = o_gpa(m, n);
    if ((u32)n > 0xffff && p) {
#ifdef LOL3JP_TEST
        p = glide_wrap(n, p);   /* 字幕を描いた後の画面を撮る */
#endif
        p = sub_glide_wrap(n, p);
    }
    return p;
}

/* ゲームは文章を読み込んだ後、仏独の字（Ï á æ ï õ ø ù ü ý など）を字体の番号へその場で置き換える。
   日本語の 2 バイトに当たると別の字に化けるので、日本語の組は飛ばして同じ置き換えをする */
static void conv_skip_jp(u8 *s, int n, const u8 *from, const u8 *to)
{
    int i, k, m = lstrlenA((const char *)from);
    for (i = 0; i < n && s[i]; i++) {
        if (IS_LEAD(s[i]) && i + 1 < n && IS_LEAD(s[i + 1])) { i++; continue; }
        for (k = 0; k < m; k++) if (s[i] == from[k]) { s[i] = to[k]; break; }
    }
}
static void __cdecl my_conv_accent(u8 *s, int n) { if (s && n) conv_skip_jp(s, n, (const u8 *)0x5539b0, (const u8 *)0x5539c0); }
static void __cdecl my_conv_dos(u8 *s, int n) { if (s && n) conv_skip_jp(s, n, (const u8 *)0x5539d0, (const u8 *)0x553a10); }

static void install(HINSTANCE h)
{
    char exe[MAX_PATH], ini[MAX_PATH], *p;
    HMODULE m = GetModuleHandleA(NULL);
    static const u8 sig_print[6] = { 0xc8, 0x40, 0x00, 0x00, 0x53, 0x51 };
    static const u8 sig_strw[6] = { 0x53, 0x57, 0x8b, 0x7c, 0x24, 0x0c };
    static const u8 sig_cw[4] = { 0x8b, 0x44, 0x24, 0x04 };
    GetModuleFileNameA(h, g_dir, MAX_PATH);
    p = strrchr(g_dir, '\\'); if (p) *p = 0;
    lstrcatA(g_dir, "\\LOL3JP");
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    p = strrchr(exe, '\\'); p = p ? p + 1 : exe;
    if (lstrcmpiA(p, "LOL3.DAT")) return;
    wsprintfA(ini, "%s\\lol3jp.ini", g_dir);
    if (!GetPrivateProfileIntA("LOL3JP", "Enable", 1, ini)) return;
    g_logon = GetPrivateProfileIntA("LOL3JP", "Log", 0, ini);
#ifdef LOL3JP_TEST
    g_logon = 1;
#endif
    if (memcmp((void *)A_BUFFER_PRINT, sig_print, 6) || memcmp((void *)A_STR_WIDTH, sig_strw, 6) ||
        memcmp((void *)A_CHAR_WIDTH, sig_cw, 4)) {
        LOG("本体が違うので何もしない");
        return;
    }
    if (!load_font()) { LOG("JPFONT.BIN が読めない"); return; }
    lstrcpyA(g_game, g_dir);
    p = strrchr(g_game, '\\'); if (p) *p = 0;
    build_trr();
    g_sub_on = GetPrivateProfileIntA("LOL3JP", "Subtitles", 1, ini);
    subs_install(m);
    o_print = (print_fn)make_tramp(A_BUFFER_PRINT, 6);
    o_strw = (strw_fn)make_tramp(A_STR_WIDTH, 6);
    put_jmp(A_BUFFER_PRINT, my_print);
    put_jmp(A_STR_WIDTH, my_strw);
    {
        static const u8 sig_conv[12] = { 0x8b, 0x44, 0x24, 0x08, 0x85, 0xc0, 0x74, 0x51, 0x57, 0x56, 0x8b, 0x74 };
        static const u8 tbl_acc[10] = { 0xcf, 0xe1, 0xe6, 0xef, 0xf5, 0xf8, 0xf9, 0xfc, 0xfd, 0 };
        if (!memcmp((void *)A_CONV_ACCENT, sig_conv, 12) && !memcmp((void *)0x5539b0, tbl_acc, 10)) put_jmp(A_CONV_ACCENT, my_conv_accent);
        else LOG("字の置き換え(1)が見つからない");
        if (!memcmp((void *)A_CONV_DOS, sig_conv, 12) && *(u8 *)0x5539d0 == 0x80) put_jmp(A_CONV_DOS, my_conv_dos);
        else LOG("字の置き換え(2)が見つからない");
    }
    o_cfa = (cfa_fn)patch_iat(m, "KERNEL32.dll", "CreateFileA", my_cfa);
    o_gpa = (gpa_fn)patch_iat(m, "KERNEL32.dll", "GetProcAddress", my_gpa);
    LOG("取り付け完了 字体 %d 種、訳 %d ファイル", g_njf, g_ntrr);
#ifdef LOL3JP_TEST
    {
        char t[2 * MAX_PATH];
        if (GetEnvironmentVariableA("LOL3JP_DRAWTEST", t, sizeof t)) {
            draw_test(t);
            ExitProcess(0);
        }
        if (GetEnvironmentVariableA("LOL3JP_SUBTEST", t, sizeof t)) {
            sub_test(t);
            ExitProcess(0);
        }
    }
    ctl_start(m);
#endif
}

/* ===================== winmm の転送 ===================== */
#include "winmm_exports.h"
#define DECL(n) static FARPROC p_##n;
WINMM_FUNCS(DECL)
static void load_winmm(void)
{
    char path[MAX_PATH];
    HMODULE w;
    GetSystemDirectoryA(path, MAX_PATH);
    lstrcatA(path, "\\winmm.dll");
    w = LoadLibraryA(path);
    if (!w) return;
#define GETP(n) p_##n = GetProcAddress(w, #n);
    WINMM_FUNCS(GETP)
}
#define THUNK(n) __declspec(naked) void ex_##n(void) { __asm { jmp dword ptr [p_##n] } }
WINMM_FUNCS(THUNK)

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        load_winmm();
        install(h);
    }
    return TRUE;
}
