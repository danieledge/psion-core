// psierr.h: an EPOC error as words for a message, and the one wording for
// something that is on the card, shared by every program (PsiMail, PsiWeb,
// PsiLinux, PsiAmp, PsiPDF; Coda's own programs keep the same text in their
// psisys/psierr.h: change both together).
//
// Messages say what did not happen and why, in the user's words, never the
// bare number: "Not saved - the disk is full", "Not changed - the card is
// not in". The number goes to the program's log, where there is one.
//
//   TBuf<80> m;
//   PsiErrMsg(m, _L("Not saved"), err);       // "Not saved - the disk is full"
//   iEikonEnv->InfoMsg(m);
//   m = _L("Cannot play");
//   PsiErrTail(m, err);                       // "Cannot play - not enough memory"
//   PsiOnCardMsg(m, _L("Help"));              // "Help is on the card - put the card in"
//
// When the reason is not known there is no tail at all: "Not saved", not
// "Not saved - for an unknown reason". So a message is built with
// PsiErrTail or PsiErrMsg, never by appending " - " and PsiErrWords.
//
// Header only, no library beyond euser. Works on the netBook and the Series
// 5mx alike (on the 5mx "the card" is the memory disk's card all the same).
// No division, nothing that leaves. (ER5: Left past a descriptor's end
// panics, USER 22, and Append past its MaxLength, USER 11: every length is
// clipped.) Host test: ssh/test/psierrtest.cpp.
#ifndef PSIERR_H
#define PSIERR_H
#include <e32std.h>

// the reason, lower case, to follow " - "; empty when none is known
inline TPtrC PsiErrWords(TInt aErr)
	{
	switch (aErr)
		{
	case KErrNoMemory:        return _L("not enough memory");
	case KErrDiskFull:        return _L("the disk is full");
	case KErrNotReady:        return _L("the card is not in");
	case KErrInUse:           return _L("it is in use");
	case KErrAccessDenied:    return _L("it is read-only or in use");
	case KErrLocked:          return _L("the card is locked");
	case KErrNotFound:        return _L("not found");
	case KErrPathNotFound:    return _L("folder not found");
	case KErrAlreadyExists:   return _L("it is there already");
	case KErrBadName:         return _L("the name is not allowed");
	case KErrCorrupt:         return _L("the file is damaged");
	case KErrEof:             return _L("the file is cut short");
	case KErrNotSupported:    return _L("not supported");
	case KErrTooBig:
	case KErrOverflow:        return _L("too big");
	case KErrTimedOut:        return _L("no answer in time");
	case KErrCancel:          return _L("stopped");
	case KErrServerBusy:      return _L("the system is busy - try again");
	case KErrCouldNotConnect: return _L("no connection");
	case KErrDied:            return _L("a program it needed stopped");
	case KErrDisMounted:      return _L("the card was taken out");
	default:                  return _L("");      // no reason known: no tail
		}
	}

// " - " + the reason appended, cut to the buffer; nothing when no reason
// is known or there is no room for more than the dash
inline void PsiErrTail(TDes& aMsg, TInt aErr)
	{
	TPtrC why(PsiErrWords(aErr));
	if (!why.Length() || aMsg.MaxLength() - aMsg.Length() < 4)
		return;
	aMsg.Append(_L(" - "));
	aMsg.Append(why.Left(Min(why.Length(), aMsg.MaxLength() - aMsg.Length())));
	}

// aWhat + " - " + the reason, cut to the buffer ("Not saved" alone when no
// reason is known)
inline void PsiErrMsg(TDes& aMsg, const TDesC& aWhat, TInt aErr)
	{
	TPtrC why(PsiErrWords(aErr));
	TInt room = why.Length() ? aMsg.MaxLength() - 3 - why.Length() : aMsg.MaxLength();
	aMsg.Copy(aWhat.Left(Min(aWhat.Length(), Max(0, room))));
	PsiErrTail(aMsg, aErr);
	}

// "<What> is on the card - put the card in" (aWhat starts with a capital)
inline void PsiOnCardMsg(TDes& aMsg, const TDesC& aWhat)
	{
	_LIT(KTail, " is on the card - put the card in");
	aMsg.Copy(aWhat.Left(Min(aWhat.Length(), Max(0, aMsg.MaxLength() - KTail().Length()))));
	aMsg.Append(KTail().Left(Min(KTail().Length(), aMsg.MaxLength() - aMsg.Length())));
	}

#endif
