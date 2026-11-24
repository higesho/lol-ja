/*
 * Lands of Lore: Guardians of Destiny 日本語化（GOG 版 DOSBox 0.74G 用）
 * DOSBOX\winmm.dll として置く（足すだけ）。winmm の関数は全部 System の winmm.dll へ回す。
 *
 * - DOSBox の fopen/fread/fseek/ftell/fclose を横取りし、文章表（TRR）を日本語版に差し替える
 * - どの音声・動画が読まれたかで字幕を出す（DOSBox の 1 行描画の手前で重ねる）
 * - ゲームの文字描画（Buffer_Print）と文字列幅（String_Pixel_Width）を DOSBox のコールバック命令
 *   （FE 38 xx xx）に書き換え、ここで日本語（EUC-JP）を描く
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32; typedef int16_t s16;

/* ===================== DOSBox 0.74G（GOG）の番地 ===================== */
#define DB_MEMBASE        0x01d442d0u  /* HostPt MemBase */
#define DB_MEMPAGES       0x01d44318u  /* memory.pages */
#define DB_CB_HANDLERS    0x00755440u  /* CallBack_Handlers[128] */
#define DB_CB_ILLEGAL     0x00416980u  /* illegal_handler */
#define DB_RENDER_LINE    0x01eebd30u  /* RENDER_DrawLine（関数ポインタ） */
#define DB_DRAWLINE_CALL1 0x004f8b9bu  /* VGA_DrawPart 内の call [RENDER_DrawLine] */
#define DB_DRAWLINE_CALL2 0x004f8ca9u
#define DB_LINES_DONE     0x01d0f454u  /* vga.draw.lines_done */
#define DB_SRC_WIDTH      0x01eebd50u  /* render.src.width */
#define DB_SRC_HEIGHT     0x01eebd58u
#define DB_SRC_BPP        0x01eebd5cu
#define DB_DAC_RGB        0x01d1f5c0u  /* vga.dac.rgb[256][3]（6bit） */
#define IAT_FOPEN  0x0242b98cu
#define IAT_FREAD  0x0242b99cu
#define IAT_FSEEK  0x0242b9a8u
#define IAT_FTELL  0x0242b9acu
#define IAT_FCLOSE 0x0242b974u

/* ===================== LOLG.DAT（LE）の既定番地 ===================== */
#define G_BUFFER_PRINT   0x00120eb8u
#define G_STRWIDTH       0x00121765u
#define G_BUFFER_PRINT2  0x0012c234u  /* 画面のメモリへ直接描く版（関数表 0x289a94 に入る） */
#define G_FONTPTR_INS    0x00120ef5u  /* mov esi,[FontPtr] (8b 35 imm32) */
#define G_FUNCTAB_INS    0x0011a7f7u  /* mov [0x289a94], 0x120eb8 (c7 05 a32 i32) */
#define D_FONTPTR        0x00289ec8u
#define D_XSPACE         0x00289ec0u
#define D_YSPACE         0x00289ec4u
#define D_XLAT           0x00196680u

/* ===================== ログ ===================== */
/* ログは試験版（build.bat test）だけ。配布版では logfp は開かず、LOG は何もしない */
static FILE *logfp;
static void LOG(const char *fmt, ...)
{
#ifdef LOL2JP_TEST
    va_list ap;
    if (!logfp) return;
    va_start(ap, fmt); vfprintf(logfp, fmt, ap); va_end(ap);
    fputc('\n', logfp); fflush(logfp);
#else
    (void)fmt;
#endif
}

/* ===================== データ（リソースに埋め込み） ===================== */
static const u8 *blob; static u32 blobsize;
static const u8 *find_sec(const char *tag, u32 *size)
{
    u32 p = 8;
    if (!blob) return 0;
    while (p + 8 <= blobsize) {
        u32 sz = *(const u32 *)(blob + p + 4);
        if (memcmp(blob + p, tag, 4) == 0) { if (size) *size = sz; return blob + p + 8; }
        p += 8 + ((sz + 3) & ~3u);
    }
    return 0;
}

/* フォント: [h][fw][hw][0] full[94*94][h] u16, half[96][h] u8 */
typedef struct { int h, fw, hw; const u16 *full; const u8 *half; } JFont;
static JFont jf8, jf12, jf14, jf16;
static void load_font(JFont *f, const char *tag)
{
    const u8 *p = find_sec(tag, 0);
    if (!p) { memset(f, 0, sizeof *f); return; }
    f->h = p[0]; f->fw = p[1]; f->hw = p[2];
    f->full = (const u16 *)(p + 4);
    f->half = p + 4 + 94 * 94 * f->h * 2;
}
/* EUC 2 バイト → 行のビット列（左詰め 16bit）。無ければ 0 */
static const u16 *jglyph(const JFont *f, u8 a, u8 b)
{
    int ku = a - 0xA1, ten = b - 0xA1;
    if (!f->full || ku < 0 || ku >= 94 || ten < 0 || ten >= 94) return 0;
    return f->full + (ku * 94 + ten) * f->h;
}
static const u8 *hglyph(const JFont *f, u8 c)
{
    if (!f->half || c < 0x20 || c > 0x7f) return 0;
    return f->half + (c - 0x20) * f->h;
}

/* ===================== ゲームのメモリ ===================== */
/* DOS/4GW の仮想記憶でページングが有効。論理番地は DOSBox の TLB か、ページ表をたどって引く */
#define DB_PAGING_CR3   0x007556acu  /* paging.base.addr */
#define DB_TLB_READ     0x007556b0u  /* paging.tlb.read[1M] */
#define DB_TLB_WRITE    0x00b556b0u
#define DB_PAGING_ON    0x01b75af4u  /* paging.enabled */
static u8 *MB(void) { return *(u8 **)DB_MEMBASE; }
static u32 MSIZE(void) { return (*(u32 *)DB_MEMPAGES) * 4096u; }
static u32 phys32(u32 a) { return (MB() && a + 4 <= MSIZE()) ? *(u32 *)(MB() + a) : 0; }
static u32 lp_page = 0xffffffff; static u8 *lp_host;
static u8 *Lw(u32 lin, int write)
{
    u32 page = lin >> 12;
    u8 *r;
    if (!MB()) return 0;
    if (page == lp_page && !write) return lp_host + (lin & 0xfff);
    r = (write ? (u8 **)DB_TLB_WRITE : (u8 **)DB_TLB_READ)[page];
    if (r) r += (lin & ~0xfffu);
    else if (!*(u8 *)DB_PAGING_ON) r = (lin < MSIZE()) ? MB() + (lin & ~0xfffu) : 0;
    else {
        u32 cr3 = *(u32 *)DB_PAGING_CR3;
        u32 pde = phys32(cr3 + ((lin >> 22) << 2)), pte;
        if (!(pde & 1)) return 0;
        pte = phys32((pde & ~0xfffu) + (((lin >> 12) & 0x3ff) << 2));
        if (!(pte & 1)) return 0;
        if ((pte & ~0xfffu) >= MSIZE()) return 0;
        r = MB() + (pte & ~0xfffu);
    }
    if (!write) { lp_page = page; lp_host = r; }
    return r + (lin & 0xfff);
}
static void L_flush(void) { lp_page = 0xffffffff; }
static u8 rd8(u32 a) { u8 *p = Lw(a, 0); return p ? *p : 0; }
static u16 rd16(u32 a) { return (u16)(rd8(a) | rd8(a + 1) << 8); }
static u32 rd32(u32 a) { return (u32)rd16(a) | (u32)rd16(a + 2) << 16; }
static void wr8(u32 a, u8 v) { u8 *p = Lw(a, 1); if (p) *p = v; }

/* ===================== CPU のレジスタ（最初のコールバックで見つける） ===================== */
static u32 *cpu_regs;   /* [0]=eax [1]=ecx [2]=edx [3]=ebx [4]=esp [5]=ebp [6]=esi [7]=edi [8]=eip */
static int cb_print, cb_width, cb_print2;
static int is_stub_at(u32 eip)
{
    int cb;
    if (rd8(eip - 4) != 0xFE || rd8(eip - 3) != 0x38) return 0;
    cb = rd8(eip - 2) | rd8(eip - 1) << 8;
    return cb == cb_print || cb == cb_width || cb == cb_print2;
}
static int find_cpu_regs_(void);
static int find_cpu_regs(void)
{
    int r = 0;
    __try { r = find_cpu_regs_(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { LOG("find_cpu_regs で例外 %08x", GetExceptionCode()); }
    return r;
}
static int find_cpu_regs_(void)
{
    /* .data と .bss から「eip の 4 バイト前が自分の差し込み、esp の先に戻り先がある」ものを探す */
    u32 *p;
    for (p = (u32 *)0x006ea000; p + 9 < (u32 *)0x02029000; p++) {
        u32 eip = p[8], esp = p[4];
        if (eip < 0x1004 || !esp || (esp & 3)) continue;
        if (!is_stub_at(eip)) continue;
        if (!rd32(esp)) continue;
        cpu_regs = p;
        LOG("cpu_regs=%p eip=%08x esp=%08x ret=%08x", p, eip, esp, rd32(esp));
        return 1;
    }
    LOG("cpu_regs が見つからない");
    return 0;
}

/* ===================== ゲームのコードの位置 ===================== */
static int located;
static u32 dcode, ddata;   /* 実行時の番地 - 既定の番地 */
static u32 A(u32 x) { return x + dcode; }   /* コード */
static u32 D(u32 x) { return x + ddata; }   /* データ */
/* LOLG.DAT の中で書き換える所（ファイル上の位置と、元の 5 バイト） */
#define F_BUFFER_PRINT 0x00190edcu
#define F_STRWIDTH     0x00191789u
#define F_BUFFER_PRINT2 0x0019c258u
static const u8 orig_bp2[5] = {0xc8, 0x48, 0x00, 0x00, 0x53};
static const u8 orig_bp[5] = {0xc8, 0x3c, 0x00, 0x00, 0x53};
static const u8 orig_sw[5] = {0x55, 0x89, 0xe5, 0x53, 0x56};

/* 差し込みの eip から、コードとデータのずれを決める */
static int locate(u32 eip)
{
    int cb = rd8(eip - 2) | rd8(eip - 1) << 8;
    u32 at = eip - 4;
    if (cb == cb_print) dcode = at - G_BUFFER_PRINT;
    else if (cb == cb_print2) dcode = at - G_BUFFER_PRINT2;
    else if (cb == cb_width) dcode = at - G_STRWIDTH;
    else return 0;
    ddata = rd32(A(G_FONTPTR_INS) + 2) - D_FONTPTR;
    if (rd8(A(G_FONTPTR_INS)) != 0x8b || rd8(A(G_FONTPTR_INS) + 1) != 0x35) { LOG("データの位置が読めない"); return 0; }
    located = 1;
    LOG("ゲームの位置 code+%08x data+%08x", dcode, ddata);
    return 1;
}

/* ===================== 字の描き方（フォントごと） ===================== */
/* style: 0=単色（画素値 1）、1=陰影（上下の濃淡＋左上の明るい縁＋右下の影） */
typedef struct { const JFont *jf; int style; u8 top, mid, bot, hi, sh; } FontMap;
static FontMap fm_cur;
static u32 fm_font;
static void pick_font(u32 font)
{
    u32 info = font + rd16(font + 4);
    int fh = rd8(info + 4);
    if (font == fm_font && fm_cur.jf) return;
    fm_font = font;
    memset(&fm_cur, 0, sizeof fm_cur);
    if (fh >= 14) { fm_cur.jf = &jf14; fm_cur.style = 1; fm_cur.top = 0xb; fm_cur.mid = 0xa; fm_cur.bot = 0x8; fm_cur.hi = 6; fm_cur.sh = 5; }
    else if (fh >= 10) { fm_cur.jf = &jf8; fm_cur.style = 1; fm_cur.top = 0xb; fm_cur.mid = 0xa; fm_cur.bot = 0x9; fm_cur.hi = 6; fm_cur.sh = 5; }
    else { fm_cur.jf = &jf8; fm_cur.style = 0; }
    if (!fm_cur.jf->full) fm_cur.jf = jf12.full ? &jf12 : &jf8;
}
/* 日本語 1 字の送り幅（字の幅＋陰影の 1 ドット） */
static int jwidth(void) { return fm_cur.jf->fw + (fm_cur.style ? 1 : 0); }

/* 画面の文字の日本語: 前 0x80-0x9F、後 0xA0-0xBF の 2 バイト（字の番号 = (前-0x80)*32 + (後-0xA0)）。
   後を 0xC0 以上にしないのは、ゲームが文字列を大文字にする所（Latin-1 の 0xE0-0xFE → -0x20）があるため。
   前と後の範囲が重ならないので、1 バイトずつ幅を測る折り返しでも字が割れない */
static const u16 *cmap; static u32 ncmap;   /* 字の番号 → EUC（上位が 1 バイト目） */
static int is_lead(u8 c) { return c >= 0xA1 && c != 0xFF; }   /* EUC（字幕） */
static int is_jl(u8 c) { return c >= 0x80 && c <= 0x9F; }
static int is_jt(u8 c) { return c >= 0xA0 && c <= 0xBF; }
static int jpair(u32 s) { return is_jl(rd8(s)) && is_jt(rd8(s + 1)); }
static const u16 *jglyph_code(const JFont *f, u8 a, u8 b)
{
    u32 idx = (u32)(a - 0x80) * 32 + (b - 0xA0);
    u16 e;
    if (!cmap || idx >= ncmap) return 0;
    e = cmap[idx];
    return jglyph(f, (u8)(e >> 8), (u8)e);
}
/* 日本語の間の半角空白は折り返し用の目印。幅 0 で描かない。1 字だけで測られた空白も 0 */
static int is_zw_space(u32 s, u32 start)
{
    if (rd8(s) != ' ') return 0;
    if (s == start && rd8(s + 1) == 0) return 1;
    return jpair(s + 1) || (s >= start + 2 && jpair(s - 2));
}
/* 日本語の間の半角空白は折り返し用の目印で、幅 0 で描かない */


/* 画素値（0..15）の 4bit 字形マスクを作る: 字形を (w+2)x(h+2) に置き、陰影を付ける */
static int make_cell(const u16 *rows, int gw, int gh, u8 *cell, int cw, int ch)
{
    int x, y;
    memset(cell, 0, cw * ch);
#define ON(xx, yy) ((xx) >= 0 && (yy) >= 0 && (xx) < gw && (yy) < gh && ((rows[(yy)] << (xx)) & 0x8000))
    for (y = 0; y < ch; y++) for (x = 0; x < cw; x++) {
        int gx = x - 1, gy = y - 1;
        u8 v = 0;
        if (ON(gx, gy)) {
            if (fm_cur.style == 0) v = 1;
            else v = gy * 3 < gh ? fm_cur.top : gy * 3 < gh * 2 ? fm_cur.mid : fm_cur.bot;
        } else if (fm_cur.style) {
            if (ON(gx - 1, gy - 1) || ON(gx - 1, gy) || ON(gx, gy - 1)) v = fm_cur.sh;
            else if (ON(gx + 1, gy + 1) || ON(gx + 1, gy) || ON(gx, gy + 1)) v = fm_cur.hi;
        }
        cell[y * cw + x] = v;
    }
#undef ON
    return 1;
}

/* ===================== 画面のメモリへ直接描く版（VESA の 64KB 窓） ===================== */
/* ゲームの番地 0xA0000 + n は画面のメモリの n バイト目。DOSBox の vga.mem.linear へ直接書く */
static int g_banked;
static u8 *vga_lin; static u32 vga_lin_size;
static const u8 *last_src0;
static void find_vga_lin(void)
{
    /* 1 行目の描画元（vga.mem.linear の中を指す）に最も近い、vga 構造体の中のポインタを探す */
    u32 a, best = 0, src = (u32)last_src0;
    if (!src) return;
    for (a = DB_LINES_DONE - 0x400; a < DB_DAC_RGB + 0x4000; a += 4) {
        u32 v = *(u32 *)a;
        if (v && v <= src && src - v < 0x800000 && v > best && !IsBadWritePtr((void *)v, 0x10000)) best = v;
    }
    if (best) { vga_lin = (u8 *)best; vga_lin_size = 0x200000; LOG("画面のメモリ %08x（1 行目 %08x）", best, src); }
}
static void px(u32 addr, u8 v)
{
    if (!g_banked) { wr8(addr, v); return; }
    if (addr >= 0xA0000 && vga_lin && addr - 0xA0000 < vga_lin_size) vga_lin[addr - 0xA0000] = v;
}

/* ===================== Buffer_Print の代わり ===================== */
static u32 do_print(u32 vp, u32 str, s32 x, s32 y, u32 fc, u32 bc)
{
    u32 font, info, offb, widb, heib, vbuf, vw, vh, pitch, xlat;
    s32 fh, xs, ys, ybot;
    u32 rowstart, cur;
    u32 s = str;
    u8 xl[17];
    int i;

    if (!str) return 0;
    font = rd32(D(D_FONTPTR));
    if (!font) return 0;
    pick_font(font);
    info = font + rd16(font + 4); offb = font + rd16(font + 6); widb = font + rd16(font + 8); heib = font + rd16(font + 0xc);
    fh = rd8(info + 4);
    xs = (s32)rd32(D(D_XSPACE)); ys = (s32)rd32(D(D_YSPACE));
    vbuf = rd32(vp); vw = rd32(vp + 4); vh = rd32(vp + 8); pitch = vw + rd32(vp + 12);
    if (y < 0 || x < 0) return 0;
    if (fh + y >= (s32)vh) return 0;
    ybot = y + fh;
    xlat = D(D_XLAT);
    wr8(xlat + 1, (u8)fc); wr8(xlat + 0x10, (u8)fc); wr8(xlat, (u8)bc);
    for (i = 0; i < 17; i++) xl[i] = rd8(xlat + i);
    rowstart = vbuf + (u32)y * pitch;
    cur = rowstart + x;
    for (;;) {
        u8 c = rd8(s);
        s32 w;
        if (!c) break;
        if (c == '\n' || c == '\r') {
            s32 step = fh + ys;
            if (ybot + step > (s32)vh) { s++; break; }
            rowstart += pitch * step; ybot += step; y += step;
            cur = rowstart; x = 0; s++;
            continue;
        }
        if (c == ' ' && is_zw_space(s, str)) { s++; continue; }
        if (jpair(s)) {
            /* 日本語 1 字 */
            const JFont *jf = fm_cur.jf;
            const u16 *g = jglyph_code(jf, c, rd8(s + 1));
            int adv = jwidth();
            int cw = jf->fw + 2, ch = jf->h + 2, gx, gy;
            u8 cell[20 * 20];
            s32 top;
            if (x + adv + xs > (s32)vw) break;
            if (g) {
                make_cell(g, jf->fw, jf->h, cell, cw, ch);
                top = y + (fh - ch) / 2 + (fm_cur.style ? 0 : 0);
                if (fm_cur.style == 0) top = y + fh - ch + 1;   /* 単色は下揃え */
                for (gy = 0; gy < ch; gy++) {
                    s32 yy = top + gy;
                    if (yy < 0 || yy >= (s32)vh) continue;
                    for (gx = 0; gx < cw; gx++) {
                        s32 xx = x - 1 + gx;
                        u8 v = cell[gy * cw + gx], col;
                        if (!v || xx < 0 || xx >= (s32)vw) continue;
                        col = xl[v & 15];
                        if (col) px(vbuf + (u32)yy * pitch + xx, col);
                    }
                }
            }
            x += adv + xs; cur += adv + xs; s += 2;
            continue;
        }
        /* 元のフォントで 1 字 */
        w = rd8(widb + c);
        if (x + w + xs > (s32)vw) break;
        {
            u32 go = font + rd16(offb + c * 2);
            int yo = rd8(heib + c * 2), gh = rd8(heib + c * 2 + 1), bw = (w + 1) / 2, r, k;
            u32 p = cur;
            for (r = 0; r < fh; r++, p += pitch) {
                if (r < yo || r >= yo + gh) {
                    if (xl[0]) for (k = 0; k < w; k++) px(p + k, xl[0]);
                } else {
                    u32 src = go + (r - yo) * bw;
                    for (k = 0; k < w; k++) {
                        u8 b = rd8(src + k / 2), v = (k & 1) ? (b >> 4) : (b & 15), col = xl[v];
                        if (col) px(p + k, col);
                    }
                }
            }
        }
        x += w + xs; cur += w + xs; s++;
    }
    return s - str;
}

static u32 do_strwidth(u32 str)
{
    u32 font, widb, s;
    s32 xs, cur = 0, mx = 0;
    if (!str) return 0;
    font = rd32(D(D_FONTPTR));
    if (!font) return 0;
    pick_font(font);
    widb = font + rd16(font + 8);
    xs = (s32)rd32(D(D_XSPACE));
    for (s = str;; ) {
        u8 c = rd8(s);
        if (!c) break;
        if (c == '\r') { if (cur > mx) mx = cur; cur = 0; s++; continue; }
        if (c == ' ' && is_zw_space(s, str)) { s++; continue; }
        if (jpair(s)) { cur += jwidth() + xs; s += 2; continue; }
        /* 1 バイトずつ測られたとき: 前は 0、後は 1 字分 */
        if (is_jl(c) && rd8(s + 1) == 0 && s == str) { s++; continue; }
        if (is_jt(c) && s == str && rd8(s + 1) == 0) { cur += jwidth() + xs; s++; continue; }
        cur += (s16)(rd8(widb + c) + xs); s++;
    }
    if (cur > mx) mx = cur;
    return (u32)(u16)(s16)mx;
}

/* ===================== コールバック ===================== */
typedef u32 (*CBHandler)(void);

static int cb_enter(void)
{
    L_flush();
    if (!cpu_regs && !find_cpu_regs()) return 0;
    if (!located && !locate(cpu_regs[8])) return 0;
    return 1;
}
static int ncall, nprint;
static u32 cb_print_fn(void)
{
    u32 esp;
    if (ncall < 5) LOG("cb_print %d", ncall++);
    if (!cb_enter()) return 0;
    esp = cpu_regs[4];
    if (logfp && nprint < 60) {
        u32 vp = rd32(esp + 4), st = rd32(esp + 8), i;
        char sb[48];
        for (i = 0; i < 40 && rd8(st + i); i++) { u8 c = rd8(st + i); sb[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '.'; }
        sb[i] = 0;
        LOG("print vp=%08x buf=%08x w=%d h=%d xadd=%d +16=%d +20=%d +24=%d x=%d y=%d fc=%x \"%s\"", vp, rd32(vp), rd32(vp + 4), rd32(vp + 8),
            rd32(vp + 12), rd32(vp + 16), rd32(vp + 20), rd32(vp + 24), rd32(esp + 12), rd32(esp + 16), rd32(esp + 20), sb);
        nprint++;
    }
    cpu_regs[0] = do_print(rd32(esp + 4), rd32(esp + 8), (s32)rd32(esp + 12), (s32)rd32(esp + 16), rd32(esp + 20), rd32(esp + 24));
    return 0;
}
/* 画面のメモリへ直接描く版: 引数は同じ。書き込み先だけ DOSBox の画面のメモリへ */
static u32 cb_print2_fn(void)
{
    u32 esp;
    if (!cb_enter()) return 0;
    esp = cpu_regs[4];
    if (!vga_lin) find_vga_lin();
    if (logfp && nprint < 60) { LOG("print2 buf=%08x x=%d y=%d", rd32(rd32(esp + 4)), rd32(esp + 12), rd32(esp + 16)); nprint++; }
    g_banked = 1;
    cpu_regs[0] = do_print(rd32(esp + 4), rd32(esp + 8), (s32)rd32(esp + 12), (s32)rd32(esp + 16), rd32(esp + 20), rd32(esp + 24));
    g_banked = 0;
    return 0;
}
static u32 cb_width_fn(void)
{
    u32 esp;
    if (ncall < 5) LOG("cb_width %d", ncall++);
    if (!cb_enter()) return 0;
    esp = cpu_regs[4];
    cpu_regs[0] = do_strwidth(rd32(esp + 4));
    return 0;
}
static int alloc_cb(CBHandler h)
{
    u32 *tab = (u32 *)DB_CB_HANDLERS;
    int i;
    for (i = 127; i > 0; i--) {
        if (tab[i] == (u32)h) return i;
    }
    for (i = 127; i > 0; i--) {
        if (tab[i] == DB_CB_ILLEGAL) { tab[i] = (u32)h; return i; }
    }
    return -1;
}

/* LOLG.DAT を読んだバッファの中で、差し込み（FE 38 cb ; C3）を当てる */
static void patch_read(u8 *buf, u32 pos, u32 n)
{
    static const struct { u32 off; const u8 *orig; int *cb; } P[3] = {
        {F_BUFFER_PRINT, orig_bp, &cb_print}, {F_STRWIDTH, orig_sw, &cb_width}, {F_BUFFER_PRINT2, orig_bp2, &cb_print2}};
    int k, i;
    if (cb_print <= 0) cb_print = alloc_cb(cb_print_fn);
    if (cb_width <= 0) cb_width = alloc_cb(cb_width_fn);
    if (cb_print2 <= 0) cb_print2 = alloc_cb(cb_print2_fn);
    if (cb_print <= 0 || cb_width <= 0 || cb_print2 <= 0) return;
    for (k = 0; k < 3; k++) {
        u8 st[5];
        int bad = 0;
        st[0] = 0xFE; st[1] = 0x38; st[2] = (u8)*P[k].cb; st[3] = (u8)(*P[k].cb >> 8); st[4] = 0xC3;
        for (i = 0; i < 5; i++) {
            u32 fo = P[k].off + i;
            if (fo >= pos && fo < pos + n && buf[fo - pos] != P[k].orig[i]) bad = 1;
        }
        if (bad) { LOG("LOLG.DAT の中身が違う %08x", P[k].off); continue; }
        for (i = 0; i < 5; i++) {
            u32 fo = P[k].off + i;
            if (fo >= pos && fo < pos + n) buf[fo - pos] = st[i];
        }
        if (P[k].off >= pos && P[k].off < pos + n) LOG("LOLG.DAT 書き換え %08x cb=%d", P[k].off, *P[k].cb);
    }
}

/* ===================== 字幕 ===================== */
typedef struct { u32 file, off, size, nseg, seg; } SubEnt;
typedef struct { u32 start, end, text; } SubSeg;
static const SubEnt *sub_ent; static u32 sub_nent;
static const SubSeg *sub_seg; static const char *sub_text;
typedef struct { char name[24]; u32 first, count; } SubFile;
static const SubFile *sub_file; static u32 sub_nfile;

static const SubEnt *cur_sub; static DWORD cur_sub_t0;
static u32 shown_text = 0xffffffff;
static u8 sub_mask[480 * 72]; static int sub_w, sub_h, sub_top, sub_on;
static int sub_lastw;
/* 動画（大きいもの）を途中で飛ばしたら字幕を消すための、読み込みの様子 */
#define SUB_BIG 65536
static DWORD cur_last_read; static u32 cur_reads, cur_maxpos;

static void sub_stop(const char *why)
{
    if (cur_sub) LOG("字幕を消す（%s）", why);
    cur_sub = 0;
}

static void sub_trigger(u32 fileidx, u32 pos, u32 n)
{
    const SubFile *f = &sub_file[fileidx];
    u32 lo = f->first, hi = f->first + f->count;
    /* off が [pos, pos+n) に入るもの（二分探索） */
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (sub_ent[mid].off < pos) lo = mid + 1; else hi = mid;
    }
    if (lo < f->first + f->count && sub_ent[lo].off < pos + n) {
        cur_sub = &sub_ent[lo];
        cur_sub_t0 = cur_last_read = GetTickCount();
        cur_reads = 1; cur_maxpos = pos + n;
        LOG("字幕 %s +%08x", f->name, sub_ent[lo].off);
        return;
    }
    /* 今の動画の続きの読み込みか、別の所の読み込みか */
    if (cur_sub && cur_sub->file == fileidx && cur_sub->size >= SUB_BIG) {
        u32 a = cur_sub->off, b = cur_sub->off + cur_sub->size;
        if (pos < b && pos + n > a) {
            cur_last_read = GetTickCount(); cur_reads++;
            if (pos + n > cur_maxpos) cur_maxpos = pos + n;
        } else {
            sub_stop("同じ書庫の別の所を読んだ");
        }
    }
}

/* 書庫を閉じたら、その中の動画の字幕は終わり */
static void sub_closed(int fileidx)
{
    if (cur_sub && fileidx >= 0 && cur_sub->file == (u32)fileidx && cur_sub->size >= SUB_BIG)
        sub_stop("書庫を閉じた");
}

/* 字幕を 1 枚のマスク（0=透明 1=字 2=縁）に描く */
static void render_sub(const char *t, int scrw)
{
    const JFont *jf = scrw >= 600 ? (jf16.full ? &jf16 : &jf12) : (jf12.full ? &jf12 : &jf8);
    int lines = 1, li = 0, x, y, i;
    const u8 *s;
    int lw[4] = {0, 0, 0, 0};
    for (s = (const u8 *)t; *s; s++) if (*s == '\n') lines++;
    if (lines > 3) lines = 3;
    sub_h = lines * (jf->h + 2) + 2;
    sub_w = scrw;
    if (sub_w > 640) sub_w = 640;
    if ((u32)(sub_w * sub_h) > sizeof sub_mask) sub_h = sizeof sub_mask / sub_w;
    memset(sub_mask, 0, sub_w * sub_h);
    /* 行の幅を測って中央に */
    for (s = (const u8 *)t, li = 0; *s && li < 3; ) {
        if (*s == '\n') { li++; s++; continue; }
        if (is_lead(s[0]) && is_lead(s[1])) { lw[li] += jf->fw; s += 2; }
        else { lw[li] += jf->hw; s++; }
    }
    for (s = (const u8 *)t, li = 0, x = (sub_w - lw[0]) / 2; *s && li < 3; ) {
        const u16 *g16 = 0; const u8 *g8 = 0; int gw;
        if (*s == '\n') { li++; s++; x = (sub_w - lw[li]) / 2; continue; }
        if (is_lead(s[0]) && is_lead(s[1])) { g16 = jglyph(jf, s[0], s[1]); gw = jf->fw; s += 2; }
        else { g8 = hglyph(jf, s[0]); gw = jf->hw; s++; }
        for (y = 0; y < jf->h; y++) {
            for (i = 0; i < gw; i++) {
                int on = g16 ? (g16[y] << i) & 0x8000 : g8 ? (g8[y] << i) & 0x80 : 0;
                if (on) {
                    int yy = 1 + li * (jf->h + 2) + y, xx = x + i, dx, dy;
                    for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
                        int ax = xx + dx, ay = yy + dy;
                        if (ax >= 0 && ay >= 0 && ax < sub_w && ay < sub_h && sub_mask[ay * sub_w + ax] == 0)
                            sub_mask[ay * sub_w + ax] = 2;
                    }
                    if (xx >= 0 && xx < sub_w && yy < sub_h) sub_mask[yy * sub_w + xx] = 1;
                }
            }
        }
        x += gw;
    }
}

static u8 col_white, col_black;
static void pick_colors(void)
{
    const u8 *rgb = (const u8 *)DB_DAC_RGB;
    int i, bw = -1, bb = 1 << 30, ww = -1, bwv = -1;
    for (i = 0; i < 256; i++) {
        int r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        int lum = r * 3 + g * 6 + b, dk = r * r + g * g + b * b;
        int dw = (63 - r) * (63 - r) + (63 - g) * (63 - g) + (63 - b) * (63 - b);
        if (ww < 0 || dw < bwv) { ww = i; bwv = dw; }
        if (dk < bb) { bb = dk; bw = i; }
        (void)lum;
    }
    col_white = (u8)ww; col_black = (u8)bw;
}

/* 1 フレームの最初の行で、字幕の表示状態を決める */
static void sub_frame(void)
{
    u32 scrw = *(u32 *)DB_SRC_WIDTH, scrh = *(u32 *)DB_SRC_HEIGHT;
    u32 t, k, want = 0xffffffff;
    sub_on = 0;
    /* 動画を少しずつ読んでいたのに、終わりまで読まないまま 2 秒止まった → 飛ばされた */
    if (cur_sub && cur_sub->size >= SUB_BIG && cur_reads >= 3 &&
        cur_maxpos < cur_sub->off + cur_sub->size && GetTickCount() - cur_last_read > 2000)
        sub_stop("読み込みが止まった");
    if (!cur_sub) { shown_text = 0xffffffff; return; }
    t = GetTickCount() - cur_sub_t0;
    for (k = 0; k < cur_sub->nseg; k++) {
        const SubSeg *sg = &sub_seg[cur_sub->seg + k];
        if (t >= sg->start && t < sg->end) { want = sg->text; break; }
    }
    if (want == 0xffffffff) {
        const SubSeg *last = &sub_seg[cur_sub->seg + cur_sub->nseg - 1];
        if (t >= last->end) cur_sub = 0;
        shown_text = 0xffffffff;
        return;
    }
    if (scrw < 200 || scrw > 1024 || scrh < 150) return;
    if (want != shown_text || (int)scrw != sub_lastw) {
        render_sub(sub_text + want, scrw);
        shown_text = want; sub_lastw = scrw;
    }
    pick_colors();
    sub_top = (int)scrh - sub_h - (scrh >= 400 ? 12 : 6);
    sub_on = 1;
}

typedef void (__cdecl *LineFn)(const void *);
static u8 linebuf[4096 * 4];
static void __cdecl my_drawline(const void *src)
{
    LineFn orig = *(LineFn *)DB_RENDER_LINE;
    u32 y = *(u32 *)DB_LINES_DONE;
    if (y == 0) { sub_frame(); if (src) last_src0 = (const u8 *)src; }
    if (sub_on && src && *(u32 *)DB_SRC_BPP == 8) {
        int ry = (int)y - sub_top;
        u32 w = *(u32 *)DB_SRC_WIDTH;
        if (ry >= 0 && ry < sub_h && w <= sizeof linebuf) {
            int x0 = ((int)w - sub_w) / 2, i;
            const u8 *m = sub_mask + ry * sub_w;
            memcpy(linebuf, src, w);
            for (i = 0; i < sub_w; i++) {
                int xx = x0 + i;
                if (xx < 0 || xx >= (int)w || !m[i]) continue;
                linebuf[xx] = m[i] == 1 ? col_white : col_black;
            }
            orig(linebuf);
            return;
        }
    }
    orig(src);
}

/* ===================== ファイルの横取り ===================== */
typedef void *(__cdecl *fopen_t)(const char *, const char *);
typedef size_t (__cdecl *fread_t)(void *, size_t, size_t, void *);
typedef int (__cdecl *fseek_t)(void *, long, int);
typedef long (__cdecl *ftell_t)(void *);
typedef int (__cdecl *fclose_t)(void *);
static fopen_t r_fopen; static fread_t r_fread; static fseek_t r_fseek; static ftell_t r_ftell; static fclose_t r_fclose;

typedef struct {
    void *fp;
    int virt;            /* 1 = 中身を差し替え（読みはここで返す） */
    u8 *hdr; u32 hdrlen; /* 先頭の差し替え（MIX の索引） */
    u32 origsize;
    const u8 *whole;     /* 丸ごと差し替え（ばらの TRR） */
    u8 *tail; u32 taillen;
    u32 vsize, pos;
    int subfile;         /* 字幕の表の番号（-1 = 無し） */
    int lolg;            /* LOLG.DAT（読んだ所に差し込みを当てる） */
} VF;
#define MAXVF 64
static VF vfs[MAXVF];
static VF *vf_find(void *fp) { int i; for (i = 0; i < MAXVF; i++) if (vfs[i].fp == fp) return &vfs[i]; return 0; }

/* "..\\DAT\\L4_HJI.MIX" → "DAT/L4_HJI.MIX"、"..\\LOCAL.MIX" → "LOCAL.MIX"（大文字） */
static void norm_name(const char *path, char *out, int outn)
{
    const char *b = path + strlen(path), *e = b;
    int parts = 0, i;
    char tmp[260];
    while (b > path) {
        if (b[-1] == '\\' || b[-1] == '/') { if (++parts == 2) break; }
        b--;
    }
    /* 親フォルダが DAT のときだけ残す */
    for (i = 0; b + i < e && i < 259; i++) tmp[i] = (char)toupper((u8)b[i]);
    tmp[i] = 0;
    if (strncmp(tmp, "DAT\\", 4) == 0 || strncmp(tmp, "DAT/", 4) == 0) { tmp[3] = '/'; strncpy(out, tmp, outn); }
    else {
        const char *q = strrchr(tmp, '\\'), *q2 = strrchr(tmp, '/');
        if (q2 > q) q = q2;
        strncpy(out, q ? q + 1 : tmp, outn);
    }
    out[outn - 1] = 0;
}

/* 'MIXP': { char name[24]; u32 id; u32 size; data(4 揃え) }... */
/* 'LOOS': { char name[24]; u32 size; data } */
static int setup_vf(VF *v, const char *nm)
{
    u32 sz, p;
    const u8 *sec;
    int any = 0;
    /* ばらのファイル */
    sec = find_sec("LOOS", &sz);
    for (p = 0; sec && p + 28 <= sz; ) {
        u32 n = *(const u32 *)(sec + p + 24);
        if (_stricmp((const char *)sec + p, nm) == 0) {
            v->virt = 1; v->whole = sec + p + 28; v->vsize = n; v->pos = 0;
            LOG("差し替え %s (%u)", nm, n);
            return 1;
        }
        p += 28 + ((n + 3) & ~3u);
    }
    /* MIX の中身 */
    sec = find_sec("MIXP", &sz);
    for (p = 0; sec && p + 32 <= sz; ) {
        u32 id = *(const u32 *)(sec + p + 24), n = *(const u32 *)(sec + p + 28);
        if (_stricmp((const char *)sec + p, nm) == 0) {
            if (!any) {
                u8 h6[6]; u16 cnt;
                long save = r_ftell(v->fp);
                r_fseek(v->fp, 0, SEEK_END); v->origsize = (u32)r_ftell(v->fp);
                r_fseek(v->fp, 0, SEEK_SET);
                if (r_fread(h6, 1, 6, v->fp) != 6) { r_fseek(v->fp, save, SEEK_SET); return 0; }
                cnt = *(u16 *)h6;
                v->hdrlen = 6 + cnt * 12;
                v->hdr = (u8 *)malloc(v->hdrlen);
                memcpy(v->hdr, h6, 6);
                r_fread(v->hdr + 6, 1, cnt * 12, v->fp);
                r_fseek(v->fp, save, SEEK_SET);
                v->tail = 0; v->taillen = 0;
                any = 1;
            }
            {
                u16 cnt = *(u16 *)v->hdr, k;
                for (k = 0; k < cnt; k++) {
                    u8 *e = v->hdr + 6 + k * 12;
                    if (*(u32 *)e == id) {
                        u32 newoff = v->origsize - v->hdrlen + v->taillen;
                        v->tail = (u8 *)realloc(v->tail, v->taillen + n);
                        memcpy(v->tail + v->taillen, sec + p + 32, n);
                        v->taillen += n;
                        *(u32 *)(e + 4) = newoff; *(u32 *)(e + 8) = n;
                        LOG("差し替え %s:%08x (%u)", nm, id, n);
                    }
                }
            }
        }
        p += 32 + ((n + 3) & ~3u);
    }
    if (any) {
        *(u32 *)(v->hdr + 2) = v->origsize - v->hdrlen + v->taillen;
        v->virt = 1; v->vsize = v->origsize + v->taillen; v->pos = 0;
        return 1;
    }
    return 0;
}

static void *__cdecl my_fopen(const char *path, const char *mode)
{
    void *fp = r_fopen(path, mode);
    char nm[32];
    u32 i;
    if (!fp || !path) return fp;
    norm_name(path, nm, sizeof nm);
    if (blob) {
        VF *v = vf_find(0);
        if (v) {
            memset(v, 0, sizeof *v);
            v->fp = fp; v->subfile = -1;
            for (i = 0; i < sub_nfile; i++) if (_stricmp(sub_file[i].name, nm) == 0) v->subfile = (int)i;
            v->lolg = _stricmp(nm, "LOLG.DAT") == 0;
            if (!setup_vf(v, nm) && v->subfile < 0 && !v->lolg) v->fp = 0;
        }
    }
    LOG("fopen %s -> %s", path, nm);
    return fp;
}

static size_t __cdecl my_fread(void *buf, size_t sz, size_t cnt, void *fp)
{
    VF *v = fp ? vf_find(fp) : 0;
    u32 n, done = 0, pos;
    if (!v) return r_fread(buf, sz, cnt, fp);
    if (!v->virt) {
        long p0 = r_ftell(fp);
        size_t r = r_fread(buf, sz, cnt, fp);
        if (v->subfile >= 0 && r) sub_trigger((u32)v->subfile, (u32)p0, (u32)(r * sz));
        if (v->lolg && r) patch_read((u8 *)buf, (u32)p0, (u32)(r * sz));
        return r;
    }
    n = (u32)(sz * cnt);
    pos = v->pos;
    if (pos >= v->vsize) return 0;
    if (n > v->vsize - pos) n = v->vsize - pos;
    if (v->subfile >= 0) sub_trigger((u32)v->subfile, pos, n);
    while (done < n) {
        u32 p = pos + done, k;
        if (v->whole) { memcpy((u8 *)buf + done, v->whole + p, n - done); done = n; break; }
        if (p < v->hdrlen) {
            k = v->hdrlen - p; if (k > n - done) k = n - done;
            memcpy((u8 *)buf + done, v->hdr + p, k); done += k;
        } else if (p < v->origsize) {
            size_t r;
            k = v->origsize - p; if (k > n - done) k = n - done;
            r_fseek(fp, (long)p, SEEK_SET);
            r = r_fread((u8 *)buf + done, 1, k, fp);
            done += (u32)r;
            if (r < k) break;
        } else {
            k = v->vsize - p; if (k > n - done) k = n - done;
            memcpy((u8 *)buf + done, v->tail + (p - v->origsize), k); done += k;
        }
    }
    v->pos = pos + done;
    return sz ? done / sz : 0;
}

static int __cdecl my_fseek(void *fp, long off, int whence)
{
    VF *v = fp ? vf_find(fp) : 0;
    long np;
    if (!v || !v->virt) return r_fseek(fp, off, whence);
    np = whence == SEEK_SET ? off : whence == SEEK_CUR ? (long)v->pos + off : (long)v->vsize + off;
    if (np < 0) return -1;
    v->pos = (u32)np;
    return 0;
}
static long __cdecl my_ftell(void *fp)
{
    VF *v = fp ? vf_find(fp) : 0;
    if (!v || !v->virt) return r_ftell(fp);
    return (long)v->pos;
}
static int __cdecl my_fclose(void *fp)
{
    VF *v = fp ? vf_find(fp) : 0;
    if (v) {
        sub_closed(v->subfile);
        if (v->hdr) free(v->hdr);
        if (v->tail) free(v->tail);
        memset(v, 0, sizeof *v);
    }
    return r_fclose(fp);
}

/* ===================== 取り付け ===================== */
static void put32(u32 addr, u32 val)
{
    DWORD old;
    VirtualProtect((void *)addr, 4, PAGE_EXECUTE_READWRITE, &old);
    *(u32 *)addr = val;
    VirtualProtect((void *)addr, 4, old, &old);
}
static void patch_call(u32 at, void *fn)
{
    DWORD old;
    u8 *p = (u8 *)at;
    VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE8; *(u32 *)(p + 1) = (u32)fn - (at + 5); p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
}

static int check_dosbox(void)
{
    static const u8 call[6] = {0xff, 0x15, 0x30, 0xbd, 0xee, 0x01};
    HMODULE crt = GetModuleHandleA("msvcrt.dll");
    if (!crt) return 0;
    if (IsBadReadPtr((void *)DB_DRAWLINE_CALL1, 6) || memcmp((void *)DB_DRAWLINE_CALL1, call, 6) || memcmp((void *)DB_DRAWLINE_CALL2, call, 6)) return 0;
    if (*(void **)IAT_FOPEN != (void *)GetProcAddress(crt, "fopen")) return 0;
    if (*(void **)IAT_FREAD != (void *)GetProcAddress(crt, "fread")) return 0;
    if (*(void **)IAT_FSEEK != (void *)GetProcAddress(crt, "fseek")) return 0;
    if (*(void **)IAT_FTELL != (void *)GetProcAddress(crt, "ftell")) return 0;
    if (*(void **)IAT_FCLOSE != (void *)GetProcAddress(crt, "fclose")) return 0;
    return 1;
}

static void load_blob(HMODULE self)
{
    HRSRC r = FindResourceA(self, "LOL2JP", (LPCSTR)RT_RCDATA);
    HGLOBAL g;
    u32 sz;
    if (!r) return;
    g = LoadResource(self, r);
    sz = SizeofResource(self, r);
    if (!g || sz < 8) return;
    blob = (const u8 *)LockResource(g); blobsize = sz;
    if (memcmp(blob, "L2JP", 4)) { blob = 0; return; }
    { u32 n; cmap = (const u16 *)find_sec("CMAP", &n); ncmap = cmap ? n / 2 : 0; }
    load_font(&jf8, "FN08"); load_font(&jf12, "FN12"); load_font(&jf14, "FN14"); load_font(&jf16, "FN16");
    {
        u32 s1, s2, s3;
        sub_file = (const SubFile *)find_sec("SUBF", &s1); sub_nfile = sub_file ? s1 / sizeof(SubFile) : 0;
        sub_ent = (const SubEnt *)find_sec("SUBE", &s2); sub_nent = sub_ent ? s2 / sizeof(SubEnt) : 0;
        sub_seg = (const SubSeg *)find_sec("SUBS", &s3);
        sub_text = (const char *)find_sec("SUBT", 0);
        if (!sub_ent || !sub_seg || !sub_text) sub_nfile = 0;
    }
}

static void install(HMODULE self)
{
#ifdef LOL2JP_TEST
    /* 試験用: lol2jp_debug.txt があればログを書く */
    char path[MAX_PATH], *q;
    GetModuleFileNameA(self, path, MAX_PATH);
    q = strrchr(path, '\\');
    if (q) {
        strcpy(q + 1, "lol2jp_debug.txt");
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            strcpy(q + 1, "lol2jp.log");
            logfp = fopen(path, "w");
        }
    }
#endif
    if (!check_dosbox()) { LOG("DOSBox の版が違うので何もしない"); return; }
    load_blob(self);
    LOG("データ %u バイト、字幕 %u 件", blobsize, sub_nent);
    r_fopen = *(fopen_t *)IAT_FOPEN; r_fread = *(fread_t *)IAT_FREAD; r_fseek = *(fseek_t *)IAT_FSEEK;
    r_ftell = *(ftell_t *)IAT_FTELL; r_fclose = *(fclose_t *)IAT_FCLOSE;
    put32(IAT_FOPEN, (u32)my_fopen); put32(IAT_FREAD, (u32)my_fread); put32(IAT_FSEEK, (u32)my_fseek);
    put32(IAT_FTELL, (u32)my_ftell); put32(IAT_FCLOSE, (u32)my_fclose);
    patch_call(DB_DRAWLINE_CALL1, (void *)my_drawline);
    patch_call(DB_DRAWLINE_CALL2, (void *)my_drawline);
    LOG("取り付け完了");
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

#ifdef LOL2JP_TEST
#include "testctl.inc"
#endif

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        load_winmm();
        install(h);
#ifdef LOL2JP_TEST
        ctl_start();
#endif
    }
    return TRUE;
}
