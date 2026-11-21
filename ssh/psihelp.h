// psihelp.h - the Help dialog's text, the same in every bundled program
// (the Coda sheet 2.13): a read-only rich text in a box of whole lines (12
// on a 480-line screen, 7 on 240, in the text's own line height), which
// scrolls a whole line at a time, so the top line is never cut: Up and Down
// a line (Up at the top goes back to the Topic line), Pg Up and Pg Dn a box
// less a line, Home and End, the pen on the bar. The scroll bar is drawn
// beside the text only when the topic is longer than the box; a topic that
// fits has none, and its text takes the bar's room. As Coda's own Help
// (its psisys/psihelp.h, CPsiHelpBox) since 21 November 2026; until then
// this box scrolled by pixels and always showed its bar.
//
// The dialog's line is a custom control whose resource is the editor's own
// RTXTED (its width is the text's and the bar's together):
//   DLG_LINE { type=EPsiCtHelpText; prompt=""; id=...;
//       control=RTXTED { width=520; height=118; numlines=7; textlimit=8000;
//           flags=EEikEdwinNoAutoSelection|EEikEdwinReadOnly; }; }
// and the dialog's CreateCustomControlL gives a new CPsiHelpText for
// EPsiCtHelpText (psihelp.hrh, which the .rss includes too), with SetTop
// for Up at the text's top. After each SetTextL, HandleTextChangedL and
// SetCursorPosL(0): TextChangedL(). The code is psihelp.cpp: the program's
// .mmp adds it (SUBPROJECT psitermssh, SOURCE psihelp.cpp) and links
// form.lib.
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
const TInt KPsiHelpButH = 20;               // its arrow buttons

class CPsiHelpText : public CCoeControl
	{
public:
	~CPsiHelpText() { delete iEditor; }
	CEikRichTextEditor* Editor() const { return iEditor; }
	// Up at the top of the text calls aTop (the dialog: back to its Topic line)
	void SetTop(const TCallBack& aTop) { iTop = aTop; }
	// a new text: at its top, without a cursor (it is for reading), the bar
	// shown only if it is longer than the box
	void TextChangedL();
private:
	void ConstructFromResourceL(TResourceReader& aReader);
	void SetContainerWindowL(const CCoeControl& aContainer);
	// the text's own line height (its first formatted line), else the font's
	TInt LineHeight() const;
	TSize MinimumSize();
	void SizeChangedL() { LayoutL(); }
	void PositionChanged() { TRAPD(err, LayoutL()); (void)err; }
	// the text's box, whole lines high, less the bar when it is needed
	void LayoutL();
	// the whole text formatted (an edwin formats only the band in view once
	// its text is long), so its lines can be counted and any one put at the top
	void FormatL();
	TInt Count() const;                     // the text's lines
	TInt Shown() const;                     // the lines the box shows
	TInt CountComponentControls() const { return iEditor ? 1 : 0; }
	CCoeControl* ComponentControl(TInt /*aIndex*/) const { return iEditor; }
	// the bar's parts: the shaft, the thumb in it, the up and down buttons
	// at its foot (as EIKON's)
	void Parts(TRect& aShaft, TRect& aThumb, TRect& aUp, TRect& aDown) const;
	void Face(CWindowGc& aGc, const TRect& aRect, TBool aDown) const;
	static void Arrow(CWindowGc& aGc, const TRect& aRect, TBool aUp);
	void DrawBar(CWindowGc& aGc) const;
	void Draw(const TRect& /*aRect*/) const;
	void UpdateBar();
	// a whole line at the top (aTop from 0), never one cut through
	void ScrollToL(TInt aTop);
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
	// the pen on the bar: the arrows a line, the shaft a box, the thumb dragged
	void HandlePointerEventL(const TPointerEvent& aEvent);
	CEikRichTextEditor* iEditor;
	TCallBack iTop;
	TRect iBar;                // empty when the text fits
	TInt iLineH;
	TInt iTopLine;             // the line at the top, from 0
	TInt iPress;               // the pen: 1 up, 2 down, 3 the thumb
	TInt iGrab;
	};


#endif
