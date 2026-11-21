// psimem.h - short of memory: what the programs say to each other.
//
// One program on the Psion watches the free memory (the launcher, where it
// is installed) and offers the user a "Free memory" dialog. It and the
// programs talk through window-group messages, which need no shared code:
// RWsSession::SendMessageToWindowGroup / TApaTask::SendMessage with
// KPsiMemMsgUid, received in CEikAppUi::ProcessMessageL(aUid, aParams).
// The parameter is one 8-bit word.
//
// To a program (it may ignore any of them; none needs an answer):
//   "trim"   give back what costs nothing to make again: caches, decoded
//            pictures away from the view, the heap's free end
//            (User::Heap().Compress()). Sent when under 3 MB are free.
//   "sleep"  as trim, and stop the engine if nothing would be lost (no
//            transfer, no form half filled in, no dialog up), as the
//            program does by itself after some minutes behind another. It
//            wakes when it next comes to the front. Sent when under 1.5 MB
//            are free and the program is behind, or when the user asks.
//
// To the watcher (found by its UID, KPsiMemWatcherUid):
//   "dialog" open the Free memory dialog now (the user asked).
//   "short"  this program has just had to say "Not enough memory": the
//            dialog is offered (not more than once every few minutes).
//
// (20 November 2026, PsiHub 0.62) The Free memory dialog's own word, and
// the answers to it:
//   "sleepnow"  the user ticked "Put <program> to sleep": as "sleep", now,
//            whatever the time behind and the memory free, if nothing
//            would be lost; the program answers.
// The answers, to the watcher (PsiMemAnswer): the word, the program's UID
// in eight hex digits, then for "cannot" why, in words:
//   "slept 01000a7a"                  asleep now (or it was already)
//   "cannot 01000a7a a page is still opening"
// A program that does not answer is judged by what it left running.
//
// A program with no watcher installed loses nothing: PsiMemTell returns
// EFalse and the program carries on as before.
//
// (20 November 2026) The one copy: Coda's launcher, toolbar and the
// programs on the card all build with this file (Coda's own psisys/psimem.h,
// which had the Free memory dialog's words and the free memory figure
// first, is gone). A program that defined "sleepnow" and PsiMemAnswer
// itself, under #ifndef KPsiMemSleepNow, now takes them from here: the
// same words, the same code.

#ifndef __PSIMEM_H
#define __PSIMEM_H

#include <e32std.h>
#include <w32std.h>
#include <apgtask.h>
#include <e32hal.h>
#include <f32file.h>

const TInt KPsiMemMsgUid = 0x01000BA0;
const TInt KPsiMemWatcherUid = 0x01000A9B;

// the levels (KPsiMemTidy, KPsiMemLow, KPsiMemVeryLow), the state the
// watcher publishes (PsiMemLevelNow, PsiMemRegister, PsiMemBusy) and
// PsiMemCompress: psimemlvl.h, which the engines include alone
#include "psimemlvl.h"

#define KPsiMemTrim   _L8("trim")
#define KPsiMemSleep  _L8("sleep")
#define KPsiMemDialog _L8("dialog")
#define KPsiMemShort  _L8("short")
#define KPsiMemSleepNow _L8("sleepnow")
#define KPsiMemSlept  _L8("slept")
#define KPsiMemCannot _L8("cannot")

// a word to the watcher; EFalse if it is not running (or the message did
// not go)
inline TBool PsiMemTell(RWsSession& aWs, const TDesC8& aWord)
	{
	const TUid watcher = { KPsiMemWatcherUid };
	const TUid msg = { KPsiMemMsgUid };
	TApaTaskList tasks(aWs);
	TApaTask t = tasks.FindApp(watcher);
	return t.Exists() && t.SendMessage(msg, aWord) == KErrNone;
	}

// (18 November 2026) "dialog" is not read while one of the watcher's own
// dialogs is up (the Free memory dialog left behind another program: EIKON
// holds window-group messages under a waiting dialog). This window-server
// event, which is read, brings the watcher to the front if a dialog of its
// is up; the toolbar's free memory line sends it with "dialog".
const TUint32 KPsiMemFrontEvent = 0x4d697350;   // 'PsiM'

inline void PsiMemFront(RWsSession& aWs)
	{
	const TUid watcher = { KPsiMemWatcherUid };
	TApaTaskList tasks(aWs);
	TApaTask t = tasks.FindApp(watcher);
	if (!t.Exists())
		return;
	TWsEvent e;
	e.SetType(EEventUser);
	*(TUint32*)e.EventData() = KPsiMemFrontEvent;
	aWs.SendEventToWindowGroup(t.WgId(), e);
	}

// an answer to the watcher: "slept 01000a7a", "cannot 01000a7a <why>"
inline TBool PsiMemAnswer(RWsSession& aWs, TUint aUid, const TDesC8& aWord, const TDesC& aWhy)
	{
	TBuf8<96> m(aWord);
	m.AppendFormat(_L8(" %08x"), aUid);
	if (aWhy.Length())
		{
		m.Append(' ');
		for (TInt i = 0; i < aWhy.Length() && m.Length() < m.MaxLength(); i++)
			m.Append((TUint8)aWhy[i]);
		}
	return PsiMemTell(aWs, m);
	}

// (20 November 2026) The free memory, one figure for all: the toolbar's
// line and large face (PsiBatt) and PsiHub's Free memory dialog read it
// here and write it the same way. The kernel's free RAM (C: takes its
// room from the same pool, so C:'s free space is this too), in KB; -1 if
// it cannot be read.
inline TInt PsiMemFreeKb()
	{
	TMemoryInfoV1Buf m;
	if (UserHal::MemoryInfo(m) != KErrNone || m().iFreeRamInBytes < 0)
		return -1;
	return m().iFreeRamInBytes >> 10;
	}

// A program's side: which word a window-group message is, in its
// ProcessMessageL (before CEikAppUi's, which would open it as a file):
//   switch (PsiMemWordOf(aUid, aParams)) { case EPsiMemWordTrim: ... }
// "sleep" and "sleepnow" differ only in that "sleepnow" wants an answer
// (PsiMemAnswer) and comes whatever the memory free.
enum TPsiMemWord { EPsiMemWordNone, EPsiMemWordTrim, EPsiMemWordSleep, EPsiMemWordSleepNow };

inline TPsiMemWord PsiMemWordOf(TUid aUid, const TDesC8& aParams)
	{
	if (aUid.iUid != KPsiMemMsgUid)
		return EPsiMemWordNone;
	if (aParams == KPsiMemTrim)
		return EPsiMemWordTrim;
	if (aParams == KPsiMemSleep)
		return EPsiMemWordSleep;
	if (aParams == KPsiMemSleepNow)
		return EPsiMemWordSleepNow;
	return EPsiMemWordNone;
	}

// "8.9 MB" (tenths, cut, not rounded up: never more than there is), "750 KB"
inline void PsiMemSizeText(TDes& aOut, TInt aKb)
	{
	aOut.Zero();
	if (aKb < 0)
		aKb = 0;
	if (aKb >= 1024)
		{
		// (no division: PsiBatt.dll, in the ROM, links no helper for it)
		TInt tenths = (aKb * 10) >> 10, mb = 0;
		while (tenths >= 10)
			{
			tenths -= 10;
			mb++;
			}
		aOut.Format(_L("%d.%d MB"), mb, tenths);
		}
	else
		aOut.Format(_L("%d KB"), aKb);
	}

// "8.9 MB free"
inline void PsiMemFreeText(TDes& aOut, TInt aKb)
	{
	PsiMemSizeText(aOut, aKb);
	aOut.Append(_L(" free"));
	}

// (20 November 2026) The memory has changed (PsiHub's Free memory dialog
// opened, or closed, put to sleep or tidied a program, or moved files):
// every toolbar reads the free memory again at once. The toolbars are
// controls in each program's thread, which see none of their program's
// window-server events, so the bell is this file: each toolbar watches
// its name (RFs::NotifyChange), as it does its settings' files. Rung by
// writing it again, a few bytes on C:: the free memory, in KB, as the
// ringer has just shown it, so the toolbar shows the same figure at once
// (PsiMemRungKb), and its own reading 2 s on.
_LIT(KPsiMemChangedFile, "C:\\System\\Data\\PsiMem.chg");

inline void PsiMemRing(RFs& aFs, TInt aKb = -1)
	{
	RFile f;
	TInt err = f.Replace(aFs, KPsiMemChangedFile, EFileWrite | EFileShareAny);
	if (err == KErrPathNotFound)
		{
		aFs.MkDirAll(KPsiMemChangedFile);
		err = f.Replace(aFs, KPsiMemChangedFile, EFileWrite | EFileShareAny);
		}
	if (err != KErrNone)
		return;
	TBuf8<16> b;
	b.Num(aKb >= 0 ? aKb : PsiMemFreeKb());
	f.Write(b);
	f.Close();
	}

// the figure the bell was rung with, -1 if none
inline TInt PsiMemRungKb(RFs& aFs)
	{
	RFile f;
	if (f.Open(aFs, KPsiMemChangedFile, EFileRead | EFileShareAny) != KErrNone)
		return -1;
	TBuf8<16> b;
	TInt err = f.Read(b);
	f.Close();
	TInt kb = -1;
	TLex8 lex(b);
	if (err != KErrNone || lex.Val(kb) != KErrNone || kb < 0)
		return -1;
	return kb;
	}

#endif
