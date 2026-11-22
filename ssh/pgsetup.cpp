// pgsetup.cpp - the setup assistant's dialog (pgsetup.h), compiled into each
// program that carries it (its .mmp: SUBPROJECT psitermssh, SOURCE
// pgsetup.cpp, beside pglinktest.cpp). The choices and what they write are
// pgsetup.h's plain C; this is only the EIKON side.
#include <e32base.h>
#include <eikenv.h>
#include <eikdialg.h>
#include <eikdialg.hrh>
#include <eikcmds.hrh>
#include <eikchlst.h>
#include <eiklabel.h>
#include <eikedwin.h>
#include <eikmfne.h>
#include <eikappui.h>
#include <eikapp.h>
#include "psilink.h"
#include "pglinktest.h"
#include "pgsetup.h"

static void PgSuToLink(const TPsiLink& aIn, PgSuLink& aOut)
	{
	aOut.net_mode = aIn.iNetMode;
	aOut.baud_index = aIn.iBaudIndex;
	aOut.use_proxy = aIn.iUseProxy;
	TInt n = aIn.iProxyHost.Length() < PG_SU_HOST_MAX ? aIn.iProxyHost.Length() : PG_SU_HOST_MAX;
	for (TInt i = 0; i < n; i++)
		aOut.host[i] = (char)aIn.iProxyHost[i];
	aOut.host[n] = 0;
	aOut.port = aIn.iProxyPort;
	}

static void PgSuFromLink(const PgSuLink& aIn, TPsiLink& aOut)
	{
	aOut.iNetMode = aIn.net_mode;
	aOut.iBaudIndex = aIn.baud_index;
	aOut.iUseProxy = aIn.use_proxy;
	aOut.iProxyHost.Copy(TPtrC8((const TUint8*)aIn.host));
	aOut.iProxyPort = aIn.port;
	}

static void PgSuText(TDes& aOut, const char* aText)
	{
	TPtrC8 p((const TUint8*)aText);
	aOut.Copy(p.Left(p.Length() < aOut.MaxLength() ? p.Length() : aOut.MaxLength()));
	}

CPgSetupDialog::CPgSetupDialog(TInt aTestDialog, TInt aTestFirstLine, TInt aQueryDialog)
	: iTestDialog(aTestDialog), iTestFirstLine(aTestFirstLine), iQueryDialog(aQueryDialog)
	{
	iTall = Tall();
	iHadFile = EFalse;
	Mem::FillZ(&iChoice, sizeof(iChoice));
	}

TBool CPgSetupDialog::Tall()
	{
	return CEikonEnv::Static()->ScreenDevice()->SizeInPixels().iHeight >= 400;
	}

TBool CPgSetupDialog::FirstRun(RFs& aFs, const TDesC& aAppIni)
	{
	TEntry e;
	return aFs.Entry(KPsiLinkFile, e) != KErrNone && aFs.Entry(aAppIni, e) != KErrNone;
	}

void CPgSetupDialog::PreLayoutDynInitL()
	{
	TPsiLink link;
	link.SetDefaults();
	iHadFile = link.Load(iCoeEnv->FsSession());
	PgSuLink l;
	PgSuToLink(link, l);
	pg_su_from_link(&l, iHadFile, iTall, &iChoice);
	// the annotations' longest text first, so the dialog is laid out at
	// one size whatever is chosen (the dialog-size rule); the real ones in
	// PostLayoutDynInitL
	TBuf<80> t;
	PgSuText(t, PG_SU_CONN_NOTE_LONGEST);
	SetLabelL(EPgSuDlgConnNote, t);
	PgSuText(t, PG_SU_HELPER_NOTE_LONGEST);
	SetLabelL(EPgSuDlgHelperNote, t);
	SetChoiceListCurrentItem(EPgSuDlgConn, iChoice.conn);
	SetChoiceListCurrentItem(EPgSuDlgBaud, iChoice.baud_index);
	SetChoiceListCurrentItem(EPgSuDlgHelper, iChoice.helper);
	TBuf<PG_SU_HOST_MAX> h;
	PgSuText(h, iChoice.host);
	SetEdwinTextL(EPgSuDlgHost, &h);
	SetNumberEditorValue(EPgSuDlgPort, iChoice.port);
	AppPreLayoutL();
	}

void CPgSetupDialog::PostLayoutDynInitL()
	{
	ShowChoiceL();
	}

// Centred, never wider or taller than the screen less a margin
void CPgSetupDialog::SetSizeAndPositionL(const TSize& aSize)
	{
	TSize screen = iEikonEnv->ScreenDevice()->SizeInPixels();
	TSize size(aSize.iWidth < screen.iWidth - 8 ? aSize.iWidth : screen.iWidth - 8,
		aSize.iHeight < screen.iHeight - 8 ? aSize.iHeight : screen.iHeight - 8);
	SetCornerAndSizeL(EHCenterVCenter, size);
	}

void CPgSetupDialog::ReadChoice()
	{
	iChoice.conn = ChoiceListCurrentItem(EPgSuDlgConn);
	iChoice.baud_index = ChoiceListCurrentItem(EPgSuDlgBaud);
	iChoice.helper = ChoiceListCurrentItem(EPgSuDlgHelper);
	TBuf<PG_SU_HOST_MAX> h;
	GetEdwinText(h, EPgSuDlgHost);
	h.Trim();
	TInt n = h.Length();
	for (TInt i = 0; i < n; i++)
		iChoice.host[i] = (char)h[i];
	iChoice.host[n] = 0;
	iChoice.port = NumberEditorValue(EPgSuDlgPort);
	}

// The annotations and dimming follow the choices: dimmed, never hidden
void CPgSetupDialog::ShowChoiceL()
	{
	TBuf<80> t;
	PgSuText(t, pg_su_conn_note(&iChoice, iTall));
	SetLabelL(EPgSuDlgConnNote, t);
	PgSuText(t, pg_su_helper_note(&iChoice, iTall));
	SetLabelL(EPgSuDlgHelperNote, t);
	if (iChoice.conn == PG_SU_CONN_ATOM)
		SetChoiceListCurrentItem(EPgSuDlgBaud, 4);     // the Atom: 115200
	SetLineDimmedNow(EPgSuDlgBaud, pg_su_baud_dimmed(&iChoice));
	TBool dim = pg_su_address_dimmed(&iChoice);
	SetLineDimmedNow(EPgSuDlgHost, dim);
	SetLineDimmedNow(EPgSuDlgPort, dim);
	}

void CPgSetupDialog::HandleControlStateChangeL(TInt aControlId)
	{
	if (aControlId == EPgSuDlgConn || aControlId == EPgSuDlgHelper || aControlId == EPgSuDlgBaud)
		{
		ReadChoice();
		if (aControlId == EPgSuDlgConn)
			{
			TInt was = iChoice.helper;
			pg_su_fix(&iChoice);
			if (was != iChoice.helper)
				SetChoiceListCurrentItem(EPgSuDlgHelper, iChoice.helper);
			}
		ShowChoiceL();
		AppChoiceChangedL();
		}
	else
		AppControlChangedL(aControlId);
	}

// Test: the way online as shown (not yet saved), with the flow control and
// the dial-up's first command as they are in Connection settings
void CPgSetupDialog::TestL()
	{
	ReadChoice();
	TPsiLink link;
	link.SetDefaults();
	link.Load(iCoeEnv->FsSession());
	PgSuLink l;
	PgSuToLink(link, l);
	pg_su_to_link(&iChoice, &l);
	PgLinkTestL(l.baud_index, link.iRtsCts, l.net_mode, link.iPppStart, iTestDialog, iTestFirstLine);
	}

TBool CPgSetupDialog::OkToExitL(TInt aButtonId)
	{
	if (aButtonId == EPgSuBidTest)
		{
		TestL();
		return EFalse;
		}
	if (aButtonId != EEikBidOk)
		return ETrue;
	ReadChoice();
	pg_su_fix(&iChoice);
	if (pg_su_check(&iChoice) == 1)
		{
		iEikonEnv->InfoMsg(_L("No address entered"));
		TryChangeFocusToL(EPgSuDlgHost);
		return EFalse;
		}
	if (!AppOkToExitL())
		return EFalse;
	RFs& fs = iCoeEnv->FsSession();
	TPsiLink link;
	link.SetDefaults();
	TBool had = link.Load(fs);
	PgSuLink l;
	PgSuToLink(link, l);
	// plain HTTP from the Psion to the computer: said once, before it is
	// used - only for a PsiProxy new to the settings (22 November 2026: it
	// was asked at every OK, with nothing changed)
	if (pg_su_privacy_query_due(&iChoice, &l, had) && !AskPrivacyL())
		return EFalse;
	pg_su_to_link(&iChoice, &l);
	PgSuFromLink(l, link);
	link.Save(fs);
	AppSaveL();
	return ETrue;
	}

// The question, in the program's query dialog (pgsetup.rssi's
// r_pgsu_query_dialog) when it gave one; else as before, in a query window
TBool CPgSetupDialog::AskPrivacyL()
	{
	TBuf<PG_SU_QUERY_WHY_MAX> why;
	char w[PG_SU_QUERY_WHY_MAX + 1];
	pg_su_query_why(&iChoice, w);
	PgSuText(why, w);
	TBuf<64> fact, question, title;
	PgSuText(fact, PG_SU_QUERY_FACT);
	PgSuText(question, PG_SU_QUERY_QUESTION);
	PgSuText(title, PG_SU_QUERY_TITLE);
	if (iQueryDialog)
		return CPgSuQueryDialog::AskL(iQueryDialog, title, fact, why, question);
	return iEikonEnv->QueryWinL(fact, question);
	}

TBool CPgSuQueryDialog::AskL(TInt aDialog, const TDesC& aTitle, const TDesC& aFact, const TDesC& aWhy, const TDesC& aQuestion)
	{
	CPgSuQueryDialog* d = new(ELeave) CPgSuQueryDialog(aFact, aWhy, aQuestion);
	d->PrepareLC(aDialog);
	d->SetTitleL(aTitle);
	return d->RunLD() == EEikBidYes;
	}

void CPgSuQueryDialog::PreLayoutDynInitL()
	{
	SetLabelL(EPgSuDlgQueryFact, iFact);
	SetLabelL(EPgSuDlgQueryWhy, iWhy);
	SetLabelL(EPgSuDlgQueryQuestion, iQuestion);
	}

TBool PgSetupRunL(TInt aDialog, TInt aTestDialog, TInt aTestFirstLine, TInt aQueryDialog)
	{
	CPgSetupDialog* d = new(ELeave) CPgSetupDialog(aTestDialog, aTestFirstLine, aQueryDialog);
	return d->ExecuteLD(aDialog);
	}
