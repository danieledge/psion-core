// psipurge.h - purgeable caches (Part 2, P2-07): a cache in a global chunk
// that the memory watcher can empty from outside, at once, without the
// owner running (a busy engine cannot answer a "trim" message).
//
// The chunk's first page is its head (TPsiPurgeHead: a pin count, a purged
// flag); the cache is the rest, committed as the owner fills it. The owner
// pins the cache while it uses it; the watcher (PsiPurgeAll) takes the pin
// word with User::LockedDec only when it is 0, so a pinned cache is never
// touched, and then shrinks the chunk to its head (RChunk::Adjust): the
// pages are back with the Psion. At its next pin the owner finds Purged()
// and builds the cache again.
//
//   TPsiPurge c;  c.Create(_L("PsiPurgeWebPics"), 512 * 1024);   (KErrNone, or no cache)
//   TUint8* p = c.Pin(bytes);      0: not now (being purged, or no memory)
//   if (c.Fresh()) ... fill it ...
//   c.Unpin();
//   c.Close();
//
// One thread of the owner pins (the owner's own pins are not counted
// against each other). Names start with "PsiPurge" (PsiPurgeAll finds them
// so).
//
// Not yet proven on the Psion: that a process may Adjust a global chunk it
// has only opened (MEM-REVIEW P2-07 stage 1). If it may not, PsiPurgeAll
// frees nothing (Adjust returns an error) and no cache is harmed.
//
// Off switch: PSI_PURGE=0 at build time (the default): Create returns
// KErrNotSupported and the program keeps its cache as it does today.
#ifndef PSI_PURGE_H
#define PSI_PURGE_H
#include <e32std.h>
#ifndef PSI_PURGE
#define PSI_PURGE 0
#endif

const TUint32 KPsiPurgeMagic = 0x67725550;     // 'PUrg'
const TInt KPsiPurgeHead = 4096;
struct TPsiPurgeHead
	{
	TUint32 iMagic;
	TInt iPin;          // pins by the owner; -1 while the watcher purges
	TInt iPurged;       // the cache was emptied: build it again
	TInt iPurges;       // how often
	TUint32 iSpare[4];
	};

class TPsiPurge
	{
public:
	TPsiPurge() : iHead(0), iFresh(EFalse) {}
	TInt Create(const TDesC& aName, TInt aMax)
		{
#if PSI_PURGE
		TInt r = iChunk.CreateGlobal(aName, KPsiPurgeHead, KPsiPurgeHead + aMax);
		if (r != KErrNone)
			return r;
		iHead = (TPsiPurgeHead*)iChunk.Base();
		Mem::FillZ(iHead, sizeof(*iHead));
		iHead->iMagic = KPsiPurgeMagic;
		iHead->iPurged = 1;                     // (nothing in it yet)
		return KErrNone;
#else
		(void)aName; (void)aMax;
		return KErrNotSupported;
#endif
		}
	// the cache, with room for aBytes, pinned; 0 if not now
	TUint8* Pin(TInt aBytes)
		{
		iFresh = EFalse;
		if (!iHead)
			return 0;
		if (User::LockedInc(iHead->iPin) < 0)
			{
			User::LockedDec(iHead->iPin);        // the watcher has it
			return 0;
			}
		if (iHead->iPurged)
			{
			iFresh = ETrue;
			iHead->iPurged = 0;
			}
		if (iChunk.Size() < KPsiPurgeHead + aBytes && iChunk.Adjust(KPsiPurgeHead + aBytes) != KErrNone)
			{
			iHead->iPurged = 1;
			User::LockedDec(iHead->iPin);
			return 0;
			}
		return iChunk.Base() + KPsiPurgeHead;
		}
	// 1: the cache is empty (new, or purged): build it again
	TBool Fresh() const { return iFresh; }
	void Unpin()
		{
		if (iHead)
			User::LockedDec(iHead->iPin);
		}
	void Close()
		{
		if (iHead)
			iChunk.Close();
		iHead = 0;
		}
private:
	RChunk iChunk;
	TPsiPurgeHead* iHead;
	TBool iFresh;
	};

// The watcher's side: every unpinned cache emptied. Bytes given back.
inline TInt PsiPurgeAll()
	{
	TInt freed = 0;
	TFindChunk find(_L("*PsiPurge*"));
	TFullName name;
	while (find.Next(name) == KErrNone)
		{
		RChunk c;
		if (c.Open(find) != KErrNone)
			continue;
		TPsiPurgeHead* h = (TPsiPurgeHead*)c.Base();
		if (c.Size() >= KPsiPurgeHead && h->iMagic == KPsiPurgeMagic)
			{
			if (User::LockedDec(h->iPin) == 0)
				{
				TInt was = c.Size();
				if (was > KPsiPurgeHead && c.Adjust(KPsiPurgeHead) == KErrNone)
					{
					freed += was - KPsiPurgeHead;
					h->iPurged = 1;
					h->iPurges++;
					}
				}
			User::LockedInc(h->iPin);
			}
		c.Close();
		}
	return freed;
	}
#endif
