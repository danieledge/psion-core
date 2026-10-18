# CLAUDE.md: psion-core

The shared code and tools of the Psion Series 5mx suite (PsiTerm, PsiMail,
PsiWeb, PsiCast, PsiLinux, PsiConnect, PsiProxy). Every program's repository
carries this one as the git submodule `core/`. Read `docs/BUILDING.md` before
changing anything here: it lists every file and what depends on it.

## What this is

- `ssh/psiglue.cpp`: the network and serial layer linked into every engine.
  Modem route (`RComm` on `COMM::0`, Hayes `ATDT host:port`, then raw TCP) or
  the Psion's own PPP stack (ESOCK, NetDial, NIFMAN). Bounded timeouts
  (lookup 60 s, connect 30 s, CONNECT 25 s), a switch-on recheck, `link:`
  log lines. `docs/epoc-comms-best-practices.md` is the reference.
- `ssh/psishared.h`: the global chunk (`RChunk`) layout every program and
  engine share (single-writer rings, a command queue, progress strings,
  heartbeats both ways). Every engine's chunk starts with `PsiShared`;
  `PSI_SHARED_NAME` names it ("PsiTermSSH", "PsiMailShared"...).
- `ssh/psilink.h`: `C:\System\Data\PsiLink.ini`, the connection settings
  all programs share (versioned; v2 added the optional proxy, v4 "Stay
  connected"). `ssh/pglinktest.cpp`: the Test button. `ssh/psihold.cpp`:
  `psihold.exe`, one running per Psion, found by scanning
  `\System\Apps\*\psihold.exe` (no program names compiled in).
- `ssh/tls13.c`: the TLS 1.3 client (X25519, ChaCha20-Poly1305) on
  LibTomCrypt (`ssh/db/libtomcrypt`), LibTomMath and Dropbear's
  `curve25519.c` (`ssh/db/src`). Each program's own `tls/` folder provides
  `includes.h` and `options.h` for it.
- `web/engine/pwnet.c`: one connection at a time over psiglue, the HTTP
  client of PsiWeb, PsiCast and the other single-connection programs (`PW_NO_TLS` builds it
  without TLS).
- `mail/engine/img/`: the picture decoders to 16 greys, also compiled into
  the Atom firmware's picture converter.
- `mail/compat/pmcompat.h`, `web/compat/`: what ESTLIB lacks. The engines
  must not use ESTLIB's `sprintf` and stdio (it corrupted the stack:
  KERN-EXEC 3); `web/compat/nsprintf.c` replaces them.
- `tools/`: the build container (`tools/docker/psibuild`), the emulator
  tools (`tools/emu`, see its README), release signing (`tools/release`),
  the icon, font and grey tools. `server/`: the optional local update server.

The target: ARM710T at 36 MHz, no FPU, no Thumb, about 16 MB RAM, 640x240 in
16 greys, EPOC Release 5, gcc 3.0 (Psion 98r2). All network input is
untrusted: bounded copies everywhere.

## Layout rule

The paths here are the same relative paths as in the PsiTerm repository
before the split (`ssh/`, `mail/`, `web/`, `tools/`, `pkg/`, `server/`,
`docs/`), so each program's build only changed a prefix (`$TOP/ssh` became
`$CORE/ssh`) and `git log --follow` works on every file. Keep it that way:
add shared files beside their relatives, and do not rename folders.

Interfaces that programs depend on and must not change size or meaning
without every program being rebuilt: `PsiShared` (`ssh/psishared.h`),
`TPsiLink` (`ssh/psilink.h`), the `pg_*` functions psiglue exports and the
`pwn_*` functions of pwnet.

## Building and testing

Nothing builds from here on its own. Build a program with core as its
`core/` submodule (`core/tools/docker/psibuild "bash build.sh"` in its
checkout). The host tests of the shared layer: `make -f test/Makefile.host
test` in `ssh/`. The picture decoders are tested by psimail's
`test/imgtest.py` and the firmware's host tests.

A program picks up a core change by moving its submodule pointer: commit and
push here first, then `git -C core checkout <commit>` and commit in the
program. The SDK keeps every program's objects in one folder
(`$EPOCROOT/epoc32/build/.../ptproj/marmd/rel`): delete the `*.o` there
after switching checkouts (stale objects have caused USER 19 panics).

## Rules

- Never commit keys (the release key lives outside every repository), the
  SDK, the emulator or its ROMs, or anything private: no LAN addresses, home
  paths, SSIDs or local headers (`*_local.h`). The proxy's address is a
  user setting, never compiled in.
- Don't change the connection settings (the dialog, its fields, formats or
  defaults) unless Dan asks.
- Writing style: British English, plain short sentences, the EIKON glossary
  terms, no em dashes.
- Commit messages are plain British English.
