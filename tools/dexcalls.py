#!/usr/bin/env python3
"""List, per method, the const-strings and invoke targets in bytecode order.

usage: dexcalls.py classes.dex <class-descriptor> <method-name> [<method-name> ...]
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
    e = b.index(0, o)
    return b[o:e].decode("utf-8", "replace")

d = open(sys.argv[1], "rb").read()
want_cls = sys.argv[2]
want = set(sys.argv[3:])
(str_n, str_o, type_n, type_o, proto_n, proto_o, field_n, field_o,
 meth_n, meth_o, cls_n, cls_o) = struct.unpack_from("<12I", d, 0x38)
strs = [mutf8(d, struct.unpack_from("<I", d, str_o + 4 * i)[0]) for i in range(str_n)]
types = [strs[struct.unpack_from("<I", d, type_o + 4 * i)[0]] for i in range(type_n)]

def type_list(off):
    if not off:
        return []
    n = struct.unpack_from("<I", d, off)[0]
    return [types[struct.unpack_from("<H", d, off + 4 + 2 * i)[0]] for i in range(n)]

protos = []
for i in range(proto_n):
    _, ret, params = struct.unpack_from("<III", d, proto_o + 12 * i)
    protos.append("(" + "".join(type_list(params)) + ")" + types[ret])
methods = []
for i in range(meth_n):
    c, p, n = struct.unpack_from("<HHI", d, meth_o + 8 * i)
    methods.append("%s.%s%s" % (types[c].strip("L;"), strs[n], protos[p]))
fields = []
for i in range(field_n):
    c, t, n = struct.unpack_from("<HHI", d, field_o + 8 * i)
    fields.append("%s.%s" % (types[c].strip("L;"), strs[n]))


def op_size(op):
    if op in (0x02, 0x05, 0x08, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c, 0x1f, 0x20, 0x22, 0x23, 0x29):
        return 2
    if op in (0x03, 0x06, 0x09, 0x14, 0x17, 0x1b, 0x24, 0x25, 0x26, 0x2a, 0x2b, 0x2c):
        return 3
    if op == 0x18:
        return 5
    if 0x2d <= op <= 0x3d or 0x44 <= op <= 0x6d or 0x90 <= op <= 0xaf or 0xd0 <= op <= 0xe2:
        return 2
    if 0x6e <= op <= 0x72 or 0x74 <= op <= 0x78:
        return 3
    return 1

def decode(insns, base_name):
    out = []
    i = 0
    n = len(insns)
    while i < n:
        w = insns[i]
        op = w & 0xff
        if w == 0x0100:
            sz = insns[i + 1]; i += 4 + 2 * sz; continue
        if w == 0x0200:
            sz = insns[i + 1]; i += 2 + 4 * sz; continue
        if w == 0x0300:
            width = insns[i + 1]; sz = insns[i + 2] | (insns[i + 3] << 16)
            i += 4 + (width * sz + 1) // 2; continue
        if op == 0x1a:
            out.append('  "%s"' % strs[insns[i + 1]].replace(chr(10), " "))
        elif op == 0x1b:
            out.append('  "%s"' % strs[insns[i + 1] | (insns[i + 2] << 16)])
        elif op == 0x12:
            v = (w >> 12) & 0xf
            v = v - 16 if v > 7 else v
            out.append("  const/4 v%d = %d" % ((w >> 8) & 0xf, v))
        elif op == 0x13:
            v = insns[i + 1]
            v = v - 65536 if v > 32767 else v
            out.append("  const/16 v%d = %d" % ((w >> 8) & 0xff, v))
        elif 0x32 <= op <= 0x37:
            out.append("  if-%s" % ["eq","ne","lt","ge","gt","le"][op - 0x32])
        elif 0x6e <= op <= 0x72 or 0x74 <= op <= 0x78:
            out.append("  invoke " + methods[insns[i + 1]])
        elif 0x52 <= op <= 0x6d:
            nm = "iget" if op < 0x59 else "iput" if op < 0x60 else "sget" if op < 0x67 else "sput"
            out.append("  %s %s" % (nm, fields[insns[i + 1]]))
        i += op_size(op)
    return out

for i in range(cls_n):
    (cidx, flags, sup, ifs, src, ann, cdata, sv) = struct.unpack_from("<8I", d, cls_o + 32 * i)
    if types[cidx] != want_cls:
        continue
    if not cdata:
        continue
    o = cdata
    sf, o = uleb(d, o); inf, o = uleb(d, o); dm, o = uleb(d, o); vm, o = uleb(d, o)
    for _ in range(sf + inf):
        _x, o = uleb(d, o); _a, o = uleb(d, o)
    for cnt in (dm, vm):
        idx = 0
        for _ in range(cnt):
            x, o = uleb(d, o); acc, o = uleb(d, o); code, o = uleb(d, o); idx += x
            full = methods[idx]
            name = full.split(".", 1)[1].split("(")[0]
            if name in want and code:
                regs, ins, outs, tries, dbg, isz = struct.unpack_from("<HHHHII", d, code)
                insns = struct.unpack_from("<%dH" % isz, d, code + 16)
                print("== " + full)
                for line in decode(insns, name):
                    print(line)
