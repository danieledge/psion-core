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
// SetTextL: TextChangedL(). Include this header in one .cpp of the program
// only.
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
	void TextChangedL()
		{
		iAbove = 0;
		iAtEnd = EFalse;
		iEditor->TextView()->SetCursorVisibilityL(TCursor::EFCursorInvisible, TCursor::EFCursorInvisible);
		UpdateBar();
		}
private:
	void ConstructFromResourceL(TResourceReader& aReader)
		{
		iEditor = new(ELeave) CEikRichTextEditor(TEikBorder(TEikBorder::ENone));
		if (DrawableWindow())
			iEditor->SetContainerWindowL(*this);
		iEditor->ConstructFromResourceL(aReader);
		}
	void SetContainerWindowL(const CCoeControl& aContainer)
		{
		CCoeControl::SetContainerWindowL(aContainer);
		if (iEditor)
			iEditor->SetContainerWindowL(*this);
		}
	// the text's own line height (its first formatted line), else the font's
	TInt LineHeight() const
		{
		CTextLayout* lay = iEditor ? iEditor->TextLayout() : NULL;
		TInt h = 0;
		if (lay && lay->NumFormattedLines() > 0)
			{
			TRect line;
			lay->GetLineRect(1, line);
			h = line.Height();
			}
		if (h < 8 || h > 40)
			h = CEikonEnv::Static()->NormalFont()->HeightInPixels() + 4;
		return h;
		}
	TSize MinimumSize()
		{
		TSize s = iEditor->MinimumSize();
		TInt lines = iEikonEnv->ScreenDevice()->SizeInPixels().iHeight >= 400 ? 12 : 7;
		return TSize(s.iWidth + KPsiHelpBarW + 2, lines * LineHeight() + 2);
		}
	void SizeChangedL() { Layout(); }
	void PositionChanged() { Layout(); }
	void Layout()
		{
		if (!iEditor)
			return;
		TRect r = Rect();
		r.Shrink(1, 1);
		iBar = TRect(r.iBr.iX - KPsiHelpBarW, r.iTl.iY, r.iBr.iX, r.iBr.iY);
		TRAPD(err, iEditor->SetRectL(TRect(r.iTl, TPoint(iBar.iTl.iX, r.iBr.iY))));
		(void)err;
		}
	TInt CountComponentControls() const { return iEditor ? 1 : 0; }
	CCoeControl* ComponentControl(TInt /*aIndex*/) const { return iEditor; }
	// the text's height, shown and above. An edwin formats only the band in
	// view once its text is over 2000 characters, so a long text's height is
	// reckoned from its length (and known once the end has been reached)
	void Model(TInt& aTotal, TInt& aShown, TInt& aAbove) const
		{
		CTextLayout* lay = iEditor->TextLayout();
		aShown = iEditor->Rect().Height();
		aTotal = lay ? lay->FormattedHeightInPixels() : aShown;
		TInt chars = iEditor->TextLength();
		if (chars > 2000)
			{
			const CFont* f = iEikonEnv->NormalFont();
			TInt perLine = iEditor->Rect().Width() / (f->WidthZeroInPixels() > 0 ? f->WidthZeroInPixels() : 8);
			if (perLine < 10) perLine = 10;
			TInt est = (chars / perLine + chars / 300 + 2) * LineHeight();
			if (est > aTotal) aTotal = est;
			}
		if (iAtEnd) aTotal = iAbove + aShown;
		if (aTotal < aShown) aTotal = aShown;
		aAbove = iAbove;
		if (aAbove > aTotal - aShown) aAbove = aTotal - aShown;
		if (aAbove < 0) aAbove = 0;
		}
	// the bar's parts: the shaft, the thumb in it, the up and down buttons
	// at its foot (as EIKON's)
	void Parts(TRect& aShaft, TRect& aThumb, TRect& aUp, TRect& aDown) const
		{
		TInt total, shown, above;
		Model(total, shown, above);
		const TRect& r = iBar;
		const TInt bh = 20;
		aDown = TRect(r.iTl.iX, r.iBr.iY - bh, r.iBr.iX, r.iBr.iY);
		aUp = TRect(r.iTl.iX, r.iBr.iY - 2 * bh, r.iBr.iX, r.iBr.iY - bh);
		aShaft = TRect(r.iTl.iX, r.iTl.iY, r.iBr.iX, aUp.iTl.iY);
		TInt sh = aShaft.Height();
		TInt th = total > 0 ? sh * shown / total : sh;
		if (th < 16) th = 16;
		if (th > sh) th = sh;
		TInt room = total - shown;
		TInt ty = aShaft.iTl.iY + (room > 0 ? (sh - th) * above / room : 0);
		aThumb = TRect(aShaft.iTl.iX, ty, aShaft.iBr.iX, ty + th);
		}
	void Face(CWindowGc& aGc, const TRect& aRect, TBool aDown) const
		{
		aGc.SetPenStyle(CGraphicsContext::ESolidPen);
		aGc.SetPenColor(KRgbBlack);
		aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		aGc.SetBrushColor(iEikonEnv->ControlColor(EEikColorButtonFaceClear, *this));
		aGc.DrawRect(aRect);
		TRect in(aRect);
		in.Shrink(1, 1);
		aGc.SetPenColor(aDown ? KRgbDarkGray : KRgbWhite);
		aGc.DrawLine(in.iTl, TPoint(in.iBr.iX - 1, in.iTl.iY));
		aGc.DrawLine(in.iTl, TPoint(in.iTl.iX, in.iBr.iY - 1));
		aGc.SetPenColor(aDown ? KRgbWhite : KRgbDarkGray);
		aGc.DrawLine(TPoint(in.iTl.iX, in.iBr.iY - 1), TPoint(in.iBr.iX, in.iBr.iY - 1));
		aGc.DrawLine(TPoint(in.iBr.iX - 1, in.iTl.iY), TPoint(in.iBr.iX - 1, in.iBr.iY));
		}
	static void Arrow(CWindowGc& aGc, const TRect& aRect, TBool aUp)
		{
		aGc.SetPenColor(KRgbBlack);
		TInt cx = aRect.iTl.iX + aRect.Width() / 2, cy = aRect.iTl.iY + aRect.Height() / 2;
		for (TInt i = 0; i < 4; i++)
			{
			TInt y = aUp ? cy - 2 + i : cy + 1 - i;
			aGc.DrawLine(TPoint(cx - i, y), TPoint(cx + i + 1, y));
			}
		}
	void DrawBar(CWindowGc& aGc) const
		{
		if (iBar.Width() <= 0)
			return;
		TRect shaft, thumb, up, down;
		Parts(shaft, thumb, up, down);
		aGc.SetPenStyle(CGraphicsContext::ESolidPen);
		aGc.SetPenColor(KRgbBlack);
		aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		aGc.SetBrushColor(iEikonEnv->ControlColor(EEikColorScrollBarShaft, *this));
		aGc.DrawRect(shaft);
		TRect t(thumb.iTl.iX + 1, thumb.iTl.iY, thumb.iBr.iX - 1, thumb.iBr.iY);
		Face(aGc, t, EFalse);
		aGc.SetPenColor(KRgbBlack);
		TInt cy = t.iTl.iY + t.Height() / 2;
		for (TInt k = -4; k <= 4; k += 2)
			if (cy + k > t.iTl.iY + 2 && cy + k < t.iBr.iY - 2)
				aGc.DrawLine(TPoint(t.iTl.iX + 5, cy + k), TPoint(t.iBr.iX - 5, cy + k));
		Face(aGc, up, iPress == 1);
		Arrow(aGc, up, ETrue);
		Face(aGc, down, iPress == 2);
		Arrow(aGc, down, EFalse);
		aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
		}
	void Draw(const TRect& /*aRect*/) const
		{
		CWindowGc& gc = SystemGc();
		gc.SetPenStyle(CGraphicsContext::ESolidPen);   // the line's edge, as the dialog's other lines
		gc.SetPenColor(KRgbBlack);
		gc.SetBrushStyle(CGraphicsContext::ENullBrush);
		gc.DrawRect(Rect());
		DrawBar(gc);
		}
	void UpdateBar()
		{
		if (!IsReadyToDraw())
			return;
		ActivateGc();
		DrawBar(SystemGc());
		DeactivateGc();
		}
	// the end of the text is in view: a scroll down has found nothing more
	TBool AtEnd() const
		{
		return iAtEnd;
		}
	// scrolls the text: the pixels it moved (0 at the top or the end)
	TInt ScrollL(TCursorPosition::TMovementType aMove)
		{
		TInt px = iEditor->TextView()->ScrollDisplayL(aMove);
		if (px < 0) px = -px;
		TBool down = aMove == TCursorPosition::EFLineDown || aMove == TCursorPosition::EFPageDown;
		if (down)
			iAtEnd = (px == 0);
		else if (px > 0)
			iAtEnd = EFalse;
		iAbove += down ? px : -px;
		if (iAbove < 0) iAbove = 0;
		iEditor->TextView()->SetCursorVisibilityL(TCursor::EFCursorInvisible, TCursor::EFCursorInvisible);
		UpdateBar();
		return px;
		}
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
		{
		if (aType != EEventKey)
			return EKeyWasNotConsumed;
		switch (aKeyEvent.iCode)
			{
		case EKeyUpArrow:
			if (iAbove <= 0 || ScrollL(TCursorPosition::EFLineUp) == 0)
				{
				iAbove = 0;
				UpdateBar();
				if (iTop.iFunction)
					iTop.CallBack();
				}
			return EKeyWasConsumed;
		case EKeyDownArrow:
			if (!AtEnd())
				ScrollL(TCursorPosition::EFLineDown);
			return EKeyWasConsumed;
		case EKeyPageUp:
			ScrollL(TCursorPosition::EFPageUp);
			return EKeyWasConsumed;
		case EKeyPageDown:
			if (!AtEnd())
				ScrollL(TCursorPosition::EFPageDown);
			return EKeyWasConsumed;
		case EKeyHome:
			{
			for (TInt guard = 0; guard < 100 && iAbove > 0 && ScrollL(TCursorPosition::EFPageUp) > 0; guard++) {}
			iAbove = 0;
			UpdateBar();
			return EKeyWasConsumed;
			}
		case EKeyEnd:
			{
			for (TInt guard = 0; guard < 100 && !AtEnd() && ScrollL(TCursorPosition::EFPageDown) > 0; guard++) {}
			return EKeyWasConsumed;
			}
		default:
			return EKeyWasNotConsumed;
			}
		}
	// the pen on the bar: the arrows a line, the shaft a page, the thumb dragged
	void HandlePointerEventL(const TPointerEvent& aEvent)
		{
		TPoint p = aEvent.iPosition;
		TRect shaft, thumb, up, down;
		Parts(shaft, thumb, up, down);
		if (aEvent.iType == TPointerEvent::EButton1Down)
			{
			iPress = 0;
			if (!iBar.Contains(p))
				return;                      // (the text: nothing to select)
			if (up.Contains(p)) { iPress = 1; ScrollL(TCursorPosition::EFLineUp); }
			else if (down.Contains(p)) { iPress = 2; if (!AtEnd()) ScrollL(TCursorPosition::EFLineDown); }
			else if (thumb.Contains(p)) { iPress = 3; iGrab = p.iY - thumb.iTl.iY; }
			else if (p.iY < thumb.iTl.iY) ScrollL(TCursorPosition::EFPageUp);
			else if (!AtEnd()) ScrollL(TCursorPosition::EFPageDown);
			UpdateBar();
			return;
			}
		if (aEvent.iType == TPointerEvent::EDrag && iPress == 3)
			{
			TInt total, shown, above;
			Model(total, shown, above);
			TInt room = shaft.Height() - thumb.Height();
			if (room > 0 && total > shown)
				{
				TInt want = (p.iY - iGrab - shaft.iTl.iY) * (total - shown) / room;
				for (TInt guard = 0; guard < 200; guard++)
					{
					TInt at = iAbove;
					if (at + 12 < want && !AtEnd()) ScrollL(TCursorPosition::EFLineDown);
					else if (at - 12 > want && at > 0) ScrollL(TCursorPosition::EFLineUp);
					else break;
					if (iAbove == at) break;
					}
				}
			return;
			}
		if (aEvent.iType == TPointerEvent::EButton1Up && iPress)
			{
			iPress = 0;
			UpdateBar();
			}
		}
	CEikRichTextEditor* iEditor;
	TCallBack iTop;
	TRect iBar;
	TInt iAbove;               // pixels scrolled
	TBool iAtEnd;              // the last scroll down moved nothing
	TInt iPress;               // the pen: 1 up, 2 down, 3 the thumb
	TInt iGrab;
	};

#endif
