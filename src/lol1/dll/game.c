/* ゲーム（LANDS / MAIN / FINALE）を見つけて書き換え、文字を描く */
#include "loljp.h"
#include "patterns.h"
#include "kinsoku.h"
#include <string.h>

typedef struct {
    int used;
    uint32_t lo, hi;                 /* 線形番地の範囲 */
    char name[16];
    uint32_t textprint, charwidth, undip, brk, strwidth;
    uint16_t fontptr, xsp, ysp, logicpage, pagetbl, widthptr, cwxsp, linebuf;
    uint32_t xlat;                   /* Draw_Char の CS の先頭（表は +2） */
} Prog;

#define MAXPROG 4
static Prog g_prog[MAXPROG];
static int cbTP, cbCW, cbUD, cbBR, cbSW;
int g_trace;

/* ---------- 補助 ---------- */
static Prog *FindProg(void)
{
    uint32_t ip = segbase(S_CS) + (uint16_t)*(volatile uint32_t *)0x7551E0;
    int i;
    for (i = 0; i < MAXPROG; i++)
        if (g_prog[i].used && ip >= g_prog[i].lo && ip < g_prog[i].hi) return &g_prog[i];
    return NULL;
}

static int Match(uint32_t a, const Pattern *p)
{
    const uint8_t *m = lin(a);
    int i;
    for (i = 0; i < p->len; i++)
        if ((m[i] & p->m[i]) != (p->b[i] & p->m[i])) return 0;
    return 1;
}

static uint32_t Scan(uint32_t lo, uint32_t hi, const Pattern *p, int *count)
{
    uint32_t a, hit = 0;
    *count = 0;
    for (a = lo; a + p->len <= hi; a++) {
        if (rd8(a) != p->b[0]) continue;
        if (Match(a, p)) { if (!*count) hit = a; (*count)++; }
    }
    return hit;
}

static void PutCB(uint32_t a, int cb)
{
    wr8(a, 0xFE); wr8(a + 1, 0x38); wr16(a + 2, (uint16_t)cb);
}

/* ---------- 文字を描く ---------- */
typedef struct {
    uint32_t font, dest, xlat;
    int info, offs, widths, hts, fh, xsp, ysp;
} Ctx;

static int SetupCtx(Prog *p, Ctx *c)
{
    uint32_t ds = segbase(S_DS);
    uint16_t page, seg;
    c->font = farptr(ds + p->fontptr);
    if (!c->font) return 0;
    c->info = rd16(c->font + 4);
    c->offs = rd16(c->font + 6);
    c->widths = rd16(c->font + 8);
    c->hts = rd16(c->font + 0xC);
    c->fh = rd8(c->font + c->info + 4);
    c->xsp = (int16_t)rd16(ds + p->xsp);
    c->ysp = (int16_t)rd16(ds + p->ysp);
    page = rd16(ds + p->logicpage);
    seg = rd16(ds + p->pagetbl + ((page & 0xE) << 1));
    c->dest = (uint32_t)seg << 4;
    c->xlat = p->xlat + 2;
    return 1;
}

static void DrawChar(Ctx *c, uint8_t ch, int x, int y)
{
    uint16_t o = rd16(c->font + c->offs + ch * 2);
    int cw, yo, gh, tr, r, k;
    uint8_t bgc = rd8(c->xlat);
    uint32_t d, s;
    if (!o) return;
    cw = rd8(c->font + c->widths + ch);
    if (x >= 320 || x + cw > 320) return;
    if (y >= 200 || y + c->fh > 200) return;
    yo = rd8(c->font + c->hts + ch * 2);
    gh = rd8(c->font + c->hts + ch * 2 + 1);
    tr = c->fh - yo - gh;
    d = c->dest + y * 320 + x;
    s = c->font + o;
    for (r = 0; r < yo; r++, d += 320)
        if (bgc) for (k = 0; k < cw; k++) vwr8(d + k, bgc);
    for (r = 0; r < gh; r++, d += 320) {
        for (k = 0; k < cw; k++) {
            uint8_t b = rd8(s), col;
            if (!(k & 1)) col = rd8(c->xlat + (b & 0x0F));
            else { col = rd8(c->xlat + (b & 0xF0)); s++; }
            if (col) vwr8(d + k, col);
        }
        if (cw & 1) s++;
    }
    for (r = 0; r < tr; r++, d += 320)
        if (bgc) for (k = 0; k < cw; k++) vwr8(d + k, bgc);
}

static void DrawJp(Ctx *c, const uint8_t *g, int x, int y)
{
    uint8_t bgc = rd8(c->xlat), fgc = rd8(c->xlat + 1);
    int h = c->fh < 8 ? 8 : c->fh;
    int off = c->fh >= 9 ? (c->fh - 8 + 1) / 2 : 0;
    int r, k;
    if (x + 8 > 320) return;
    for (r = 0; r < h; r++) {
        int yy = y + r, gr = r - off;
        uint32_t d;
        if (yy >= 200) break;
        if (r >= c->fh && (gr < 0 || gr > 6)) continue;
        d = c->dest + yy * 320 + x;
        for (k = 0; k < 8; k++) {
            int on = (gr >= 0 && gr < 8) && (g[gr] & (0x80 >> k));
            if (on) { if (fgc) vwr8(d + k, fgc); }
            else if (bgc && r < c->fh) vwr8(d + k, bgc);
        }
    }
}

/* 案内の文字列を host に写す（描く直前の訳も当てる） */
#define MAXDRAW 1024
static const uint8_t *GetDrawString(uint32_t str, uint8_t *buf)
{
    int n = 0;
    const char *ja;
    while (n < MAXDRAW - 1 && (buf[n] = rd8(str + n)) != 0) n++;
    buf[n] = 0;
    if (n && buf[0] < 0x80 && (ja = Text_Find((const char *)buf, n)) != NULL) return (const uint8_t *)ja;
    return buf;
}

/* 文を細切れに描くとき、2 バイト文字の 1 バイト目だけで切れた分を次の切れ端へ持ち越す。
   ゲームは 1 バイト目も 2 バイト目も幅 4 と数えるので、次の切れ端は 4 だけ右から始まる。 */
static struct { int valid, x, y; uint8_t lead; } g_pend;

static void TextPrint(Prog *p, const uint8_t *str, int x, int y, uint8_t fg, uint8_t bg)
{
    Ctx c;
    int x0, pv = g_pend.valid;
    g_pend.valid = 0;
    if (!SetupCtx(p, &c)) return;
    if (x < 0) x = 0;
    if (x >= 320) return;
    if (y < 0) y = 0;
    if (y >= 200) return;
    x0 = x;
    if (g_trace) {
        char h[200]; int i;
        for (i = 0; i < 60 && str[i]; i++) wsprintfA(h + i * 3, "%02X ", str[i]);
        h[i * 3] = 0;
        LogF("TP %d,%d fg%d bg%d fh=%d [%s]", x, y, fg, bg, c.fh, h);
    }
    /* Set_Font_Palette_Range(bg,fg, 0, 1) と同じ */
    wr8(c.xlat + 0, bg);
    wr8(c.xlat + 1, fg);
    wr8(c.xlat + 16, fg);
    if (pv && x == g_pend.x && y == g_pend.y && str[0] >= 0xA1) {
        const uint8_t *g = Font_Glyph(g_pend.lead, str[0]);
        if (g) DrawJp(&c, g, x - 4, y);
        str++; x += 4;
    } else if (pv && g_trace) LogF("持ち越し不一致 %d,%d 予定 %d,%d", x, y, g_pend.x, g_pend.y);
    for (;;) {
        uint8_t ch = *str, ch2;
        int w;
        const uint8_t *g = NULL;
        if (!ch) break;
        if (ch == 0x0D) { x = x0; y += c.fh + c.ysp; str++; continue; }
        ch2 = str[1];
        if (ch >= 0xA1 && !ch2) {
            g_pend.valid = 1; g_pend.lead = ch; g_pend.x = x + 4; g_pend.y = y;
            break;
        }
        if (ch >= 0xA1 && ch2 >= 0xA1) { g = Font_Glyph(ch, ch2); w = 8; }
        else w = rd8(c.font + c.widths + ch) + c.xsp;
        if (x + w > 320) { x = x0; y += c.fh + c.ysp; }
        if (y >= 200) break;
        if (g) { DrawJp(&c, g, x, y); str += 2; }
        else { DrawChar(&c, ch, x, y); str++; }
        x += w;
    }
}

/* ---------- コールバック ---------- */
static uint32_t CB_TextPrint(void)
{
    Prog *p = FindProg();
    uint32_t ss = segbase(S_SS), bp = reg16(R_BP);
    if (p) {
        static uint8_t buf[MAXDRAW];
        uint32_t str = ((uint32_t)rd16(ss + bp + 8) << 4) + rd16(ss + bp + 6);
        TextPrint(p, GetDrawString(str, buf), (int16_t)rd16(ss + bp + 0xA), (int16_t)rd16(ss + bp + 0xC),
                  rd8(ss + bp + 0xE), rd8(ss + bp + 0x10));
    }
    return 0;
}

static uint32_t CB_CharWidth(void)
{
    Prog *p = FindProg();
    uint32_t ss = segbase(S_SS), sp = reg16(R_SP), ds = segbase(S_DS);
    uint8_t ch = rd8(ss + sp + 4);
    uint16_t w = 0;
    if (ch >= 0xA1) w = 4;
    else if (p) w = (uint16_t)(rd8(farptr(ds + p->widthptr) + ch) + rd16(ds + p->cwxsp));
    set16(R_AX, w);
    return 0;
}

static uint32_t CB_StrWidth(void)
{
    static uint8_t buf[MAXDRAW];
    Prog *p = FindProg();
    uint32_t ss = segbase(S_SS), sp = reg16(R_SP), ds = segbase(S_DS);
    uint32_t str = farptr(ss + sp + 4), wt;
    const uint8_t *s;
    int w = 0, best = 0, xsp;
    if (!p || !str) { set16(R_AX, 0); return 0; }
    s = GetDrawString(str, buf);
    wt = farptr(ds + p->widthptr);
    xsp = (int16_t)rd16(ds + p->cwxsp);
    while (*s) {
        if (*s == 0x0D) { if (w > best) best = w; w = 0; s++; continue; }
        if (s[0] >= 0xA1 && s[1] >= 0xA1) { w += 8; s += 2; continue; }
        if (*s >= 0xA1) { w += 4; s++; continue; }
        w += rd8(wt + *s) + xsp;
        s++;
    }
    if (w > best) best = w;
    set16(R_AX, (uint16_t)best);
    return 0;
}

static const uint8_t T1[16] = {0x20,0x65,0x74,0x61,0x69,0x6E,0x6F,0x73,0x72,0x6C,0x68,0x63,0x64,0x75,0x70,0x6D};
static const uint8_t T2[128] = {
    0x74,0x61,0x73,0x69,0x6F,0x20,0x77,0x62,0x20,0x72,0x6E,0x73,0x64,0x61,0x6C,0x6D,0x68,0x20,0x69,0x65,0x6F,0x72,
    0x61,0x73,0x6E,0x72,0x74,0x6C,0x63,0x20,0x73,0x79,0x6E,0x73,0x74,0x63,0x6C,0x6F,0x65,0x72,0x20,0x64,0x74,0x67,
    0x65,0x73,0x69,0x6F,0x6E,0x72,0x20,0x75,0x66,0x6D,0x73,0x77,0x20,0x74,0x65,0x70,0x2E,0x69,0x63,0x61,0x65,0x20,
    0x6F,0x69,0x61,0x64,0x75,0x72,0x20,0x6C,0x61,0x65,0x69,0x79,0x6F,0x64,0x65,0x69,0x61,0x20,0x6F,0x74,0x72,0x75,
    0x65,0x74,0x6F,0x61,0x6B,0x68,0x6C,0x72,0x20,0x65,0x69,0x75,0x2C,0x2E,0x6F,0x61,0x6E,0x73,0x72,0x63,0x74,0x6C,
    0x61,0x69,0x6C,0x65,0x6F,0x69,0x72,0x61,0x74,0x70,0x65,0x61,0x6F,0x69,0x70,0x20,0x62,0x6D};

#define MAXSTR 2048
static uint32_t CB_UnDip(void)
{
    static char e1[MAXSTR * 2 + 4], e2[MAXSTR * 2 + 4];
    uint32_t ss = segbase(S_SS), sp = reg16(R_SP);
    uint32_t src = farptr(ss + sp + 4), dst = farptr(ss + sp + 8);
    int n = 0, m = 0, i, k;
    const char *ja;
    for (i = 0; i < MAXSTR; i++) {
        int8_t b = (int8_t)rd8(src + i);
        int v = b;               /* 元は符号拡張して 0x80 を見る */
        if (!v) break;
        if (v & 0x80) {
            v &= 0x7F;
            e1[n++] = (char)T1[(v & 0x78) >> 3];
            e1[n++] = (char)T2[v];
        } else e1[n++] = (char)v;
    }
    e1[n] = 0;
    /* Fixup_Text と同じ処理を施したものを照合に使う */
    for (k = 0; k < n; k++) {
        if (e1[k] == 0x1B && k + 1 < n) { e2[m++] = (char)(e1[k + 1] + 0x7F); k++; }
        else e2[m++] = e1[k];
    }
    e2[m] = 0;
    ja = Text_Find(e2, m);
    if (ja) {
        int len = (int)strlen(ja);
        if (len > 511) { LogF("長すぎる訳 (%d): %.40s", len, e2); len = 511; }
        for (k = 0; k < len; k++) wr8(dst + k, (uint8_t)ja[k]);
        wr8(dst + len, 0);
        set16(R_AX, (uint16_t)len);
    } else {
        for (k = 0; k < n; k++) wr8(dst + k, (uint8_t)e1[k]);
        wr8(dst + n, 0);
        set16(R_AX, (uint16_t)n);
        if (g_trace) LogF("未訳: %s", e2);
    }
    return 0;
}

static int InList(const unsigned short *l, int n, unsigned short c)
{
    int i;
    for (i = 0; i < n; i++) if (l[i] == c) return 1;
    return 0;
}

static int JpBreakOk(uint32_t b, int di)
{
    /* 直前の切れ端が 1 バイト目で終わっていれば、行の先頭は 2 バイト目 */
    int st = g_pend.valid ? 1 : 0, i = st;
    uint8_t cur, prev;
    if (di <= 0) return 0;
    while (i < di) {
        if (rd8(b + i) >= 0xA1 && rd8(b + i + 1) >= 0xA1) i += 2; else i++;
    }
    if (i != di) return 0;
    cur = rd8(b + di); prev = rd8(b + di - 1);
    if (cur < 0xA1 && prev < 0xA1) return 0;
    if (cur >= 0xA1 && InList(KIN_HEAD, sizeof KIN_HEAD / 2, (unsigned short)((cur << 8) | rd8(b + di + 1)))) return 0;
    if (cur < 0xA1 && strchr(".,!?:;)]'\"%", cur)) return 0;
    if (prev >= 0xA1 && di >= 2 && di - 2 >= st
        && InList(KIN_TAIL, sizeof KIN_TAIL / 2, (unsigned short)((rd8(b + di - 2) << 8) | prev))) return 0;
    if (prev >= 0xA1 && di == 1 && st == 1
        && InList(KIN_TAIL, sizeof KIN_TAIL / 2, (unsigned short)((g_pend.lead << 8) | prev))) return 0;
    if (prev < 0xA1 && strchr("([\"'", prev)) return 0;
    return 1;
}

static uint32_t CB_Break(void)
{
    Prog *p = FindProg();
    uint32_t ss = segbase(S_SS), bp = reg16(R_BP);
    uint8_t c = rd8(ss + ((bp - 0xB) & 0xFFFF));
    int zf = (c == 0x20);
    if (!zf && p) zf = JpBreakOk(segbase(S_DS) + p->linebuf, (int16_t)reg16(R_DI));
    if (zf) DBX_FLAGS |= FLAG_ZF; else DBX_FLAGS &= ~FLAG_ZF;
    if (g_trace && p) {
        char h[200]; int i, di = (int16_t)reg16(R_DI);
        uint32_t b = segbase(S_DS) + p->linebuf;
        for (i = 0; i < 60 && rd8(b + i); i++) wsprintfA(h + i * 3, "%02X ", rd8(b + i));
        h[i * 3] = 0;
        LogF("BR di=%d c=%02X zf=%d fit=%d [%s]", di, c, zf, (int16_t)rd16(ss + ((bp - 2) & 0xFFFF)), h);
    }
    return 0;
}

/* ---------- キャラ選び確認の Yes/No ボタン（BACKGRND.CPS に描き込まれた絵） ---------- */
#define MAXBTN 4
#define BTNMAX (64 * 16)
typedef struct { int x, y, w, h; uint8_t en[BTNMAX], ja[BTNMAX]; } Button;
static Button g_btn[MAXBTN];
static int g_nbtn, cbGC;

static void LoadButtons(void)
{
    char path[MAX_PATH];
    HANDLE f;
    DWORD got;
    uint8_t hd[6];
    int i;
    wsprintfA(path, "%s\\buttons.bin", g_dir);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { LogF("ボタンの絵がない: %s", path); return; }
    if (ReadFile(f, hd, 6, &got, NULL) && got == 6 && !memcmp(hd, "LJBT", 4)) {
        int n = hd[4] | (hd[5] << 8);
        for (i = 0; i < n && g_nbtn < MAXBTN; i++) {
            uint16_t r[4];
            Button *b = &g_btn[g_nbtn];
            if (!ReadFile(f, r, 8, &got, NULL) || got != 8) break;
            b->x = r[0]; b->y = r[1]; b->w = r[2]; b->h = r[3];
            if (b->w * b->h > BTNMAX || b->x + b->w > 320 || b->y + b->h > 200) break;
            if (!ReadFile(f, b->en, b->w * b->h, &got, NULL) || (int)got != b->w * b->h) break;
            if (!ReadFile(f, b->ja, b->w * b->h, &got, NULL) || (int)got != b->w * b->h) break;
            g_nbtn++;
        }
    }
    CloseHandle(f);
    LogF("ボタンの絵 %d 個", g_nbtn);
}

/* 画面（A0000）も読める mem_readb の代わり。DOSBox の読み出し用の頁表と、頁ごとの処理係 */
typedef uint32_t (__cdecl *ReadBFn)(void *, uint32_t);
static uint8_t vrd8(uint32_t a)
{
    uint8_t *h = ((uint8_t **)0x7556B0)[a >> 12];
    void *obj;
    if (h) return h[a];
    obj = ((void **)0xF556B0)[a >> 12];
    return (uint8_t)((ReadBFn)((*(void ***)obj)[2]))(obj, a);
}

static void ApplyButtons(void)
{
    int i, x, y, n = 0, m = 0;
    uint32_t a;
    for (i = 0; i < g_nbtn; i++) {
        Button *b = &g_btn[i];
        /* 裏の絵（通常メモリ上の 320 幅の頁）: 丸ごと一致する所を差し替える */
        for (a = 0; a + 320 * b->h <= 0xA0000; a++) {
            if (DBX_MEMBASE[a] != b->en[0] || memcmp(DBX_MEMBASE + a, b->en, b->w)) continue;
            for (y = 1; y < b->h; y++)
                if (memcmp(DBX_MEMBASE + a + y * 320, b->en + y * b->w, b->w)) break;
            if (y < b->h) continue;
            for (y = 0; y < b->h; y++) memcpy(DBX_MEMBASE + a + y * 320, b->ja + y * b->w, b->w);
            n++;
        }
        /* 表の画面: 英語の画素のままの所だけ替える（マウスの矢印の下は触らない） */
        for (y = 0; y < b->h; y++)
            for (x = 0; x < b->w; x++) {
                uint8_t e = b->en[y * b->w + x], j = b->ja[y * b->w + x];
                uint32_t v = 0xA0000 + (uint32_t)(b->y + y) * 320 + b->x + x;
                if (e != j && vrd8(v) == e) { vwr8(v, j); m++; }
            }
    }
    LogF("Yes/No ボタン: 裏 %d か所、画面 %d 画素", n, m);
}

/* ---------- 絵に描き込まれた英語（勇者の肖像の巻物、宿の看板など） ----------
   pics.bin の英語の画素の塊を、0.1 秒ごとにメモリ上の 320 幅の頁と画面から探して日本語に替える */
#define MAXPIC 16
typedef struct { int w, h; uint8_t *en, *ja; } Pic;
static Pic g_pic[MAXPIC];
static int g_npic;

static void LoadPics(void)
{
    char path[MAX_PATH];
    HANDLE f;
    DWORD got;
    uint8_t hd[6];
    int i;
    wsprintfA(path, "%s\\pics.bin", g_dir);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    if (ReadFile(f, hd, 6, &got, NULL) && got == 6 && !memcmp(hd, "LJPC", 4)) {
        int n = hd[4] | (hd[5] << 8);
        for (i = 0; i < n && g_npic < MAXPIC; i++) {
            uint16_t r[2];
            Pic *q = &g_pic[g_npic];
            if (!ReadFile(f, r, 4, &got, NULL) || got != 4) break;
            q->w = r[0]; q->h = r[1];
            if (q->w < 1 || q->w > 320 || q->h < 1 || q->h > 200) break;
            q->en = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, q->w * q->h);
            q->ja = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, q->w * q->h);
            if (!q->en || !q->ja) break;
            if (!ReadFile(f, q->en, q->w * q->h, &got, NULL) || (int)got != q->w * q->h) break;
            if (!ReadFile(f, q->ja, q->w * q->h, &got, NULL) || (int)got != q->w * q->h) break;
            g_npic++;
        }
    }
    CloseHandle(f);
    LogF("絵の差し替え %d 枚", g_npic);
}

/* base（1 行 st バイト）の [lo, hi) で q の英語の塊が丸ごと一致する所の先頭を返す。無ければ -1 */
static long FindPic(const uint8_t *base, long lo, long hi, const Pic *q, int st)
{
    long a, last = hi - (long)st * (q->h - 1) - q->w;
    int y;
    if (q->w > st) return -1;
    for (a = lo; a <= last; a++) {
        if (base[a] != q->en[0] || memcmp(base + a, q->en, q->w)) continue;
        for (y = 1; y < q->h; y++)
            if (memcmp(base + a + (long)y * st, q->en + y * q->w, q->w)) break;
        if (y == q->h) return a;
    }
    return -1;
}

static void PicScan(void)
{
    static uint8_t scr[64000];
    int i, y, x, have = 0;
    for (i = 0; i < g_npic; i++) {
        Pic *q = &g_pic[i];
        static const int stride[] = { 320, 176 };   /* 画面の頁と、景色の絵（176x120）の置き場 */
        long a, from;
        int s;
        /* 通常メモリ */
        for (s = 0; s < 2; s++)
            for (from = 0; (a = FindPic(DBX_MEMBASE, from, 0xA0000, q, stride[s])) >= 0; from = a + 1) {
                for (y = 0; y < q->h; y++) memcpy(DBX_MEMBASE + a + (long)y * stride[s], q->ja + y * q->w, q->w);
                LogF("絵 %d を差し替え（メモリ %05lX 幅 %d）", i, a, stride[s]);
            }
        /* 画面（mode 13h のときだけ） */
        if (rd8(0x449) != 0x13) continue;
        if (!have) { long k; for (k = 0; k < 64000; k++) scr[k] = vrd8(0xA0000 + k); have = 1; }
        if ((a = FindPic(scr, 0, 64000, q, 320)) >= 0) {
            for (y = 0; y < q->h; y++)
                for (x = 0; x < q->w; x++) {
                    uint8_t j = q->ja[y * q->w + x];
                    long o = a + (long)y * 320 + x;
                    if (scr[o] != j) { vwr8(0xA0000 + o, j); scr[o] = j; }
                }
            LogF("絵 %d を差し替え（画面 %ld,%ld）", i, a % 320, a / 320);
        }
    }
}

void Game_Tick(void)
{
    static DWORD last;
    DWORD now;
    if (!g_npic) return;
    now = GetTickCount();
    if (now - last < 100) return;
    last = now;
    PicScan();
}

/* _Get_Confirm の入口（push bp / mov bp,sp / push si を書き換えたので代わりに行う） */
static uint32_t CB_GetConfirm(void)
{
    uint32_t ss = segbase(S_SS);
    uint16_t sp = reg16(R_SP);
    sp -= 2; wr16(ss + sp, reg16(R_BP)); set16(R_BP, sp);
    sp -= 2; wr16(ss + sp, reg16(R_SI)); set16(R_SP, sp);
    ApplyButtons();
    return 0;
}

static void PatchConfirm(uint32_t lo, uint32_t hi)
{
    /* push bp; mov bp,sp; push si; call Clear_KeyBuffer; call Get_Key; mov si,ax; cmp si,0C6h; je; cmp si,0C7h; jne */
    static const uint8_t b[] = { 0x55, 0x8B, 0xEC, 0x56, 0x9A, 0, 0, 0, 0, 0x9A, 0, 0, 0, 0, 0x8B, 0xF0,
                                 0x81, 0xFE, 0xC6, 0x00, 0x74, 0x06, 0x81, 0xFE, 0xC7, 0x00, 0x75, 0x43,
                                 0xB8, 0xC2, 0x00, 0x50, 0xB8, 0x80, 0x00, 0x50, 0xB8, 0xB4, 0x00, 0x50,
                                 0xB8, 0x58, 0x00, 0x50 };
    static const uint8_t m[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF };
    static const Pattern pat = { "CONFIRM", sizeof b, b, m };
    uint32_t t;
    int n;
    if (!g_nbtn || !cbGC) return;
    t = Scan(lo, hi, &pat, &n);
    if (n != 1) { if (n) LogF("Get_Confirm が %d か所", n); return; }
    PutCB(t, cbGC);
    LogF("Get_Confirm を書き換え (%05X)", t);
}

/* _Intro_Print は声を鳴らすと字幕を出さずに帰る。帰りの jmp を消して、声と一緒に字幕も出す */
static void PatchIntroText(uint32_t lo, uint32_t hi)
{
    static const uint8_t b[] = { 0x74, 0x18, 0xB8, 0xD0, 0x00, 0x50, 0xB8, 0xFF, 0x00, 0x50, 0xFF, 0x36, 0, 0,
                                 0xFF, 0x36, 0, 0, 0x9A, 0, 0, 0, 0, 0x83, 0xC4, 0x08, 0xE9, 0xBA, 0x01 };
    static const uint8_t m[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0,
                                 0xFF, 0xFF, 0, 0, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const Pattern pat = { "INTROVOICE", sizeof b, b, m };
    uint32_t t;
    int n;
    t = Scan(lo, hi, &pat, &n);
    if (n != 1) { if (n) LogF("Intro_Print の声の分岐が %d か所", n); return; }
    wr8(t + 26, 0x90); wr8(t + 27, 0x90); wr8(t + 28, 0x90);
    LogF("オープニングの字幕を声と一緒に出す (%05X)", t + 26);
}

/* 本編の起動時、「文字は」は声が無いときだけオンになる（cmp [20DB],0 / je で 0 を入れる）。
   je を消して、いつも文字ありで始める（設定の画面でオフにはできる） */
static void PatchTextOn(uint32_t lo, uint32_t hi)
{
    static const uint8_t b[] = { 0xC6, 0x06, 0xFF, 0x81, 0x4B, 0x80, 0x3E, 0xDB, 0x20, 0x00, 0x74, 0x05,
                                 0xB8, 0x01, 0x00, 0xEB, 0x02, 0x33, 0xC0, 0xA2, 0xFE, 0x81 };
    static const uint8_t m[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const Pattern pat = { "TEXTON", sizeof b, b, m };
    uint32_t t;
    int n;
    t = Scan(lo, hi, &pat, &n);
    if (n != 1) { if (n) LogF("文字の設定の初期化が %d か所", n); return; }
    wr8(t + 10, 0x90); wr8(t + 11, 0x90);
    LogF("文字はオンで始める (%05X)", t + 10);
}

/* FINALE.EXE の台詞を出す関数は、声が有効だと文を出さずに帰る（cmp [23A9],0 / je）。
   je を jmp にして、声と一緒に文も出す */
static void PatchFinaleText(uint32_t lo, uint32_t hi)
{
    static const uint8_t b[] = { 0x83, 0x3E, 0xA9, 0x23, 0x00, 0x74, 0x09, 0xF7, 0xC6, 0x00, 0x80,
                                 0x75, 0x03, 0xE9, 0x8A, 0x01 };
    static const uint8_t m[] = { 0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const Pattern pat = { "FINALETEXT", sizeof b, b, m };
    uint32_t t;
    int n;
    t = Scan(lo, hi, &pat, &n);
    if (n != 1) { if (n) LogF("エンディングの声の分岐が %d か所", n); return; }
    wr8(t + 5, 0xEB);
    LogF("エンディングの台詞を声と一緒に出す (%05X)", t + 5);
}

/* ---------- 起動の検出と書き換え ---------- */
void Game_Init(void)
{
    LoadButtons();
    if (g_nbtn) cbGC = AllocCallback(CB_GetConfirm);
    LoadPics();
    cbTP = AllocCallback(CB_TextPrint);
    cbCW = AllocCallback(CB_CharWidth);
    cbUD = AllocCallback(CB_UnDip);
    cbBR = AllocCallback(CB_Break);
    cbSW = AllocCallback(CB_StrWidth);
    LogF("コールバック %d %d %d %d", cbTP, cbCW, cbUD, cbBR);
}

static void Patch(Prog *p)
{
    uint32_t t = p->textprint, dc, gp;
    /* 必要な番地を読む（書き換え前に） */
    if (rd8(t + 0x36) != 0xC4 || rd8(t + 0x37) != 0x3E || rd8(t + 0xB4) != 0x03 || rd8(t + 0xD0) != 0x03
        || rd8(t + 0xFA) != 0x9A || rd8(t + 0x10A) != 0x83) { LogF("%s: Text_Print の形が違う", p->name); return; }
    p->fontptr = rd16(t + 0x38);
    p->ysp = rd16(t + 0xB6);
    p->xsp = rd16(t + 0xD2);
    dc = ((uint32_t)rd16(t + 0xFD) << 4) + rd16(t + 0xFB);       /* Draw_Char */
    p->xlat = (uint32_t)rd16(t + 0xFD) << 4;
    if (rd8(dc + 0x0C) != 0xFF || rd8(dc + 0x10) != 0x9A) { LogF("%s: Draw_Char の形が違う", p->name); return; }
    p->logicpage = rd16(dc + 0x0E);
    gp = ((uint32_t)rd16(dc + 0x13) << 4) + rd16(dc + 0x11);      /* Get_Page */
    if (rd8(gp + 0x10) != 0x8B || rd8(gp + 0x11) != 0x97) { LogF("%s: Get_Page の形が違う", p->name); return; }
    p->pagetbl = rd16(gp + 0x12);
    p->widthptr = rd16(p->charwidth + 0x0D);
    p->cwxsp = rd16(p->charwidth + 0x18);

    /* Text_Print: 鍵の処理の後で描画を受け持ち、??exit へ跳ぶ */
    PutCB(t + 0x36, cbTP);
    wr8(t + 0x3A, 0xE9);
    wr16(t + 0x3B, (uint16_t)(0x10A - (0x36 + 7)));
    PutCB(p->charwidth, cbCW); wr8(p->charwidth + 4, 0xCB);
    PutCB(p->undip, cbUD); wr8(p->undip + 4, 0xCB);
    if (p->strwidth) { PutCB(p->strwidth, cbSW); wr8(p->strwidth + 4, 0xCB); }
    if (p->brk) {
        p->linebuf = rd16(p->brk + 2);
        PutCB(p->brk + 0x28, cbBR);
    }
    LogF("%s: 書き換え完了 TP=%05X CW=%05X UD=%05X BR=%05X font=%04X page=%04X/%04X xlat=%05X",
         p->name, t, p->charwidth, p->undip, p->brk, p->fontptr, p->logicpage, p->pagetbl, p->xlat);
}

/* MAIN.EXE の人物の表（0x82 刻み、名前は 11 バイトの欄）の名前を訳に替える */
#define NAMESTRIDE 0x82
#define NAMELEN 11
static void PatchNames(uint32_t lo, uint32_t hi)
{
    static const Pattern pat = { "NAMES", 11, (const uint8_t *)"Michael\0\0\0\0",
                                 (const uint8_t *)"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" };
    uint32_t t, a;
    int n, k, i, done = 0;
    t = Scan(lo, hi, &pat, &n);
    if (n != 1) { if (n) LogF("名前の表が %d か所", n); return; }
    if (memcmp(lin(t + NAMESTRIDE * 4), "Conrad", 7)) { LogF("名前の表の形が違う"); return; }
    for (k = 0; k < 9; k++) {
        char en[NAMELEN + 1];
        const char *ja;
        a = t + NAMESTRIDE * k;
        for (i = 0; i < NAMELEN; i++) en[i] = (char)rd8(a + i);
        en[NAMELEN] = 0;
        if ((uint8_t)en[0] >= 0x80 || !en[0]) continue;
        ja = Text_Find(en, (int)strlen(en));
        if (!ja || strlen(ja) >= NAMELEN) continue;
        for (i = 0; i < NAMELEN; i++) wr8(a + i, 0);
        for (i = 0; ja[i]; i++) wr8(a + i, (uint8_t)ja[i]);
        done++;
    }
    LogF("名前を %d 人差し替え (%05X)", done, t);
}

static void OnLoaded(uint32_t lo, uint32_t hi, const char *fname)
{
    Prog np, *p = NULL;
    int n1, n2, n3, n4, i;
    const char *b;
    if (hi > 0xA0000) hi = 0xA0000;
    memset(&np, 0, sizeof np);
    np.lo = lo; np.hi = hi;
    np.textprint = Scan(lo, hi, &PAT_TEXTPRINT, &n1);
    np.charwidth = Scan(lo, hi, &PAT_CHARWIDTH, &n2);
    np.undip = Scan(lo, hi, &PAT_UNDIP, &n3);
    np.brk = Scan(lo, hi, &PAT_BREAK, &n4);
    if (n4 != 1) np.brk = 0;
    {
        int n5;
        np.strwidth = Scan(lo, hi, &PAT_STRWIDTH, &n5);
        if (n5 != 1) np.strwidth = 0;
    }
    LogF("load %s 範囲 %05X-%05X 一致 %d %d %d %d", fname, lo, hi, n1, n2, n3, n4);
    if (n1 != 1 || n2 != 1 || n3 != 1 || g_nopatch) return;
    b = strrchr(fname, 92); b = b ? b + 1 : fname;
    for (i = 0; i < 15 && b[i]; i++) np.name[i] = b[i];
    np.name[i] = 0;
    for (i = 0; i < MAXPROG; i++)
        if (g_prog[i].used && g_prog[i].lo < hi && lo < g_prog[i].hi) g_prog[i].used = 0;
    for (i = 0; i < MAXPROG; i++) if (!g_prog[i].used) { p = &g_prog[i]; break; }
    if (!p) p = &g_prog[0];
    *p = np;
    p->used = 1;
    Patch(p);
    PatchNames(lo, hi);
    PatchConfirm(lo, hi);
    PatchIntroText(lo, hi);
    PatchTextOn(lo, hi);
    PatchFinaleText(lo, hi);
}

#define CUR_PSP 0xB30
static uint32_t PspRange(uint16_t psp, uint32_t *hi)
{
    uint32_t mcb = ((uint32_t)psp - 1) << 4;
    *hi = ((uint32_t)psp << 4) + (uint32_t)rd16(mcb + 3) * 16 + 16;
    return ((uint32_t)psp + 0x10) << 4;
}

uint32_t Game_Int21(CB_Handler orig)
{
    uint16_t ax = reg16(R_AX);
    uint32_t r;
    if ((ax >> 8) == 0x4B) {
        char name[80];
        uint8_t al = (uint8_t)ax;
        uint32_t pb = segbase(S_ES) + reg16(R_BX), fn = segbase(S_DS) + reg16(R_DX);
        uint16_t oldpsp = rd16(CUR_PSP);
        int i;
        for (i = 0; i < 79 && rd8(fn + i); i++) name[i] = (char)rd8(fn + i);
        name[i] = 0;
        r = orig();
        LogF("4B%02X %s psp %04X->%04X", al, name, oldpsp, rd16(CUR_PSP));
        if (al == 0 || al == 1) {
            uint16_t psp = rd16(CUR_PSP);
            if (psp != oldpsp && rd16((uint32_t)psp << 4) == 0x20CD) {
                uint32_t hi, lo = PspRange(psp, &hi);
                OnLoaded(lo, hi, name);
            }
        } else if (al == 3) {
            uint32_t flags = rd16(segbase(S_SS) + reg16(R_SP) + 4);
            if (!(flags & FLAG_CF)) {
                uint32_t lo = (uint32_t)rd16(pb) << 4;
                OnLoaded(lo, lo + 0x60000, name);
            }
        }
        return r;
    }
    if ((ax >> 8) == 0x3F) {
        /* 本編の一部は後から読み込まれる（オーバーレイ）ので、読んだ中身も見る */
        uint32_t buf = segbase(S_DS) + reg16(R_DX), flags;
        r = orig();
        flags = rd16(segbase(S_SS) + reg16(R_SP) + 4);
        if (!(flags & FLAG_CF) && reg16(R_AX) >= 22 && buf + reg16(R_AX) <= 0xA0000)
            PatchTextOn(buf, buf + reg16(R_AX));
        return r;
    }
    return orig();
}
