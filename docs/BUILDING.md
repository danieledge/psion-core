# Building, layout and releases

Developer notes for the Psion suite: PsiTerm, PsiMail, PsiWeb and the
programs built on the same code. Each program is its own repository and
carries this one, psion-core, as the git submodule `core/`.

## What you need

Linux, wine, and the EPOC R5 C++ SDK with the gcc 3.0 Psion toolchain:
[psion_cpp_sdk_linux](https://github.com/static-void/psion_cpp_sdk_linux)
(containing `epoc_cpp_sdk/` and `gcc-3.0-psion-98r2-9/`). Docker gives you
all of it without installing anything on the host: see *The build
container* below.

## Getting a program

```sh
git clone --recurse-submodules https://github.com/danieledge/psimail.git
cd psimail
core/tools/docker/psibuild setup        # once: the image, the SDK (beside the checkout)
core/tools/docker/psibuild all          # dist/PsiMail.sis
```

A checkout made without `--recurse-submodules` gets its core with
`git submodule update --init`.

## The build container

`core/tools/docker/psibuild` runs a command in the `psion-build` image with
the work area mounted at the same path, so the SDK's absolute paths and
symlinks keep working. The work area (`PSION_WORK`) is the folder above the
program's checkout:

```
$PSION_WORK/psiterm                  a program's checkout, with core/
$PSION_WORK/psimail
$PSION_WORK/psion_cpp_sdk_linux      the SDK (psibuild setup fetches it)
$PSION_WORK/.wine                    wine's prefix (bmconv, makesis, makmake...)
$PSION_WORK/psionEmulators           the emulator, for core/tools/emu
```

The program whose checkout holds this core is found from the script's own
place (`core/..`); `PSION_APP` names it when core is checked out on its own.

## Building each program

Without the container, set `PSION_SDK` to the SDK's folder and run the
program's `build.sh` directly. With it:

| Program | Command, from its checkout | Makes |
|---|---|---|
| PsiTerm | `core/tools/docker/psibuild "bash build.sh"` | `dist/PsiTerm.sis` |
| PsiMail | `core/tools/docker/psibuild "bash build.sh host"` | `dist/PsiMail.sis`, and with `host` the PC engine `build/mail-host/psimail-host` for the tests |
| PsiWeb | `core/tools/docker/psibuild "bash build.sh"` | `dist/PsiWeb.sis` (the first build fetches Links at pinned checksums) |
| PsiConnect (the Atom firmware) | `pio run -e atoms3r`, `make -C hosttest test` | the firmware, its host tests (no SDK needed) |

Every program's build also makes `psihold.exe` (`core/ssh/psihold.mk`) and
puts it in the program's folder on the Psion. The SDK's `makmake` works on
projects inside the SDK tree, so each build links its folders there:
`$EPOCROOT/ptproj/<name>` (PsiCast uses `castproj`). The link
`ptproj/psitermssh` points at `core/ssh`, which is where the programs' `.mmp`
files find the shared sources (`SUBPROJECT psitermssh`).

**Stale objects:** the SDK keeps every ptproj program's objects in one
folder (`$EPOCROOT/epoc32/build/.../ptproj/marmd/rel`). After switching
branches or checkouts, delete the `*.o` files there: stale objects have
caused panics (USER 19).

## What is in core

The same relative paths as in the PsiTerm repository before the split, so
nothing had to be renamed and `git log` follows every file back:

| Path | What |
|---|---|
| `ssh/psiglue.cpp` | the connection layer every engine links: the serial port and a Wi-Fi modem, or the Psion's own dial-up (PPP) |
| `ssh/psishared.h`, `ssh/psibell.h` | the shared memory chunk between each program and its engine, and the doorbells |
| `ssh/psilink.h` | `C:\System\Data\PsiLink.ini`, the connection settings every program shares |
| `ssh/psidial.h` | the connection for a program that uses the socket server itself (Internet time, PsiBoot's updates): the Psion Internet route's first send to the modem, as psiglue's, and the link taken down after if it brought it up; a call on the modem route. Header only (c32.lib, nifman.lib) |
| `ssh/psiwork.h` | work memory: the big buffers an engine needs only while it is connected or syncing, each a chunk of its own (`pg_work_alloc`, `pg_work_free`, `pg_work_trim` in `psiglue.cpp`; `calloc` on a PC), so that freeing gives the pages back to the Psion. Used by `tls13.c` (taken by `tls_connect`, given back by `tls_close`), `certcheck.c` (the chain, from Certificate to CertificateVerify), `caldav.c`, `carddav.c`, `vcard.c` (one sync) and PsiMail's IMAP buffers (while a mailbox is open) |
| `ssh/psimem.h` | short of memory: the free-memory levels and the window-group messages (`trim`, `sleep`, `dialog`, `short`, `sleepnow` and its answers `slept`/`cannot`) between the programs and the launcher that watches the memory; the free memory figure (`PsiMemFreeKb`, `PsiMemFreeText`) and the toolbars' bell (`PsiMemRing`). Coda's launcher and toolbar use it too |
| `ssh/psimemlvl.h` | the memory level the watcher publishes in the global chunk `PsiMemLevel` (`PsiMemLevelNow`; `pg_mem_level` for the C engines), the list of programs that answer `trim` and `sleep` (`PsiMemRegister`, `PsiMemBusy`, `PsiMemUnregister`), `PsiMemWordOf` and `PsiMemCompress`. Included by `psimem.h` |
| `ssh/psialloc.h`, `ssh/psialloc.c` | a malloc for the C engines, off unless a build sets `PSI_ALLOC=1`: blocks of 32 KB and more are chunks of their own (`pg_chunk_new`, `pg_chunk_adjust`, `pg_chunk_close` in `psiglue.cpp`), given back at once when freed and grown in place by `realloc`. Host test `ssh/test/psialloctest.c` |
| `ssh/psivec.h` | a growing array in a work block of its own (`psi_work_alloc_max`, `psi_work_grow`): no copy and no hole as it grows; `PSI_VEC=0` gives the same calls on `realloc` |
| `ssh/psipurge.h` | purgeable caches, off unless a build sets `PSI_PURGE=1`: a cache in a global chunk named `PsiPurge...` that the memory watcher empties with `PsiPurgeAll` when it is not pinned |
| `ssh/psikeep.h` | settings kept on the card as well as on C: (`D:\System\Data\Kept\`), without passwords or keys, put back on a new C:. Header only (efsrv.lib) |
| `ssh/psizero.h` | folders made on a card after a file of zeros, so that a computer's disk check reads them right (EPOC does not clear a new folder's cluster): one file for all the folders to make, sized by the card's cluster. Used by `psikeep.h` and Coda's `psimkdir.h`. Header only (efsrv.lib) |
| `ssh/psierr.h` | an EPOC error as words for a message (`PsiErrWords`, `PsiErrTail`, `PsiErrMsg`: "Not saved - the disk is full", no tail when the reason is not known) and "<What> is on the card - put the card in" (`PsiOnCardMsg`). PsiMail, PsiWeb, PsiLinux, PsiAmp and PsiPDF; Coda's own programs keep the same words in their own copy. Header only (euser.lib). Host test `ssh/test/psierrtest.cpp` |
| `ssh/pglinktest.cpp`, `.h` | Connection settings > Test, compiled into every program |
| `ssh/aesgcm.c`, `.h` | AES-128-GCM, TLS 1.3's second cipher (`TLS_AES_GCM`: PsiWeb from 0.86; PsiMail can define it in its `tls/includes.h`) |
| `ssh/psilink.rh` | The words of the shared connection settings (the route names), for every program's `.rss` |
| `ssh/pgsetup.cpp`, `.h`, `.hrh`, `.rssi` | The setup assistant for new users (PsiWeb, PsiMail): `docs/setup-assistant.md` |
| `ssh/pglinkseen.h`, `ssh/pgdevbox.h` | the link's last sign of life; the optional dev box (PsiProxy-only routes) |
| `ssh/psihold.cpp`, `ssh/psihold.mk` | `psihold.exe`, which keeps the Psion Internet link up after use ("Stay connected") |
| `ssh/psigrey*.h`, `ssh/psigreyui.cpp`, `ssh/psidisp.h` | the 16-grey calibration, its Preferences screen, and the display helpers |
| `ssh/ptlog.cpp` | the engines' log file (written only when the app's "Keep a log" is ticked: `PsiShared.log_on`) |
| `ssh/tls13.c`, `.h` | the TLS 1.3 client (X25519, ChaCha20-Poly1305), used by PsiMail, PsiWeb and the updaters |
| `ssh/zlib/` | zlib, as it is (inflate for PNG pictures and updates, deflate for PsiCast) |
| `ssh/db/libtomcrypt/`, `ssh/db/libtommath/` | LibTomCrypt and LibTomMath from Dropbear (`ltc_sources.txt`, `ltm_sources.txt` list what the programs build) |
| `ssh/db/src/curve25519.c`, `curve25519_gf32.h`, `dbrandom.h` | Dropbear's X25519 (TweetNaCl, with a 32-bit field for the ARM710) and its random number interface, shared by TLS and SSH; `ssh/tools/gen_gf32.py` makes the field code |
| `ssh/test/` | the host tests of the shared layer (`make -f test/Makefile.host test` in `ssh/`) |
| `mail/compat/pmcompat.h`, `web/compat/` | the C-library shims for ESTLIB (headers it lacks, `nsprintf.c`) |
| `mail/engine/img/` | the picture decoders (JPEG including progressive, PNG, GIF) to 16 greys, used by PsiMail and the Atom firmware's picture converter |
| `mail/engine/cal/` | the calendar engine, shared by PsiMail and PsiSync: CalDAV (`caldav.c`) over HTTP (`http.c`), iCalendar (`ics.c`), time zones (`caltz.c`), XML (`xmlscan.c`), UTF-8 and Windows-1252 (`cp1252.c`), string helpers (`calutil.c`); and, in plain C with no C library so the Agenda half and the Control panel can link them, the rules for each Agenda entry at a sync (`calmerge.c`), Agenda repeats as RRULEs (`calrpt.c`) and places to time zones (`calzone.c`), tested by PsiSync's `test/synctest.c`. `calport.h` is what a program gives it; `calacct.h` the layout of PsiMail's account and calendar settings |
| `mail/engine/card/` | contacts: CardDAV (`carddav.c`, on the calendar engine's HTTP, XML and port) and vCards (`vcard.c`: a server's 3.0 or 4.0 card to the Psion's 2.1 for Contacts' own reader, and the Psion's changes merged back into the server's card, kind by kind, everything else kept). PsiSync; tested by its `test/synctest.c` and `test/cardtest.py` |
| `mail/contacts/` | the Contacts half of contacts sync (`pmcard.cpp`): the cards into the Psion's Contacts database through Contacts' own model and vCard reader and writer, the same rules as the calendar's (`calmerge.c`). PsiSync |
| `mail/engine/tlsv/` | the TLS certificate check (`certcheck.c`, RSA in `pmrsa.c`, ECDSA on P-256 and P-384 in `ecverify.h`, which `certcheck.c` includes, the built-in roots in `roots.h`, made by psimail's `tools/mkroots.py`), linked into `ssh/tls13.c` built with `TLS_VERIFY`: PsiMail, PsiSync, PsiWeb (0.84 on). Build `certcheck.c` with `-march=armv4` (UMULL: ECDSA about three times faster; every EPOC R5 machine has it) |
| `mail/agenda/` | the Agenda half of the calendar (`pmcal.cpp`, `CPmCalSync`): the engine's events into the Psion's Agenda through the Agenda server, and the Agenda's changes back. PsiMail.app and PsiSync. `pmcalc.cpp` compiles `calmerge.c` and `calrpt.c` as C++ for an EIKON program (makmake builds `.c` files with BX, which the netBook lacks). PsiSync's options (`TPmCalSettings::iSpare[0]`, `KPmCalOpt*`): keep both versions when both changed, repeating entries to the server, a guard against emptying the Agenda; PsiMail leaves them off |
| `mail/host/pgnet_host.c` | a PC stand-in for `psiglue.cpp`, for the engines' host builds and tests |
| `web/engine/pwnet.c`, `.h` | one network connection at a time over psiglue: the HTTP client PsiWeb, PsiCast and the other single-connection programs use |
| `web/tls/` | the X25519 field arithmetic, ChaCha20-Poly1305 and SHA-256 behind `tls13.c` (each program's own `tls/` folder adds its LibTomCrypt configuration) |
| `tools/docker/` | the `psion-build` image and `psibuild` |
| `tools/v4bx.py`, `tools/v4bx.mk` | the SDK's `libgcc.a` with every BX made MOV PC, which every engine links (`$(LIBGCC)`): its helpers return with BX, an undefined instruction on the netBook's SA-1100 (KERN-EXEC 3). `v4bx.py --check` counts the BX left in a linked `.tmp.exe` |
| `tools/emu/` | the emulator tools: card images, screenshots, network tests ([README](../tools/emu/README.md)) |
| `tools/release/` | `sign.py` (release signing) and `publish-local.sh` |
| `tools/icon/`, `tools/fonts/`, `tools/mkicons.py`, `tools/mkgrey.py`, `tools/mkcalpic.py` | the AIF writer, the font conversion (`tools/fonts/mkgd.py`: Terminus, Spleen, Gohufont, Cozette into one `.gdr`), the toolbar pictures and the grey tables |
| `tools/drawtest/`, `tools/shot/`, `tools/sbtest/` | PsiTerm's terminal rendering tests and screenshot renderer |
| `pkg/psiterm.gdr`, `pkg/terminus-licence.txt`, `pkg/font-licences.txt` | the terminal fonts in EPOC format (typefaces Terminus, Spleen, Gohufont and Cozette), installed by PsiTerm and PsiLinux. Rebuild: `python3 mkgd.py > psiterm.gd` in `tools/fonts`, then the SDK's `fnttran psiterm.gd psiterm.gdr` |
| `server/` | the optional local update and upload server (plain HTTP) |
| `docs/` | these notes, [TESTING.md](TESTING.md), the EPOC robustness and comms best practice, the display and power notes, the October 2026 review |

The programs' own code stays in their repositories: PsiTerm's
`app/`, `ssh/db` (the rest of Dropbear), `ssh/psishim.c`, `ssh/sftp.c`,
`libvterm/`; PsiMail's `app/`, `engine/`; PsiWeb's `app/`, `links/`.

## Updates and signing

**Tools > Update PsiTerm** asks where to look (GitHub, its test builds, or a
local server), then fetches `dist/version.txt` and `dist/PsiTerm.sis`
from the psiterm repository over HTTPS. The Psion speaks a minimal TLS 1.3
client (`ssh/tls13.c`) over the same modem or dial-up link SSH uses. PsiMail
and PsiWeb do the same with `dist/PsiMail-version.txt` and
`dist/PsiWeb-version.txt`, downloading in 64 KB pieces that are each checked.

**Where the files are read from has not changed with the split.** Every
installed PsiTerm, PsiMail and PsiWeb reads
`https://github.com/danieledge/psiterm/.../dist/`, so the psiterm repository's
`dist/` keeps all three programs' `.sis`, `.sig` and version files. PsiMail's
and PsiWeb's `release.sh` sign a build and write those files into a psiterm
checkout beside their own (`../psiterm/dist/`, or `$PSITERM`), to be committed
and pushed there.

Every release is signed. `dist/<Product>.sis.sig` holds an Ed25519 signature
over the version and the SHA-256 of the .sis, and the program checks it
against the public key built into it before installing anything. The Psion
does not check TLS certificates for updates (no CA store, too slow), so the
signature is what makes updates trustworthy, from GitHub or anywhere else.
PsiMail's and PsiWeb's signed text names the product ("PsiWeb update"...), so
one program's signature can never pass for another's.

To make a release: build, run
`core/tools/release/sign.py [--product PsiMail|PsiWeb] dist/X.sis <version>`
with the release key (kept outside every repository: `--key`,
`$PSITERM_RELEASE_KEY` or `~/.psiterm-signing/release.key`), and set the
version file. PsiTerm reads `version.txt` from the branch, then fetches the
signature and the .sis from the **tag** `v<version>` (`.../v0.95/dist/`), so
GitHub's cache can never mix old and new files: tag the commit and push the
tag before, or with, the `version.txt` that names it. PsiMail and PsiWeb read
the branch's `dist/`.

The dialog's Local server choice points the programs at your own server
instead (`core/server/psion-update.sh`, plain HTTP, port 8686;
`core/tools/release/publish-local.sh PsiMail 0.3.1` puts a build there). The
same server receives PsiTerm's Debug screenshots, a developer feature.
