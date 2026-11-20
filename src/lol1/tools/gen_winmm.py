"""SysWOW64 の winmm.dll の書き出しから、転送用の表（winmm_exports.h と winmm.def）を作る"""
import pefile

BS = chr(92)
pe = pefile.PE('C:/Windows/SysWOW64/winmm.dll')
names = [(s.name.decode(), s.ordinal) for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name]
with open('dll/winmm_exports.h', 'w', encoding='utf-8', newline='\n') as f:
    f.write('/* SysWOW64 の winmm.dll の書き出し（tools/gen_winmm.py が作る） */\n')
    f.write('#define WINMM_FUNCS(X) ' + BS + '\n')
    f.write((' ' + BS + '\n').join(f'    X({n})' for n, _ in names) + '\n')
with open('dll/winmm.def', 'w', encoding='utf-8', newline='\n') as f:
    f.write('LIBRARY winmm\nEXPORTS\n')
    for n, o in names:
        f.write(f'    {n} = ex_{n} @{o}\n')
print(len(names), 'functions')
