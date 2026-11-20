/* 試験用の操作口（loljp.ini の [Debug] Test=1 のときだけ）
   共有メモリ Local\LOLJP_CTL_<pid> に命令を書くと、SDL のイベントとして積む */
#include "loljp.h"

typedef struct { uint8_t type, which, state; uint16_t x, y; int16_t xrel, yrel; } SMotion;
typedef struct { uint8_t type, which, button, state; uint16_t x, y; } SButton;
typedef struct { uint8_t scancode; int sym; int mod; uint16_t unicode; } SKeysym;
typedef struct { uint8_t type, which, state; SKeysym keysym; } SKey;

typedef union { uint8_t type; uint8_t pad[64]; } SEvent;

typedef struct {
    volatile LONG seq, ack;
    int cmd, a, b, c, d;
} Ctl;

static int (__cdecl *pPushEvent)(SEvent *);
static Ctl *g_ctl;

static void Push(SEvent *e) { if (pPushEvent) pPushEvent(e); }

static void DoCmd(int cmd, int a, int b, int c, int d)
{
    SEvent e;
    memset(&e, 0, sizeof e);
    switch (cmd) {
    case 1: { /* 移動 a=x b=y（窓の座標） */
        SMotion *m = (SMotion *)&e;
        m->type = 4; m->x = (uint16_t)a; m->y = (uint16_t)b; m->xrel = (int16_t)c; m->yrel = (int16_t)d;
        Push(&e); break; }
    case 2: { /* ボタン a=1左 3右 b=1押す 0離す c,d=座標 */
        SButton *bt = (SButton *)&e;
        bt->type = b ? 5 : 6; bt->button = (uint8_t)a; bt->state = (uint8_t)b; bt->x = (uint16_t)c; bt->y = (uint16_t)d;
        Push(&e); break; }
    case 3: { /* キー a=scancode b=sym c=1押す 0離す d=unicode */
        SKey *k = (SKey *)&e;
        k->type = c ? 2 : 3; k->state = (uint8_t)c; k->keysym.scancode = (uint8_t)a; k->keysym.sym = b; k->keysym.unicode = (uint16_t)d;
        Push(&e); break; }
    }
}

static DWORD WINAPI CtlThread(LPVOID p)
{
    LONG last = 0;
    for (;;) {
        Sleep(5);
        if (g_ctl->seq != last) {
            last = g_ctl->seq;
            DoCmd(g_ctl->cmd, g_ctl->a, g_ctl->b, g_ctl->c, g_ctl->d);
            g_ctl->ack = last;
        }
    }
    return 0;
}

void TestCtl_Start(void)
{
    char name[64];
    HANDLE m;
    HMODULE sdl = GetModuleHandleA("SDL.dll");
    if (!sdl) sdl = LoadLibraryA("SDL.dll");
    pPushEvent = (int (__cdecl *)(SEvent *))GetProcAddress(sdl, "SDL_PushEvent");
    wsprintfA(name, "Local\\LOLJP_CTL_%lu", GetCurrentProcessId());
    m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, name);
    if (!m) return;
    g_ctl = (Ctl *)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
    if (!g_ctl) return;
    CreateThread(NULL, 0, CtlThread, NULL, 0, NULL);
    LogF("試験の操作口 %s", name);
}
