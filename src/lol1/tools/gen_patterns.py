"""関数の命令列（far call の番地と DS 変数の番地は伏せる）を C の表にする"""
import sys, struct, re, capstone, os
sys.path.insert(0, os.path.dirname(__file__))
from tdsyms import syms
def load(path):
    d = open(path,'rb').read(); h = struct.unpack_from('<14H', d, 0); return d, h[4]*16
def mask_of(code, n):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    bs = bytearray(); ms = bytearray()
    for ins in md.disasm(code, 0):
        if len(bs) >= n: break
        b = ins.bytes; m = bytearray(b'\xff'*len(b))
        if b[0] == 0x9A:
            for k in range(1,5): m[k] = 0
        elif re.search(r'\[0x[0-9a-f]+\]', ins.op_str) or re.search(r'\[(bx|si|di|bp)[^\]]*[+-] 0x[0-9a-f]{3,4}\]', ins.op_str):
            # disp16 を伏せる（即値があればその前）
            imm = 0
            if re.search(r', (0x[0-9a-f]+|\d+)$', ins.op_str) and b[0] in (0x80,0x81,0x83,0xc6,0xc7):
                imm = 1 if b[0] in (0x80,0x83,0xc6) else 2
            e = len(b) - imm
            m[e-2] = 0; m[e-1] = 0
        bs += b; ms += m
    return bytes(bs[:n]), bytes(ms[:n])
def carr(b): return ','.join(f'0x{x:02X}' for x in b)
LANDS = 'orig/cd/DATA/ENG/LANDS.EXE'
d, hdr = load(LANDS)
S = {n: (s, o) for n, s, o, f, t in syms(LANDS)}
out = ['/* gen_patterns.py が作る */', 'typedef struct { const char *name; int len; const unsigned char *b, *m; } Pattern;']
defs = []
for key, name, n in [('TEXTPRINT','_Text_Print',56), ('CHARWIDTH','_Char_Pixel_Width',36), ('UNDIP','_UnDip_Text',48), ('STRWIDTH','_String_Pixel_Width',40)]:
    s, o = S[name]; fo = hdr + s*16 + o
    b, m = mask_of(d[fo:fo+0x200], n)
    out.append(f'static const unsigned char P_{key}_B[] = {{{carr(b)}}};')
    out.append(f'static const unsigned char P_{key}_M[] = {{{carr(m)}}};')
    defs.append((key, len(b)))
# MAIN.EXE の改行判定
md_, mhdr = load('../MAIN.EXE')
fo = 0x18AFF
b, m = mask_of(md_[fo:fo+0x60], 0x2E)
out.append(f'static const unsigned char P_BREAK_B[] = {{{carr(b)}}};')
out.append(f'static const unsigned char P_BREAK_M[] = {{{carr(m)}}};')
defs.append(('BREAK', len(b)))
for k, n in defs:
    out.append(f'static const Pattern PAT_{k} = {{"{k}", {n}, P_{k}_B, P_{k}_M}};')
open('dll/patterns.h','w',encoding='utf-8').write('\n'.join(out)+'\n')
print('\n'.join(out))
