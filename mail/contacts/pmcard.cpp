// PMCARD.CPP - the Contacts half of contacts sync (see pmcard.h)
//
// One pass, synchronous (psisync.exe has no window to keep answering):
//   1. what became of the changes sent last time (pushed.txt)
//   2. every contact we look after: the facts to cal_merge (calmerge.c, the
//      calendar's rules, tested on a PC), and what it says done
//   3. the server's new cards -> new contacts
//   4. contacts made or changed on the Psion since sync was turned on ->
//      the server (Contacts' own vCard writer, then the engine merges them
//      into the server's card)
// Then the links are saved (contacts.txt) and the changes left in push.txt.

#include <e32std.h>
#include <s32file.h>
#include <cntdb.h>
#include <cntitem.h>
#include <cntdef.h>
#include <vuid.h>
#include "pmcard.h"
extern "C" {
#include "calmerge.h"
int vc_to_psion(const char *in, int len, char *out, int max);     // card.h
}

_LIT8(KMapMagic, "#PSICON1");
const TUint KFlagPending = CM_PENDING;
const TUint KFlagDeleting = CM_DELETING;
const TUint KFlagForce = CM_FORCE;

struct TPcMap
	{
	TInt32 iId;                // TContactItemId
	TInt64 iChanged;           // its LastModified when we last looked
	TUint32 iHash;             // the server card's (index.txt), 0 = take it as it is
	TUint iFlags;
	HBufC8* iKey;              // href ("" = new, not yet on the server)
	TBool iSeen;
	TBool iGone;               // (MappedL) deleted in Contacts
	TInt64 iNow;               // (MappedL) its LastModified now
	};

// ---------------------------------------------------------------- helpers

static TPtrC8 Field(const TDesC8& aLine, TInt aIndex)
	{
	TPtrC8 rest(aLine);
	for (TInt i = 0; ; i++)
		{
		TInt t = rest.Locate('\t');
		TPtrC8 f = t < 0 ? rest : rest.Left(t);
		if (i == aIndex) return f;
		if (t < 0) return TPtrC8();
		rest.Set(rest.Mid(t + 1));
		}
	}

static TUint32 Hex(const TDesC8& aText)
	{
	TUint v = 0;
	TLex8 l(aText);
	l.Val(v, EHex);
	return v;
	}

static TInt64 Hex64(const TDesC8& aText)
	{
	TUint hi = 0, lo = 0;
	if (aText.Length() >= 16)
		{
		TLex8 a(aText.Left(8)), b(aText.Mid(8, 8));
		a.Val(hi, EHex);
		b.Val(lo, EHex);
		}
	return TInt64((TInt)hi, lo);
	}

static void AppendHex64(TDes8& aOut, const TInt64& aValue)
	{
	aOut.AppendNumFixedWidth((TUint)aValue.High(), EHex, 8);
	aOut.AppendNumFixedWidth(aValue.Low(), EHex, 8);
	}

// the lines of a file (each without its line end), or NULL
static CDesC8ArrayFlat* ReadLinesL(RFs& aFs, const TDesC& aPath)
	{
	RFile f;
	if (f.Open(aFs, aPath, EFileRead | EFileShareReadersOnly) != KErrNone)
		return NULL;
	CleanupClosePushL(f);
	TInt size = 0;
	User::LeaveIfError(f.Size(size));
	HBufC8* buf = HBufC8::NewLC(size + 1);
	TPtr8 p = buf->Des();
	User::LeaveIfError(f.Read(p, size));
	CDesC8ArrayFlat* lines = new(ELeave) CDesC8ArrayFlat(32);
	CleanupStack::PushL(lines);
	TPtrC8 rest(*buf);
	while (rest.Length())
		{
		TInt n = rest.Locate('\n');
		TPtrC8 line = n < 0 ? rest : rest.Left(n);
		if (line.Length() && line[line.Length() - 1] == '\r') line.Set(line.Left(line.Length() - 1));
		if (line.Length()) lines->AppendL(line);
		rest.Set(n < 0 ? TPtrC8() : rest.Mid(n + 1));
		}
	CleanupStack::Pop();                 // lines
	CleanupStack::PopAndDestroy(2);      // buf, f
	return lines;
	}

static void WriteLinesL(RFs& aFs, const TDesC& aPath, const CDesC8Array& aLines)
	{
	TFileName tmp(aPath);
	tmp.Append('~');
	RFile f;
	User::LeaveIfError(f.Replace(aFs, tmp, EFileWrite));
	CleanupClosePushL(f);
	for (TInt i = 0; i < aLines.Count(); i++)
		{
		User::LeaveIfError(f.Write(aLines[i]));
		User::LeaveIfError(f.Write(_L8("\n")));
		}
	User::LeaveIfError(f.Flush());
	CleanupStack::PopAndDestroy();
	User::LeaveIfError(aFs.Replace(tmp, aPath));
	}

// ---------------------------------------------------------------- the pass

class CPcPass : public CBase
	{
public:
	CPcPass(RFs& aFs, MPmCalObserver& aObserver) : iFs(aFs), iObserver(aObserver) {}
	~CPcPass();
	void RunL(const TDesC& aDir, TBool aCopyExisting);
	TInt iAdded, iChanged, iRemoved, iSent, iFailed, iKept;
	TBool iGuarded;
	TBool iGuardedOut;         // most linked contacts gone from Contacts at once: no deletions sent
private:
	void Path(const TDesC& aName, TDes& aPath) const { aPath = iDir; aPath.Append(aName); }
	void LoadL(TBool aCopyExisting);
	void ReadPushedL();
	void MappedL();
	void NewFromServerL();
	void NewOnPsionL();
	void SaveL();
	TInt FindIndex(const TDesC8& aHref) const;
	TBool Writable(const TDesC8& aIndexLine) const;
	void V21Of(const TDesC8& aIndexLine, TDes& aPath) const;
	TContactItemId ImportL(const TDesC8& aIndexLine);
	TInt64 Modified(TContactItemId aId, TBool& aGone);
	void PushL(const TDesC8& aLine);
	void PushContactL(TChar aOp, TContactItemId aId, const TDesC8& aIndexLine);
	void AddMapL(TContactItemId aId, const TDesC8& aKey, TUint32 aHash, TUint aFlags);
	void Log(const TDesC8& aText) { iObserver.CalLog(aText); }
private:
	RFs& iFs;
	MPmCalObserver& iObserver;
	TFileName iDir;
	CContactDatabase* iDb;
	CArrayFixFlat<TPcMap>* iMap;
	CDesC8ArrayFlat* iIndex;   // index.txt's lines
	CDesC8ArrayFlat* iBooks;   // books.txt's lines
	CDesC8ArrayFlat* iPush;
	TInt64 iSince;
	TBuf8<12> iDefaultBook;
	TBool iPushedRead;
	};

CPcPass::~CPcPass()
	{
	delete iDb;
	if (iMap)
		{
		for (TInt i = 0; i < iMap->Count(); i++) delete (*iMap)[i].iKey;
		delete iMap;
		}
	delete iIndex;
	delete iBooks;
	delete iPush;
	}

TInt CPcPass::FindIndex(const TDesC8& aHref) const
	{
	if (!aHref.Length() || !iIndex) return -1;
	for (TInt i = 0; i < iIndex->Count(); i++)
		if (Field((*iIndex)[i], 1) == aHref) return i;
	return -1;
	}

TBool CPcPass::Writable(const TDesC8& aIndexLine) const
	{
	TPtrC8 book = Field(aIndexLine, 0);
	for (TInt i = 0; iBooks && i < iBooks->Count(); i++)
		if (Field((*iBooks)[i], 0) == book) return Field((*iBooks)[i], 4).Locate('W') >= 0;
	return EFalse;
	}

void CPcPass::V21Of(const TDesC8& aIndexLine, TDes& aPath) const
	{
	TPtrC8 file = Field(aIndexLine, 3);          // cXXXXXXXX.vcf
	TBuf<20> name;
	name.Copy(file.Left(Min(file.Length(), 16)));
	TInt dot = name.Locate('.');
	if (dot >= 0) name.SetLength(dot);
	name.Append(_L(".v21"));
	Path(name, aPath);
	}

void CPcPass::AddMapL(TContactItemId aId, const TDesC8& aKey, TUint32 aHash, TUint aFlags)
	{
	TPcMap m;
	TBool gone;
	m.iId = aId;
	m.iChanged = aId != KNullContactId ? Modified(aId, gone) : TInt64(0);
	m.iHash = aHash;
	m.iFlags = aFlags;
	m.iSeen = ETrue;
	m.iKey = aKey.AllocLC();
	iMap->AppendL(m);
	CleanupStack::Pop();
	}

TInt64 CPcPass::Modified(TContactItemId aId, TBool& aGone)
	{
	CContactItem* item = NULL;
	TRAPD(err, item = iDb->ReadContactL(aId));
	if (err == KErrNoMemory)
		User::Leave(err);
	aGone = err != KErrNone || !item || item->IsDeleted();
	TInt64 t = item ? item->LastModified().Int64() : TInt64(0);
	delete item;
	return t;
	}

TContactItemId CPcPass::ImportL(const TDesC8& aIndexLine)
	{
	TFileName path;
	V21Of(aIndexLine, path);
	RFileReadStream r;
	if (r.Open(iFs, path, EFileRead | EFileShareReadersOnly) != KErrNone)
		return KNullContactId;
	CleanupClosePushL(r);
	TBool ok = EFalse;
	CArrayPtr<CContactItem>* items = iDb->ImportContactsL(TUid::Uid(KVersitEntityUidVCard), r, ok,
		CContactDatabase::EImportSingleContact);
	TContactItemId id = KNullContactId;
	if (items)
		{
		if (items->Count()) id = (*items)[0]->Id();
		items->ResetAndDestroy();
		delete items;
		}
	CleanupStack::PopAndDestroy();       // r
	return id;
	}

// changes not yet sent are kept, unless this pass has a newer one for that contact
void CPcPass::PushL(const TDesC8& aLine)
	{
	TPtrC8 key = Field(aLine, 1);
	for (TInt i = 0; i < iPush->Count(); i++)
		if (Field((*iPush)[i], 1) == key) { iPush->Delete(i); break; }
	iPush->AppendL(aLine);
	iSent++;
	}

// a contact written out by Contacts' own vCard writer (o<id>.v21), and its line in push.txt
void CPcPass::PushContactL(TChar aOp, TContactItemId aId, const TDesC8& aIndexLine)
	{
	TBuf8<400> line;
	line.Append(aOp);
	line.Append(_L8("\tc"));
	line.AppendNum((TInt)aId);
	line.Append('\t');
	if (aOp == 'D')
		{
		line.Append(Field(aIndexLine, 1));
		line.Append('\t');
		line.Append(Field(aIndexLine, 2));
		PushL(line);
		return;
		}
	TBuf<20> name;
	name.Format(_L("o%d.v21"), (TInt)aId);
	TFileName path;
	Path(name, path);
	CContactIdArray* ids = CContactIdArray::NewLC();
	ids->AddL(aId);
	RFileWriteStream w;
	User::LeaveIfError(w.Replace(iFs, path, EFileWrite));
	CleanupClosePushL(w);
	iDb->ExportSelectedContactsL(TUid::Uid(KVersitEntityUidVCard), *ids, w, CContactDatabase::EExcludeUid);
	w.CommitL();
	CleanupStack::PopAndDestroy(2);      // w, ids
	TBuf8<20> name8;
	name8.Copy(name);
	if (aOp == 'N')
		{
		line.Append(iDefaultBook);
		line.Append('\t');
		line.Append(name8);
		// the card's UID on the server: the same if this is sent again
		line.Append(_L8("\tpsion-c-"));
		AppendHex64(line, iSince);
		line.Append('-');
		line.AppendNum((TInt)aId);
		}
	else
		{
		line.Append(Field(aIndexLine, 1));
		line.Append('\t');
		line.Append(Field(aIndexLine, 2));
		line.Append('\t');
		line.Append(name8);
		}
	PushL(line);
	}

void CPcPass::LoadL(TBool aCopyExisting)
	{
	TFileName path;
	iMap = new(ELeave) CArrayFixFlat<TPcMap>(32);
	Path(_L("index.txt"), path);
	iIndex = ReadLinesL(iFs, path);
	if (!iIndex) iIndex = new(ELeave) CDesC8ArrayFlat(4);
	for (TInt k = iIndex->Count() - 1; k >= 0; k--)
		if ((*iIndex)[k].Length() && (*iIndex)[k][0] == '#') iIndex->Delete(k);
	Path(_L("books.txt"), path);
	iBooks = ReadLinesL(iFs, path);
	for (TInt b = 0; iBooks && b < iBooks->Count(); b++)
		if ((*iBooks)[b][0] != '#' && Field((*iBooks)[b], 4).Locate('D') >= 0)
			iDefaultBook = Field((*iBooks)[b], 0).Left(Min(Field((*iBooks)[b], 0).Length(), 12));
	Path(_L("push.txt"), path);
	iPush = ReadLinesL(iFs, path);
	if (!iPush) iPush = new(ELeave) CDesC8ArrayFlat(8);
	Path(_L("contacts.txt"), path);
	CDesC8ArrayFlat* lines = ReadLinesL(iFs, path);
	TBool newFile = ETrue;
	if (lines)
		{
		CleanupStack::PushL(lines);
		for (TInt i = 0; i < lines->Count(); i++)
			{
			TPtrC8 l = (*lines)[i];
			if (l[0] == '#')
				{
				if (Field(l, 0) == KMapMagic) { iSince = Hex64(Field(l, 1)); newFile = EFalse; }
				continue;
				}
			if (newFile) break;
			TPcMap m;
			TLex8 lx(Field(l, 0));
			TInt id = 0;
			lx.Val(id);
			m.iId = id;
			m.iChanged = Hex64(Field(l, 1));
			m.iHash = Hex(Field(l, 2));
			TLex8 lf(Field(l, 3));
			TUint flags = 0;
			lf.Val(flags);
			m.iFlags = flags;
			m.iSeen = EFalse;
			m.iKey = Field(l, 4).AllocLC();
			iMap->AppendL(m);
			CleanupStack::Pop();
			}
		CleanupStack::PopAndDestroy();
		}
	if (newFile)
		{
		TTime now;
		now.HomeTime();
		iSince = aCopyExisting ? TInt64(0) : now.Int64();
		}
	}

void CPcPass::ReadPushedL()
	{
	TFileName path;
	Path(_L("pushed.txt"), path);
	CDesC8ArrayFlat* lines = ReadLinesL(iFs, path);
	if (!lines)
		return;
	CleanupStack::PushL(lines);
	for (TInt k = 0; k < lines->Count(); k++)
		{
		TPtrC8 line = (*lines)[k];
		TPtrC8 key = Field(line, 0), what = Field(line, 1), href = Field(line, 2);
		if (key.Length() < 2 || key[0] != 'c') continue;
		TLex8 lx(key.Mid(1));
		TInt id = 0;
		lx.Val(id);
		TBool ok = what == _L8("ok");
		for (TInt i = 0; i < iMap->Count(); i++)
			{
			TPcMap& m = (*iMap)[i];
			if (m.iId != id) continue;
			if (m.iFlags & KFlagPending)
				{
				if (ok)
					{
					delete m.iKey;
					m.iKey = NULL;
					m.iKey = href.AllocL();
					m.iHash = 0;          // the server's copy is ours: taken as it is
					m.iFlags &= ~KFlagPending;
					}
				else { delete m.iKey; iMap->Delete(i); iFailed++; }
				}
			else if (m.iFlags & KFlagDeleting)
				{
				if (ok || what == _L8("gone")) { delete m.iKey; iMap->Delete(i); }
				else { m.iFlags = KFlagForce; iFailed++; }
				}
			else if (!ok)
				{
				m.iFlags |= KFlagForce;      // put the server's version back
				if (what != _L8("gone")) iFailed++;
				}
			break;
			}
		}
	CleanupStack::PopAndDestroy();
	iPushedRead = ETrue;
	}

void CPcPass::MappedL()
	{
	// the guard, both ways (calmerge.c cal_guard): most linked contacts gone
	// from the server at once -> none deleted here; most gone from Contacts
	// (the file replaced or emptied apart from the links) -> no deletions
	// sent, the links kept
	TInt linked = 0, missing = 0, gone = 0, i;
	for (i = 0; i < iMap->Count(); i++)
		{
		TPcMap& m = (*iMap)[i];
		m.iGone = EFalse;
		m.iNow = m.iId != KNullContactId ? Modified(m.iId, m.iGone) : TInt64(0);
		if (m.iFlags & (KFlagPending | KFlagDeleting)) continue;
		linked++;
		if (FindIndex(*m.iKey) < 0) missing++;
		if (m.iGone && FindIndex(*m.iKey) >= 0) gone++;
		}
	iGuarded = cal_guard(linked, missing);
	iGuardedOut = cal_guard(linked, gone);
	for (i = 0; i < iMap->Count(); )
		{
		TPcMap& m = (*iMap)[i];
		if (m.iFlags & KFlagPending) { i++; continue; }
		TInt ix = FindIndex(*m.iKey);
		if (ix >= 0) m.iSeen = ETrue;
		TBool gone = m.iGone;
		TInt64 changed = m.iNow;
		CmFacts f;
		Mem::FillZ(&f, sizeof(f));
		f.flags = m.iFlags;
		f.on_server = ix >= 0;
		f.gone = gone;
		f.local_changed = !gone && changed != m.iChanged;
		f.in_window = 1;
		TUint32 hash = 0;
		TPtrC8 line;
		if (ix >= 0)
			{
			line.Set((*iIndex)[ix]);
			hash = Hex(Field(line, 4));
			TFileName v21;
			V21Of(line, v21);
			TEntry e;
			f.readable = iFs.Entry(v21, e) == KErrNone;
			f.writable = Writable(line);
			f.no_baseline = m.iHash == 0;
			f.server_changed = hash != m.iHash;
			}
		TInt action = cal_merge(&f, CM_OPT_KEEPBOTH);
		if (action == CM_DELETE_LOCAL && iGuarded)
			action = CM_SKIP;
		if (action == CM_PUSH_DELETE && iGuardedOut)
			action = CM_SKIP;
		if (f.on_server && f.readable && !f.gone && f.no_baseline && !(m.iFlags & KFlagForce))
			m.iHash = hash;
		TBuf8<80> log;
		log.Format(_L8("contacts: %d %s"), (TInt)m.iId, cal_merge_name(action));
		Log(log);
		switch (action)
			{
		case CM_UNLINK:
			delete m.iKey;
			iMap->Delete(i);
			continue;
		case CM_DELETE_LOCAL:
			TRAPD(err, iDb->DeleteContactL(m.iId));
			(void)err;
			iRemoved++;
			delete m.iKey;
			iMap->Delete(i);
			continue;
		case CM_PUSH_DELETE:
			PushContactL('D', m.iId, line);
			m.iFlags = KFlagDeleting;
			break;
		case CM_READD:
			{
			TContactItemId id = ImportL(line);
			if (id != KNullContactId)
				{
				m.iId = id;
				TBool g;
				m.iChanged = Modified(id, g);
				m.iHash = hash;
				m.iFlags = 0;
				iAdded++;
				}
			}
			break;
		case CM_KEEP_BOTH:
		case CM_UPDATE_LOCAL:
			{
			// the server's card in afresh (Contacts' reader); the old contact
			// deleted - or, changed on the Psion too, kept beside it, unlinked
			// (and so sent as new)
			TContactItemId id = ImportL(line);
			if (id == KNullContactId) break;
			TContactItemId old = m.iId;
			m.iId = id;
			TBool g;
			m.iChanged = Modified(id, g);
			m.iHash = hash;
			m.iFlags = 0;
			if (action == CM_UPDATE_LOCAL)
				{
				TRAPD(err2, iDb->DeleteContactL(old));
				(void)err2;
				iChanged++;
				}
			else
				iKept++;
			}
			break;
		case CM_PUSH_CHANGE:
			PushContactL('M', m.iId, line);
			m.iChanged = changed;
			break;
		default:
			break;
			}
		i++;
		}
	}

void CPcPass::NewFromServerL()
	{
	for (TInt k = 0; k < iIndex->Count(); k++)
		{
		TPtrC8 line = (*iIndex)[k];
		TPtrC8 href = Field(line, 1);
		TBool known = EFalse;
		for (TInt i = 0; i < iMap->Count() && !known; i++)
			if (*(*iMap)[i].iKey == href) known = ETrue;
		if (known) continue;
		TContactItemId id = ImportL(line);
		if (id == KNullContactId) continue;
		AddMapL(id, href, Hex(Field(line, 4)), 0);
		iAdded++;
		}
	}

void CPcPass::NewOnPsionL()
	{
	if (!iDefaultBook.Length())
		{
		Log(_L8("contacts: no address book for new contacts (books.txt has none marked D)"));
		return;
		}
	CContactIdArray* ids = iDb->ContactsChangedSinceL(TTime(iSince));
	CleanupStack::PushL(ids);
	for (TInt k = 0; k < ids->Count(); k++)
		{
		TContactItemId id = (*ids)[k];
		TBool known = EFalse;
		for (TInt i = 0; i < iMap->Count() && !known; i++)
			if ((*iMap)[i].iId == id) known = ETrue;
		if (known) continue;
		CContactItem* item = NULL;
		TRAPD(err, item = iDb->ReadContactL(id));
		if (err != KErrNone || !item) continue;
		TBool card = item->Type() == KUidContactCard && !item->IsDeleted();
		delete item;
		if (!card) continue;
		PushContactL('N', id, KNullDesC8);
		AddMapL(id, KNullDesC8, 0, KFlagPending);
		}
	CleanupStack::PopAndDestroy();
	}

void CPcPass::SaveL()
	{
	CDesC8ArrayFlat* out = new(ELeave) CDesC8ArrayFlat(32);
	CleanupStack::PushL(out);
	TBuf8<300> l(KMapMagic);
	l.Append('\t');
	AppendHex64(l, iSince);
	out->AppendL(l);
	for (TInt i = 0; i < iMap->Count(); i++)
		{
		const TPcMap& m = (*iMap)[i];
		l.Zero();
		l.AppendNum((TInt)m.iId);
		l.Append('\t');
		AppendHex64(l, m.iChanged);
		l.Append('\t');
		l.AppendNumFixedWidth((TUint)m.iHash, EHex, 8);
		l.Append('\t');
		l.AppendNum((TInt)m.iFlags);
		l.Append('\t');
		l.Append(m.iKey->Left(Min(m.iKey->Length(), 250)));
		out->AppendL(l);
		}
	TFileName path;
	Path(_L("contacts.txt"), path);
	WriteLinesL(iFs, path, *out);
	CleanupStack::PopAndDestroy();
	Path(_L("push.txt"), path);
	if (iPush->Count())
		WriteLinesL(iFs, path, *iPush);
	else
		iFs.Delete(path);
	if (iPushedRead)
		{
		Path(_L("pushed.txt"), path);
		iFs.Delete(path);
		}
	}

void CPcPass::RunL(const TDesC& aDir, TBool aCopyExisting)
	{
	iDir = aDir;
	iFs.MkDirAll(iDir);
	iObserver.CalProgress(_L("Opening Contacts\x85"));
	iDb = CContactDatabase::OpenL();
	LoadL(aCopyExisting);
	ReadPushedL();
	iObserver.CalProgress(_L("Updating Contacts\x85"));
	MappedL();
	NewFromServerL();
	NewOnPsionL();
	SaveL();
	}

// ---------------------------------------------------------------- PmCardSync

TInt PmCardSync::Run(RFs& aFs, const TDesC& aDir, TBool aCopyExisting, MPmCalObserver& aObserver,
	TDes& aSummary, TBool& aPushed)
	{
	aPushed = EFalse;
	aSummary.Zero();
	CPcPass* pass = new CPcPass(aFs, aObserver);
	if (!pass)
		return KErrNoMemory;
	TRAPD(err, pass->RunL(aDir, aCopyExisting));
	if (err == KErrNone)
		{
		// (built whole, then clipped to the caller's room: every part at once
		// is about 140 letters, and a shorter buffer was USER 11)
		TBuf<200> sum;
		if (pass->iAdded || pass->iChanged || pass->iRemoved)
			{
			sum = _L("Contacts: ");
			TBool any = EFalse;
			if (pass->iAdded) { sum.AppendFormat(_L("%d new"), pass->iAdded); any = ETrue; }
			if (pass->iChanged) { if (any) sum.Append(_L(", ")); sum.AppendFormat(_L("%d changed"), pass->iChanged); any = ETrue; }
			if (pass->iRemoved) { if (any) sum.Append(_L(", ")); sum.AppendFormat(_L("%d removed"), pass->iRemoved); }
			}
		else
			sum = _L("Contacts up to date");
		if (pass->iSent)
			sum.AppendFormat(_L("; sending %d"), pass->iSent);
		if (pass->iKept)
			sum.AppendFormat(_L("; %d changed on both: the Psion's kept"), pass->iKept);
		if (pass->iGuarded)
			sum.Append(_L("; many gone from the server - none removed"));
		if (pass->iGuardedOut)
			sum.Append(_L("; many gone from Contacts - none deleted on the server"));
		aSummary.Copy(sum.Left(Min(sum.Length(), aSummary.MaxLength())));
		aPushed = pass->iSent > 0;
		}
	else if (err == KErrNotFound || err == KErrPathNotFound)
		aSummary = _L("Contacts not synced - the Contacts file was not found (open Contacts once)");
	else
		{
		// words, the number to the log (review R105; psierr.h's table)
		switch (err)
			{
		case KErrNoMemory: aSummary = _L("Contacts not synced - not enough memory"); break;
		case KErrDiskFull: aSummary = _L("Contacts not synced - the disk is full"); break;
		case KErrNotReady: aSummary = _L("Contacts not synced - the card is not in"); break;
		case KErrInUse:
		case KErrLocked: aSummary = _L("Contacts not synced - Contacts is busy (close it and sync again)"); break;
		case KErrAccessDenied: aSummary = _L("Contacts not synced - the Contacts file cannot be written"); break;
		case KErrCorrupt: aSummary = _L("Contacts not synced - the file is damaged"); break;
		case KErrArgument:
		case KErrOverflow:
		case KErrNotSupported: aSummary = _L("Contacts not synced - a contact from the server could not be read"); break;
		default: aSummary = _L("Contacts not synced - for an unknown reason (the number is in the log)"); break;
			}
		TBuf8<40> l;
		l.Format(_L8("contacts: the pass failed (%d)"), err);
		aObserver.CalLog(l);
		}
	delete pass;
	return err;
	}

void PmCardSync::Forget(RFs& aFs, const TDesC& aDir)
	{
	TFileName p(aDir);
	p.Append(_L("contacts.txt"));
	aFs.Delete(p);
	p = aDir;
	p.Append(_L("push.txt"));
	aFs.Delete(p);
	}

// ---------------------------------------------------------------- a .vcf file

static TInt FindF(const TDesC8& aIn, TInt aFrom, const TDesC8& aWhat)
	{
	TInt at = aIn.Mid(aFrom).FindF(aWhat);
	return at < 0 ? -1 : aFrom + at;
	}

static void ImportFileL(RFs& aFs, const TDesC& aVcf, const TDesC& aTemp, TInt& aCount, TInt& aSkipped)
	{
	RFile f;
	User::LeaveIfError(f.Open(aFs, aVcf, EFileRead | EFileShareReadersOnly));
	CleanupClosePushL(f);
	TInt size = 0;
	User::LeaveIfError(f.Size(size));
	if (size > 1024 * 1024)
		User::Leave(KErrTooBig);
	HBufC8* in = HBufC8::NewLC(size);
	TPtr8 ip = in->Des();
	User::LeaveIfError(f.Read(ip));
	const TInt KOut = 16384;
	HBufC8* out = HBufC8::NewLC(KOut);
	CContactDatabase* db = CContactDatabase::OpenL();
	CleanupStack::PushL(db);
	TInt at = 0;
	for (;;)
		{
		TInt b = FindF(ip, at, _L8("BEGIN:VCARD"));
		if (b < 0) break;
		TInt e = FindF(ip, b, _L8("END:VCARD"));
		if (e < 0) { aSkipped++; break; }
		e += 9;
		at = e;
		TInt n = vc_to_psion((const char*)ip.Ptr() + b, e - b, (char*)out->Des().Ptr(), KOut);
		if (n <= 0) { aSkipped++; continue; }
		out->Des().SetLength(n);
		RFile t;
		User::LeaveIfError(t.Replace(aFs, aTemp, EFileWrite));
		TInt werr = t.Write(*out);
		t.Close();
		User::LeaveIfError(werr);
		RFileReadStream r;
		User::LeaveIfError(r.Open(aFs, aTemp, EFileRead));
		CleanupClosePushL(r);
		TBool ok = EFalse;
		CArrayPtr<CContactItem>* items = db->ImportContactsL(TUid::Uid(KVersitEntityUidVCard), r, ok,
			CContactDatabase::EImportSingleContact);
		if (items && items->Count()) aCount++; else aSkipped++;
		if (items) { items->ResetAndDestroy(); delete items; }
		CleanupStack::PopAndDestroy();   // r
		}
	aFs.Delete(aTemp);
	CleanupStack::PopAndDestroy(4);      // db, out, in, f
	}

TInt PmCardSync::ImportFile(RFs& aFs, const TDesC& aVcf, const TDesC& aTemp, TInt& aCount, TInt& aSkipped)
	{
	aCount = aSkipped = 0;
	TRAPD(err, ImportFileL(aFs, aVcf, aTemp, aCount, aSkipped));
	return err;
	}
