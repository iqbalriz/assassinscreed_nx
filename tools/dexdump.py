#!/usr/bin/env python3
"""Minimal dex reader: list methods (with access flags and descriptors) of classes.

usage: dexdump.py classes.dex [class-substring ...]
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

def main():
    d = open(sys.argv[1], "rb").read()
    filt = sys.argv[2:]
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
        methods.append((types[c], strs[n], protos[p]))
    fields = []
    for i in range(field_n):
        c, t, n = struct.unpack_from("<HHI", d, field_o + 8 * i)
        fields.append((types[c], strs[n], types[t]))
    for i in range(cls_n):
        (cidx, flags, sup, ifs, src, ann, cdata, sv) = struct.unpack_from("<8I", d, cls_o + 32 * i)
        name = types[cidx]
        if filt and not any(f in name for f in filt):
            continue
        print("class", name, "extends", types[sup] if sup != 0xffffffff else "-", hex(flags))
        if not cdata:
            continue
        o = cdata
        sf, o = uleb(d, o); inf, o = uleb(d, o); dm, o = uleb(d, o); vm, o = uleb(d, o)
        idx = 0
        for _ in range(sf):
            x, o = uleb(d, o); _a, o = uleb(d, o); idx += x
            print("  static field", fields[idx][1], fields[idx][2])
        idx = 0
        for _ in range(inf):
            x, o = uleb(d, o); _a, o = uleb(d, o); idx += x
            print("  field", fields[idx][1], fields[idx][2])
        for kind, cnt in (("direct", dm), ("virtual", vm)):
            idx = 0
            for _ in range(cnt):
                x, o = uleb(d, o); acc, o = uleb(d, o); code, o = uleb(d, o); idx += x
                flags_s = []
                if acc & 8: flags_s.append("static")
                if acc & 0x100: flags_s.append("native")
                print("  %-7s %-14s %s%s" % (kind, " ".join(flags_s), methods[idx][1], methods[idx][2]))

main()
