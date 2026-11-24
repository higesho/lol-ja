/* 会話の字幕
   - 声を鳴らす MSS の関数（_AIL_set_sample_file / _AIL_load_sample_buffer）を横取りし、
     音のデータの頭の指紋を LOL3JP\LOL3_SUB.DAT の表と照らして、会話の何行目かを知る
   - 字幕の文は、組み立てた日本語の DTEXT.TRR の同じ行
   - Glide の画面切り替え（grBufferSwap）の直前に、裏の画面へ直接描く（RGB565） */

typedef struct { u32 ha, hb, ms; u16 idx, pad; } SubEnt;
static SubEnt *g_sub;
static int g_nsub;
static int g_sub_on = 1;

/* 今の字幕 */
static volatile int g_cur_idx = -1;
static volatile DWORD g_cur_until, g_cur_start, g_cur_ms;

static u32 fnv_n(const u8 *p, u32 n)
{
    u32 h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

static int load_subs(void)
{
    char p[MAX_PATH];
    DWORD sz;
    u8 *b;
    wsprintfA(p, "%s\\LOL3_SUB.DAT", g_dir);
    b = read_all(p, &sz);
    if (!b || sz < 8 || memcmp(b, "L3SB", 4)) { free(b); return 0; }
    g_nsub = *(u32 *)(b + 4);
    if ((u32)g_nsub * sizeof(SubEnt) + 8 > sz) { g_nsub = 0; free(b); return 0; }
    g_sub = (SubEnt *)(b + 8);
    return g_nsub;
}

static void sub_trigger(int idx, u32 ms)
{
    if (idx < 0 || (u32)idx >= g_dt_n) return;
    g_cur_idx = idx;
    g_cur_start = GetTickCount();
    g_cur_ms = ms;
    g_cur_until = g_cur_start + ms + 400;
}

static void sub_check(const u8 *p, u32 len, int whole)
{
    u32 h;
    int i;
    if (!g_nsub || !p || len < 64) return;
    __try {
        h = fnv_n(p, len < 2048 ? len : 2048);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    for (i = 0; i < g_nsub; i++) {
        if ((whole ? g_sub[i].ha : g_sub[i].hb) == h) {
#ifdef LOL3JP_TEST
            LOG("声 %s len=%u h=%08x → 行 %d", whole ? "set" : "load", len, h, g_sub[i].idx);
#endif
            sub_trigger(g_sub[i].idx, g_sub[i].ms);
            return;
        }
    }
#ifdef LOL3JP_TEST
    if (whole) {
        static int ns; char hx[200]; int k, n = 0;
        if (ns++ < 60) {
            for (k = 0; k < 64; k++) n += wsprintfA(hx + n, "%02x", p[k]);
            LOG("声 set len=%u h=%08x 当たりなし 頭 %s", len, h, hx);
        }
    } else { static int nn; if (nn++ < 20) LOG("声 load len=%u h=%08x 当たりなし", len, h); }
#endif
}

typedef int (__stdcall *aset_fn)(void *, const void *, int);
typedef void (__stdcall *aload_fn)(void *, u32, const void *, u32);
static aset_fn o_aset;
static aload_fn o_aload;
static int __stdcall my_aset(void *s, const void *img, int block)
{
    if (img) {
        u32 n = 2048;
        if (!memcmp(img, "RIFF", 4)) n = *(u32 *)((u8 *)img + 4) + 8;
        sub_check((const u8 *)img, n, 1);
    }
    return o_aset(s, img, block);
}
static void __stdcall my_aload(void *s, u32 num, const void *buf, u32 len)
{
    sub_check((const u8 *)buf, len, 0);
    o_aload(s, num, buf, len);
}

/* ---- 字幕の文: 色の印と折り返し用の空白を外し、行に分ける ---- */
#define SUB_MAXL 24
#define SUB_PAGE 3
static int sub_lines(const u8 *s, u8 out[SUB_MAXL][160], int maxchars)
{
    u8 t[1024];
    int n = 0, i, nl = 0;
    /* 印・改行を外す。日本語に挟まれた空白は「折り返してよい所」として 0x01 にする */
    for (i = 0; s[i] && n < (int)sizeof t - 2; i++) {
        u8 c = s[i];
        if ((c == 2 || c == 3) && s[i + 1]) { i++; continue; }
        if (c == '\r' || c == '\n') continue;
        if (c == ' ' && i > 0 && s[i - 1] >= 0xA1 && IS_LEAD(s[i + 1])) { t[n++] = 1; continue; }
        t[n++] = c;
    }
    t[n] = 0;
    /* 1 行は全角 maxchars 字分（半角は半分）まで。折り返せる所（0x01 と空白）で切る */
    {
        int pos = 0;
        while (t[pos] && nl < SUB_MAXL) {
            int w = 0, j = pos, brk = -1, k = 0;
            while (t[j]) {
                int cw = IS_LEAD(t[j]) ? 2 : (t[j] == 1 ? 0 : 1);
                if (w + cw > maxchars * 2) break;
                if (t[j] == 1 || t[j] == ' ') brk = j;
                w += cw;
                j += IS_LEAD(t[j]) && t[j + 1] ? 2 : 1;
            }
            if (t[j] && brk > pos) j = brk;
            for (i = pos; i < j && k < 158; i++) if (t[i] != 1) out[nl][k++] = t[i];
            out[nl][k] = 0;
            nl++;
            pos = j;
            while (t[pos] == 1 || t[pos] == ' ') pos++;
        }
    }
    return nl;
}

/* ---- RGB565 の画面に描く ---- */
static void put565(u16 *base, int stride, int w, int h, int x, int y, u16 c)
{
    if (x >= 0 && y >= 0 && x < w && y < h) base[y * stride + x] = c;
}

static void draw_text565(u16 *base, int stride, int w, int h, const JFont *f, const u8 *s, int x, int y, u16 col)
{
    for (; *s; ) {
        if (IS_LEAD(s[0]) && IS_LEAD(s[1])) {
            int idx = (s[0] - 0xA1) * 94 + (s[1] - 0xA1), r, c;
            const u8 *g = f->data + idx * f->h * f->bpr;
            for (r = 0; r < f->h; r++)
                for (c = 0; c < f->w; c++)
                    if ((g[r * f->bpr + (c >> 3)] >> (7 - (c & 7))) & 1) put565(base, stride, w, h, x + c, y + r, col);
            x += f->w;
            s += 2;
        } else {
            /* 半角: 全角の字体の中から同じ形の全角の字（Ａ～ｚ・０～９・記号）を半分の幅で使う */
            u8 c = *s++;
            u8 e[3] = { 0 };
            if (c >= '0' && c <= '9') { e[0] = 0xA3; e[1] = (u8)(0xB0 + c - '0'); }
            else if (c >= 'A' && c <= 'Z') { e[0] = 0xA3; e[1] = (u8)(0xC1 + c - 'A'); }
            else if (c >= 'a' && c <= 'z') { e[0] = 0xA3; e[1] = (u8)(0xE1 + c - 'a'); }
            else if (c == ':') { e[0] = 0xA1; e[1] = 0xA7; }
            else if (c == '!') { e[0] = 0xA1; e[1] = 0xAA; }
            else if (c == '?') { e[0] = 0xA1; e[1] = 0xA9; }
            else if (c == '.') { e[0] = 0xA1; e[1] = 0xA5; }
            else if (c == ',') { e[0] = 0xA1; e[1] = 0xA4; }
            else if (c == '-') { e[0] = 0xA1; e[1] = 0xDD; }
            else if (c == '\'') { e[0] = 0xA1; e[1] = 0xC7; }
            if (e[0]) {
                int idx = (e[0] - 0xA1) * 94 + (e[1] - 0xA1), r, cc;
                const u8 *g = f->data + idx * f->h * f->bpr;
                for (r = 0; r < f->h; r++)
                    for (cc = 0; cc < f->w; cc++)
                        if ((g[r * f->bpr + (cc >> 3)] >> (7 - (cc & 7))) & 1) put565(base, stride, w, h, x + cc, y + r, col);
                x += f->w;
            } else x += f->w / 2;
        }
    }
}

static int text_w(const JFont *f, const u8 *s)
{
    int w = 0;
    for (; *s; ) {
        if (IS_LEAD(s[0]) && IS_LEAD(s[1])) { w += f->w; s += 2; }
        else { u8 c = *s++; w += (isalnum(c) || strchr(":!?.,-'", c)) ? f->w : f->w / 2; }
    }
    return w;
}

/* 字幕の配置を決める。描くものが無ければ 0 */
static u8 g_lines[SUB_MAXL][160];
typedef struct { const JFont *f; int first, cnt, lh, top, bot; } SubLay;
static volatile int g_mov_on;
static int sub_layout(int w, int h, SubLay *L)
{
    int idx = g_cur_idx, nl, k, maxc, pages, page;
    DWORD now = GetTickCount();
    if (idx < 0 || !g_dt_buf) return 0;
    if ((int)(now - g_cur_until) > 0) { g_cur_idx = -1; return 0; }
    L->f = &g_jf[0];                    /* 画面の高さの 1/30 以下で一番大きい字体 */
    for (k = g_njf - 1; k >= 0; k--) if (g_jf[k].h * 30 <= h) { L->f = &g_jf[k]; break; }
    maxc = (w - 32) / L->f->w;
    nl = sub_lines(g_dt_buf + g_dt_off[idx], g_lines, maxc);
    if (nl <= 0) return 0;
    /* 3 行ずつのページにして、声の長さに合わせてめくる */
    pages = (nl + SUB_PAGE - 1) / SUB_PAGE;
    page = 0;
    if (pages > 1 && g_cur_ms) {
        page = (int)((now - g_cur_start) * pages / g_cur_ms);
        if (page >= pages) page = pages - 1;
    }
    L->first = page * SUB_PAGE;
    L->cnt = nl - L->first < SUB_PAGE ? nl - L->first : SUB_PAGE;
    L->lh = L->f->h + 4;
    /* 動画の間は画面の下、遊んでいる間は下の操作盤（画面の下 1/6）の上 */
    L->bot = g_mov_on ? h - 10 : h * 5 / 6 - 6;
    L->top = L->bot - L->cnt * L->lh;
    return 1;
}

/* RGB565 の領域（画面の top 行から rows 行分）に字幕を描く */
static void sub_draw565(u16 *base, int stride_px, int w, int rows, const SubLay *L, int top)
{
    int i;
    for (i = 0; i < L->cnt; i++) {
        const u8 *ln = g_lines[L->first + i];
        int tw = text_w(L->f, ln), x = (w - tw) / 2, y = L->top + 2 + i * L->lh - top, dx, dy;
        for (dy = -1; dy <= 1; dy++)
            for (dx = -1; dx <= 1; dx++)
                if (dx || dy) draw_text565(base, stride_px, w, rows, L->f, ln, x + dx, y + dy, 0x0000);
        draw_text565(base, stride_px, w, rows, L->f, ln, x, y, 0xFFFF);
    }
}

/* ---- Glide ---- */
typedef struct { int size; void *lfbPtr; u32 strideInBytes; int writeMode; int origin; } GrLfbInfo;
typedef int (__stdcall *lfbread2_fn)(u32, u32, u32, u32, u32, u32, void *);
typedef int (__stdcall *lfbwrite2_fn)(u32, u32, u32, u32, u32, u32, int, void *);
typedef void (__stdcall *gswap_fn)(u32);
typedef u32 (__stdcall *gscr_fn)(void);
static lfbread2_fn g_lfbrd;
static lfbwrite2_fn g_lfbwr;
static gswap_fn g_swap_real;
static gscr_fn g_scrw, g_scrh;

static void mov_tick(void);
static int g_nmov;
static void __stdcall sub_swap(u32 interval)
{
    mov_tick();
    if (g_cur_idx >= 0 && g_lfbrd && g_lfbwr) {
        int w = g_scrw ? (int)g_scrw() : 640, h = g_scrh ? (int)g_scrh() : 480;
        SubLay L;
        if (w > 0 && h > 0 && w <= 4096 && h <= 4096 && sub_layout(w, h, &L)) {
            /* 字幕の帯だけを読み出し、字を重ねて書き戻す */
            int top = L.top, rows = L.bot - L.top + 4;
            u16 *buf;
            if (top < 0) { rows += top; top = 0; }
            if (top + rows > h) rows = h - top;
            buf = rows > 0 ? (u16 *)malloc(w * rows * 2) : NULL;
            if (buf) {
                /* GR_BUFFER_BACKBUFFER=1、GR_LFB_SRC_FMT_565=0 */
                if (g_lfbrd(1, 0, top, w, rows, w * 2, buf)) {
                    sub_draw565(buf, w, w, rows, &L, top);
                    g_lfbwr(1, 0, top, 0, w, rows, w * 2, buf);
                }
                free(buf);
            }
        }
    }
    g_swap_real(interval);
}

static FARPROC sub_glide_wrap(LPCSTR n, FARPROC p)
{
    if (!lstrcmpA(n, "_grLfbReadRegion@28")) g_lfbrd = (lfbread2_fn)p;
    else if (!lstrcmpA(n, "_grLfbWriteRegion@32")) g_lfbwr = (lfbwrite2_fn)p;
    else if (!lstrcmpA(n, "_grSstScreenWidth@0")) g_scrw = (gscr_fn)p;
    else if (!lstrcmpA(n, "_grSstScreenHeight@0")) g_scrh = (gscr_fn)p;
    else if (!lstrcmpA(n, "_grBufferSwap@4") && (g_nsub || g_nmov)) {
        /* 読み書きの関数はゲームが取りに来ないこともあるので、同じ DLL から直接取る */
        HMODULE gm = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &gm) && gm) {
            if (!g_lfbrd) g_lfbrd = (lfbread2_fn)GetProcAddress(gm, "_grLfbReadRegion@28");
            if (!g_lfbwr) g_lfbwr = (lfbwrite2_fn)GetProcAddress(gm, "_grLfbWriteRegion@32");
            if (!g_scrw) g_scrw = (gscr_fn)GetProcAddress(gm, "_grSstScreenWidth@0");
            if (!g_scrh) g_scrh = (gscr_fn)GetProcAddress(gm, "_grSstScreenHeight@0");
        }
        LOG("字幕の描き先 read=%p write=%p", g_lfbrd, g_lfbwr);
        g_swap_real = (gswap_fn)p; return (FARPROC)sub_swap;
    }
    return p;
}

/* ---- 動画の字幕: LOL3JP\LOL3_MOV.DAT（動画名の指紋, 始め ms, 終わり ms, 行番号）---- */
#define A_PLAY_MOVIE 0x46fe20      /* (名前 "xxx.vqa", ?) cdecl */
typedef struct { u32 mv, st, en; u16 idx, pad; } MovEnt;
static MovEnt *g_mov;
static volatile u32 g_mov_hash;
static volatile DWORD g_mov_start;
typedef int (__cdecl *play_fn)(const char *, int);
static play_fn o_play;

static u32 lol3_hash(const char *s, int n)
{
    u32 v = 0;
    int i;
    for (i = 0; i < n && s[i]; i++) {
        int c = (u8)s[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        v = (v >> 6) | (v << 26);
        v += (c - 0x30) & 0x3f;
    }
    return v;
}

static int __cdecl my_play(const char *name, int a2)
{
    int r;
    if (name && g_nmov) {
        const char *b = strrchr(name, '\\'), *dot;
        b = b ? b + 1 : name;
        dot = strchr(b, '.');
        g_mov_hash = lol3_hash(b, dot ? (int)(dot - b) : 64);
        g_mov_start = GetTickCount();
        g_mov_on = 1;
        LOG("動画 %s", name);
    }
    r = o_play(name, a2);
    g_mov_on = 0;
    g_cur_idx = -1;
    return r;
}

/* 動画の再生中なら、経過時間で今の台詞を決める */
static void mov_tick(void)
{
    DWORD t;
    int i;
    if (!g_mov_on) return;
    t = GetTickCount() - g_mov_start;
    for (i = 0; i < g_nmov; i++) {
        if (g_mov[i].mv == g_mov_hash && t >= g_mov[i].st && t < g_mov[i].en) {
            if (g_cur_idx != g_mov[i].idx) {
                g_cur_idx = g_mov[i].idx;
                g_cur_start = g_mov_start + g_mov[i].st;
                g_cur_ms = g_mov[i].en - g_mov[i].st;
            }
            g_cur_until = g_mov_start + g_mov[i].en;
            return;
        }
    }
}

static void movie_install(void)
{
    char p[MAX_PATH];
    DWORD sz;
    u8 *b;
    static const u8 sig[5] = { 0xa0, 0x2f, 0x55, 0x54, 0x00 };
    wsprintfA(p, "%s\\LOL3_MOV.DAT", g_dir);
    b = read_all(p, &sz);
    if (!b || sz < 8 || memcmp(b, "L3MV", 4)) { free(b); return; }
    g_nmov = *(u32 *)(b + 4);
    if ((u32)g_nmov * sizeof(MovEnt) + 8 > sz) { g_nmov = 0; free(b); return; }
    g_mov = (MovEnt *)(b + 8);
    if (memcmp((void *)A_PLAY_MOVIE, sig, 5)) { g_nmov = 0; return; }
    o_play = (play_fn)make_tramp(A_PLAY_MOVIE, 5);
    put_jmp(A_PLAY_MOVIE, my_play);
    LOG("動画の字幕 %d 件", g_nmov);
}

static void subs_install(HMODULE m)
{
    if (g_sub_on && g_dt_buf) movie_install();
    if (!g_sub_on || !load_subs() || !g_dt_buf) { LOG("字幕なし（表 %d）", g_nsub); g_nsub = 0; return; }
    o_aset = (aset_fn)patch_iat(m, "mss32.dll", "_AIL_set_sample_file@12", my_aset);
    o_aload = (aload_fn)patch_iat(m, "mss32.dll", "_AIL_load_sample_buffer@16", my_aload);
    if (!o_aset) o_aset = (aset_fn)GetProcAddress(GetModuleHandleA("mss32.dll"), "_AIL_set_sample_file@12");
    LOG("字幕 %d 件、set_sample_file=%p load_sample_buffer=%p", g_nsub, o_aset, o_aload);
}
