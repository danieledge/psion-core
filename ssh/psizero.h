// psizero.h - folders made on a card that a computer's disk check reads right
//
// EPOC's FAT code does not clear a new folder's cluster: it writes the "."
// and ".." entries and an end marker and leaves the rest of the cluster as
// it was, the bytes of a file deleted long ago. EPOC stops at the marker;
// macOS's and Linux's checkers read on, report the leftovers as bad and
// cross-linked entries, and a "repair" can delete the folder (Coda's
// review R5, 19 November 2026). So on a card (not C: or Z:) the folders to
// make are made after a file of zeros has been written and deleted where
// the first of them goes: EPOC gives the first free clusters out again,
// the new folders' among them, which then hold zeros.
//
// The cost (20 November 2026). EPOC's FAT code reads the whole FAT each
// time it gives out clusters (once for each write that needs new ones) and
// when it frees them. The zeros were 32 KB for each folder in 64 writes of
// 512 bytes: on a card of 512-byte clusters (a 16 or 32 MB card) 64 reads
// of the FAT for each folder, minutes at a program's first start before its
// window was drawn. Now: one file for all the folders to make, as big as
// they need (a cluster each and one more, in case a name needs a new
// cluster of its parent), the cluster size measured on the card: three
// reads of the FAT in all, then one for each folder as before.
//
// Used by psikeep.h (PsiMail, PsiTerm, PsiWeb, PsiSync, Coda's Clips) and
// Coda's psimkdir.h. Header only (efsrv.lib); nothing leaves; no writable
// static data (ER5).
#ifndef __PSIZERO_H
#define __PSIZERO_H
#include <e32std.h>
#include <f32file.h>

const TInt KPsiZeroClusterMax = 32 * 1024;   // a FAT card's largest cluster

// aClusters of the card's clusters of zeros written to aFolder\~PsiZero.tmp
// and deleted (an error ignored: the folders are made all the same). The
// cluster size: what the first 512 bytes took from the free space; if that
// cannot be read, 32 KB in all, as before.
static void PsiZeros(RFs& aFs, const TDesC& aFolder, TInt aClusters)
	{
	TInt drive;
	if (aFolder.Length() < 3 || RFs::CharToDrive(aFolder[0], drive) != KErrNone)
		return;
	TFileName tmp(aFolder);
	tmp.Append(_L("~PsiZero.tmp"));
	TInt size = KPsiZeroClusterMax;
	HBufC8* z = HBufC8::New(size);
	if (!z)
		{
		size = 512;                         // (short of memory: 512 at a time)
		if ((z = HBufC8::New(size)) == NULL)
			return;
		}
	TPtr8 zp(z->Des());
	zp.FillZ(size);
	RFile f;
	if (f.Replace(aFs, tmp, EFileWrite) != KErrNone)
		{
		delete z;
		return;
		}
	TVolumeInfo v;
	TBool known = aFs.Volume(v, drive) == KErrNone;
	TInt64 before(0);
	if (known)
		before = v.iFree;
	TInt err = f.Write(zp.Left(512));
	if (err == KErrNone)
		err = f.Flush();
	TInt want = KPsiZeroClusterMax;
	if (known && err == KErrNone && aFs.Volume(v, drive) == KErrNone)
		{
		TInt64 took = before - v.iFree;
		TUint c = took.Low();
		if (took.High() == 0 && c >= 512 && c <= (TUint)KPsiZeroClusterMax && (c & (c - 1)) == 0)
			want = aClusters * (TInt)c;
		}
	for (TInt done = 512; err == KErrNone && done < want; )
		{
		TInt n = Min(size, want - done);
		err = f.Write(zp.Left(n));
		done += n;
		}
	f.Flush();
	f.Close();
	aFs.Delete(tmp);
	delete z;
	}

// the folders of aPath made ("D:\Coda\Logs\" or a file's path: what comes
// after the last '\' is left out, as RFs::MkDirAll does), on a card after
// the zeros -> KErrNone, KErrAlreadyExists when all of them were there, or
// the error of the folder that could not be made
static TInt PsiMkDirsClean(RFs& aFs, const TDesC& aPath)
	{
	if (aPath.Length() < 3 || aPath[1] != ':' || aPath[2] != '\\')
		return aFs.MkDirAll(aPath);
	TChar drive(aPath[0]);
	drive.UpperCase();
	if (drive == 'C' || drive == 'Z')
		return aFs.MkDirAll(aPath);         // (RAM and ROM: nothing to clear)
	TEntry e;
	TInt last = aPath.LocateReverse('\\');
	if (last > 2 && aFs.Entry(aPath.Left(last), e) == KErrNone)
		return KErrAlreadyExists;             // (all there: one look)
	TInt from = -1, missing = 0;
	for (TInt i = 3; i < aPath.Length(); i++)
		{
		if (aPath[i] != '\\')
			continue;
		if (from < 0)
			{
			if (aFs.Entry(aPath.Left(i), e) == KErrNone)
				continue;                     // (there already)
			from = i;                         // (this one and every one below it)
			}
		missing++;
		}
	if (!missing)
		return KErrAlreadyExists;
	TInt p = from - 1;                        // the first one's parent
	while (p > 2 && aPath[p] != '\\')
		p--;
	PsiZeros(aFs, aPath.Left(p + 1), missing + 1);
	for (TInt j = from; j < aPath.Length(); j++)
		{
		if (aPath[j] != '\\')
			continue;
		TInt err = aFs.MkDir(aPath.Left(j + 1));
		if (err != KErrNone && err != KErrAlreadyExists)
			return err;
		}
	return KErrNone;
	}

#endif
