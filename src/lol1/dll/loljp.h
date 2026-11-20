/* Lands of Lore 日本語化 DLL（DOSBox 0.74 GOG 版に winmm.dll として読ませる） */
#pragma once
#include <windows.h>
#include <stdint.h>

/* ---- DOSBox 0.74 (GOG, md5 38541989cd5335f8e3080f6c2d1002a8) の内部 ---- */
typedef uint32_t (*CB_Handler)(void);
#define DBX_CBTABLE     ((CB_Handler *)0x755440)   /* CallBack_Handlers[128] */
#define DBX_CBMAX       128
#define DBX_REGS        ((volatile uint32_t *)0x7551C0) /* eax ecx edx ebx esp ebp esi edi */
#define DBX_FLAGS       (*(volatile uint32_t *)0x7551E4)
#define DBX_SEGVAL(i)   (((volatile uint32_t *)0x7550E0)[i])
#define DBX_SEGPHYS(i)  (((volatile uint32_t *)0x755100)[i])
#define DBX_MEMBASE     (*(uint8_t **)0x1D442D0)
#define DBX_INT21       0x4B33B0                    /* DOS_21Handler */
#define DBX_LOOPCALL    0x4012A9                    /* Normal_Loop: call [eax*4+CallBack_Handlers] */
#define DBX_MEMWRITEB   0x4F33D0                    /* void mem_writeb(PhysPt, Bit8u)（VGA なども正しく書ける） */
typedef void (__cdecl *MemWriteB)(uint32_t, uint32_t);
#define vwr8(a, v) (((MemWriteB)DBX_MEMWRITEB)((a), (v)))

enum { R_AX, R_CX, R_DX, R_BX, R_SP, R_BP, R_SI, R_DI };
enum { S_ES, S_CS, S_SS, S_DS };
#define FLAG_CF 0x0001
#define FLAG_ZF 0x0040

static __inline uint16_t reg16(int r) { return (uint16_t)DBX_REGS[r]; }
static __inline void set16(int r, uint16_t v) { DBX_REGS[r] = (DBX_REGS[r] & 0xFFFF0000u) | v; }
static __inline uint8_t *lin(uint32_t a) { return DBX_MEMBASE + a; }
static __inline uint8_t rd8(uint32_t a) { return DBX_MEMBASE[a]; }
static __inline uint16_t rd16(uint32_t a) { return (uint16_t)(DBX_MEMBASE[a] | (DBX_MEMBASE[a+1] << 8)); }
static __inline void wr8(uint32_t a, uint8_t v) { DBX_MEMBASE[a] = v; }
static __inline void wr16(uint32_t a, uint16_t v) { DBX_MEMBASE[a] = (uint8_t)v; DBX_MEMBASE[a+1] = (uint8_t)(v >> 8); }
static __inline uint32_t farptr(uint32_t a) { return ((uint32_t)rd16(a + 2) << 4) + rd16(a); }
static __inline uint32_t segbase(int s) { return DBX_SEGVAL(s) << 4; }

/* main.c */
extern char g_dir[MAX_PATH];
extern int g_nopatch;
extern int g_trace;
extern int g_reload;
void LogF(const char *fmt, ...);
int  AllocCallback(CB_Handler h);

/* game.c */
void Game_Init(void);
void Game_OnExec(void);
uint32_t Game_Int21(CB_Handler orig);
void Game_Tick(void);

/* testctl.c */
void TestCtl_Start(void);

/* text.c */
int  Text_Load(const char *path);
const char *Text_Find(const char *en, int len);
int  Font_Load(const char *path);
const uint8_t *Font_Glyph(uint8_t c1, uint8_t c2);
