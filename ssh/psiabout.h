// psiabout.h - the About box every bundled program shows (PsiTerm, PsiMail,
// PsiWeb; the Coda sheet 2.7 and 2.13): one layout, one wording.
// Resources: psiabout.rssi (the .rss includes it after EIK_APP_INFO).
// Include this header in one .cpp of the program only (the dialogs are
// defined here).
//
//   PsiAboutL(_L("PsiTerm"), KPsiTermVersion, _L("SSH terminal"), credits);
//
// shows:
//   title   About PsiTerm
//   line 1  PsiTerm 1.20                                   (bold)
//   line 2  SSH terminal for the Psion Series 5mx, Series 7 & netBook
//   line 3  (c) Dan Edge 2026 (on Coda's netBook: "- part of Coda for the
//           Psion netBook" after it)
//   line 4  Buy me a coffee: buymeacoffee.com/danedge      (small)
//   Credits... opens the credits (other people's code, one per line, '\n'
//   between them); Continue closes.
// Status (the connection, the screen) is not here: it belongs to the
// program's status line or Status information.
#ifndef PSIABOUT_H
#define PSIABOUT_H

#include <eikdialg.h>
#include <eiklabel.h>
#include <eikenv.h>
#include "psiabout.hrh"
#include "psitbar.h"           // (Coda's netBook or not)

const TInt KPsiAboutWidth = 480;   // the Coda sheet 2.8: fixed, under 500

// at most aMax characters of aText (ER5's Left panics, USER 22, past the end)
inline TPtrC PsiAboutLeft(const TDesC& aText, TInt aMax)
	{
	return aText.Left(aText.Length() < aMax ? aText.Length() : aMax);
	}

// a dialog of fixed width, centred, never larger than the screen
class CPsiAboutBase : public CEikDialog
	{
protected:
	void SetSizeAndPositionL(const TSize& aSize)
		{
		TSize screen = iEikonEnv->ScreenDevice()->SizeInPixels();
		TInt w = aSize.iWidth > KPsiAboutWidth ? aSize.iWidth : KPsiAboutWidth;
		if (w > screen.iWidth - 8)
			w = screen.iWidth - 8;
		TInt h = aSize.iHeight < screen.iHeight - 8 ? aSize.iHeight : screen.iHeight - 8;
		SetCornerAndSizeL(EHCenterVCenter, TSize(w, h));
		}
	void SetLabelTextL(TInt aId, const TDesC& aText)
		{
		CEikLabel* l = (CEikLabel*)Control(aId);
		l->SetTextL(aText);
		}
	};

class CPsiCreditsDialog : public CPsiAboutBase
	{
public:
	CPsiCreditsDialog(const TDesC& aCredits) : iCredits(aCredits) { }
private:
	void PreLayoutDynInitL() { SetLabelTextL(EPsiAbDlgCredits, iCredits); }
	TPtrC iCredits;
	};

class CPsiAboutDialog : public CPsiAboutBase
	{
public:
	CPsiAboutDialog(const TDesC& aName, const TDesC& aVersion, const TDesC& aWhat, const TDesC& aCredits)
		: iName(aName), iVersion(aVersion), iWhat(aWhat), iCredits(aCredits) { }
private:
	void PreLayoutDynInitL()
		{
		TBuf<64> title(_L("About "));
		title.Append(PsiAboutLeft(iName, 40));
		SetTitleL(title);
		TBuf<64> name(PsiAboutLeft(iName, 40));
		name.Append(' ');
		name.Append(PsiAboutLeft(iVersion, 8));
		CEikLabel* l = (CEikLabel*)Control(EPsiAbDlgName);
		l->SetFont(iEikonEnv->TitleFont());   // (bold, the heading)
		l->SetTextL(name);
		TBuf<100> what(PsiAboutLeft(iWhat, 40));
		what.Append(_L(" for the Psion Series 5mx, Series 7 & netBook"));
		SetLabelTextL(EPsiAbDlgWhat, what);
		TBuf<80> copy;
		copy.Append(TChar(0xa9));
		copy.Append(_L(" Dan Edge 2026"));
		if (PsiTbarKind() == EPsiTbarCoda)
			copy.Append(_L(" - part of Coda for the Psion netBook"));
		SetLabelTextL(EPsiAbDlgCopy, copy);
		}
	TBool OkToExitL(TInt aButtonId)
		{
		if (aButtonId == EPsiAbBidCredits)
			{
			CPsiCreditsDialog* dlg = new(ELeave) CPsiCreditsDialog(iCredits);
			dlg->ExecuteLD(R_PSI_CREDITS_DIALOG);
			return EFalse;
			}
		return ETrue;
		}
	TPtrC iName, iVersion, iWhat, iCredits;
	};

// About <aName>: aWhat is what the program is ("SSH terminal"), aCredits
// the other people's code, a line each
inline void PsiAboutL(const TDesC& aName, const TDesC& aVersion, const TDesC& aWhat, const TDesC& aCredits)
	{
	CPsiAboutDialog* dlg = new(ELeave) CPsiAboutDialog(aName, aVersion, aWhat, aCredits);
	dlg->ExecuteLD(R_PSI_ABOUT_DIALOG);
	}

#endif
