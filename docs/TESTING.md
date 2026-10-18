# Testing PsiTerm, PsiMail, PsiWeb and the Atom modem

This is how the suite is tested: what runs on a PC, what runs in an emulator, what needs the real Series 5mx, and what is checked before each release. Everything runs inside the `psion-build` Docker image unless noted (`core/tools/docker/psibuild "<command>"`, from a program's checkout). Each program is its own repository (psiterm, psimail, psiweb, psiconnect for the Atom firmware) with this core as its `core/` submodule; the paths below are inside each one.

## The layers

| Layer | What it proves | Speed | Where |
|---|---|---|---|
| 1. Host unit tests | Parsers, protocols, report wording, firmware logic | seconds | PC |
| 2. Fuzzers | Untrusted input cannot crash the parsers | minutes | PC (ASan/UBSan) |
| 3. Host engine builds against real servers | SSH, SFTP, TLS and updates with real network code | minutes | PC |
| 4. ARM harness (unicorn) | The exact ARM `psiweb.exe` runs pages within the memory limit; CPU time estimated | minutes | PC |
| 5. Psion emulator | The real ROM, EIKON UI, apps and engines together, including the network through a fake modem | minutes | PC |
| 6. The 5mx | Timing, the real serial port and modem, Psion Internet (PPP), power | by hand | Device |

Each layer catches what the one before it cannot. Nothing is released on layers 1–3 alone.

## 1. Host unit tests

- **PsiMail** (`test/` in psimail, after `bash build.sh host`): `foldertest.sh`, `undotest.sh`, `invtest.py` (invitations), `invitehost.py`, `htmltest.py` (HTML to the reader's format), `certtest.py` (certificate checks). `fakeimap.py` is a scriptable IMAP server with drops, slow links, UIDPLUS and invitations; `smtpserver.py` takes the sends.
- **The shared connection layer** (`core/ssh/test`): `make -f test/Makefile.host test` in `core/ssh` runs `linktest` (every sentence Connection settings > Test can report), `linkseentest`, `psilinktest` (every version of PsiLink.ini loads) and `psiholdtest`.
- **Atom modem firmware** (psiconnect): `make -C hosttest test` runs the modem code (the same `modem.cpp` the Atom runs) against a fake hardware layer.

## 2. Fuzzers

All network input is untrusted. After any change to a parser:

- `test/parsefuzz.py` (psimail): MIME, IMAP, ICS, XML and HTML parsers.
- `test/imgtest.py` (psimail): the picture decoders in `core/mail/engine/img` (JPEG including progressive, PNG, GIF), with ASan and UBSan.

## 3. Host engine builds

psiterm's `ssh/test/Makefile.host` builds `psissh-host`: the real Dropbear client and `psishim.c` on a PC, with `test/psiglue_host.c` standing in for the Psion's network layer. Against a throwaway `sshd` (a high port, its own keys, never the user's `~/.ssh`):

- `e2e.py`, `scenarios.py`, `keylogin.py`, `savedpw.py`: logins, keys, saved passwords.
- `sftp_test.py`: file transfer, including Stop and failures.
- `update.py`: signed updates, including bad signatures and interrupted downloads (test key: `update-test.key`).
- Hooks in `psiglue_host.c` drive the shared-memory requests the apps make: `PSI_XFER` (file transfer scripts) and `PSI_TQ` (tmux queries: `list`, `select`).

## 4. ARM harness for PsiWeb

psiweb's `links/emu/run_links.py` (and `run_pages.sh`) runs the exact ARM `psiweb.exe` (Links) under unicorn, with Python standing in for EPOC and the app, and real TCP for the network. It:

- enforces the 10 MB heap the engine has on the Psion;
- counts ARM instructions, to estimate time on the 36 MHz ARM710 (at an assumed 15 MIPS);
- records a page's traffic and replays it with an instruction-driven clock, so runs repeat exactly, and `--rate` imitates a 10 KB/s modem;
- saves screenshots at 640×240 in 16 greys.

The fixed page set (info.cern.ch, 68k.news, text.npr.org, mobile Wikipedia, BBC with and without pictures, a local picture page) is the gate for every change to the engine: time, heap and screenshots before and after. `emu/run_psiweb.py` does the same for the old NetSurf engine.

## 5. The Psion emulator

`core/tools/emu/` drives the psionEmulators harness (the real 5mx ROM) with a CF card built from a program's checkout. See `core/tools/emu/README.md`.

- `mkcard.ts <app>` builds the card; `run.sh NAME "T KEY" "T tap X Y"…` boots, taps and types, and saves a screenshot a second.
- **Smoke tests:** each app starts, every menu opens (EIKON panics if a menu dims an item it does not have), dialogs fit, About and Help open.
- **Network tests** (`net.py at|web|mail|ssh|all`, about 3 minutes): `run.sh` with `EMU_SERIAL=socket` switches the ROM's Remote link off and attaches the serial bridge to UART2 (COMM::0). `net.py` starts the fake modem (psiconnect's `hosttest/hostmodem`, from the checkout beside the program's), a local web server, `fakeimap.py` and a throwaway key-only `sshd`, then checks:
  - PsiTerm types `AT` and the modem answers `OK`;
  - PsiWeb loads a page through the modem;
  - PsiMail's Check mail fetches messages;
  - PsiTerm logs in over SSH with a key from the card and runs a command.
- **Known emulator quirk:** loading can stall, or end in KERN-EXEC 3, on some card layouts. Try another layout (the `PAD.BIN` size) before suspecting the code.
- **Not possible in the emulator:** Psion Internet (PPP), DCD dropping, RTS/CTS.

The test seed (`core/tools/emu/seed.py on`, in psimail) gives PsiMail a mailbox and calendar for screenshots. It must never be committed or released.

## 6. On the 5mx

What only the device shows: real timing, the real UART at 115200, modems (WiRSa, the Atom), Psion Internet, switch-off and on, memory with other apps running. Each release's notes list what to try. The apps write logs to send back:

- `psimail.log` / `psimail.old` in PsiMail's folder (lines `mm:ss.t`, with `link:` steps and command timings);
- `C:\System\Data\PsiWeb.log` / `PsiWeb.old` (status, fetches, memory per page);
- optionally the OS's own logs, by creating `C:\LOGS\Netdial\` and `C:\LOGS\Etel\`.

## Reviews

Structured reviews look for what tests do not: error recovery, conformance to the SDK and comms best practice, and the EIKON style guide. The October 2026 review and its fixes are in `docs/review-2026-10/`; the rules are in `epoc-robustness-best-practices.md` and `epoc-comms-best-practices.md` beside this file.

## Before each release

1. Clean build of each program (`core/tools/docker/psibuild all` in its checkout), after deleting the SDK's shared objects in `$EPOCROOT/epoc32/build/.../ptproj/marmd/rel/*.o`.
2. Host tests and fuzzers (layers 1–2) pass; `linktest` passes.
3. PsiWeb page set in the ARM harness within the heap limit (layer 4), when the engine changed.
4. Each app starts in the emulator with no panic; changed menus opened; `net.py all` when networking changed.
5. `grep TESTSEED app/psimail.cpp` (psimail) is empty.
6. Versions bumped in the `_LIT` and the `.pkg` (two digits), signed with `core/tools/release/sign.py`, version files updated (PsiMail's and PsiWeb's `release.sh` write them into the psiterm checkout's `dist/`); PsiTerm tagged `v<version>`.
7. Device checks listed in the release notes.

## What changed in October 2026

- **Emulator network tests:** found that COMM::0 is UART2 and that the ROM's Remote link held it; added `EMU_SERIAL=socket` and `net.py`. This reproduced PsiWeb's long-standing "Checking the modem..." symptom on a PC for the first time.
- **ARM harness for Links:** instruction profiling, record and replay, the 10 MB heap gate, and the page set as an acceptance test.
- **Host hooks:** `PSI_TQ` for the tmux query channel, alongside `PSI_XFER`.
- **New checks:** CTS hint in `linktest`; certificate tests in `certtest.py`; the coarse-clock switch (`PW_COARSE=1`) in psiweb's `emu/run_psiweb.py`, which showed NetSurf's one-second scheduler problem.
- **Reviews:** three reviews (error recovery, best practice, EIKON) with every finding fixed or answered (`docs/review-2026-10/`).
