/* 試験用の操作（配布版には入れない）
   LOL3JP\cmd.txt に命令を書くと実行し、終わると LOL3JP\cmd.done を作る。
     shot <名前>      画面を LOL3JP\shot\<名前>.bmp に書き出す
     tap <vk> [ms]    キーを押して離す（vk は 16 進）
     down <vk> / up <vk>
     move <x> <y>     マウスを動かす（ゲームの画面の座標）
     click <x> <y> [r]  左（r なら右）クリック
     wait <ms>
*/
static HWND g_hwnd;
static volatile u8 g_fake[256];
static volatile int g_fakemouse, g_mx, g_my;
static volatile int g_shot_req;
static char g_shot_name[MAX_PATH];

typedef SHORT (WINAPI *gks_fn)(int);
static gks_fn o_gaks, o_gks;
static SHORT WINAPI my_gaks(int vk) { SHORT r = o_gaks(vk); if (g_fake[vk & 255]) r |= (SHORT)0x8001; return r; }
static SHORT WINAPI my_gks(int vk) { SHORT r = o_gks(vk); if (g_fake[vk & 255]) r |= (SHORT)0x8000; return r; }

typedef BOOL (WINAPI *gcp_fn)(LPPOINT);
static gcp_fn o_gcp;
static BOOL WINAPI my_gcp(LPPOINT p)
{
    BOOL r = o_gcp(p);
    if (g_fakemouse && p) { p->x = g_mx; p->y = g_my; }
    return r;
}

/* 試験中に他の窓が前面を取ってもゲームが 3D を止めないよう、非活性の知らせを握りつぶす */
static WNDPROC g_oldproc;
static LRESULT CALLBACK keep_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ACTIVATEAPP && !w) { LOG("非活性(ACTIVATEAPP)を無視"); return 0; }
    if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) { LOG("非活性(ACTIVATE)を無視"); return 0; }
    if (m == WM_KILLFOCUS) return 0;
    return CallWindowProcA(g_oldproc, h, m, w, l);
}

typedef HWND (WINAPI *cwe_fn)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static cwe_fn o_cwe;
static HWND WINAPI my_cwe(DWORD ex, LPCSTR cls, LPCSTR name, DWORD st, int x, int y, int w, int h, HWND par, HMENU m, HINSTANCE in, LPVOID lp)
{
    HWND r = o_cwe(ex, cls, name, st, x, y, w, h, par, m, in, lp);
    if (!g_hwnd && r) {
        g_hwnd = r;
        if (!GetEnvironmentVariableA("LOL3JP_NOKEEP", NULL, 0))
            g_oldproc = (WNDPROC)SetWindowLongA(r, GWL_WNDPROC, (LONG)keep_proc);
    }
    LOG("窓 %s %p", name && (u32)name > 0xffff ? name : "", r);
    return r;
}

/* ---- Glide の画面の書き出し ---- */
typedef void (__stdcall *swap_fn)(u32);
typedef int (__stdcall *lfbread_fn)(u32, u32, u32, u32, u32, u32, void *);
typedef u32 (__stdcall *scr_fn)(void);
static swap_fn o_swap;
static lfbread_fn o_lfbread;
static scr_fn o_scrw, o_scrh;

static void save_bmp565(const char *path, const u16 *px, int w, int h, int stride_px)
{
    FILE *f = fopen(path, "wb");
    int x, y, rowb = (w * 3 + 3) & ~3;
    u8 hdr[54] = { 'B', 'M' };
    u8 *row;
    if (!f) return;
    *(u32 *)(hdr + 2) = 54 + rowb * h; *(u32 *)(hdr + 10) = 54; *(u32 *)(hdr + 14) = 40;
    *(int *)(hdr + 18) = w; *(int *)(hdr + 22) = h; *(u16 *)(hdr + 26) = 1; *(u16 *)(hdr + 28) = 24;
    fwrite(hdr, 1, 54, f);
    row = (u8 *)calloc(rowb, 1);
    for (y = h - 1; y >= 0; y--) {
        for (x = 0; x < w; x++) {
            u16 c = px[y * stride_px + x];
            row[x * 3 + 0] = (u8)((c & 31) << 3);
            row[x * 3 + 1] = (u8)(((c >> 5) & 63) << 2);
            row[x * 3 + 2] = (u8)(((c >> 11) & 31) << 3);
        }
        fwrite(row, 1, rowb, f);
    }
    free(row);
    fclose(f);
}

static void __stdcall my_swap(u32 interval)
{
    if (g_shot_req && o_lfbread) {
        int w = o_scrw ? o_scrw() : 640, h = o_scrh ? o_scrh() : 480;
        u16 *buf = (u16 *)malloc(w * h * 2);
        if (buf) {
            char p[MAX_PATH];
            o_lfbread(1 /* 裏の画面 */, 0, 0, w, h, w * 2, buf);
            wsprintfA(p, "%s\\shot\\%s.bmp", g_dir, g_shot_name);
            save_bmp565(p, buf, w, h, w);
            free(buf);
        }
        g_shot_req = 0;
    }
    o_swap(interval);
}

static FARPROC glide_wrap(LPCSTR n, FARPROC p)
{
    if (!lstrcmpA(n, "_grBufferSwap@4")) { o_swap = (swap_fn)p; return (FARPROC)my_swap; }
    if (!lstrcmpA(n, "_grLfbReadRegion@28")) o_lfbread = (lfbread_fn)p;
    if (!lstrcmpA(n, "_grSstScreenWidth@0")) o_scrw = (scr_fn)p;
    if (!lstrcmpA(n, "_grSstScreenHeight@0")) o_scrh = (scr_fn)p;
    return p;
}

static LPARAM key_lp(int vk, int up)
{
    UINT sc = MapVirtualKeyA(vk, 0);
    u32 ext = ((vk >= 0x21 && vk <= 0x2E) || vk == VK_RCONTROL || vk == VK_RMENU) ? (1u << 24) : 0;
    if (vk == 0x200) { vk = 0; }
    return 1 | (sc << 16) | ext | (up ? (3u << 30) : 0);
}

static void do_cmd(char *line)
{
    char op[32] = { 0 };
    int a = 0, b = 0;
    char s[MAX_PATH] = { 0 };
    if (sscanf(line, "%31s", op) != 1) return;
    if (!strcmp(op, "shot")) {
        sscanf(line, "%*s %259s", s);
        lstrcpyA(g_shot_name, s);
        g_shot_req = 1;
        for (a = 0; a < 300 && g_shot_req; a++) Sleep(10);
        if (g_shot_req) { g_shot_req = 0; LOG("shot 失敗（画面の切り替えが来ない）"); }
    } else if (!strcmp(op, "tap") || !strcmp(op, "down") || !strcmp(op, "up")) {
        b = 80;
        sscanf(line, "%*s %x %d", &a, &b);
        a &= 255;
        if (strcmp(op, "up")) { g_fake[a] = 1; PostMessageA(g_hwnd, WM_KEYDOWN, a, key_lp(a, 0)); }
        if (!strcmp(op, "tap")) Sleep(b);
        if (strcmp(op, "down")) { g_fake[a] = 0; PostMessageA(g_hwnd, WM_KEYUP, a, key_lp(a, 1)); }
    } else if (!strcmp(op, "move")) {
        sscanf(line, "%*s %d %d", &a, &b);
        g_mx = a; g_my = b; g_fakemouse = 1;
        PostMessageA(g_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(a, b));
    } else if (!strcmp(op, "click")) {
        sscanf(line, "%*s %d %d %3s", &a, &b, s);
        g_mx = a; g_my = b; g_fakemouse = 1;
        PostMessageA(g_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(a, b));
        Sleep(60);
        if (s[0] == 'r') {
            g_fake[VK_RBUTTON] = 1; PostMessageA(g_hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(a, b)); Sleep(100);
            g_fake[VK_RBUTTON] = 0; PostMessageA(g_hwnd, WM_RBUTTONUP, 0, MAKELPARAM(a, b));
        } else {
            g_fake[VK_LBUTTON] = 1; PostMessageA(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(a, b)); Sleep(100);
            g_fake[VK_LBUTTON] = 0; PostMessageA(g_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(a, b));
        }
    } else if (!strcmp(op, "sub")) {      /* sub <行> <ms>: 字幕を直接出す */
        b = 4000;
        sscanf(line, "%*s %d %d", &a, &b);
        sub_trigger(a, (u32)b);
    } else if (!strcmp(op, "wait")) {
        sscanf(line, "%*s %d", &a);
        Sleep(a);
    }
}

static DWORD WINAPI ctl_thread(LPVOID p)
{
    char cmd[MAX_PATH], done[MAX_PATH], tmp[MAX_PATH];
    (void)p;
    wsprintfA(cmd, "%s\\cmd.txt", g_dir);
    wsprintfA(tmp, "%s\\cmd.run", g_dir);
    wsprintfA(done, "%s\\cmd.done", g_dir);
    wsprintfA(g_shot_name, "%s\\shot", g_dir);
    CreateDirectoryA(g_shot_name, NULL);
    for (;;) {
        FILE *f;
        Sleep(50);
        DeleteFileA(tmp);
        if (!MoveFileA(cmd, tmp)) continue;
        f = fopen(tmp, "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof line, f)) do_cmd(line);
            fclose(f);
        }
        f = fopen(done, "w"); if (f) fclose(f);
    }
    return 0;
}

/* 落ちた時に積み上げを記録する */
static LONG CALLBACK veh(EXCEPTION_POINTERS *e)
{
    static int once;
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && !once) {
        u32 *sp = (u32 *)e->ContextRecord->Esp; int i; char b[1200]; int n = 0;
        once = 1;
        n += wsprintfA(b + n, "落ちた eip=%08x esp=%08x 積み:", e->ContextRecord->Eip, (u32)sp);
        __try {
            for (i = 0; i < 160 && n < 1100; i++) {
                u32 v = sp[i];
                if ((v >= 0x401000 && v < 0x540000) || (v >= 0x10001000 && v < 0x10200000)) n += wsprintfA(b + n, " %x:%08x", i, v);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        n += wsprintfA(b + n, " 我=%p", (void *)veh);
        LOG("%s", b);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void ctl_start(HMODULE m)
{
    AddVectoredExceptionHandler(1, veh);
    o_gaks = (gks_fn)patch_iat(m, "USER32.dll", "GetAsyncKeyState", my_gaks);
    o_gks = (gks_fn)patch_iat(m, "USER32.dll", "GetKeyState", my_gks);
    o_gcp = (gcp_fn)patch_iat(m, "USER32.dll", "GetCursorPos", my_gcp);
    o_cwe = (cwe_fn)patch_iat(m, "USER32.dll", "CreateWindowExA", my_cwe);
    CreateThread(NULL, 0, ctl_thread, NULL, 0, NULL);
    LOG("試験の操作を開始");
}
