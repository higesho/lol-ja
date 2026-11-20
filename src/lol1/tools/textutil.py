"""訳の表の共通処理（英文の書き方: 改行 0x0D は \\r、制御文字は \\xNN）"""
BS = chr(92)


def esc(b):
    out = []
    for c in b:
        if c == 0x0D:
            out.append(BS + 'r')
        elif c == 0x0A:
            out.append(BS + 'n')
        elif c == 0x5C:
            out.append(BS + BS)
        elif c == 0x09:
            out.append(BS + 't')
        elif c < 0x20 or c >= 0x7F:
            out.append(BS + 'x%02X' % c)
        else:
            out.append(chr(c))
    return ''.join(out)


def unesc_str(s):
    """エスケープを戻して str（制御文字入り）にする"""
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == BS and i + 1 < len(s):
            n = s[i + 1]
            if n == 'r':
                out.append(chr(13)); i += 2; continue
            if n == 'n':
                out.append(chr(10)); i += 2; continue
            if n == 't':
                out.append(chr(9)); i += 2; continue
            if n == BS:
                out.append(BS); i += 2; continue
            if n == 'x':
                out.append(chr(int(s[i + 2:i + 4], 16))); i += 4; continue
        out.append(c)
        i += 1
    return ''.join(out)


def unesc(s):
    """英文用: latin-1 のバイト列にする"""
    return unesc_str(s).encode('latin-1')


def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h
