# psion-core

The shared code and tools behind the Psion Series 5mx suite:
[PsiTerm](https://github.com/danieledge/psiterm) (SSH),
[PsiMail](https://github.com/danieledge/psimail) (email and calendar),
[PsiWeb](https://github.com/danieledge/psiweb) (web browser),
[PsiCast](https://github.com/danieledge/psicast) (screen sharing),
[PsiLinux](https://github.com/danieledge/psilinux),
[PsiConnect](https://github.com/danieledge/psiconnect) (the Atom modem firmware)
and [PsiProxy](https://github.com/danieledge/psiproxy). The suite's home page
is [danieledge.github.io/psion](https://danieledge.github.io/psion/).

Each program's repository carries this one as the git submodule `core/`.
There is nothing to install from here on its own: clone a program with
`--recurse-submodules` and build it as its README says.

## What is here

- **The connection layer** (`ssh/psiglue.cpp`): the serial port with a
  Wi-Fi modem (Hayes `ATDT host:port`, then raw TCP), or the Psion's own
  "Psion Internet" dial-up (PPP) stack. Bounded timeouts, a switch-on
  recheck, `link:` log lines. Every engine links it.
- **The shared memory layout** between a program and its engine
  (`ssh/psishared.h`), the doorbells (`ssh/psibell.h`) and the engines' log
  (`ssh/ptlog.cpp`).
- **The connection settings** every program shares (`ssh/psilink.h`,
  `C:\System\Data\PsiLink.ini`), the **Test** button (`ssh/pglinktest.cpp`)
  and `psihold.exe` (`ssh/psihold.cpp`), which keeps the dial-up link up
  after use.
- **The screen**: the 16-grey calibration and its Preferences screen
  (`ssh/psigrey*`, `ssh/psigreyui.cpp`, `ssh/psidisp.h`).
- **TLS 1.3** (`ssh/tls13.c`: X25519, ChaCha20-Poly1305), with LibTomCrypt,
  LibTomMath and zlib (`ssh/db/`, `ssh/zlib/`).
- **The HTTP client** the single-connection programs use
  (`web/engine/pwnet.c`), and the C-library shims for ESTLIB
  (`mail/compat/`, `web/compat/`).
- **The picture decoders** to 16 greys (`mail/engine/img/`: JPEG including
  progressive, PNG, GIF), used by PsiMail and by the Atom firmware's picture
  converter.
- **The tools**: the `psion-build` Docker image and `psibuild`
  (`tools/docker/`), the emulator tools (`tools/emu/`), release signing
  (`tools/release/`), the icon, font and grey tools, and the optional local
  update server (`server/`).
- **The documentation** that applies to every program: [building and
  releases](docs/BUILDING.md), [testing](docs/TESTING.md), the EPOC
  [robustness](docs/epoc-robustness-best-practices.md) and
  [comms](docs/epoc-comms-best-practices.md) best practice, the
  [display](docs/display.md) and [power](docs/power.md) notes.

[docs/BUILDING.md](docs/BUILDING.md) lists every file and says why the
layout is what it is: the same relative paths as in the PsiTerm repository
before the split (October 2026), so the history of every file carries on
here unbroken.

## Using it from a program

```sh
git clone --recurse-submodules https://github.com/danieledge/psimail.git
cd psimail
core/tools/docker/psibuild setup     # once: the build image and the SDK
core/tools/docker/psibuild all       # dist/PsiMail.sis
```

A program's build links `core/ssh` into the SDK tree as `ptproj/psitermssh`,
where its `.mmp` finds the shared sources, builds its engine with
`core/ssh/psiglue.cpp` and friends, and makes `psihold.exe` with
`core/ssh/psihold.mk`. The emulator tools run from the program's checkout:
`node --experimental-strip-types core/tools/emu/mkcard.ts psimail`, then
`core/tools/emu/run.sh NAME ...` (see [tools/emu/README.md](tools/emu/README.md)).

## Changing core

Core changes are made in a program's `core/` submodule (or a checkout of
this repository), committed and pushed here first, and then the program's
submodule pointer is moved to the new commit. The host tests of the shared
layer run with `make -f test/Makefile.host test` in `ssh/`.

## Support

If the suite is useful to you, you can
[buy me a coffee](https://buymeacoffee.com/danedge).

## Licence

MIT (`LICENSE`). Third-party code keeps its own licence: see
[THIRD-PARTY.md](THIRD-PARTY.md).
