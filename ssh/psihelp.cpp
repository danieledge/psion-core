// psihelp.cpp - the Help dialog's text in whole lines, with a scroll bar
// only when it is longer than its box (psihelp.h), for every bundled
// program: its .mmp adds this file beside pglinktest.cpp (SUBPROJECT
// psitermssh, SOURCE psihelp.cpp) and links form.lib.
#include "psihelp.h"

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
	TInt lines = iEikonEnv->ScreenDevice()->SizeInPixels().iHeight >= 400 ? 12 : 7;
	return TSize(iEditor->MinimumSize().iWidth + KPsiHelpBarW + 2, lines * LineHeight() + 2);
	}

void CPsiHelpText::FormatL()
	{
	CTextView* view = iEditor->TextView();
	if (!view || !iEditor->TextLayout())
		return;
	iEditor->TextLayout()->SetAmountToFormat(CTextLayout::EFFormatAllText);
	view->FormatTextL();
	}

TInt CPsiHelpText::Count() const
	{
	CTextLayout* lay = iEditor->TextLayout();
	return lay ? lay->NumFormattedLines() : 0;
	}

TInt CPsiHelpText::Shown() const
	{
	return iLineH > 0 ? iEditor->Rect().Height() / iLineH : 0;
	}

// whole lines high (any pixels over stay blank below the last), as wide as
// the line less the bar when the text is longer than the box
void CPsiHelpText::LayoutL()
	{
	if (!iEditor)
		return;
	TRect r = Rect();
	r.Shrink(1, 1);
	if (r.Width() <= KPsiHelpBarW + 8 || r.Height() <= 8)
		return;
	iLineH = LineHeight();
	TInt h = (r.Height() / iLineH) * iLineH;
	if (h <= 0)
		h = r.Height();
	iEditor->SetRectL(TRect(r.iTl, TSize(r.Width() - KPsiHelpBarW, h)));
	FormatL();
	if (Count() > Shown())
		iBar = TRect(r.iBr.iX - KPsiHelpBarW, r.iTl.iY, r.iBr.iX, r.iBr.iY);
	else
		{
		iBar = TRect();
		iEditor->SetRectL(TRect(r.iTl, TSize(r.Width(), h)));
		FormatL();
		}
	iTopLine = 0;
	}

void CPsiHelpText::TextChangedL()
	{
	iTopLine = 0;
	iPress = 0;
	if (iEditor->TextView())
		iEditor->TextView()->SetCursorVisibilityL(TCursor::EFCursorInvisible, TCursor::EFCursorInvisible);
	LayoutL();
	if (IsReadyToDraw())
		DrawDeferred();                 // (all of it again: the text's width may have changed)
	}

void CPsiHelpText::Parts(TRect& aShaft, TRect& aThumb, TRect& aUp, TRect& aDown) const
	{
	const TRect& r = iBar;
	aDown = TRect(r.iTl.iX, r.iBr.iY - KPsiHelpButH, r.iBr.iX, r.iBr.iY);
	aUp = TRect(r.iTl.iX, r.iBr.iY - 2 * KPsiHelpButH, r.iBr.iX, r.iBr.iY - KPsiHelpButH);
	aShaft = TRect(r.iTl.iX, r.iTl.iY, r.iBr.iX, aUp.iTl.iY);
	TInt total = Count(), shown = Shown();
	if (total < shown)
		total = shown;
	TInt sh = aShaft.Height();
	TInt th = total > 0 ? sh * shown / total : sh;
	if (th < 16) th = 16;
	if (th > sh) th = sh;
	TInt room = total - shown;
	TInt ty = aShaft.iTl.iY + (room > 0 ? (sh - th) * Min(iTopLine, room) / room : 0);
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
	Face(aGc, t, iPress == 3);
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

// the line's edge, as the dialog's other lines; white under the last whole
// line (the editor draws the text and its own ground)
void CPsiHelpText::Draw(const TRect& /*aRect*/) const
	{
	CWindowGc& gc = SystemGc();
	gc.SetPenStyle(CGraphicsContext::ESolidPen);
	gc.SetPenColor(KRgbBlack);
	gc.SetBrushStyle(CGraphicsContext::ENullBrush);
	gc.DrawRect(Rect());
	if (iEditor)
		{
		TRect r = Rect();
		r.Shrink(1, 1);
		TRect e = iEditor->Rect();
		TRect below(r.iTl.iX, e.iBr.iY, iBar.Width() > 0 ? iBar.iTl.iX : r.iBr.iX, r.iBr.iY);
		if (below.Height() > 0)
			{
			gc.SetPenStyle(CGraphicsContext::ENullPen);
			gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
			gc.SetBrushColor(KRgbWhite);
			gc.DrawRect(below);
			}
		}
	DrawBar(gc);
	}

void CPsiHelpText::UpdateBar()
	{
	if (!IsReadyToDraw() || iBar.Width() <= 0)
		return;
	ActivateGc();
	DrawBar(SystemGc());
	DeactivateGc();
	}

void CPsiHelpText::ScrollToL(TInt aTop)
	{
	TInt most = Count() - Shown();
	if (aTop > most)
		aTop = most;
	if (aTop < 0)
		aTop = 0;
	if (aTop == iTopLine || !iEditor->TextView())
		return;
	TPoint moved = iEditor->TextView()->SetViewLineAtTopL(aTop + 1);
	iTopLine = aTop;
	if (moved.iY == CTextLayout::EFScrollRedrawWholeScreen)
		iEditor->DrawNow();
	UpdateBar();
	}

TKeyResponse CPsiHelpText::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
	{
	if (aType != EEventKey)
		return EKeyWasNotConsumed;
	TInt page = Shown() > 1 ? Shown() - 1 : 1;
	switch (aKeyEvent.iCode)
		{
	case EKeyUpArrow:
		if (iTopLine > 0)
			ScrollToL(iTopLine - 1);
		else if (iTop.iFunction)
			iTop.CallBack();
		break;
	case EKeyDownArrow:
		ScrollToL(iTopLine + 1);
		break;
	case EKeyPageUp:
		ScrollToL(iTopLine - page);
		break;
	case EKeyPageDown:
		ScrollToL(iTopLine + page);
		break;
	case EKeyHome:
		ScrollToL(0);
		break;
	case EKeyEnd:
		ScrollToL(Count());
		break;
	default:
		return EKeyWasNotConsumed;
		}
	return EKeyWasConsumed;
	}

void CPsiHelpText::HandlePointerEventL(const TPointerEvent& aEvent)
	{
	if (iBar.Width() <= 0)
		return;                          // (the text: nothing to select)
	TPoint p = aEvent.iPosition;
	TRect shaft, thumb, up, down;
	Parts(shaft, thumb, up, down);
	TInt page = Shown() > 1 ? Shown() - 1 : 1;
	if (aEvent.iType == TPointerEvent::EButton1Down)
		{
		iPress = 0;
		if (!iBar.Contains(p))
			return;
		if (up.Contains(p)) { iPress = 1; ScrollToL(iTopLine - 1); }
		else if (down.Contains(p)) { iPress = 2; ScrollToL(iTopLine + 1); }
		else if (thumb.Contains(p)) { iPress = 3; iGrab = p.iY - thumb.iTl.iY; }
		else ScrollToL(p.iY < thumb.iTl.iY ? iTopLine - page : iTopLine + page);
		UpdateBar();
		}
	else if (aEvent.iType == TPointerEvent::EDrag && iPress == 3)
		{
		TInt room = shaft.Height() - thumb.Height(), most = Count() - Shown();
		if (room > 0 && most > 0)
			ScrollToL(((p.iY - iGrab - shaft.iTl.iY) * most + room / 2) / room);
		}
	else if (aEvent.iType == TPointerEvent::EButton1Up && iPress)
		{
		iPress = 0;
		UpdateBar();
		}
	}
