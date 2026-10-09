#!/usr/bin/env python3
"""imports_needed.txt without pyelftools: the same result as gen_imports.py's
needed_from_libs(): every undefined .dynsym symbol of the modules that no module
of the set exports, with whether every use is weak.

usage: mkneeded.py <libdir> <out> <module> [<module> ...]
"""
import os, struct, sys

def dynsyms(path):
    d = open(path, "rb").read()
    assert d[:4] == b"\x7fELF" and d[4] == 1 and d[5] == 1, "32-bit little-endian ELF expected"
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2e)
    secs = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(
            "<IIIIIIIIII", d, shoff + i * shentsize)
        secs.append((name, typ, off, size, link, entsize))
    out = []
    for (name, typ, off, size, link, entsize) in secs:
        if typ != 11:  # SHT_DYNSYM
            continue
        stroff = secs[link][2]
        for k in range(size // 16):
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
                "<IIIBBH", d, off + k * 16)
            if not st_name:
                continue
            e = d.index(b"\0", stroff + st_name)
            nm = d[stroff + st_name:e].decode()
            out.append((nm, st_shndx == 0, st_info >> 4))  # bind: 0 local, 1 global, 2 weak
    return out

def main():
    libdir, outp, mods = sys.argv[1], sys.argv[2], sys.argv[3:]
    und, exp = {}, set()
    for m in mods:
        for nm, is_und, bind in dynsyms(os.path.join(libdir, m)):
            if is_und:
                und[nm] = und.get(nm, True) and bind == 2
            elif bind != 0:
                exp.add(nm)
    need = {n: w for n, w in und.items() if n not in exp}
    with open(outp, "w", newline="\n") as f:
        f.write("# symbol weak(0/1) -- regenerate with gen_imports.py --libs\n")
        for n in sorted(need):
            f.write("%s %d\n" % (n, 1 if need[n] else 0))
    print("%d symbols needed (%d undefined, %d exported by the set)" % (len(need), len(und), len(exp)))

main()
