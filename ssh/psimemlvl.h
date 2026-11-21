// psimemlvl.h - how short of memory the Psion is: one level for every
// program and engine (21 November 2026, BETA 1.08: the memory-pressure
// service, MEM review P2-02). Included by psimem.h (the programs and the
// watcher) and by psiglue.cpp (the engines: pg_mem_level, psiwork.h), so it
// needs only the kernel's headers. No writable statics (an .app is a DLL).
//
// The interface is the one group M1 proposed for the netBook's watcher
// (psion-netbook psisys/psimemsvc.h, whose core half steps aside when
// PSIMEM_LEVEL is defined here): copied as it is.
//
// The state, one global chunk "PsiMemLevel" of one page, made by whoever
// needs it first (the watcher, or a program that answers) and kept while
// anyone holds it open:
//   iLevel   0 normal (3 MB or more free), 1 tidy (under 3 MB), 2 low
//            (under 1.5 MB), 3 critical (under 750 KB): written by the
//            watcher (PsiHub; PsiStart when PsiHub is not running) at each
//            look; -1 while the service is off (PsiMem.ini level=0).
//            Engines with no window group (psimail.exe, psisync.exe,
//            psiweb.exe) read it in their idle tick (PsiMemLevelNow, or
//            psi_mem_level() in C) instead of a message: at 1 or more they
//            give back their heap's free end (psi_work_trim), at 2 or more
//            they end if nothing is being fetched, as on "sleep".
//   iAnswer  the UIDs of the programs running now that take "trim" and
//            "sleep" (KPsiMemMsgUid): PsiMemRegister at the program's
//            start, PsiMemUnregister at its end. Only these (and PsiWeb
//            and PsiPDF) are sent the words: CEikAppUi::ProcessMessageL
//            reads an unknown message UID as a file to open, so a program
//            that does not answer must never register.
//   iBusy    beside each UID: non-zero while that program must not be
//            closed or put to sleep (PsiMemBusy: a transfer, a dialog up).
//
// The off switch, the watcher's options (PsiMem.ini: level=, idletrim=,
// close=), PsiMemPublish and PsiMemTellAll are the watcher's side, in the
// netBook's psisys/psimemsvc.h.

#ifndef __PSIMEMLVL_H
#define __PSIMEMLVL_H

#include <e32std.h>
#include <e32hal.h>

// the levels (free RAM, bytes)
const TInt KPsiMemTidy = 3 * 1024 * 1024;       // under this: "trim"
const TInt KPsiMemLow = 1536 * 1024;            // under this: "sleep" to programs behind; the user is told once
const TInt KPsiMemVeryLow = 750 * 1024;         // under this: the dialog is offered

#ifndef PSIMEM_LEVEL
#define PSIMEM_LEVEL

_LIT(KPsiMemLevelChunk, "PsiMemLevel");
const TUint32 KPsiMemLevelMagic = 0x764c4d50;      // 'PMLv'
const TInt KPsiMemAnswerMax = 16;

enum TPsiMemLevel { EPsiMemNormal = 0, EPsiMemTidyLevel = 1, EPsiMemLowLevel = 2, EPsiMemCritical = 3 };

struct TPsiMemState
	{
	TUint32 iMagic;
	TInt32 iVersion;            // 1
	TInt32 iLevel;              // TPsiMemLevel; -1: the service is off
	TInt32 iFreeKb;
	TUint32 iChange;            // +1 at each change of level
	TUint32 iWatcher;           // the UID of the program that wrote iLevel
	TUint32 iTick;              // User::TickCount() / 64 at that look
	TUint32 iSpare;
	TUint32 iAnswer[KPsiMemAnswerMax];
	TUint32 iBusy[KPsiMemAnswerMax];
	};

inline TInt PsiMemLevelOf(TInt aFreeBytes)
	{
	if (aFreeBytes < 0)
		return EPsiMemNormal;
	if (aFreeBytes < KPsiMemVeryLow)
		return EPsiMemCritical;
	if (aFreeBytes < KPsiMemLow)
		return EPsiMemLowLevel;
	if (aFreeBytes < KPsiMemTidy)
		return EPsiMemTidyLevel;
	return EPsiMemNormal;
	}

// the state, opened (made if aMake and not there): NULL if it cannot be
inline TPsiMemState* PsiMemStateOpen(RChunk& aChunk, TBool aMake)
	{
	TInt err = aChunk.OpenGlobal(KPsiMemLevelChunk, EFalse);
	if (err != KErrNone && aMake)
		{
		err = aChunk.CreateGlobal(KPsiMemLevelChunk, 4096, 4096);
		if (err == KErrAlreadyExists)
			err = aChunk.OpenGlobal(KPsiMemLevelChunk, EFalse);
		else if (err == KErrNone)
			{
			TPsiMemState* s = (TPsiMemState*)aChunk.Base();
			Mem::FillZ(s, sizeof(TPsiMemState));
			s->iVersion = 1;
			s->iLevel = -1;
			s->iFreeKb = -1;
			s->iMagic = KPsiMemLevelMagic;
			}
		}
	if (err != KErrNone)
		return NULL;
	TPsiMemState* s = (TPsiMemState*)aChunk.Base();
	if (aChunk.Size() < (TInt)sizeof(TPsiMemState) || s->iMagic != KPsiMemLevelMagic)
		{
		aChunk.Close();
		return NULL;
		}
	return s;
	}

// the level now (an engine's idle tick): TPsiMemLevel, or -1 when no
// watcher has said (no chunk, or the service off)
inline TInt PsiMemLevelNow()
	{
	RChunk c;
	TPsiMemState* s = PsiMemStateOpen(c, EFalse);
	if (!s)
		return -1;
	TInt level = s->iLevel;
	c.Close();
	return level;
	}

// a program that takes "trim" and "sleep" says so at its start: it keeps
// aChunk open until PsiMemUnregister (at its end, or never: the slot is
// freed by the watcher once the program has gone)
inline TInt PsiMemRegister(RChunk& aChunk, TUint aUid)
	{
	TPsiMemState* s = PsiMemStateOpen(aChunk, ETrue);
	if (!s)
		return KErrNotFound;
	TInt freeAt = -1;
	for (TInt i = 0; i < KPsiMemAnswerMax; i++)
		{
		if (s->iAnswer[i] == aUid)
			return KErrNone;
		if (!s->iAnswer[i] && freeAt < 0)
			freeAt = i;
		}
	if (freeAt < 0)
		return KErrOverflow;
	s->iBusy[freeAt] = 0;
	s->iAnswer[freeAt] = aUid;
	return KErrNone;
	}

// busy (not to be closed or slept by the service) or not
inline void PsiMemBusy(RChunk& aChunk, TUint aUid, TBool aBusy)
	{
	if (!aChunk.Handle())
		return;
	TPsiMemState* s = (TPsiMemState*)aChunk.Base();
	for (TInt i = 0; i < KPsiMemAnswerMax; i++)
		if (s->iAnswer[i] == aUid)
			s->iBusy[i] = aBusy;
	}

inline void PsiMemUnregister(RChunk& aChunk, TUint aUid)
	{
	if (!aChunk.Handle())
		return;
	TPsiMemState* s = (TPsiMemState*)aChunk.Base();
	for (TInt i = 0; i < KPsiMemAnswerMax; i++)
		if (s->iAnswer[i] == aUid)
			s->iAnswer[i] = s->iBusy[i] = 0;
	aChunk.Close();
	}

#endif // PSIMEM_LEVEL

// The program's own heap given back as far as it will go: the bytes
// returned. (User::Heap().Compress() returns only the free cell at the
// top: what is freed below the top stays until the cells above it go.)
inline TInt PsiMemCompress()
	{
	RHeap& h = User::Heap();
	TInt before = h.Size();
	h.Compress();
	TInt after = h.Size();
	return before > after ? before - after : 0;
	}

#endif
