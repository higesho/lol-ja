/* 試験: ゲームの窓を作る前に字の描画だけを試して終わる（試験用 DLL のみ）
   環境変数 LOL3JP_DRAWTEST=<文の一覧>;<出力.bmp> を付けて LOL3.DAT を起動する。
   文の一覧は EUC-JP、1 行 1 文で「字体名 色 文」。文の中の | は改行。 */
static u8 *dt_from_wsx(const char *wsx, const char *name)
{
    HANDLE h = CreateFileA(wsx, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    u32 id = 0;
    const char *p;
    DWORD sz;
    u8 *b;
    for (p = name; *p; p++) {
        int c = (u8)*p;
        if (c >= 'a' && c <= 'z') c -= 32;
        id = (id >> 6) | (id << 26);
        id += (c - 0x30) & 0x3f;
    }
    if (h == INVALID_HANDLE_VALUE) return NULL;
    b = wsx_get(h, id, &sz);
    CloseHandle(h);
    return b;
}

/* 字幕の試験: LOL3JP_SUBTEST=<会話の行番号,...>;<出力.bmp>（行ごとに上から並べて描く） */
static void sub_test(const char *arg)
{
    static u16 scr[640 * 480 * 4];
    const char *sc = strchr(arg, ';');
    char outp[MAX_PATH];
    int idxs[8], n = 0, k, y;
    FILE *o;
    if (!sc) return;
    lstrcpyA(outp, sc + 1);
    {
        const char *p = arg;
        while (p < sc && n < 8) { idxs[n++] = atoi(p); while (p < sc && *p != ',') p++; if (*p == ',') p++; }
    }
    o = fopen(outp, "wb");
    if (!o) return;
    {
        static u8 hdr[54];
        int H = 480 * n;
        memset(hdr, 0, sizeof hdr);
        hdr[0] = 'B'; hdr[1] = 'M';
        *(u32 *)(hdr + 2) = 54 + 640 * 3 * H; *(u32 *)(hdr + 10) = 54; *(u32 *)(hdr + 14) = 40;
        *(int *)(hdr + 18) = 640; *(int *)(hdr + 22) = H; *(u16 *)(hdr + 26) = 1; *(u16 *)(hdr + 28) = 24;
        fwrite(hdr, 1, 54, o);
        for (k = n - 1; k >= 0; k--) {
            int i;
            for (i = 0; i < 640 * 480; i++) scr[i] = (u16)(((i / 640) * 20 / 480) << 11 | 0x0208);   /* 背景の色 */
            g_cur_idx = idxs[k]; g_cur_until = GetTickCount() + 10000;
            g_cur_ms = 10000; g_cur_start = GetTickCount() - (k == n - 1 ? 9990 : 0);   /* 最後の行は最後のページ */
            { SubLay L; g_mov_on = 1; if (sub_layout(640, 480, &L)) sub_draw565(scr, 640, 640, 480, &L, 0); }
            for (y = 479; y >= 0; y--) {
                u8 row[640 * 3];
                int x;
                for (x = 0; x < 640; x++) {
                    u16 c = scr[y * 640 + x];
                    row[x * 3] = (u8)((c & 31) << 3); row[x * 3 + 1] = (u8)(((c >> 5) & 63) << 2); row[x * 3 + 2] = (u8)((c >> 11) << 3);
                }
                fwrite(row, 1, sizeof row, o);
            }
        }
    }
    fclose(o);
    LOG("試験: 字幕を書いた %s", outp);
}

typedef void (__cdecl *setfont_fn)(void *);
typedef void (__cdecl *setpal_fn)(void *, int, int);

static void draw_test(const char *arg)
{
    static u8 screen[640 * 480];
    VP vp = { screen, 640, 480, 0, 0, 0, NULL, 0 };
    char list[MAX_PATH], outp[MAX_PATH], wsx[MAX_PATH], line[2048];
    const char *sc = strchr(arg, ';');
    u8 *pal;
    FILE *f, *o;
    int y = 4, i;
    if (!sc) return;
    lstrcpynA(list, arg, (int)(sc - arg) + 1);
    lstrcpyA(outp, sc + 1);
    wsprintfA(wsx, "%s\\DAT\\LOCAL.WSX", g_game);
    pal = dt_from_wsx(wsx, "basepal.pal");
    f = fopen(list, "rb");
    if (!f) { LOG("試験: 一覧が開けない %s", list); return; }
    while (fgets(line, sizeof line, f)) {
        char fname[32];
        int col, n = 0, lines = 1, w;
        u8 *font;
        char *s;
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "%31s %d %n", fname, &col, &n) < 2) continue;
        s = line + n;
        font = dt_from_wsx(wsx, fname);
        if (!font) { LOG("試験: 字体なし %s", fname); continue; }
        ((setfont_fn)0x504920)(font);
        {
            u8 tbl[16];
            int k;
            for (k = 0; k < 16; k++) tbl[k] = (u8)(k ? col + k - 1 : 0);
            ((setpal_fn)0x5048d0)(tbl, 0, 15);
        }
        for (i = 0; s[i]; i++) if (s[i] == '|') s[i] = '\r';
        my_print(&vp, s, 8, y, col, 0);
        w = my_strw(s);
        for (i = 0; s[i]; i++) if (s[i] == '\r') lines++;
        y += lines * (V_FONT_HEIGHT + V_LINE_SPACE);
        for (i = 0; i < w && 8 + i < 640 && y < 480; i++) screen[y * 640 + 8 + i] = (u8)col;   /* 測った幅 */
        y += 4;
        if (y > 470) break;
    }
    fclose(f);
    o = fopen(outp, "wb");
    if (o) {
        static u8 hdr[54 + 1024];
        int k;
        memset(hdr, 0, sizeof hdr);
        hdr[0] = 'B'; hdr[1] = 'M';
        *(u32 *)(hdr + 2) = 54 + 1024 + 640 * 480; *(u32 *)(hdr + 10) = 54 + 1024; *(u32 *)(hdr + 14) = 40;
        *(int *)(hdr + 18) = 640; *(int *)(hdr + 22) = 480; *(u16 *)(hdr + 26) = 1; *(u16 *)(hdr + 28) = 8;
        *(u32 *)(hdr + 46) = 256;
        for (k = 0; k < 256; k++) {
            u8 r = pal ? pal[k * 3] : (u8)k, g = pal ? pal[k * 3 + 1] : (u8)k, b = pal ? pal[k * 3 + 2] : (u8)k;
            if (pal && r < 64 && g < 64 && b < 64) { r <<= 2; g <<= 2; b <<= 2; }
            hdr[54 + k * 4] = b; hdr[54 + k * 4 + 1] = g; hdr[54 + k * 4 + 2] = r;
        }
        fwrite(hdr, 1, sizeof hdr, o);
        for (k = 479; k >= 0; k--) fwrite(screen + k * 640, 1, 640, o);
        fclose(o);
    }
    LOG("試験: 書いた %s", outp);
}
