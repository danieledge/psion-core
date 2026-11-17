// PMCARD.H - the Contacts half of contacts sync (PsiSync; see
// core/mail/engine/card/card.h for the engine's half and its files)
//
// The engine keeps the server's cards in <dir>card\ (each as it came, and
// as the Psion's vCard 2.1). PmCardSync puts them into the Psion's Contacts
// database (C:\System\Data\Contacts.cdb, through Contacts' own model:
// CContactDatabase, shared with the Contacts program, which can stay open)
// with Contacts' own vCard reader, and hands back what changed there
// (push.txt, the changed contacts written out by Contacts' own vCard
// writer). The rules for each contact are the calendar's (calmerge.c):
// changed on both sides, both are kept; deletions carefully, both ways.
//
//   <dir>card\contacts.txt  "#PSICON1 TAB since (hex)", then per contact we
//                           look after: id TAB last modified (hex) TAB server
//                           hash TAB flags TAB href ("" = new, not yet on the server)
#ifndef __PMCARD_H
#define __PMCARD_H

#include <e32base.h>
#include <f32file.h>
#include "pmcal.h"        // MPmCalObserver, Clip

class PmCardSync
	{
public:
	// one pass: the server's cards into Contacts, Contacts' changes into
	// push.txt. aCopyExisting: the first time, send the Psion's own
	// contacts too. Returns KErrNone or why not; aSummary says what changed,
	// aPushed if there is something for the engine to send.
	static TInt Run(RFs& aFs, const TDesC& aDir, TBool aCopyExisting, MPmCalObserver& aObserver,
		TDes& aSummary, TBool& aPushed);
	// forgets the links between contacts and the server's cards
	static void Forget(RFs& aFs, const TDesC& aDir);
	};

#endif
