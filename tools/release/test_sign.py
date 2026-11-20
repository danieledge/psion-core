#!/usr/bin/env python3
"""Host test of sign.py's version check: a .sis is signed only as its own
version (its header), with two digits; a throwaway key, made here.

    python3 tools/release/test_sign.py
"""
import os, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
fails = 0
with tempfile.TemporaryDirectory() as t:
    key = os.path.join(t, "k")
    open(key, "w").write(os.urandom(32).hex())
    sis = os.path.join(t, "X.sis")
    h = bytearray(0x40)
    h[4:8] = (0x1000006D).to_bytes(4, "little"); h[8:12] = (0x10000419).to_bytes(4, "little")
    h[0x28:0x2a] = (1).to_bytes(2, "little"); h[0x2a:0x2c] = (12).to_bytes(2, "little")
    open(sis, "wb").write(bytes(h))
    open(os.path.join(t, "bad.sis"), "wb").write(b"not a sis")
    for path, ver, want in [("X.sis", "1.12", 0), ("X.sis", "1.13", 1), ("X.sis", "1.2", 1), ("X.sis", "01.12", 1), ("bad.sis", "1.12", 1)]:
        p = os.path.join(t, path)
        if os.path.exists(p + ".sig"):
            os.remove(p + ".sig")
        r = subprocess.run([sys.executable, os.path.join(here, "sign.py"), "--key", key, p, ver], capture_output=True, text=True)
        wrote = os.path.exists(p + ".sig")
        ok = (r.returncode == 0) == (want == 0) and wrote == (want == 0)
        fails += not ok
        print("%s %s %s: exit %d, %s" % ("ok  " if ok else "FAIL", path, ver, r.returncode, "signed" if wrote else "refused"))
print("test_sign: %s" % ("all passed" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
