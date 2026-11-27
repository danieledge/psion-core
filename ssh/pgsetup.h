/* pgsetup.h - the setup assistant: a one-time dialog for a new user that
 * chooses how the Psion goes online and which web route the program uses,
 * shared by PsiWeb and PsiMail (any program with Connection settings can
 * carry it). docs/setup-assistant.md is the full description.
 *
 * It writes the existing connection settings only (TPsiLink, PsiLink.ini):
 * no new fields, no new file version, no changed standards. What it writes,
 * by choice:
 *
 *   This Psion goes online by
 *     Network card or dial-up  iNetMode 1 (the Psion's own TCP/IP: the
 *                              netBook's PC Card network card, or dial-up)
 *     Atom modem               iNetMode 0, iBaudIndex 4 (115200)
 *     Other WiFi modem         iNetMode 0, iBaudIndex as chosen
 *   Pages are fetched by
 *     Direct                   iUseProxy 0 (the address and port are kept)
 *     Atom modem helper        iUseProxy 1, "psiproxy", 8080 (only with
 *                              the Atom modem)
 *     PsiProxy on a computer   iUseProxy 1, the address and port typed in;
 *                              through the Atom modem: "psiproxy", 8080
 *                              (the modem passes pages on to PsiProxy,
 *                              AT$PU, so there is nothing to set here)
 *   Everything else (flow control, the dial-up's first command, "After
 *   use") is left as it is.
 *
 * The pure part below (plain C, no EPOC calls) is what the host test checks
 * (ssh/test/setuptest.cpp). The EIKON dialog, CPgSetupDialog, is in
 * pgsetup.cpp: each program's .mmp adds it beside pglinktest.cpp, and its
 * .rss includes pgsetup.rssi.
 */
#ifndef PGSETUP_H
#define PGSETUP_H

#include "pgsetup.hrh"

enum { PG_SU_CONN_NET = 0, PG_SU_CONN_ATOM, PG_SU_CONN_MODEM };
enum { PG_SU_HELPER_DIRECT = 0, PG_SU_HELPER_ATOM, PG_SU_HELPER_PSIPROXY };

#define PG_SU_HOST_MAX 60
#define PG_SU_ATOM_HOST "psiproxy"
#define PG_SU_ATOM_PORT 8080
#define PG_SU_PSIPROXY_PORT 8081
#define PG_SU_PSIPROXY_URL "github.com/danieledge/psiproxy"

/* The settings the assistant reads and writes, as plain C */
typedef struct
	{
	int net_mode;                  /* TPsiLink::iNetMode */
	int baud_index;                /* iBaudIndex, 0..4 */
	int use_proxy;                 /* iUseProxy */
	char host[PG_SU_HOST_MAX + 1]; /* iProxyHost */
	int port;                      /* iProxyPort */
	} PgSuLink;

/* What the dialog shows */
typedef struct
	{
	int conn;                      /* PG_SU_CONN_* */
	int helper;                    /* PG_SU_HELPER_* */
	int baud_index;
	char host[PG_SU_HOST_MAX + 1]; /* PsiProxy's address (empty if none) */
	int port;
	} PgSuChoice;

static inline int pg_su_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; }

static inline int pg_su_same(const char* a, const char* b)
	{
	while (*a && *b && pg_su_lower((unsigned char)*a) == pg_su_lower((unsigned char)*b)) { a++; b++; }
	return !*a && !*b;
	}

/* The Atom modem's own helper, as TPsiLink::IsAtomProxy (an empty host too) */
static inline int pg_su_is_atom_host(const char* aHost)
	{
	return !aHost[0] || pg_su_same(aHost, "psiproxy") || pg_su_same(aHost, "192.168.7.1");
	}

static inline void pg_su_copy(char* aDst, const char* aSrc)
	{
	int i = 0;
	for (; aSrc[i] && i < PG_SU_HOST_MAX; i++)
		aDst[i] = aSrc[i];
	aDst[i] = 0;
	}

/* What to show first: the route aLink gives, whether it is PsiLink.ini
 * (aHaveFile) or the standard settings (no file yet): the program's own
 * current route, on every machine. */
static inline void pg_su_from_link(const PgSuLink* aLink, int aHaveFile, int aTall, PgSuChoice* aOut)
	{
	aOut->baud_index = aLink->baud_index >= 0 && aLink->baud_index <= 4 ? aLink->baud_index : 4;
	aOut->host[0] = 0;
	aOut->port = PG_SU_PSIPROXY_PORT;
	/* no file yet: aLink holds the program's standard settings (TPsiLink::
	   SetDefaults), and the dialog opens on that route, the one the program
	   uses until it is changed (UI-156; it was the network card on a tall
	   screen whatever the program used). aHaveFile and aTall are kept for
	   the callers */
	(void)aHaveFile; (void)aTall;
	int atomHelper = aLink->use_proxy && pg_su_is_atom_host(aLink->host);
	if (aLink->net_mode)
		aOut->conn = PG_SU_CONN_NET;
	else if (atomHelper || aOut->baud_index == 4)
		aOut->conn = PG_SU_CONN_ATOM;
	else
		aOut->conn = PG_SU_CONN_MODEM;
	if (!aLink->use_proxy)
		aOut->helper = PG_SU_HELPER_DIRECT;
	else if (atomHelper)
		aOut->helper = aOut->conn == PG_SU_CONN_ATOM ? PG_SU_HELPER_ATOM : PG_SU_HELPER_PSIPROXY;
	else
		aOut->helper = PG_SU_HELPER_PSIPROXY;
	if (!pg_su_is_atom_host(aLink->host))
		{
		pg_su_copy(aOut->host, aLink->host);   /* PsiProxy's, even while not in use */
		if (aLink->port > 0 && aLink->port < 65536)
			aOut->port = aLink->port;
		}
	}

/* The Atom modem helper is only for the Atom modem: on another way online
 * it becomes Direct (called when the first page's choice changes) */
static inline void pg_su_fix(PgSuChoice* aC)
	{
	if (aC->helper == PG_SU_HELPER_ATOM && aC->conn != PG_SU_CONN_ATOM)
		aC->helper = PG_SU_HELPER_DIRECT;
	}

/* Whether the lines are dimmed */
static inline int pg_su_baud_dimmed(const PgSuChoice* aC) { return aC->conn != PG_SU_CONN_MODEM; }
static inline int pg_su_address_dimmed(const PgSuChoice* aC)
	{
	return aC->helper != PG_SU_HELPER_PSIPROXY || aC->conn == PG_SU_CONN_ATOM;
	}

/* OK: 0 fine, 1 no address for PsiProxy (the caller says "No address
 * entered" and goes to that line), 2 the Atom modem helper without the Atom
 * modem (cannot happen after pg_su_fix; the caller picks Direct) */
static inline int pg_su_check(const PgSuChoice* aC)
	{
	if (aC->helper == PG_SU_HELPER_ATOM && aC->conn != PG_SU_CONN_ATOM)
		return 2;
	if (aC->helper == PG_SU_HELPER_PSIPROXY && aC->conn != PG_SU_CONN_ATOM && !aC->host[0])
		return 1;
	return 0;
	}

/* PsiProxy on a computer is chosen: pages cross the network as plain HTTP */
static inline int pg_su_needs_privacy_query(const PgSuChoice* aC) { return aC->helper == PG_SU_HELPER_PSIPROXY; }

/* Writes the choice into aLink: only the fields listed at the top */
static inline void pg_su_to_link(const PgSuChoice* aC, PgSuLink* aLink)
	{
	switch (aC->conn)
		{
	case PG_SU_CONN_NET:
		aLink->net_mode = 1;
		break;
	case PG_SU_CONN_ATOM:
		aLink->net_mode = 0;
		aLink->baud_index = 4;
		break;
	default:
		aLink->net_mode = 0;
		aLink->baud_index = aC->baud_index >= 0 && aC->baud_index <= 4 ? aC->baud_index : 4;
		break;
		}
	if (aC->helper == PG_SU_HELPER_DIRECT)
		aLink->use_proxy = 0;
	else if (aC->helper == PG_SU_HELPER_ATOM || aC->conn == PG_SU_CONN_ATOM)
		{
		aLink->use_proxy = 1;
		pg_su_copy(aLink->host, PG_SU_ATOM_HOST);
		aLink->port = PG_SU_ATOM_PORT;
		}
	else
		{
		aLink->use_proxy = 1;
		pg_su_copy(aLink->host, aC->host);
		aLink->port = aC->port > 0 && aC->port < 65536 ? aC->port : PG_SU_PSIPROXY_PORT;
		}
	}

/* (22 November 2026) Ask about plain HTTP across the network on OK? Only
 * when PsiProxy on a computer is NEW to the settings: not the helper they
 * already had (aWas, PsiLink.ini as it is; aHadFile: the file was there),
 * at the same address and port. So OK with nothing changed, or after
 * another change, never asks again (it asked at every OK). As PsiWeb 1.03 */
static inline int pg_su_privacy_query_due(const PgSuChoice* aC, const PgSuLink* aWas, int aHadFile)
	{
	if (!pg_su_needs_privacy_query(aC))
		return 0;
	PgSuLink now = *aWas;
	pg_su_to_link(aC, &now);
	return !(aHadFile && aWas->use_proxy && pg_su_same(aWas->host, now.host) && aWas->port == now.port);
	}

/* The question's words, in its own dialog (not its title bar): the title,
 * the fact (bold), why (an annotation, PG_SU_QUERY_WHY_MAX at most), then
 * the question, before No and Yes */
#define PG_SU_QUERY_TITLE    "PsiProxy on a computer"
#define PG_SU_QUERY_FACT     "Pages and passwords cross your network unencrypted"
#define PG_SU_QUERY_QUESTION "Use PsiProxy on this network?"
#define PG_SU_QUERY_HOST_MAX 24            /* the address shown, at most */
#define PG_SU_QUERY_WHY_MAX  110
/* the label's size: as wide as the widest text it takes on a 640-pixel
   screen (fifteen W's stand for an address of 24 ordinary letters, as
   measured in PsiWeb 1.03) */
#define PG_SU_QUERY_WHY_LONGEST "Between this Psion and PsiProxy at WWWWWWWWWWWWWWW. Do not use on public WiFi"

/* why, for the choice: through the Atom modem the plain part is the modem's
 * hop to PsiProxy; otherwise the Psion's own, to the address (cut to
 * PG_SU_QUERY_HOST_MAX). aOut holds PG_SU_QUERY_WHY_MAX + 1 */
static inline void pg_su_query_why(const PgSuChoice* aC, char* aOut)
	{
	const char* a;
	int n = 0;
	if (aC->conn == PG_SU_CONN_ATOM)
		a = "Between the Atom modem and PsiProxy";
	else
		{
		a = "Between this Psion and PsiProxy at ";
		while (*a && n < PG_SU_QUERY_WHY_MAX)
			aOut[n++] = *a++;
		for (int i = 0; aC->host[i] && i < PG_SU_QUERY_HOST_MAX && n < PG_SU_QUERY_WHY_MAX; i++)
			aOut[n++] = aC->host[i];
		a = "";
		}
	while (*a && n < PG_SU_QUERY_WHY_MAX)
		aOut[n++] = *a++;
	a = ". Do not use on public WiFi";
	while (*a && n < PG_SU_QUERY_WHY_MAX)
		aOut[n++] = *a++;
	aOut[n] = 0;
	}

/* The annotations under the two choices (one paragraph each). aTall: the
 * netBook's wording where it differs. */
static inline const char* pg_su_conn_note(const PgSuChoice* aC, int aTall)
	{
	switch (aC->conn)
		{
	case PG_SU_CONN_NET:
		return aTall ? "The PC Card network card, or a dial-up, as set in the Control panel"
		             : "The Psion's own dial-up, as set in the Control panel";
	case PG_SU_CONN_ATOM:
		return "PsiConnect Atom on the serial cable. Turn the Remote link off";
	default:
		return "A WiFi modem that dials with ATDT host:port";
		}
	}

static inline const char* pg_su_helper_note(const PgSuChoice* aC, int aTall)
	{
	switch (aC->helper)
		{
	case PG_SU_HELPER_DIRECT:
		return aTall ? "Best with a network card. Pages stay private"
		             : "Pages stay private. Big sites are slower";
	case PG_SU_HELPER_ATOM:
		return "The modem makes pages lighter and can see them";
	default:
		return aC->conn == PG_SU_CONN_ATOM
			? "Set in the modem - nothing to enter"   /* (UI-157: no AT commands) */
			: "Fastest - get it at " PG_SU_PSIPROXY_URL;
		}
	}

/* The longest annotation of each, for the dialog's fixed size */
#define PG_SU_CONN_NOTE_LONGEST   "The PC Card network card, or a dial-up, as set in the Control panel"
#define PG_SU_HELPER_NOTE_LONGEST "Fastest - get it at " PG_SU_PSIPROXY_URL

/* The route in plain words, for Page information, About and the start page:
 * "Direct", "Atom modem helper", "PsiProxy on a computer" */
static inline const char* pg_su_route_name(int aUseProxy, const char* aHost)
	{
	if (!aUseProxy || !aHost[0])
		return "Direct";
	return pg_su_is_atom_host(aHost) ? "Atom modem helper" : "PsiProxy on a computer";
	}

/* ---- the EIKON dialog (pgsetup.cpp) ---- */
#if defined(__cplusplus) && !defined(PG_SU_HOST)
#include <eikdialg.h>
#include <f32file.h>

// The assistant. A program with nothing of its own to set uses it as it
// is (PgSetupRunL); one with pages of its own derives from it and fills
// them in the App... hooks. Saved only on OK: the shared link first, then
// the program's own (AppSaveL); Cancel changes nothing.
class CPgSetupDialog : public CEikDialog
	{
public:
	// aTestDialog, aTestFirstLine: the program's Test connection window
	// (the same resource Connection settings > Test uses); aQueryDialog:
	// R_PGSU_QUERY_DIALOG from the program's .rsg, the PsiProxy question
	// (0: in a query window's title bar, as before)
	CPgSetupDialog(TInt aTestDialog, TInt aTestFirstLine, TInt aQueryDialog = 0);
	// ETrue for a new user: neither PsiLink.ini nor the program's own
	// settings file (aAppIni) is there. Anyone with settings from any of
	// the programs is not asked.
	static TBool FirstRun(RFs& aFs, const TDesC& aAppIni);
	// ETrue on a screen 400 or more high (the netBook, the Series 7)
	static TBool Tall();
	const PgSuChoice& Choice() const { return iChoice; }
protected:
	// for a program's own pages and lines
	virtual void AppPreLayoutL() {}
	virtual void AppControlChangedL(TInt /*aControlId*/) {}
	virtual void AppChoiceChangedL() {}          // the connection or helper changed
	virtual TBool AppOkToExitL() { return ETrue; }   // its own checks; EFalse keeps the dialog
	virtual void AppSaveL() {}                   // after the shared link is saved
	TBool iTall;
	TBool iHadFile;
private:
	void PreLayoutDynInitL();
	void PostLayoutDynInitL();
	void HandleControlStateChangeL(TInt aControlId);
	TBool OkToExitL(TInt aButtonId);
	void SetSizeAndPositionL(const TSize& aSize);
	void ReadChoice();
	void ShowChoiceL();
	void TestL();
	TBool AskPrivacyL();
	TInt iTestDialog;
	TInt iTestFirstLine;
	TInt iQueryDialog;
	PgSuChoice iChoice;
	};

// (22 November 2026) A question with its explanation in the dialog, not in
// its title bar: the fact (bold), why (an annotation), then the question;
// No and Yes (r_pgsu_query_dialog). ETrue for Yes
class CPgSuQueryDialog : public CEikDialog
	{
public:
	// aDialog: R_PGSU_QUERY_DIALOG (pgsetup.rssi) from the program's .rsg
	static TBool AskL(TInt aDialog, const TDesC& aTitle, const TDesC& aFact, const TDesC& aWhy, const TDesC& aQuestion);
private:
	CPgSuQueryDialog(const TDesC& aFact, const TDesC& aWhy, const TDesC& aQuestion)
		: iFact(aFact), iWhy(aWhy), iQuestion(aQuestion) {}
	void PreLayoutDynInitL();
	const TDesC& iFact;
	const TDesC& iWhy;
	const TDesC& iQuestion;
	};

// Runs the assistant from aDialog (a DIALOG built from pgsetup.rssi's
// pages). ETrue if it was confirmed (the settings are saved).
TBool PgSetupRunL(TInt aDialog, TInt aTestDialog, TInt aTestFirstLine, TInt aQueryDialog = 0);
#endif

#endif
