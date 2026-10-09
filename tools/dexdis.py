#!/usr/bin/env python3
"""Small Dalvik disassembler: a method's instructions with registers, for the
opcodes that carry data (constants, moves, invokes, conversions, branches).

usage: dexdis.py classes.dex <class-descriptor> <method-name> [<method-name> ...]
"""
import struct, sys

def uleb(b, o):
    r = 0; s = 0
    while True:
        c = b[o]; o += 1
        r |= (c & 0x7f) << s; s += 7
        if not c & 0x80:
            return r, o

def mutf8(b, o):
    _, o = uleb(b, o)
    return b[o:b.index(0, o)].decode("utf-8", "replace")

d = open(sys.argv[1], "rb").read()
want_cls, want = sys.argv[2], set(sys.argv[3:])
(str_n, str_o, type_n, type_o, proto_n, proto_o, field_n, field_o, meth_n, meth_o, cls_n, cls_o) = struct.unpack_from("<12I", d, 0x38)
strs = [mutf8(d, struct.unpack_from("<I", d, str_o + 4 * i)[0]) for i in range(str_n)]
types = [strs[struct.unpack_from("<I", d, type_o + 4 * i)[0]] for i in range(type_n)]

def tl(off):
    if not off: return []
    n = struct.unpack_from("<I", d, off)[0]
    return [types[struct.unpack_from("<H", d, off + 4 + 2 * i)[0]] for i in range(n)]

protos = []
for i in range(proto_n):
    _, ret, params = struct.unpack_from("<III", d, proto_o + 12 * i)
    protos.append("(" + "".join(tl(params)) + ")" + types[ret])
methods = []
for i in range(meth_n):
    c, p, n = struct.unpack_from("<HHI", d, meth_o + 8 * i)
    methods.append("%s.%s%s" % (types[c].strip("L;").split("/")[-1], strs[n], protos[p]))
fields = []
for i in range(field_n):
    c, t, n = struct.unpack_from("<HHI", d, field_o + 8 * i)
    fields.append("%s.%s" % (types[c].strip("L;").split("/")[-1], strs[n]))

def size(op):
    if op in (0x02,0x05,0x08,0x13,0x15,0x16,0x19,0x1a,0x1c,0x1f,0x20,0x22,0x23,0x29): return 2
    if op in (0x03,0x06,0x09,0x14,0x17,0x1b,0x24,0x25,0x26,0x2a,0x2b,0x2c): return 3
    if op == 0x18: return 5
    if 0x2d <= op <= 0x3d or 0x44 <= op <= 0x6d or 0x90 <= op <= 0xaf or 0xd0 <= op <= 0xe2: return 2
    if 0x6e <= op <= 0x72 or 0x74 <= op <= 0x78: return 3
    return 1

def s4(v): return v - 16 if v > 7 else v

def decode(ins):
    i = 0; n = len(ins)
    while i < n:
        w = ins[i]; op = w & 0xff; a = (w >> 8) & 0xf; b = (w >> 12) & 0xf; aa = w >> 8
        pc = i
        if w in (0x0100, 0x0200, 0x0300):
            if w == 0x0100: i += 4 + 2 * ins[i + 1]
            elif w == 0x0200: i += 2 + 4 * ins[i + 1]
            else: i += 4 + (ins[i + 1] * (ins[i + 2] | (ins[i + 3] << 16)) + 1) // 2
            continue
        t = None
        if op == 0x12: t = "const/4 v%d, %d" % (a, s4(b))
        elif op == 0x13: v = ins[i+1]; t = "const/16 v%d, %d" % (aa, v - 65536 if v > 32767 else v)
        elif op == 0x14: t = "const v%d, %d" % (aa, struct.unpack("<i", struct.pack("<I", ins[i+1] | (ins[i+2] << 16)))[0])
        elif op == 0x16: v = ins[i+1]; t = "const-wide/16 v%d, %d" % (aa, v - 65536 if v > 32767 else v)
        elif op == 0x17: t = "const-wide/32 v%d, %d" % (aa, struct.unpack("<i", struct.pack("<I", ins[i+1] | (ins[i+2] << 16)))[0])
        elif op == 0x18: t = "const-wide v%d, %d" % (aa, struct.unpack("<q", struct.pack("<HHHH", *ins[i+1:i+5]))[0])
        elif op == 0x01: t = "move v%d, v%d" % (a, b)
        elif op == 0x07: t = "move-object v%d, v%d" % (a, b)
        elif 0x9b <= op <= 0xa0 or 0xbb <= op <= 0xbf or op in (0x8c,0x8d,0x8b): t = "wide-op%02x v%d, v%d (v%d)" % (op, aa, ins[i+1] & 0xff, ins[i+1] >> 8)
        elif op in (0x31, 0x2f, 0x2e, 0x30): t = "cmp-%02x v%d, v%d, v%d" % (op, aa, ins[i+1] & 0xff, ins[i+1] >> 8)
        elif op in (0x0a, 0x0b, 0x0c): t = "move-result v%d" % aa
        elif op == 0x87: t = "float-to-int v%d, v%d" % (a, b)
        elif op == 0x82: t = "int-to-float v%d, v%d" % (a, b)
        elif op in (0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf):
            t = "2addr-op%02x v%d, v%d" % (op, a, b)
        elif op == 0xd8: t = "add-int/lit8 v%d, v%d, %d" % (aa, ins[i+1] & 0xff, struct.unpack("b", bytes([ins[i+1] >> 8]))[0])
        elif 0x32 <= op <= 0x37: t = "if-%s v%d, v%d" % (["eq","ne","lt","ge","gt","le"][op-0x32], a, b)
        elif 0x38 <= op <= 0x3d: t = "if-%sz v%d" % (["eq","ne","lt","ge","gt","le"][op-0x38], aa)
        elif 0x6e <= op <= 0x72:
            cnt = (w >> 12) & 0xf; g = (w >> 8) & 0xf; r = ins[i+2]
            regs = [r & 0xf, (r >> 4) & 0xf, (r >> 8) & 0xf, (r >> 12) & 0xf, g][:cnt]
            t = "invoke v{%s} %s" % (",".join(map(str, regs)), methods[ins[i+1]])
        elif 0x74 <= op <= 0x78:
            t = "invoke/range v%d..v%d %s" % (ins[i+2], ins[i+2] + aa - 1, methods[ins[i+1]])
        elif op == 0x1a: t = 'const-string v%d, "%s"' % (aa, strs[ins[i+1]])
        elif 0x52 <= op <= 0x6d:
            nm = "iget" if op < 0x59 else "iput" if op < 0x60 else "sget" if op < 0x67 else "sput"
            t = "%s v%d%s %s" % (nm, a if nm in ("iget","iput") else aa, (", v%d" % b) if nm in ("iget","iput") else "", fields[ins[i+1]])
        if t: print("  %04x  %s" % (pc, t))
        i += size(op)

for i in range(cls_n):
    (cidx, flags, sup, ifs, src, ann, cdata, sv) = struct.unpack_from("<8I", d, cls_o + 32 * i)
    if types[cidx] != want_cls or not cdata: continue
    o = cdata
    sf, o = uleb(d, o); inf, o = uleb(d, o); dm, o = uleb(d, o); vm, o = uleb(d, o)
    for _ in range(sf + inf):
        _x, o = uleb(d, o); _a, o = uleb(d, o)
    for cnt in (dm, vm):
        idx = 0
        for _ in range(cnt):
            x, o = uleb(d, o); acc, o = uleb(d, o); code, o = uleb(d, o); idx += x
            full = methods[idx]
            if full.split(".", 1)[1].split("(")[0] in want and code:
                regs, ins_, outs, tries, dbg, isz = struct.unpack_from("<HHHHII", d, code)
                print("== %s   (registers %d, ins %d)" % (full, regs, ins_))
                decode(struct.unpack_from("<%dH" % isz, d, code + 16))
