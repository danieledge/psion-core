// psihelp.h - the Help dialog's text, the same in every bundled program
// (the Coda sheet 2.13): a read-only rich text in a box of whole lines (12
// on a 480-line screen, 7 on 240; never a line cut through), with a scroll
// bar beside it: Up and Down a line (Up at the top goes back to the Topic
// line), Pg Up and Pg Dn a page, Home and End, the pen on the bar. As
// PsiMail's CPmHelpText (pmhelp.cpp), made shareable.
//
// The dialog's line is a custom control whose resource is the editor's own
// RTXTED (its width is the text's; the bar goes beside it):
//   DLG_LINE { type=EPsiCtHelpText; prompt=""; id=...;
//       control=RTXTED { width=520; height=118; numlines=7; textlimit=8000;
//           flags=EEikEdwinNoAutoSelection|EEikEdwinReadOnly; }; }
// and the dialog's CreateCustomControlL gives a new CPsiHelpText for
// EPsiCtHelpText (psihelp.hrh, which the .rss includes too), with SetTop
// (the program links form.lib)
// for Up at the text's top. After each
// SetTextL: TextChangedL(). The code is psihelp.cpp: the program's .mmp
// adds it (SUBPROJECT psitermssh, SOURCE psihelp.cpp).
#ifndef PSIHELP_H
#define PSIHELP_H

#include <coecntrl.h>
#include <eikrted.h>
#include <eikenv.h>
#include <frmtlay.h>
#include <frmtview.h>
#include <eikcolor.h>

#include "psihelp.hrh"                      // EPsiCtHelpText, the custom control's type

const TInt KPsiHelpBarW = 23;               // EIKON's scroll bar width

class CPsiHelpText : public CCoeControl
	{
public:
	~CPsiHelpText() { delete iEditor; }
	CEikRichTextEditor* Editor() const { return iEditor; }
	// Up at the top of the text calls aTop (the dialog: back to its Topic line)
	void SetTop(const TCallBack& aTop) { iTop = aTop; }
	// a new text: at its top, without a cursor (it is for reading)
	void TextChangedL();
private:
	void ConstructFromResourceL(TResourceReader& aReader);
	void SetContainerWindowL(const CCoeControl& aContainer);
	// the text's own line height (its first formatted line), else the font's
	TInt LineHeight() const;
	TSize MinimumSize();
	void SizeChangedL() { Layout(); }
	void PositionChanged() { Layout(); }
	void Layout();
	TInt CountComponentControls() const { return iEditor ? 1 : 0; }
	CCoeControl* ComponentControl(TInt /*aIndex*/) const { return iEditor; }
	// the text's height, shown and above. An edwin formats only the band in
	// view once its text is over 2000 characters, so a long text's height is
	// reckoned from its length (and known once the end has been reached)
	void Model(TInt& aTotal, TInt& aShown, TInt& aAbove) const;
	// the bar's parts: the shaft, the thumb in it, the up and down buttons
	// at its foot (as EIKON's)
	void Parts(TRect& aShaft, TRect& aThumb, TRect& aUp, TRect& aDown) const;
	void Face(CWindowGc& aGc, const TRect& aRect, TBool aDown) const;
	static void Arrow(CWindowGc& aGc, const TRect& aRect, TBool aUp);
	void DrawBar(CWindowGc& aGc) const;
	void Draw(const TRect& /*aRect*/) const;
	void UpdateBar();
	// the end of the text is in view: a scroll down has found nothing more
	TBool AtEnd() const;
	// scrolls the text: the pixels it moved (0 at the top or the end)
	TInt ScrollL(TCursorPosition::TMovementType aMove);
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
	// the pen on the bar: the arrows a line, the shaft a page, the thumb dragged
	void HandlePointerEventL(const TPointerEvent& aEvent);
	CEikRichTextEditor* iEditor;
	TCallBack iTop;
	TRect iBar;
	TInt iAbove;               // pixels scrolled
	TBool iAtEnd;              // the last scroll down moved nothing
	TInt iPress;               // the pen: 1 up, 2 down, 3 the thumb
	TInt iGrab;
	};


#endif
