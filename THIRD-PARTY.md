# Third-party components

| Component | Where | Licence |
|---|---|---|
| LibTomCrypt and LibTomMath, as vendored in [Dropbear SSH](https://github.com/mkj/dropbear) | `ssh/db/libtomcrypt/`, `ssh/db/libtommath/` | public domain / Unlicense - see the `LICENSE` files in each |
| [Dropbear SSH](https://github.com/mkj/dropbear): its X25519 (`curve25519.c`, from [TweetNaCl](https://tweetnacl.cr.yp.to/)) and `dbrandom.h`, with a 32-bit field for the ARM710 (`curve25519_gf32.h`, made by `ssh/tools/gen_gf32.py`) | `ssh/db/src/` | MIT (Dropbear) and public domain (TweetNaCl); the rest of Dropbear is in the psiterm repository |
| [zlib](https://zlib.net/) (upstream commit `767c4c94`) | `ssh/zlib/` | zlib licence - see `ssh/zlib/LICENSE` |
| [picojpeg](https://github.com/richgel999/picojpeg) (baseline JPEG decoder, Rich Geldreich; with a luma-only mode, 1/2 and 1/4 shrink-on-decode and an 8-bit Huffman look-up added for the Psion, marked `PsiMail:` in the source) | `mail/engine/img/picojpeg.c`, `picojpeg.h` | public domain |
| [Terminus Font](https://terminus-font.sourceforge.net/) (converted to EPOC format) | `tools/fonts/`, `pkg/psiterm.gdr` | SIL Open Font License 1.1 - see `pkg/terminus-licence.txt` |
| EPOC C Standard Library (`ESTLIB.DLL`) from the Psion EPOC R5 C++ SDK | `tools/emu/ESTLIB.DLL`, for the emulator's card only | Psion's redistributable runtime, shipped with programs that use it as the SDK directs |

The PNG and GIF decoders, the progressive JPEG decoder and the 16-grey
dithering in `mail/engine/img/` (`pmimg.c`, `pmpng.c`, `pmgif.c`, `pmjpeg.c`,
`pmjprog.c`) are PsiMail's own code (MIT). `pmjprog.c` decodes the scans as
the JPEG standard (ITU T.81, Annex G) describes, and its integer IDCT is the
one in [stb_image](https://github.com/nothings/stb) by Sean Barrett (public
domain / MIT, either at your choice), adapted.

The TLS 1.3 client (`ssh/tls13.c`, with `web/tls/`) is PsiTerm's own code
(MIT), built on the LibTomCrypt and TweetNaCl code above.

Everything else here (`psiglue.cpp`, the shared headers, the tools, the
server scripts and the documentation) is this project's own code, MIT
(`LICENSE`).
