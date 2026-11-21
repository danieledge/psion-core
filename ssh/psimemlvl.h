// psimemlvl.h - how short of memory the Psion is: one level for every
// program and engine (21 November 2026, BETA 1.08: the memory-pressure
// service, MEM review P2-02). Included by psimem.h (the programs and the
// watcher) and by psiglue.cpp (the engines: pg_mem_level, psiwork.h), so it
// needs only the kernel's headers. No writable statics (an .app is a DLL).
//
// The levels, from the free memory (psimem.h's figures):
//   0  3 MB or more free: nothing to do (a program trims itself when it
//      has been behind a while, by its own rule)
//   1  under 3 MB  (KPsiMemTidy): "trim": caches, decoded pictures, the
//      heap's free end; the watcher calls User::CompressAllHeaps
//   2  under 1.5 MB (KPsiMemLow): "sleep" to programs behind; engines with
//      no transfer running end (their program starts them again)
//   3  under 750 KB (KPsiMemVeryLow), or a program said "short": the Free
//      memory dialog is offered; with "close programs behind" ticked the
//      watcher closes them, least recently used first (never the one in
//      front, never one that says "cannot")
//
// The watcher (PsiHub) publishes the level it has judged, and what it saw,
// in the global chunk "PsiMemLevel" (PsiMemPublish), which it keeps open;
// anyone reads it with PsiMemLevelNow, which falls back to the free memory
// now when no watcher runs (a card-less start, the 5mx without Coda).
//
// The off switch: C:\System\Data\PsiMem.ini, lines "name=value":
//   service=0   the watcher sends no "trim"/"sleep" beyond what it did in
//               1.07 (PsiWeb and PsiPDF) and publishes level 0, so engines
//               and programs behave as before; default 1
//   close=1     close programs behind at level 3; default 0 (ask, as now)
//   tidy=0      no User::CompressAllHeaps after a program closes or when
//               idle (P2-03); default 1
// A missing file or line is the default.

#ifndef __PSIMEMLVL_H
#define __PSIMEMLVL_H

#include <e32std.h>
#include <e32hal.h>

// the levels (free RAM, bytes)
const TInt KPsiMemTidy = 3 * 1024 * 1024;       // under this: "trim"
const TInt KPsiMemLow = 1536 * 1024;            // under this: "sleep" to programs behind; the user is told once
const TInt KPsiMemVeryLow = 750 * 1024;         // under this: the dialog is offered

enum { EPsiMemPlenty = 0, EPsiMemTrimLevel = 1, EPsiMemSleepLevel = 2, EPsiMemVeryLowLevel = 3 };

// the level the free memory alone gives (bytes; <0 unknown: 0)
inline TInt PsiMemLevelOf(TInt aFreeBytes)
	{
	if (aFreeBytes < 0 || aFreeBytes >= KPsiMemTidy)
		return EPsiMemPlenty;
	if (aFreeBytes >= KPsiMemLow)
		return EPsiMemTrimLevel;
	if (aFreeBytes >= KPsiMemVeryLow)
		return EPsiMemSleepLevel;
	return EPsiMemVeryLowLevel;
	}

inline TInt PsiMemFreeBytes()
	{
	TMemoryInfoV1Buf m;
	if (UserHal::MemoryInfo(m) != KErrNone)
		return -1;
	return m().iFreeRamInBytes;
	}

// the watcher's page (the chunk's first bytes)
_LIT(KPsiMemLevelChunk, "PsiMemLevel");
const TUint32 KPsiMemLevelMagic = 0x4c6d7350;   // 'PsmL'
struct TPsiMemState
	{
	TUint32 iMagic;
	TInt iLevel;            // as judged by the watcher (0 when service=0)
	TInt iFreeKb;           // what it saw
	TUint iTick;            // User::TickCount() then
	TUint32 iFlags;         // KPsiMemFlag...
	TInt iSpare[3];
	};
const TUint32 KPsiMemFlagService = 1;   // service=1 (the words are sent)
const TUint32 KPsiMemFlagClose = 2;     // close=1
const TUint32 KPsiMemFlagTidy = 4;      // tidy=1

// the watcher's state, if it runs: ETrue and aState filled
inline TBool PsiMemReadState(TPsiMemState& aState)
	{
	RChunk c;
	if (c.OpenGlobal(KPsiMemLevelChunk, ETrue) != KErrNone)
		return EFalse;
	TBool ok = EFalse;
	if (c.Size() >= (TInt)sizeof(TPsiMemState))
		{
		Mem::Copy(&aState, c.Base(), sizeof(TPsiMemState));
		ok = aState.iMagic == KPsiMemLevelMagic;
		}
	c.Close();
	return ok;
	}

// the level now: the watcher's when it runs and has looked in the last
// 30 s, else from the free memory now; with the service off, 0
inline TInt PsiMemLevelNow()
	{
	TPsiMemState s;
	if (PsiMemReadState(s))
		{
		if (!(s.iFlags & KPsiMemFlagService))
			return EPsiMemPlenty;
		TUint age = User::TickCount() - s.iTick;     // (ticks of 1/64 s on the Psion)
		if (age < 30 * 64)
			return s.iLevel;
		}
	return PsiMemLevelOf(PsiMemFreeBytes());
	}

// (the watcher) its judgement, published; aChunk stays open in the
// watcher (the chunk lives while a handle is open). KErrNone or an error.
inline TInt PsiMemPublish(RChunk& aChunk, TInt aLevel, TInt aFreeKb, TUint32 aFlags)
	{
	if (aChunk.Handle() == 0)
		{
		TInt err = aChunk.CreateGlobal(KPsiMemLevelChunk, 4096, 4096);
		if (err == KErrAlreadyExists)
			err = aChunk.OpenGlobal(KPsiMemLevelChunk, EFalse);
		if (err != KErrNone)
			return err;
		}
	TPsiMemState* s = (TPsiMemState*)aChunk.Base();
	s->iLevel = (aFlags & KPsiMemFlagService) ? aLevel : EPsiMemPlenty;
	s->iFreeKb = aFreeKb;
	s->iTick = User::TickCount();
	s->iFlags = aFlags;
	s->iMagic = KPsiMemLevelMagic;
	return KErrNone;
	}

// The engine's heap given back as far as it will go: the bytes returned
// (User::Heap().Compress() returns only the free cell at the top: what is
// freed below the top stays until the cells above it go)
inline TInt PsiMemCompress()
	{
	RHeap& h = User::Heap();
	TInt before = h.Size();
	h.Compress();
	TInt after = h.Size();
	return before > after ? before - after : 0;
	}

#endif
