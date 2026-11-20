/* 試験: DLL と同じ処理で訳した TRR を組み立てる（ゲームを動かさずに確かめる）
   test_build.exe <LOL3JP の場所> <ゲームのフォルダ> */
#include "lol3jp.c"

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    lstrcpyA(g_dir, argv[1]);
    lstrcpyA(g_game, argv[2]);
    g_logon = 1;
    build_trr();
    printf("組み立てたファイル %d\n", g_ntrr);
    {
        int i;
        for (i = 0; i < g_ntrr; i++) printf("%s %s\n", g_trr_name[i], g_trr_path[i]);
    }
    return g_ntrr ? 0 : 2;
}
