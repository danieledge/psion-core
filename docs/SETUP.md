# Choosing your setup

The Psion programs work in more than one way. Two questions decide which
features you get:

1. **How does the Psion get online?** Through the
   [PsiConnect](https://github.com/danieledge/psiconnect) **Atom modem**
   (WiFi), through **PsiConnect Host** (the same firmware as a program on a
   Mac or Linux computer, with a serial cable; in testing), or through any
   other WiFi modem (a WiRSa, a WiFi232).
2. **Do you also run [PsiProxy](https://github.com/danieledge/psiproxy)** on
   a computer on your network? It is optional, and adds the heavier work and
   a few services.

**The short answer:** an Atom (or PsiConnect Host) is all you need for SSH,
email and the web. PsiProxy adds the rest.

## What each setup gives

| | Atom modem | PsiConnect Host (in testing) | Plus PsiProxy (with either, or alone) |
|---|---|---|---|
| Getting online | WiFi, up to 8 saved networks, WireGuard home | the computer's own network | - |
| PsiTerm (SSH), PsiMail (mail, calendar, contacts) | yes, and the modem can do PsiMail's TLS | yes, the same | - |
| Psion Internet (PPP) for the Psion's own programs | yes | yes | - |
| PsiWeb: TLS done for the Psion, pages simplified, text-only and reader modes, gzip on the line | yes | yes | yes, with full slimming: articles in one column, home pages as lists of headlines, site menus folded into one link |
| Pictures in 16 greys | JPEG, PNG and GIF up to 64 KB | the same | every format (WebP and AVIF too), any size, and PsiMail's web pictures |
| Pages drawn by JavaScript | no | no | yes, if you turn on its headless browser |
| PsiCast (the Psion's screen in a browser) | yes, with a USB keyboard on the Atom | yes (Type on phone on Linux) | - |
| PsiLinux packages (Alpine's `apk`) | no | no | yes (PsiLinux's plug-in) |
| Send log to server (every program) | no | no | yes |

Without either, the programs still work on their own: PsiTerm and PsiMail
do their own TLS, and PsiWeb fetches pages directly with its own TLS 1.3
(slower, and heavy pages may not fit).

## Setting it up

**The Psion's proxy setting** is one of the shared Connection settings
(Tools > Connection settings > Network > Use proxy), set once for every
program.

- **With an Atom (or PsiConnect Host):** set the proxy host to
  `psiproxy`, port `8080`. That is the modem itself.
- **If you also run PsiProxy**, keep the Psion's setting as it is and tell
  the modem where PsiProxy is: `AT$PU=<computer's address>:8081` and
  `AT&W`, or on the modem's web pages (Settings > Web proxy).
  The modem then hands each call to PsiProxy, and falls back to its own
  proxy when PsiProxy does not answer (away from home). Nothing changes on
  the Psion when you add or remove PsiProxy.
- **With PsiProxy and no Atom** (another modem, or the Psion's own network):
  set the proxy host to the computer's address and port `8081`.

A program that needs PsiProxy says **"Needs PsiProxy on a computer"** when
there is none, rather than failing with a connection error.

## The setups in more detail

- **Atom modem:** [PsiConnect's README](https://github.com/danieledge/psiconnect#readme)
  (the firmware, its web proxy, pictures, PPP and the mail relay) and the
  modem's web pages for its WiFi and settings.
- **PsiConnect Host:** the same firmware for Linux and macOS, with a USB to
  RS-232 adapter. It is being tested and is not released yet.
- **PsiProxy:** [its README](https://github.com/danieledge/psiproxy#readme)
  (installing, running it as a service, the settings, security). The link
  from the Psion to PsiProxy is plain HTTP: keep it on a network you trust.
