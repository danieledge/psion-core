# The setup assistant and the route names

Shared by PsiWeb and PsiMail (any program with Connection settings can carry
it), 17 November 2026. Files: `ssh/pgsetup.h` (the choices, plain C, host
tested by `ssh/test/setuptest.cpp`), `ssh/pgsetup.cpp` (the EIKON dialog),
`ssh/pgsetup.hrh` (ids, 0x7c00 to 0x7cff), `ssh/pgsetup.rssi` (the pages,
choices and buttons), `ssh/psilink.rh` (the words).

## The route names

What the user reads, in every program (Help, About, Page information, the
dialogs). The word "proxy" is not used in what the user reads; the settings
underneath (`TPsiLink`, `PsiLink.ini`) did not change.

| Setting (`TPsiLink`) | Old words | New words (`psilink.rh`) |
|---|---|---|
| `iNetMode` 0 | Modem (ATDT host:port) | Modem (ATDT host:port) (`PSILINK_TXT_VIA_MODEM`) |
| `iNetMode` 1 | Psion TCP/IP (dial-up) / Psion Internet (PPP) | Network card or dial-up (Psion TCP/IP) (`PSILINK_TXT_VIA_NET`) |
| `iPppStart` | Psion Internet: first send | Dial-up: first send (`PSILINK_TXT_FIRST_SEND`) |
| `iKeepUp` | After use, Psion Internet | After use, Psion TCP/IP (`PSILINK_TXT_AFTER_USE`) |
| `iUseProxy` | Use proxy: No / Yes | Web helper: None - direct / Atom modem or PsiProxy (`PSILINK_TXT_HELPER*`) |
| `iProxyHost`, `iProxyPort` | Proxy host, Proxy port | Helper's address, Helper's port |
| (no proxy) | - | **Direct** |
| `iUseProxy` 1, host `psiproxy` (or 192.168.7.1) | the proxy | **Atom modem helper** |
| `iUseProxy` 1, any other host | the proxy | **PsiProxy on a computer** |

In C, `pg_su_route_name(use_proxy, host)` gives the last three. The
annotation `PSILINK_TXT_HELPER_NOTE` ("Atom modem helper: psiproxy, port 8080
- PsiProxy on a computer: its address, 8081") goes under the helper lines
where the page has room (PsiTerm's Network page has none). Infoprints: "No
helper's address entered", "Not available - set a web helper in Connection
settings". In a `.rss`: `#include "psilink.rh"` (each `.mmp` already has
`\ptproj\psitermssh` as a SYSTEMINCLUDE), then `txt=PSILINK_TXT_VIA_NET;`.

Download address for PsiProxy: https://github.com/danieledge/psiproxy
(`PG_SU_PSIPROXY_URL` without the scheme).

## The setup assistant

**When.** At a program's first start when neither `C:\System\Data\PsiLink.ini`
nor the program's own settings file is there
(`CPgSetupDialog::FirstRun(fs, KIniFile)`): a new user. Anyone with settings
from any program of the suite is not asked. Always available as
*Tools > Set up <Program>...*. It is the one dialog the user did not ask for
(a deliberate deviation from the EIKON guide: nothing can work before a
connection is chosen); Cancel changes nothing and the program carries on
with the standard settings. The program writes its own settings file at the
end of the first run either way, so it asks only once.

**What it writes.** The existing connection settings only: no new fields,
no new file version, no changed standards.

| Page | Line | Choice | Writes |
|---|---|---|---|
| Connection | This Psion goes online by | Network card or dial-up | `iNetMode` 1 |
| | | Atom modem | `iNetMode` 0, `iBaudIndex` 4 (115200) |
| | | Other WiFi modem | `iNetMode` 0, `iBaudIndex` as chosen |
| | Speed (modems only) | 9600 to 115200 | dimmed unless Other WiFi modem |
| Web helper | Pages are fetched by | Direct | `iUseProxy` 0 (address and port kept) |
| | | Atom modem helper | `iUseProxy` 1, `psiproxy`, 8080 (only with the Atom modem; another way online turns it to Direct) |
| | | PsiProxy on a computer | `iUseProxy` 1, the address and port typed; through the Atom modem: `psiproxy`, 8080 (the modem passes pages on to PsiProxy, AT$PU) |
| | PsiProxy's address, Port | | dimmed unless PsiProxy on a computer without the Atom |

Flow control, Dial-up: first send and After use are left as they are.

**First choice shown.** The program's own current route: `PsiLink.ini`'s
values if it is there, otherwise the standard settings (the modem at 115200
baud, so *Atom modem*, *Direct*) on every machine. Cancel keeps that route,
so what the dialog shows is what the program uses.

**Annotations** (under each choice, one paragraph, changing with it;
`pg_su_conn_note`, `pg_su_helper_note`):

- Network card or dial-up: "The PC Card network card, or a dial-up, as set in the Control panel" (netBook); "The Psion's own dial-up, as set in the Control panel" (5mx)
- Atom modem: "The PsiConnect Atom on the serial cable - the Remote link must be off"
- Other WiFi modem: "A WiFi modem that dials with ATDT host:port"
- Direct: "Best with a network card - nobody sees your pages" (netBook); "Private: nobody sees your pages - big sites are slower" (5mx)
- Atom modem helper: "The modem makes pages lighter - it sees your pages"
- PsiProxy on a computer: "Fastest - get it at github.com/danieledge/psiproxy"; through the Atom: "Set PsiProxy's address in the modem (AT$PU) - nothing to set here"

**On OK.** PsiProxy chosen with no address: infoprint "No address entered",
the cursor to that line. PsiProxy chosen: the query "Pages and passwords
cross your network unencrypted / Use PsiProxy on this network?" (Esc is
No; No keeps the dialog). Then `PsiLink.ini` is saved, then the program's
own pages (`AppSaveL`). Cancel discards everything.

**Test** (Ctrl+T, on every page): Connection settings' own Test
(`PgLinkTestL`) with the way online shown and the flow control and first
command from `PsiLink.ini`, in the program's own Test connection window.

**Size.** The annotation labels are laid out with the longest text of each
(`PG_SU_*_NOTE_LONGEST`), so the dialog has one size whatever is chosen, and
it is centred and never larger than the screen less 8 pixels (the dialog
size rule). Choice lists have fixed widths (`maxdisplaychar`), the address
edwin 18 characters. Measured in the 5mx emulator in PsiWeb 0.85: about
550 by 210 pixels.

## Using it in a program

`.mmp`: add `pgsetup.cpp` beside `pglinktest.cpp` (SUBPROJECT psitermssh).

`.rss`: `#include "psilink.rh"` with the other headers, and the assistant's
resources after `EIK_APP_INFO` (the first resources of the file must stay
the program's own):

```
#define PGSU_TXT_HELPER_PROMPT "Web pictures are fetched by"    // optional
#include "pgsetup.rssi"

RESOURCE DIALOG r_pm_setup_dialog
	{
	title="Set up PsiMail";
	flags=EEikDialogFlagWait;
	buttons=r_pgsu_buttons;
	pages=r_pm_setup_pages;
	}

RESOURCE ARRAY r_pm_setup_pages
	{
	items=
		{
		PAGE { text="Connection"; id=EPgSuPageConn; lines=r_pgsu_conn_lines; },
		PAGE { text="Web helper"; id=EPgSuPageHelper; lines=r_pgsu_helper_lines; }
		};
	}
```

C++, with nothing of the program's own to set:

```
#include "pgsetup.h"
...
// at the end of the app UI's ConstructL (after the view is up):
if (CPgSetupDialog::FirstRun(iCoeEnv->FsSession(), KIniFile))
	{
	PgSetupRunL(R_PM_SETUP_DIALOG, R_PM_TEST_DIALOG, EPmDlgTest1);
	// then reload the shared link into the program's own copy and save its settings file
	}
```

With pages of its own (PsiWeb's *Pages*), derive from `CPgSetupDialog` and
fill `AppPreLayoutL` (set the program's lines), `AppChoiceChangedL` (follow
`Choice()`, for example a standard that depends on the route),
`AppOkToExitL` and `AppSaveL` (save the program's own settings). The
program's own control ids must stay outside 0x7c00 to 0x7cff.
