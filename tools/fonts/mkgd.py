#!/usr/bin/env python3
"""Bitmap fonts (BDF or PCF, Unicode) -> one EPOC R5 font definition (.gd)
for fnttran, with several typefaces. Each size is mapped to code page 1252
(the Psion's own). A glyph a font lacks is taken from a donor font of the
same cell size (Spleen's curly quotes and dashes come from Terminus).

  python3 mkgd.py > psiterm.gd        (run in this folder; build.sh does it)
  python3 mkgd.py --preview out.png   (every size, for looking at)

Fonts and licences are in src/ (see THIRD-PARTY.md):
  Terminus  SIL OFL 1.1   ter-u*n (12-18 in this folder, 24 in src/)
  Spleen    BSD 2-clause  src/spleen-*.bdf.gz
  Gohufont  WTFPL         src/gohufont-uni-*.pcf.gz
  Cozette   MIT           src/cozette.bdf.gz
"""
import gzip, io, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CP1252 = [b for b in range(32, 256)]


def uni(b):
    try:
        return ord(bytes([b]).decode("cp1252"))
    except UnicodeDecodeError:
        return None


def opener(path):
    data = open(path, "rb").read()
    return gzip.decompress(data) if path.endswith(".gz") else data


class Font:
    """A bitmap font as cell-sized glyphs: glyph(u) -> list of row strings."""

    def __init__(self, path):
        self.path = path
        self.glyphs = {}
        if ".pcf" in path:
            self._pcf(path)
        else:
            self._bdf(path)

    def _bdf(self, path):
        text = opener(path).decode("latin-1").splitlines()
        props, i = {}, 0
        raw = {}
        while i < len(text):
            line = text[i]
            k = line.split(" ", 1)
            if k[0] in ("FONT_ASCENT", "FONT_DESCENT"):
                props[k[0]] = int(k[1])
            if line.startswith("STARTCHAR"):
                enc, bbx, dw, rows = None, None, None, []
                i += 1
                while not text[i].startswith("BITMAP"):
                    f = text[i].split()
                    if f[0] == "ENCODING":
                        enc = int(f[1])
                    elif f[0] == "BBX":
                        bbx = [int(x) for x in f[1:5]]
                    elif f[0] == "DWIDTH":
                        dw = int(f[1])
                    i += 1
                i += 1
                while not text[i].startswith("ENDCHAR"):
                    rows.append(int(text[i], 16))
                    i += 1
                if enc is not None and enc >= 0:
                    raw[enc] = (bbx, dw, rows)
            i += 1
        self.asc, self.h = props["FONT_ASCENT"], props["FONT_ASCENT"] + props["FONT_DESCENT"]
        self.w = raw[ord("M")][1]
        for u, (bbx, dw, rows) in raw.items():
            w, h, xo, yo = bbx
            nbytes = (w + 7) // 8
            cell = [["."] * self.w for _ in range(self.h)]
            for y, bits in enumerate(rows):
                cy = self.asc - (yo + h) + y
                for x in range(w):
                    if bits & (1 << (nbytes * 8 - 1 - x)):
                        cx = xo + x
                        if 0 <= cy < self.h and 0 <= cx < self.w:
                            cell[cy][cx] = "*"
            self.glyphs[u] = ["".join(r) for r in cell]

    def _pcf(self, path):
        from PIL import PcfFontFile
        f = PcfFontFile.PcfFontFile(io.BytesIO(opener(path)), charset_encoding="cp1252")
        (W, _), dst, _, _ = f.glyph[ord("M")]
        self.asc, self.h, self.w = -dst[1], dst[3] - dst[1], W
        for b in range(32, 256):
            g = f.glyph[b]
            u = uni(b)
            if not g or u is None:
                continue
            (_, _), d, _, im = g
            im = im.convert("L")
            cell = [["."] * self.w for _ in range(self.h)]
            for y in range(im.size[1]):
                for x in range(im.size[0]):
                    if im.getpixel((x, y)):
                        cy, cx = self.asc + d[1] + y, d[0] + x
                        if 0 <= cy < self.h and 0 <= cx < self.w:
                            cell[cy][cx] = "*"
            self.glyphs[u] = ["".join(r) for r in cell]

    def glyph(self, u):
        return self.glyphs.get(u)


def src(name):
    p = os.path.join(HERE, name)
    return p if os.path.exists(p) else os.path.join(HERE, "src", name)


# (typeface id, name shown, [(pixels, bitmap UID, file, donor file or None)])
# UIDs: Terminus 12-18 and the collection keep the ones they have had since
# 0.12; the rest are 0x01000EB0 on.
TYPEFACES = [
    ("PSITERMTERMINUS", "Terminus", [
        (12, 0x01000EAA, "ter-u12n_unicode.pcf.gz", None),
        (14, 0x01000EAB, "ter-u14n_unicode.pcf.gz", None),
        (16, 0x01000EAC, "ter-u16n_unicode.pcf.gz", None),
        (18, 0x01000EAD, "ter-u18n_unicode.pcf.gz", None),
        (24, 0x01000EB0, "ter-u24n.bdf.gz", None)]),
    ("PSITERMSPLEEN", "Spleen", [
        (12, 0x01000EB1, "spleen-6x12.bdf.gz", "ter-u12n_unicode.pcf.gz"),
        (16, 0x01000EB2, "spleen-8x16.bdf.gz", "ter-u16n_unicode.pcf.gz"),
        (24, 0x01000EB3, "spleen-12x24.bdf.gz", "ter-u24n.bdf.gz")]),
    ("PSITERMGOHU", "Gohufont", [
        (11, 0x01000EB4, "gohufont-uni-11.pcf.gz", None),
        (14, 0x01000EB5, "gohufont-uni-14.pcf.gz", "ter-u14n_unicode.pcf.gz")]),
    ("PSITERMCOZETTE", "Cozette", [
        (13, 0x01000EB6, "cozette.bdf.gz", None)]),
]
COLLECTION_UID = 0x01000EAE
COPYRIGHT = ("Terminus (c) 2019 Dimitar Toshkov Zhekov, SIL OFL 1.1; Spleen (c) 2018-2024 "
             "Frederic Cambus, BSD; Gohufont (c) Hugo Chargois, WTFPL; Cozette (c) 2020 Slavfox, MIT")


def bitmap_gd(tid, px, uid, font, donor, report):
    name = f"{tid}{px}"
    out = [f"FontBitmap {name}", f"Uid {uid}", f"MaxNormalCharWidth {font.w}",
           f"CellHeight {font.h}", f"Ascent {font.asc}", "CodeSection 32:255"]
    borrowed, missing = [], []
    for b in CP1252:
        u = uni(b)
        if b == 0xA0:
            u = 0x20
        g = font.glyph(u) if u is not None else None
        if g is None and donor is not None and u is not None and donor.w == font.w and donor.h == font.h:
            # (the donor's baseline may differ by a row: line the two up)
            g = donor.glyph(u)
            if g is not None:
                shift = font.asc - donor.asc
                blank = "." * font.w
                g = ([blank] * shift + g)[:font.h] if shift >= 0 else (g[-shift:] + [blank] * -shift)
                borrowed.append(chr(u))
        if g is None:
            g = font.glyph(ord("?"))
            if u is not None:
                missing.append(chr(u))
        out.append(f"Char {b}")
        out += g
        out.append("EndChar")
    out += ["EndCodeSection", "EndFontBitmap", ""]
    report.append(f"{name}: {font.w}x{font.h} ascent {font.asc}"
                  + (f", from the donor: {''.join(borrowed)}" if borrowed else "")
                  + (f", missing: {''.join(missing)}" if missing else ""))
    return "\n".join(out)


def build():
    cache = {}

    def load(fn):
        if fn not in cache:
            cache[fn] = Font(src(fn))
        return cache[fn]

    parts, report, faces = [], [], []
    for tid, label, sizes in TYPEFACES:
        for px, uid, fn, donor in sizes:
            parts.append(bitmap_gd(tid, px, uid, load(fn), load(donor) if donor else None, report))
        faces.append(f"Typeface {tid}\nName \"{label}\"\nFontBitmaps\n"
                     + "\n".join(f"{tid}{px}" for px, _, _, _ in sizes)
                     + "\nEndFontBitmaps\nEndTypeface\n")
    parts += faces
    parts.append(f"FontStoreFile\nCollectionUid {COLLECTION_UID}\nKPixelAspectRatio 1000\n"
                 f"CopyrightInfo\n\"{COPYRIGHT}\"\nEndCopyrightInfo\nTypefaces\n"
                 + "\n".join(t[0] for t in TYPEFACES) + "\nEndTypefaces\nEndFontStoreFile\n")
    return "\n".join(parts), report, cache


def preview(path, cache):
    from PIL import Image, ImageDraw
    text = ["dan@pi:~/www/psion$ ls -l --color",
            "-rw-r--r-- 1 dan dan  812 Nov 12 README.md",
            "if (x != 0 && y <= 1) { return a[i]; } O0 Il1|",
            "“Quoted” ‘text’ – dash — €12 … £ ½ éäß"]
    blocks = []
    for tid, label, sizes in TYPEFACES:
        for px, uid, fn, donor in sizes:
            f, d = cache[fn], cache[donor] if donor else None
            im = Image.new("L", (640, 16 + f.h * len(text) + 4), 255)
            dr = ImageDraw.Draw(im)
            dr.rectangle((0, 0, 639, 13), fill=80)
            dr.text((4, 1), f"{label} {px} ({f.w}x{f.h})", fill=255)
            for row, line in enumerate(text):
                for col, ch in enumerate(line):
                    g = f.glyph(ord(ch)) or (d.glyph(ord(ch)) if d else None) or f.glyph(ord("?"))
                    for y, r in enumerate(g):
                        for x, c in enumerate(r):
                            if c == "*":
                                im.putpixel((4 + col * f.w + x, 16 + row * f.h + y), 0)
            blocks.append(im)
    out = Image.new("L", (640, sum(b.height for b in blocks)), 255)
    y = 0
    for b in blocks:
        out.paste(b, (0, y))
        y += b.height
    out.resize((1280, out.height * 2), Image.NEAREST).save(path)


if __name__ == "__main__":
    gd, report, cache = build()
    for r in report:
        print(r, file=sys.stderr)
    if len(sys.argv) > 2 and sys.argv[1] == "--preview":
        preview(sys.argv[2], cache)
    else:
        sys.stdout.write(gd)
