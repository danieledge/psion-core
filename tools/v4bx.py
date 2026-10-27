#!/usr/bin/env python3
"""tools/v4bx.py - gcc's helper library for ARMv4 without Thumb (the netBook).

    python3 v4bx.py IN.a OUT.a      copy a COFF archive (libgcc.a), every
                                    BX Rm in its code made MOV PC, Rm
    python3 v4bx.py --check FILE    count the BX instructions left in the code
                                    of an archive, an object or a PE image

The SDK's gcc 3.0 builds its libgcc.a for ARMv4T with Thumb interworking, so
its helpers (64-bit multiply, divide and shifts, the soft-float routines)
return with BX LR. The Series 5mx's ARM710T has Thumb and runs BX. The
netBook's StrongARM SA-1100 is ARMv4 without Thumb: there BX is an undefined
instruction, and the first call into one of those helpers ends the program
with KERN-EXEC 3 (PsiTerm: the first 64-bit multiply in the RSA host key
check). MOV PC, Rm is the same jump on both, as we never enter Thumb code.
This is what GNU ld's --fix-v4bx does; the SDK's 2002 linker predates it.

Only words in code sections that carry no relocation are changed, and the
two Thumb members (_call_via_rX, _interwork_call_via_rX) are left alone: only
Thumb code calls them. The archive keeps its layout byte for byte (its
symbol index stays valid), so this is a patch, not a rebuild.
"""
import struct
import sys

ARM_MACHINES = (0x1c0, 0x1c2, 0xa00)      # COFF ARM machine ids (0xa00: the SDK's arm-epoc-pe)
SCN_CNT_CODE = 0x20
SKIP = ('call_via',)                       # Thumb members


def is_bx(w):
    return (w & 0x0ffffff0) == 0x012fff10 and (w >> 28) != 0xf


def mov_pc(w):
    # BX Rm (cond 0001 0010 1111 1111 1111 0001 Rm) -> MOV PC, Rm (cond 0001 1010 0000 1111 0000 0000 Rm)
    return (w & 0xf000000f) | 0x01a0f000


def code_words(buf, base, image):
    """(file offset, word) for every relocation-free word of the code sections
    of the COFF object (image=False) or PE image (image=True) at buf[base:]."""
    hdr = base
    if image:
        hdr = base + struct.unpack_from('<I', buf, base + 0x3c)[0] + 4
    mach, nsec = struct.unpack_from('<HH', buf, hdr)
    if mach not in ARM_MACHINES:
        return
    optsz = struct.unpack_from('<H', buf, hdr + 16)[0]
    for k in range(nsec):
        sh = hdr + 20 + optsz + 40 * k
        vsize, size, raw, relptr = struct.unpack_from('<I4xIII', buf, sh + 8)
        nrel, flags = struct.unpack_from('<H2xI', buf, sh + 32)
        if not (flags & SCN_CNT_CODE) or raw == 0:
            continue
        if image and vsize:
            size = min(size, vsize)       # (the raw data is padded)
        relocs = set()
        if not image:
            for r in range(nrel):
                relocs.add(struct.unpack_from('<I', buf, base + relptr + 10 * r)[0] & ~3)
        for off in range(0, size - 3, 4):
            if off not in relocs:
                yield base + raw + off, struct.unpack_from('<I', buf, base + raw + off)[0]


def members(buf):
    """(name, offset) of each object in a System V / GNU ar archive."""
    pos, longnames = 8, b''
    while pos + 60 <= len(buf):
        name = buf[pos:pos + 16].rstrip()
        size = int(buf[pos + 48:pos + 58])
        if name == b'//':
            longnames = buf[pos + 60:pos + 60 + size]
        elif name != b'/':
            if name.startswith(b'/') and name[1:].isdigit():
                i = int(name[1:])
                name = longnames[i:longnames.index(b'/', i)]
            yield name.rstrip(b'/').decode('latin-1'), pos + 60
        pos += 60 + size + (size & 1)


def objects(buf):
    if buf[:8] == b'!<arch>\n':
        for name, off in members(buf):
            yield name, off, False
    elif buf[:2] == b'MZ':
        yield 'image', 0, True
    else:
        yield 'object', 0, False


def main(argv):
    if len(argv) == 3 and argv[1] == '--check':
        buf = open(argv[2], 'rb').read()
        n = 0
        for name, off, image in objects(buf):
            for at, w in code_words(buf, off, image):
                if is_bx(w):
                    print('%s: BX at file offset 0x%x (%08x)' % (name, at, w))
                    n += 1
        print('%s: %d BX instruction(s)' % (argv[2], n))
        return 1 if n else 0
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    buf = bytearray(open(argv[1], 'rb').read())
    if buf[:8] != b'!<arch>\n':
        sys.stderr.write('v4bx.py: %s is not an archive\n' % argv[1])
        return 2
    n = 0
    for name, off, image in objects(bytes(buf)):
        if any(s in name for s in SKIP):
            continue
        for at, w in code_words(bytes(buf), off, image):
            if is_bx(w):
                struct.pack_into('<I', buf, at, mov_pc(w))
                n += 1
    open(argv[2], 'wb').write(buf)
    print('v4bx.py: %s: %d BX made MOV PC' % (argv[2], n))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
