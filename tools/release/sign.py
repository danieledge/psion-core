#!/usr/bin/env python3
"""Signs a PsiTerm, PsiWeb or PsiMail release so its updater will accept it.

    tools/release/sign.py dist/PsiTerm.sis 0.23 [--key ~/.psiterm-signing/release.key]
    (or with the key's hex in $PSITERM_RELEASE_KEY)
    tools/release/sign.py --product PsiWeb dist/PsiWeb.sis 0.2
    tools/release/sign.py --product PsiMail dist/PsiMail.sis 0.3

Writes <sis>.sig:
    line 1: the version
    line 2: Ed25519 signature (hex) over
            b"<Product> update\\n" + version + b"\\n" + SHA-256(<sis>)
The apps check this with the public key built into them (KUpdateKey) and
refuse any update that does not match. Keep the private key out of git.

The version must be the .sis's own (its header: major at 0x28, minor at
0x2a, as the installer compares them), with two digits after the point: a
.sis signed as 1.13 that is really 1.12 would be installed by every copy,
still say 1.12, and offer itself as an update for ever. A mismatch is
refused and nothing is written.

Uses the 'cryptography' package when it is installed, otherwise a plain
Python Ed25519 (RFC 8032): slower, but a signature still takes well under
a second.
"""
import argparse, hashlib, os, sys


def _sign_pure(seed, msg):
    p = 2 ** 255 - 19
    q = 2 ** 252 + 27742317777372353535851937790883648493
    d = -121665 * pow(121666, p - 2, p) % p

    def add(P, Q):
        # extended coordinates (X, Y, Z, T)
        x1, y1, z1, t1 = P
        x2, y2, z2, t2 = Q
        a = (y1 - x1) * (y2 - x2) % p
        b = (y1 + x1) * (y2 + x2) % p
        c = t1 * 2 * d * t2 % p
        dd = z1 * 2 * z2 % p
        e, f, g, h = b - a, dd - c, dd + c, b + a
        return (e * f % p, g * h % p, f * g % p, e * h % p)

    def mul(s, P):
        Q = (0, 1, 1, 0)
        while s:
            if s & 1:
                Q = add(Q, P)
            P = add(P, P)
            s >>= 1
        return Q

    def enc(P):
        x, y, z, _ = P
        zi = pow(z, p - 2, p)
        x, y = x * zi % p, y * zi % p
        return (y | ((x & 1) << 255)).to_bytes(32, "little")

    gy = 4 * pow(5, p - 2, p) % p
    xx = (gy * gy - 1) * pow(d * gy * gy + 1, p - 2, p) % p
    gx = pow(xx, (p + 3) // 8, p)
    if (gx * gx - xx) % p:
        gx = gx * pow(2, (p - 1) // 4, p) % p
    if gx & 1:
        gx = p - gx
    G = (gx, gy, 1, gx * gy % p)

    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    A = enc(mul(a, G))
    r = int.from_bytes(hashlib.sha512(h[32:] + msg).digest(), "little") % q
    R = enc(mul(r, G))
    k = int.from_bytes(hashlib.sha512(R + A + msg).digest(), "little") % q
    S = (r + k * a) % q
    return R + S.to_bytes(32, "little")


def sign(seed, msg):
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    except ImportError:
        return _sign_pure(seed, msg)
    return Ed25519PrivateKey.from_private_bytes(seed).sign(msg)


def sis_version(path):
    """the .sis's own version, (major, minor), from its header"""
    d = open(path, "rb").read(0x2c)
    if len(d) < 0x2c or int.from_bytes(d[4:8], "little") != 0x1000006D or int.from_bytes(d[8:12], "little") != 0x10000419:
        raise SystemExit("%s: not an EPOC R5 .sis" % path)
    return int.from_bytes(d[0x28:0x2a], "little"), int.from_bytes(d[0x2a:0x2c], "little")


def check_version(path, version):
    """refuses a version that is not the .sis's own (two digits: 1.13)"""
    parts = version.split(".")
    if len(parts) != 2 or not parts[0].isdigit() or not parts[1].isdigit() or len(parts[1]) != 2:
        raise SystemExit("version %r: two digits after the point, as the .pkg has it (1.13)" % version)
    got = sis_version(path)
    if version != "%d.%02d" % got:
        raise SystemExit("%s is %d.%02d, not %s: build it with that version (the .pkg and the program's version), or sign the version it is"
                         % (path, got[0], got[1], version))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sis"); ap.add_argument("version")
    ap.add_argument("--product", default="PsiTerm", choices=["PsiTerm", "PsiWeb", "PsiMail"])
    ap.add_argument("--key", default=None,
                    help="key file (hex seed); default: $PSITERM_RELEASE_KEY, else ~/.psiterm-signing/release.key")
    a = ap.parse_args()
    check_version(a.sis, a.version)
    if a.key:
        hexseed = open(a.key).read()
    elif os.environ.get("PSITERM_RELEASE_KEY"):
        hexseed = os.environ["PSITERM_RELEASE_KEY"]       # e.g. exported by your shell from a password manager
    else:
        hexseed = open(os.path.expanduser("~/.psiterm-signing/release.key")).read()
    seed = bytes.fromhex(hexseed.strip())
    digest = hashlib.sha256(open(a.sis, "rb").read()).digest()
    msg = a.product.encode() + b" update\n" + a.version.encode() + b"\n" + digest
    out = a.sis + ".sig"
    open(out, "w").write(a.version + "\n" + sign(seed, msg).hex() + "\n")
    print("wrote", out)


if __name__ == "__main__":
    main()
