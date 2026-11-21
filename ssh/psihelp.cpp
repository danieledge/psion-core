// psihelp.cpp - the Help dialog's text with its scroll bar (psihelp.h), for
// every bundled program: its .mmp adds this file beside pglinktest.cpp
// (SUBPROJECT psitermssh, SOURCE psihelp.cpp) and links form.lib.
#include "psihelp.h"

void CPsiHelpText::TextChangedL()
	{
	iAbove = 0;
	iAtEnd = EFalse;
	iEditor->TextView()->SetCursorVisibilityL(TCursor::EFCursorInvisible, TCursor::EFCursorInvisible);
	UpdateBar();
	}

void CPsiHelpText::ConstructFromResourceL(TResourceReader& aReader)
	{
	iEditor = new(ELeave) CEikRichTextEditor(TEikBorder(TEikBorder::ENone));
	if (DrawableWindow())
		iEditor->SetContainerWindowL(*this);
	iEditor->ConstructFromResourceL(aReader);
	}

void CPsiHelpText::SetContainerWindowL(const CCoeControl& aContainer)
	{
	CCoeControl::SetContainerWindowL(aContainer);
	if (iEditor)
		iEditor->SetContainerWindowL(*this);
	}

TInt CPsiHelpText::LineHeight() const
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

TSize CPsiHelpText::MinimumSize()
	{
	TSize s = iEditor->MinimumSize();
	TInt lines = iEikonEnv->ScreenDevice()->SizeInPixels().iHeight >= 400 ? 12 : 7;
	return TSize(s.iWidth + KPsiHelpBarW + 2, lines * LineHeight() + 2);
	}

void CPsiHelpText::Layout()
	{
	if (!iEditor)
		return;
	TRect r = Rect();
	r.Shrink(1, 1);
	iBar = TRect(r.iBr.iX - KPsiHelpBarW, r.iTl.iY, r.iBr.iX, r.iBr.iY);
	TRAPD(err, iEditor->SetRectL(TRect(r.iTl, TPoint(iBar.iTl.iX, r.iBr.iY))));
	(void)err;
	}

void CPsiHelpText::Model(TInt& aTotal, TInt& aShown, TInt& aAbove) const
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

void CPsiHelpText::Parts(TRect& aShaft, TRect& aThumb, TRect& aUp, TRect& aDown) const
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

void CPsiHelpText::Face(CWindowGc& aGc, const TRect& aRect, TBool aDown) const
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

void CPsiHelpText::Arrow(CWindowGc& aGc, const TRect& aRect, TBool aUp)
	{
	aGc.SetPenColor(KRgbBlack);
	TInt cx = aRect.iTl.iX + aRect.Width() / 2, cy = aRect.iTl.iY + aRect.Height() / 2;
	for (TInt i = 0; i < 4; i++)
		{
		TInt y = aUp ? cy - 2 + i : cy + 1 - i;
		aGc.DrawLine(TPoint(cx - i, y), TPoint(cx + i + 1, y));
		}
	}

void CPsiHelpText::DrawBar(CWindowGc& aGc) const
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

void CPsiHelpText::Draw(const TRect& /*aRect*/) const
	{
	CWindowGc& gc = SystemGc();
	gc.SetPenStyle(CGraphicsContext::ESolidPen);   // the line's edge, as the dialog's other lines
	gc.SetPenColor(KRgbBlack);
	gc.SetBrushStyle(CGraphicsContext::ENullBrush);
	gc.DrawRect(Rect());
	DrawBar(gc);
	}

void CPsiHelpText::UpdateBar()
	{
	if (!IsReadyToDraw())
		return;
	ActivateGc();
	DrawBar(SystemGc());
	DeactivateGc();
	}

TBool CPsiHelpText::AtEnd() const
	{
	return iAtEnd;
	}

TInt CPsiHelpText::ScrollL(TCursorPosition::TMovementType aMove)
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

TKeyResponse CPsiHelpText::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
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

void CPsiHelpText::HandlePointerEventL(const TPointerEvent& aEvent)
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
