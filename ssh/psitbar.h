// psitbar.h - the bundled programs' toolbars, chosen by the machine they run
// on (PsiTerm, PsiMail, PsiWeb; the Coda sheet 2.2 and 2.13). The resources
// are in psitbar.rh, which says how a program declares them.
//
// One .sis runs on the Series 5mx (240 lines), the Series 7 and the netBook
// (480 lines). Each toolbar is declared three times in a row; PsiTbarNewL
// makes the one for this machine:
//   EPsiTbarShort  240 lines: buttons of 37, as Psion's 5mx programs
//   EPsiTbarTall   480 lines, no Coda: buttons of 50
//   EPsiTbarCoda   the netBook with Coda: buttons of 50 and Coda's battery,
//                  extras block and clock, as the built-in programs
// Coda is told by Z:\System\Libs\PsiBattHook.bin: the image build adds it
// only together with the hook in Eikon's control factory that makes those
// three controls, so on any other ROM the Coda toolbar is never asked for.
//
// For the main toolbar, give EIK_APP_INFO no toolbar (toolbar=0) and in the
// AppUi's ConstructL, after BaseConstructL:
//     iToolBar = PsiTbarNewL(this, R_XX_TOOLBAR, EXxCmdName);
// (CEikAppUi owns and deletes iToolBar, and ClientRect() then leaves its
// room, exactly as for a toolbar from EIK_APP_INFO.)
#ifndef PSITBAR_H
#define PSITBAR_H

#include <eikenv.h>
#include <eiktbar.h>
#include <eikfnlab.h>

enum TPsiTbarKind { EPsiTbarShort = 0, EPsiTbarTall = 1, EPsiTbarCoda = 2 };

_LIT(KPsiTbarCodaHook, "Z:\\System\\Libs\\PsiBattHook.bin");

// which of the three toolbars this machine takes
inline TInt PsiTbarKind()
	{
	CEikonEnv* env = CEikonEnv::Static();
	if (env->ScreenDevice()->SizeInPixels().iHeight < 400)
		return EPsiTbarShort;
	TEntry e;
	if (env->FsSession().Entry(KPsiTbarCodaHook, e) == KErrNone)
		return EPsiTbarCoda;
	return EPsiTbarTall;
	}

// the button length of this machine's toolbars (37 or 50), for a program
// that lays out anything beside them
inline TInt PsiTbarButtonLength()
	{
	return PsiTbarKind() == EPsiTbarShort ? 37 : 50;
	}

// the name box shows the program's name (its main document's name, else
// its caption: CEikFileNameLabel::UpdateL); call again after a change of
// SetMainDocFileName
inline void PsiTbarNameL(CEikToolBar* aBar, TInt aNameId)
	{
	CEikFileNameLabel* label = aBar ? (CEikFileNameLabel*)aBar->ControlById(aNameId) : NULL;
	if (label)
		label->UpdateL();
	}

// a toolbar for this machine: aFirstRes is the first of its three resources
// (psitbar.rh), aNameId its name box's id. Laid out against the whole
// screen and activated, as EIKON does a toolbar from EIK_APP_INFO.
inline CEikToolBar* PsiTbarNewL(MEikCommandObserver* aObserver, TInt aFirstRes, TInt aNameId)
	{
	CEikToolBar* bar = new(ELeave) CEikToolBar;
	CleanupStack::PushL(bar);
	bar->ConstructL(aObserver, aFirstRes + PsiTbarKind(),
		TRect(TPoint(0, 0), CEikonEnv::Static()->ScreenDevice()->SizeInPixels()));
	PsiTbarNameL(bar, aNameId);
	CleanupStack::Pop();
	return bar;
	}

#endif
