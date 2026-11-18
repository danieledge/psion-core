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
// A program with no watcher installed loses nothing: PsiMemTell returns
// EFalse and the program carries on as before.

#ifndef __PSIMEM_H
#define __PSIMEM_H

#include <e32std.h>
#include <w32std.h>
#include <apgtask.h>

const TInt KPsiMemMsgUid = 0x01000BA0;
const TInt KPsiMemWatcherUid = 0x01000A9B;

// the levels (free RAM, bytes)
const TInt KPsiMemTidy = 3 * 1024 * 1024;       // under this: "trim"
const TInt KPsiMemLow = 1536 * 1024;            // under this: "sleep" to programs behind; the user is told once
const TInt KPsiMemVeryLow = 750 * 1024;         // under this: the dialog is offered

#define KPsiMemTrim   _L8("trim")
#define KPsiMemSleep  _L8("sleep")
#define KPsiMemDialog _L8("dialog")
#define KPsiMemShort  _L8("short")

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

#endif
