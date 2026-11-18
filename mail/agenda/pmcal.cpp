// PMCAL.CPP - the calendar engine's Agenda half (see pmcal.h): PsiMail's
// app/pmcal.cpp until it moved to psion-core for PsiSync too
//
// One pass, a few entries at a time so the app stays responsive:
//   1. what became of the changes sent last time (pushed.txt)
//   2. every entry we look after: changed on the server? changed or deleted
//      on the Psion? (server wins when both changed)
//   3. new server events -> new Agenda entries
//   4. new Psion entries (made since sync was turned on) -> the server
// Then the links are saved (agenda.txt) and the changes for the server are
// left in push.txt; the app asks the engine to send them.

#include <e32std.h>
#include <s32file.h>
#include <txtrich.h>
#include <txtfmlyr.h>
#include <agmmodel.h>
#include <agclient.h>
#include <agmsiter.h>
#include <agmentry.h>
#include <agmtodos.h>
#include <agmrptd.h>
#include <agmexcpt.h>
#include <agmdate.h>
#include "pmcal.h"
extern "C" {
#include "calmerge.h"         // core/mail/engine/cal: the rules for each entry
#include "calrpt.h"           // cal_rrule
}

_LIT(KMapFile, "agenda.txt");
_LIT(KEventsFile, "events.txt");
_LIT(KPushFile, "push.txt");
_LIT(KPushedFile, "pushed.txt");
_LIT(KCalsFile, "calendars.txt");
_LIT8(KMapMagic, "#PSIAGN1");

const TUint KMapPending = 1;       // made on the Psion, sent, not yet confirmed
const TUint KMapDeleting = 2;      // deleted on the Psion, deletion sent
const TUint KMapForce = 4;         // the server refused a change: put its version back
const TUint KMapSeries = 8;        // (KPmCalOptRepeats) a repeating Agenda entry, the whole of an event
                                   // on the server: key "href|*", hash of its etag

const TInt KStep = 6;              // entries per step

// ---------------------------------------------------------------- helpers

static TUint32 Fnv(const TDesC8& aText)
	{
	TUint32 h = 2166136261u;
	for (TInt i = 0; i < aText.Length(); i++)
		{
		h ^= aText[i];
		h *= 16777619u;
		}
	return h;
	}

static TInt Num(const TDesC8& aText, TInt aPos, TInt aLen)
	{
	TInt v = 0;
	for (TInt i = aPos; i < aPos + aLen && i < aText.Length(); i++)
		{
		TInt c = aText[i];
		if (c < '0' || c > '9') return v;
		v = v * 10 + c - '0';
		}
	return v;
	}

// "YYYYMMDDHHMM" -> TTime
static TTime ParseTime(const TDesC8& aText)
	{
	if (aText.Length() < 8)
		return Time::NullTTime();
	TInt y = Num(aText, 0, 4), m = Num(aText, 4, 2), d = Num(aText, 6, 2);
	TInt hh = Num(aText, 8, 2), mm = Num(aText, 10, 2);
	if (m < 1 || m > 12 || d < 1 || d > 31)
		return Time::NullTTime();
	return TTime(TDateTime(y, TMonth(m - 1), d - 1, hh, mm, 0, 0));
	}

static void AppendTime(TDes8& aOut, const TTime& aTime)
	{
	TDateTime d = aTime.DateTime();
	aOut.AppendFormat(_L8("%04d%02d%02d%02d%02d"), d.Year(), d.Month() + 1, d.Day() + 1, d.Hour(), d.Minute());
	}

static TTime Midnight(const TTime& aTime)
	{
	TDateTime d = aTime.DateTime();
	return TTime(TDateTime(d.Year(), d.Month(), d.Day(), 0, 0, 0, 0));
	}

static void AppendHex64(TDes8& aOut, const TInt64& aValue)
	{
	aOut.AppendNumFixedWidth((TUint)aValue.High(), EHex, 8);
	aOut.AppendNumFixedWidth(aValue.Low(), EHex, 8);
	}

static TInt64 ParseHex64(const TDesC8& aText)
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

static TUint32 ParseHex(const TDesC8& aText)
	{
	TUint v = 0;
	TLex8 l(aText);
	l.Val(v, EHex);
	return v;
	}

// text for our tab-separated files: no tabs or line ends
static void AppendClean(TDes8& aOut, const TDesC8& aText)
	{
	for (TInt i = 0; i < aText.Length() && aOut.Length() < aOut.MaxLength(); i++)
		{
		TUint c = aText[i];
		aOut.Append(c < 32 ? (TChar)' ' : (TChar)c);
		}
	}

// (KPmCalOptRepeats) an Agenda entry's repeat as an RRULE and the dates
// taken out of it ("20261201,20261208"); EFalse if it can't be said as one
static TBool RepeatOf(CAgnEntry* aEntry, TBool aAllDay, TInt aZone, TDes8& aRule, TDes8& aExDates)
	{
	const CAgnRptDef* r = aEntry->RptDef();
	if (!aEntry->IsRepeating() || !r)
		return EFalse;
	CalRepeat c;
	Mem::FillZ(&c, sizeof(c));
	c.type = r->Type();
	c.interval = r->Interval();
	c.forever = r->RepeatForever();
	if (!c.forever)
		{
		// UNTIL: the last day, or the end of it in UTC (the Psion's own
		// offset: an hour either way at midnight changes no instance)
		TDateTime d = r->EndDate().DateTime();
		TBuf8<20> u;
		if (aAllDay)
			u.Format(_L8("%04d%02d%02d"), d.Year(), d.Month() + 1, d.Day() + 1);
		else
			{
			TTime end(TDateTime(d.Year(), d.Month(), d.Day(), 23, 59, 59, 0));
			end -= TLocale().UniversalTimeOffset();
			TDateTime e = end.DateTime();
			u.Format(_L8("%04d%02d%02dT%02d%02d%02dZ"), e.Year(), e.Month() + 1, e.Day() + 1, e.Hour(), e.Minute(), e.Second());
			}
		Mem::Copy(c.until, u.Ptr(), u.Length());
		c.until[u.Length()] = 0;
		}
	TInt i, w;
	switch (r->Type())
		{
	case CAgnRptDef::EWeekly:
		{
		TAgnWeeklyRpt wk = r->Weekly();
		for (i = 0; i < 7; i++)
			if (wk.IsDaySet((TDay)i)) c.weekdays |= 1 << i;
		}
		break;
	case CAgnRptDef::EMonthlyByDates:
		{
		TAgnMonthlyByDatesRpt md = r->MonthlyByDates();
		for (i = 0; i < 31; i++)
			if (md.IsDateSet(i)) c.dates |= 1UL << i;
		}
		break;
	case CAgnRptDef::EMonthlyByDays:
		{
		TAgnMonthlyByDaysRpt mw = r->MonthlyByDays();
		for (w = 0; w < 5; w++)
			for (i = 0; i < 7; i++)
				if (mw.IsDaySet((TDay)i, (TAgnRpt::TWeekInMonth)w)) c.weeks[w] |= 1 << i;
		}
		break;
	case CAgnRptDef::EYearlyByDay:
		{
		TAgnYearlyByDayRpt yd = r->YearlyByDay();
		TDay day;
		TAgnRpt::TWeekInMonth week;
		TMonth month;
		TInt year;
		yd.GetStartDay(day, week, month, year);
		c.yday = day;
		c.yweek = week;
		c.ymonth = month;
		}
		break;
	default:
		break;
		}
	char rule[120];
	(void)aZone;
	if (cal_rrule(&c, rule, sizeof(rule)) < 0)
		return EFalse;
	aRule.Copy(TPtrC8((const TUint8*)rule));
	aExDates.Zero();
	const CAgnExceptionList* ex = r->Exceptions();
	for (i = 0; ex && i < ex->Count() && aExDates.Length() + 9 < aExDates.MaxLength(); i++)
		{
		TDateTime d = AgnDateTime::AgnDateToTTime((*ex)[i].Date()).DateTime();
		if (aExDates.Length()) aExDates.Append(',');
		aExDates.AppendFormat(_L8("%04d%02d%02d"), d.Year(), d.Month() + 1, d.Day() + 1);
		}
	return ETrue;
	}

// ---------------------------------------------------------------- setup

CPmCalSync* CPmCalSync::NewL(MPmCalObserver& aObserver)
	{
	CPmCalSync* self = new(ELeave) CPmCalSync(aObserver);
	CActiveScheduler::Add(self);
	return self;
	}

CPmCalSync::CPmCalSync(MPmCalObserver& aObserver)
	: CActive(EPriorityIdle), iObserver(aObserver), iPhase(EIdle)
	{
	}

CPmCalSync::~CPmCalSync()
	{
	Cancel();
	Close();
	Reset();
	}

void CPmCalSync::DefaultAgendaFile(TDes& aFile)
	{
	aFile = _L("C:\\Documents\\Agenda");
	}

void CPmCalSync::Path(const TDesC& aName, TDes& aPath) const
	{
	aPath = iDir;
	aPath.Append(aName);
	}

void CPmCalSync::Reset()
	{
	TInt i;
	if (iEvents)
		{
		for (i = 0; i < iEvents->Count(); i++) delete (*iEvents)[i].iLine;
		delete iEvents;
		iEvents = NULL;
		}
	if (iMap)
		{
		for (i = 0; i < iMap->Count(); i++) delete (*iMap)[i].iKey;
		delete iMap;
		iMap = NULL;
		}
	if (iPush)
		{
		iPush->ResetAndDestroy();
		delete iPush;
		iPush = NULL;
		}
	delete iWritable;
	iWritable = NULL;
	delete iCalNames;
	iCalNames = NULL;
	}

void CPmCalSync::Close()
	{
	delete (CAgnSyncIter*)iIter;
	iIter = NULL;
	delete iModel;
	iModel = NULL;
	if (iServ)
		{
		if (iAgendaOpen)
			iServ->CloseAgenda();
		if (iServConnected)
			iServ->Close();
		delete iServ;
		iServ = NULL;
		}
	iAgendaOpen = iServConnected = EFalse;
	delete iPara;
	iPara = NULL;
	delete iChar;
	iChar = NULL;
	if (iFsOpen)
		{
		iFs.Close();
		iFsOpen = EFalse;
		}
	}

void CPmCalSync::StartL(const TDesC& aStoreDir, const TPmCalSettings& aSettings)
	{
	if (Running())
		return;
	iSettings = aSettings;
	iDir = aStoreDir;
	iDir.Append(_L("cal\\"));
	iAdded = iChangedN = iRemoved = iSent = iFailed = iKept = 0;
	iPushSeries = EFalse;
	iGuarded = EFalse;
	iPushedRead = EFalse;
	iPhase = EOpen;
	iPos = 0;
	Next();
	}

void CPmCalSync::Next()
	{
	TRequestStatus* s = &iStatus;
	User::RequestComplete(s, KErrNone);
	SetActive();
	}

void CPmCalSync::DoCancel()
	{
	}

// A step left. (This used to be called RunError, which ER5's CActive never
// calls - e32base.h declares only RunL and DoCancel - so a leave went to
// CONE's error dialog and the sync stayed "running" with the Agenda held
// open for the rest of the session. RunL traps StepL and calls this.)
void CPmCalSync::Failed(TInt aError)
	{
	// keep what was done: entries added so far must stay linked
	if (iMap && iPush && iFsOpen)
		{
		TRAPD(err, SaveMapL(); SavePushL());
		if (err == KErrNone && iPushedRead)
			{
			TFileName path;
			Path(KPushedFile, path);
			iFs.Delete(path);
			}
		}
	Close();
	Reset();
	iPhase = EIdle;
	TBuf<80> why;
	// the likely cause, rather than a bare number (style guide 6)
	switch (aError)
		{
	case KErrNotFound: why = _L("Agenda file not found"); break;
	case KErrPathNotFound: why = _L("The Agenda file's folder is not there"); break;
	case KErrInUse:
	case KErrLocked: why = _L("The Agenda file is busy"); break;
	case KErrNoMemory: why = _L("Not enough memory for the calendar"); break;
	case KErrDiskFull: why = _L("Calendar not synced - no room left on the disk"); break;
	case KErrNotReady:
	case KErrDisMounted: why = _L("Calendar not synced - the memory disk is not there"); break;
	case KErrAccessDenied: why = _L("Calendar not synced - the disk is write-protected"); break;
	case KErrCorrupt:
	case KErrEof: why = _L("The Agenda file could not be read"); break;
	case KErrCancel: why = _L("Calendar sync stopped"); break;
	default: why.Format(_L("Calendar sync failed (%d)"), aError); break;
		}
	iObserver.CalSyncDone(aError, why, EFalse);
	}

void CPmCalSync::RunL()
	{
	TRAPD(err, StepL());
	if (err != KErrNone)
		Failed(err);
	}

void CPmCalSync::StepL()
	{
	TBool more = EFalse;
	switch (iPhase)
		{
	case EOpen:
		iObserver.CalProgress(_L("Opening the Agenda..."));
		OpenL();
		iPhase = EPushed;
		more = ETrue;
		break;
	case EPushed:
		ReadPushedL();
		iPhase = EMapped;
		iPos = 0;
		more = ETrue;
		break;
	case EMapped:
		if (!MappedStepL()) { iPhase = ENew; iPos = 0; }
		more = ETrue;
		break;
	case ENew:
		if (!NewStepL()) { iPhase = ELocal; iPos = 0; }
		more = ETrue;
		break;
	case ELocal:
		if (!LocalStepL()) iPhase = EFinish;
		more = ETrue;
		break;
	case EFinish:
		FinishL();
		break;
	default:
		break;
		}
	if (more)
		Next();
	}

// ---------------------------------------------------------------- files

HBufC8* CPmCalSync::ReadFileL(const TDesC& aName)
	{
	TFileName path;
	Path(aName, path);
	RFile f;
	if (f.Open(iFs, path, EFileRead | EFileShareReadersOnly) != KErrNone)
		return NULL;
	CleanupClosePushL(f);
	TInt size = 0;
	User::LeaveIfError(f.Size(size));
	HBufC8* buf = HBufC8::NewLC(size + 1);
	TPtr8 p = buf->Des();
	User::LeaveIfError(f.Read(p, size));
	CleanupStack::Pop();                 // buf
	CleanupStack::PopAndDestroy();       // f
	return buf;
	}

// calls back for each line (without its line end)
class TLines
	{
public:
	TLines(const TDesC8& aText) : iText(aText), iPos(0) {}
	TBool Next(TPtrC8& aLine)
		{
		if (iPos >= iText.Length()) return EFalse;
		TPtrC8 rest = iText.Mid(iPos);
		TInt n = rest.Locate('\n');
		if (n < 0) n = rest.Length();
		aLine.Set(rest.Left(n));
		if (aLine.Length() && aLine[aLine.Length() - 1] == '\r') aLine.Set(aLine.Left(aLine.Length() - 1));
		iPos += n + 1;
		return ETrue;
		}
private:
	const TDesC8& iText;
	TInt iPos;
	};

TPtrC8 CPmCalSync::Field(const TDesC8& aLine, TInt aIndex) const
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

// events.txt: calid href etag recurid flags start end alarm summary location
void CPmCalSync::EventKey(const TDesC8& aLine, TDes8& aKey) const
	{
	aKey.Zero();
	aKey.Append(Clip(Field(aLine, 1), aKey.MaxLength() - 30));
	aKey.Append('|');
	aKey.Append(Clip(Field(aLine, 3), 24));
	}

void CPmCalSync::LoadEventsL()
	{
	iEvents = new(ELeave) CArrayFixFlat<TPmCalEvent>(64);
	HBufC8* text = ReadFileL(KEventsFile);
	if (!text)
		return;
	CleanupStack::PushL(text);
	TLines lines(*text);
	TPtrC8 line;
	TBuf8<260> key;
	while (lines.Next(line))
		{
		if (line.Length() == 0)
			continue;
		if (line[0] == '#')
			{
			// "#PSIEV1 TAB first day TAB zone"
			TTime first = ParseTime(Field(line, 1));
			if (first != Time::NullTTime())
				iWinStart = first;
			continue;
			}
		TPmCalEvent e;
		EventKey(line, key);
		e.iKeyHash = Fnv(key);
		TInt skip = Field(line, 0).Length() + Field(line, 1).Length() + Field(line, 2).Length() +
			Field(line, 3).Length() + 4;
		if (skip > line.Length())
			continue;                         // not a whole line
		TPtrC8 hashed = line.Mid(skip);       // flags .. location (the ten fields PsiMail has always had:
		for (TInt f = 0, at = 0; at < hashed.Length(); at++)     //  not the sidecar's after them)
			if (hashed[at] == '\t' && ++f == 6) { hashed.Set(hashed.Left(at)); break; }
		e.iHash = Fnv(hashed);
		e.iMapped = -1;
		e.iLine = line.AllocLC();
		iEvents->AppendL(e);
		CleanupStack::Pop();                  // line
		}
	CleanupStack::PopAndDestroy();            // text
	}

TInt CPmCalSync::FindEvent(const TDesC8& aKey) const
	{
	if (aKey.Length() == 0)
		return -1;
	TUint32 h = Fnv(aKey);
	TBuf8<260> key;
	for (TInt i = 0; i < iEvents->Count(); i++)
		{
		const TPmCalEvent& e = (*iEvents)[i];
		if (e.iKeyHash != h) continue;
		EventKey(*e.iLine, key);
		if (key == aKey) return i;
		}
	return -1;
	}

// (KMapSeries) the first of a series' instances in events.txt: key "href|*".
// aMark: the instances are this map entry's (NewStepL leaves them alone)
TInt CPmCalSync::FindSeries(const TDesC8& aKey, TInt aMark) const
	{
	if (aKey.Length() < 3 || aKey.Right(2) != _L8("|*"))
		return -1;
	TPtrC8 href = aKey.Left(aKey.Length() - 2);
	TInt first = -1;
	for (TInt i = 0; i < iEvents->Count(); i++)
		{
		TPmCalEvent& e = (*iEvents)[i];
		if (Field(*e.iLine, 1) != href) continue;
		if (first < 0) first = i;
		if (aMark >= 0 && e.iMapped < 0) e.iMapped = aMark;
		}
	return first;
	}

TInt CPmCalSync::AddMapL(TUint32 aAuid, const TDesC8& aKey, TUint32 aHash, TUint aFlags)
	{
	TPmCalMap m;
	m.iAuid = aAuid;
	m.iChanged = aAuid ? Changed(aAuid) : TInt64(0);
	m.iHash = aHash;
	m.iFlags = aFlags;
	m.iKey = aKey.AllocLC();
	m.iEvent = -1;
	iMap->AppendL(m);
	CleanupStack::Pop();
	return iMap->Count() - 1;
	}

void CPmCalSync::RemoveMap(TInt aIndex)
	{
	delete (*iMap)[aIndex].iKey;
	iMap->Delete(aIndex);
	}

void CPmCalSync::LoadMapL()
	{
	iMap = new(ELeave) CArrayFixFlat<TPmCalMap>(64);
	iSince = TInt64(0);
	iNewFile = ETrue;
	HBufC8* text = ReadFileL(KMapFile);
	TTime now;
	now.HomeTime();
	TBuf8<130> file;
	file.Copy(iSettings.iAgendaFile);
	file.LowerCase();
	if (text)
		{
		CleanupStack::PushL(text);
		TLines lines(*text);
		TPtrC8 line;
		while (lines.Next(line))
			{
			if (line.Length() == 0) continue;
			if (line[0] == '#')
				{
				TBuf8<130> f(Clip(Field(line, 1), 128));
				f.LowerCase();
				if (Field(line, 0) == KMapMagic && f == file)
					{
					iNewFile = EFalse;
					iSince = ParseHex64(Field(line, 2));
					}
				continue;
				}
			if (iNewFile) break;           // another Agenda file: start again
			TPmCalMap m;
			TLex8 l(Field(line, 0));
			TUint auid = 0;
			l.Val(auid);
			m.iAuid = auid;
			m.iChanged = ParseHex64(Field(line, 1));
			m.iHash = ParseHex(Field(line, 2));
			TLex8 lf(Field(line, 3));
			TUint flags = 0;
			lf.Val(flags);
			m.iFlags = flags;
			m.iKey = Field(line, 4).AllocLC();
			m.iEvent = -1;
			iMap->AppendL(m);
			CleanupStack::Pop();
			}
		CleanupStack::PopAndDestroy();       // text
		}
	if (iNewFile)
		{
		for (TInt i = 0; i < iMap->Count(); i++) delete (*iMap)[i].iKey;
		iMap->Reset();
		// Psion entries changed from now on go to the server (or all of
		// them, if the user asked for that)
		iSince = iSettings.iCopyExisting ? TInt64(0) : now.Int64();
		}
	}

void CPmCalSync::SaveMapL()
	{
	TFileName path, tmp;
	Path(KMapFile, path);
	Path(_L("agenda.tmp"), tmp);
	RFile f;
	User::LeaveIfError(f.Replace(iFs, tmp, EFileWrite));
	CleanupClosePushL(f);
	TBuf8<400> line;
	line = KMapMagic;
	line.Append('\t');
	TBuf8<130> file;
	file.Copy(iSettings.iAgendaFile);
	line.Append(file);
	line.Append('\t');
	AppendHex64(line, iSince);
	line.Append(_L8("\t0\n"));
	User::LeaveIfError(f.Write(line));
	for (TInt i = 0; i < iMap->Count(); i++)
		{
		const TPmCalMap& m = (*iMap)[i];
		line.Zero();
		line.AppendNum((TUint)m.iAuid, EDecimal);
		line.Append('\t');
		AppendHex64(line, m.iChanged);
		line.Append('\t');
		line.AppendNumFixedWidth((TUint)m.iHash, EHex, 8);
		line.Append('\t');
		line.AppendNum((TInt)m.iFlags);
		line.Append('\t');
		line.Append(Clip(*m.iKey, line.MaxLength() - line.Length() - 1));
		line.Append('\n');
		User::LeaveIfError(f.Write(line));
		}
	User::LeaveIfError(f.Flush());
	CleanupStack::PopAndDestroy();          // f
	User::LeaveIfError(iFs.Replace(tmp, path));   // (one step: no moment with neither file)
	}

// A small file written whole and put in place in one step (robustness
// notes, section 4): a failure never leaves a short file where the old one was
static void WriteFileSafeL(RFs& aFs, const TDesC& aPath, const TDesC8& aText)
	{
	TFileName tmp(aPath);
	tmp.Append('~');
	RFile f;
	User::LeaveIfError(f.Replace(aFs, tmp, EFileWrite));
	TInt r = f.Write(aText);
	if (r == KErrNone)
		r = f.Flush();
	f.Close();
	if (r == KErrNone)
		r = aFs.Replace(tmp, aPath);
	if (r != KErrNone)
		{
		aFs.Delete(tmp);
		User::Leave(r);
		}
	}

void CPmCalSync::LoadCalendarsL()
	{
	iWritable = new(ELeave) CDesC8ArrayFlat(4);
	iCalNames = new(ELeave) CDesC8ArrayFlat(4);
	iDefaultCal.Zero();
	HBufC8* text = ReadFileL(KCalsFile);
	if (!text)
		return;
	CleanupStack::PushL(text);
	TLines lines(*text);
	TPtrC8 line;
	while (lines.Next(line))
		{
		if (line.Length() == 0 || line[0] == '#') continue;
		// id sync ctag href flags name
		TPtrC8 flags = Field(line, 4);
		if (flags.Locate('W') >= 0) iWritable->AppendL(Field(line, 0));
		if (flags.Locate('D') >= 0) iDefaultCal = Clip(Field(line, 0), 12);
		TBuf8<80> cn(Clip(Field(line, 0), 12));
		cn.Append('\t');
		cn.Append(Clip(Field(line, 5), 60));
		iCalNames->AppendL(cn);
		}
	CleanupStack::PopAndDestroy();
	}

// (the static functions below use the caller's file server session: one
// per program, as the robustness notes have it, not one per call)
void CPmCalSync::CalendarsL(RFs& aFs, const TDesC& aStoreDir, CDesCArray& aNames, CDesC8Array& aIds, TInt& aDefault)
	{
	aDefault = -1;
	RFs& fs = aFs;
	TFileName path(aStoreDir);
	path.Append(_L("cal\\calendars.txt"));
	RFile f;
	if (f.Open(fs, path, EFileRead | EFileShareReadersOnly) == KErrNone)
		{
		CleanupClosePushL(f);
		TInt size = 0;
		f.Size(size);
		HBufC8* text = HBufC8::NewLC(size + 1);
		TPtr8 p = text->Des();
		f.Read(p, size);
		TLines lines(*text);
		TPtrC8 line;
		while (lines.Next(line))
			{
			if (line.Length() == 0 || line[0] == '#') continue;
			TPtrC8 rest(line);
			TPtrC8 fields[6];
			for (TInt i = 0; i < 6; i++)
				{
				TInt t = rest.Locate('\t');
				fields[i].Set(t < 0 ? rest : rest.Left(t));
				rest.Set(t < 0 ? TPtrC8() : rest.Mid(t + 1));
				}
			if (fields[4].Locate('W') < 0) continue;     // can't add events there
			TBuf<64> name;
			name.Copy(Clip(fields[5], 64));
			if (fields[4].Locate('D') >= 0) aDefault = aNames.Count();
			aNames.AppendL(name);
			aIds.AppendL(fields[0]);
			}
		CleanupStack::PopAndDestroy(2);   // text, f
		}
	}

void CPmCalSync::SetDefaultCalendarL(RFs& aFs, const TDesC& aStoreDir, const TDesC8& aId)
	{
	RFs& fs = aFs;
	TFileName path(aStoreDir);
	path.Append(_L("cal\\"));
	fs.MkDirAll(path);
	path.Append(_L("default.txt"));
	TBuf8<64> text(Clip(aId, 60));
	text.Append('\n');
	WriteFileSafeL(fs, path, text);
	}

void CPmCalSync::ForgetL(RFs& aFs, const TDesC& aStoreDir)
	{
	RFs& fs = aFs;
	TFileName path(aStoreDir);
	path.Append(_L("cal\\agenda.txt"));
	fs.Delete(path);
	path = aStoreDir;
	path.Append(_L("cal\\push.txt"));
	fs.Delete(path);
	}

// changes not yet sent (the engine may have been offline): kept, unless
// this pass has a newer one for the same entry
void CPmCalSync::LoadPushL()
	{
	iPush = new(ELeave) CArrayPtrFlat<HBufC8>(8);
	HBufC8* text = ReadFileL(KPushFile);
	if (!text)
		return;
	CleanupStack::PushL(text);
	TLines lines(*text);
	TPtrC8 line;
	while (lines.Next(line))
		{
		if (line.Length() == 0 || line[0] == '#') continue;
		HBufC8* b = line.AllocLC();
		iPush->AppendL(b);
		CleanupStack::Pop();
		}
	CleanupStack::PopAndDestroy();
	}

void CPmCalSync::PushL(const TDesC8& aLine)
	{
	TPtrC8 key = Field(aLine, 1);
	for (TInt i = 0; i < iPush->Count(); i++)
		if (Field(*(*iPush)[i], 1) == key)
			{
			delete (*iPush)[i];
			iPush->Delete(i);
			break;
			}
	HBufC8* b = aLine.AllocLC();
	iPush->AppendL(b);
	CleanupStack::Pop();
	iSent++;
	}

void CPmCalSync::SavePushL()
	{
	TFileName path;
	Path(KPushFile, path);
	if (iPush->Count() == 0)
		{
		iFs.Delete(path);
		return;
		}
	// (these are the user's unsent Agenda changes: written whole to a
	// temporary first, then put in place, as agenda.txt is)
	TFileName tmp;
	Path(_L("push.tmp"), tmp);
	RFile f;
	User::LeaveIfError(f.Replace(iFs, tmp, EFileWrite));
	CleanupClosePushL(f);
	for (TInt i = 0; i < iPush->Count(); i++)
		{
		User::LeaveIfError(f.Write(*(*iPush)[i]));
		User::LeaveIfError(f.Write(_L8("\n")));
		}
	User::LeaveIfError(f.Flush());
	CleanupStack::PopAndDestroy();
	User::LeaveIfError(iFs.Replace(tmp, path));
	}

TBool CPmCalSync::Writable(const TDesC8& aLine) const
	{
	TInt pos;
	return iWritable->Find(Field(aLine, 0), pos) == 0;
	}

// ---------------------------------------------------------------- the Agenda

void CPmCalSync::OpenL()
	{
	User::LeaveIfError(iFs.Connect());
	iFsOpen = ETrue;
	TEntry entry;
	if (iFs.Entry(iSettings.iAgendaFile, entry) != KErrNone)
		User::Leave(KErrNotFound);         // (the Agenda server panics on a missing file)
	iWinStart = Time::NullTTime();
	LoadEventsL();
	LoadMapL();
	LoadCalendarsL();
	LoadPushL();
	TTime now;
	now.HomeTime();
	if (iWinStart == Time::NullTTime())
		iWinStart = Midnight(now) - TTimeIntervalDays(iSettings.iCal.days_back >= 0 ? iSettings.iCal.days_back : 30);
	iWinEnd = Midnight(now) + TTimeIntervalDays((iSettings.iCal.days_ahead > 0 ? iSettings.iCal.days_ahead : 180) + 1);

	iPara = CParaFormatLayer::NewL();
	iChar = CCharFormatLayer::NewL();
	iServ = RAgendaServ::NewL();
	User::LeaveIfError(iServ->Connect());
	iServConnected = ETrue;
	iModel = CAgnEntryModel::NewL();
	iModel->SetServer(iServ);
	iModel->OpenL(iSettings.iAgendaFile, TTimeIntervalMinutes(9 * 60), TTimeIntervalMinutes(9 * 60), TTimeIntervalMinutes(9 * 60));
	iAgendaOpen = ETrue;
	iServ->WaitUntilLoaded();
	if (iServ->FileIsReadOnly())
		User::Leave(KErrAccessDenied);
	}

CAgnEntry* CPmCalSync::FetchL(TUint32 aAuid)
	{
	CAgnEntry* e = NULL;
	TRAPD(err, e = iModel->FetchEntryL(TAgnUniqueId(aAuid)));
	if (err == KErrNoMemory)
		User::Leave(err);
	return err == KErrNone ? e : NULL;
	}

// (KPmCalOptStamp) what tells us the user changed an entry: a hash of what
// the Agenda shows of it, marked in the top word. The Agenda's own
// last-changed date counts whole minutes, so an edit in the minute of a
// sync was missed; and the emulator's clock creeps. PsiMail keeps the date.
const TInt KStampMark = 0x48415348;         // 'HASH'

TInt64 CPmCalSync::DateChanged(TUint32 aAuid)
	{
	TTime t = iServ->UniqueIdLastChangedDate(TAgnUniqueId(aAuid));
	return t.Int64();
	}

TInt64 CPmCalSync::Changed(TUint32 aAuid)
	{
	if (!(iSettings.iSpare[0] & KPmCalOptStamp))
		return DateChanged(aAuid);
	CAgnEntry* e = FetchL(aAuid);
	if (!e)
		return TInt64(0);
	CleanupStack::PushL(e);
	TUint32 h = 2166136261u;
	TBuf8<300> d;
	d.AppendNum((TInt)e->Type());
	TTime s, en;
	if (e->Type() == CAgnEntry::EAppt) { s = e->CastToAppt()->StartDateTime(); en = e->CastToAppt()->EndDateTime(); }
	else if (e->Type() == CAgnEntry::EEvent || e->Type() == CAgnEntry::EAnniv) { s = e->CastToEvent()->StartDate(); en = e->CastToEvent()->EndDate(); }
	else if (e->Type() == CAgnEntry::ETodo)
		{
		CAgnTodo* t = e->CastToTodo();
		s = t->IsDated() ? t->DueDate() : TTime(TInt64(0));
		en = TTime(TInt64((TInt)t->Priority(), t->IsCrossedOut() ? 1 : 0));
		}
	else { s = e->InstanceStartDate(); en = s; }
	AppendHex64(d, s.Int64());
	AppendHex64(d, en.Int64());
	if (e->HasAlarm()) AppendHex64(d, e->AlarmInstanceDateTime().Int64());
	d.Append(e->IsRepeating() ? 'R' : '-');
	TBuf<200> text;
	CRichText* rt = e->RichTextL();
	rt->Extract(text, 0, Min(rt->DocumentLength(), text.MaxLength()));
	TBuf8<200> t8;
	t8.Copy(text);
	TBuf8<120> loc;
	loc.Copy(Clip(e->Location(), 110));
	CleanupStack::PopAndDestroy();       // e
	for (TInt i = 0; i < 3; i++)
		{
		const TDesC8& part = i == 0 ? (const TDesC8&)d : i == 1 ? (const TDesC8&)t8 : (const TDesC8&)loc;
		for (TInt k = 0; k < part.Length(); k++) { h ^= part[k]; h *= 16777619u; }
		h ^= 0xff; h *= 16777619u;
		}
	return TInt64(KStampMark, h);
	}

// the entry's details as a push.txt line
void CPmCalSync::PushEntryL(TChar aOp, TUint32 aAuid, CAgnEntry* aEntry, const TDesC8& aEventLine)
	{
	HBufC8* buf = HBufC8::NewLC(1300);
	TPtr8 line = buf->Des();
	line.Append(aOp);
	line.Append(_L8("\ta"));
	line.AppendNum((TUint)aAuid, EDecimal);
	line.Append('\t');
	if (aOp == 'N' && aEntry && aEntry->Type() == CAgnEntry::ETodo)
		{
		TBuf8<12> cal;
		CalForList(aEntry->CastToTodo()->TodoListId(), cal);
		line.Append(cal);
		}
	else if (aOp == 'N')
		line.Append(iDefaultCal);
	else
		{
		line.Append(Field(aEventLine, 1));          // href
		line.Append('\t');
		line.Append(Field(aEventLine, 2));          // etag
		line.Append('\t');
		if (!iPushSeries)
			line.Append(Field(aEventLine, 3));      // recurid (a series: none, the whole event)
		}
	if (aOp != 'D' && aEntry->Type() == CAgnEntry::ETodo)
		{
		// (KPmCalOptTodos) a to-do: due, T and C (done), priority in the alarm's place
		CAgnTodo* t = aEntry->CastToTodo();
		line.Append('\t');
		if (t->IsDated()) AppendTime(line, t->DueDate());
		line.Append('\t');
		if (t->IsDated()) AppendTime(line, t->DueDate());
		line.Append(_L8("\tT"));
		if (t->IsCrossedOut()) line.Append('C');
		line.Append('\t');
		line.AppendNum((TInt)t->Priority());
		line.Append('\t');
		TBuf<200> text;
		CRichText* rt = aEntry->RichTextL();
		rt->Extract(text, 0, Min(rt->DocumentLength(), text.MaxLength()));
		text.Trim();
		if (text.Length() == 0) text = _L("(no title)");
		AppendClean(line, Clip(text, 190));
		line.Append('\t');
		if (aOp == 'N')
			{
			line.Append(_L8("\tpsion-"));
			AppendHex64(line, iSince);
			line.Append('-');
			line.AppendNum((TUint)aAuid, EDecimal);
			}
		}
	else if (aOp != 'D')
		{
		TTime start, end;
		TBool allday = aEntry->Type() != CAgnEntry::EAppt;
		if (allday)
			{
			start = Midnight(aEntry->CastToEvent()->StartDate());
			end = Midnight(aEntry->CastToEvent()->EndDate());
			}
		else
			{
			start = aEntry->CastToAppt()->StartDateTime();
			end = aEntry->CastToAppt()->EndDateTime();
			}
		if (end < start) end = start;
		TInt alarm = -2;                            // leave the server's alarms alone
		if (iSettings.iAlarms)
			{
			// "no alarm" removes the server's only if it had one we could
			// read (it may have kinds the Psion can't show)
			TLex8 l(Field(aEventLine, 7));
			TInt had = -1;
			if (aOp == 'M') l.Val(had);
			if (aOp == 'N' || had >= 0) alarm = -1;
			if (aEntry->HasAlarm())
				{
				TTimeIntervalMinutes m;
				start.MinutesFrom(aEntry->AlarmInstanceDateTime(), m);
				alarm = m.Int() > 0 ? m.Int() : 0;
				}
			}
		line.Append('\t');
		AppendTime(line, start);
		line.Append('\t');
		AppendTime(line, end);
		line.Append('\t');
		if (allday) line.Append('A');
		line.Append('\t');
		line.AppendNum(alarm);
		line.Append('\t');
		TBuf<200> text;
		CRichText* rt = aEntry->RichTextL();
		rt->Extract(text, 0, Min(rt->DocumentLength(), text.MaxLength()));
		text.Trim();
		if (text.Length() == 0) text = _L("(no title)");
		AppendClean(line, Clip(text, 190));
		line.Append('\t');
		AppendClean(line, Clip(aEntry->Location(), 110));
		if (aOp == 'N')
			{
			// the event's UID on the server: the same if this is sent again
			line.Append(_L8("\tpsion-"));
			AppendHex64(line, iSince);
			line.Append('-');
			line.AppendNum((TUint)aAuid, EDecimal);
			}
		// (KPmCalOptRepeats) a repeating entry: its rule and the dates out of it
		TBuf8<120> rule;
		TBuf8<240> ex;
		if ((iSettings.iSpare[0] & KPmCalOptRepeats) && RepeatOf(aEntry, allday, iSettings.iCal.zone, rule, ex))
			{
			line.Append(_L8("\tR\t"));
			line.Append(rule);
			line.Append('\t');
			line.Append(ex);
			}
		}
	PushL(line);
	CleanupStack::PopAndDestroy();       // buf
	}

void CPmCalSync::FillL(CAgnEntry* aEntry, const TDesC8& aLine, const TTime& aStart)
	{
	CRichText* rt = aEntry->RichTextL();
	rt->Reset();
	TBuf<200> text;
	text.Copy(Clip(Field(aLine, 8), 200));
	rt->InsertL(0, text);
	if (aEntry->Type() == CAgnEntry::ETodo)
		{
		// priority in the alarm's place; done: crossed out
		CAgnTodo* t = aEntry->CastToTodo();
		TLex8 lp(Field(aLine, 7));
		TInt prio = 0;
		lp.Val(prio);
		if (prio >= 1 && prio <= 9) t->SetPriority(prio);
		TBool done = Field(aLine, 4).Locate('C') >= 0;
		if (done && !t->IsCrossedOut()) { TTime now; now.HomeTime(); t->CrossOut(now); }
		else if (!done && t->IsCrossedOut()) t->UnCrossOut();
		return;
		}
	TBuf<120> loc;
	loc.Copy(Clip(Field(aLine, 9), 120));
	aEntry->SetLocationL(loc);
	if (iSettings.iAlarms)
		{
		TLex8 l(Field(aLine, 7));
		TInt before = -1;
		l.Val(before);
		if (before < 0)
			aEntry->ClearAlarm();
		else
			{
			// the Agenda's alarm: so many days before, at a time of day
			TTime at = aStart - TTimeIntervalMinutes(before);
			TInt days = Midnight(aStart).DaysFrom(Midnight(at)).Int();
			TDateTime d = at.DateTime();
			aEntry->SetAlarm(TTimeIntervalDays(days), TTimeIntervalMinutes(d.Hour() * 60 + d.Minute()));
			}
		}
	}

static TBool IsTodo(const TDesC8& aFlags) { return aFlags.Locate('T') >= 0; }

// (KPmCalOptTodos) the Agenda to-do list for a calendar: the one with its
// name, made if there is none
TAgnTodoListId CPmCalSync::TodoListL(const TDesC8& aCalId)
	{
	TBuf<64> name(_L("To-do list"));
	for (TInt i = 0; iCalNames && i < iCalNames->Count(); i++)
		if (Field((*iCalNames)[i], 0) == aCalId) { name.Copy(Clip(Field((*iCalNames)[i], 1), 60)); break; }
	CAgnTodoListNames* names = CAgnTodoListNames::NewL();
	CleanupStack::PushL(names);
	iModel->PopulateTodoListNamesL(names);
	for (TInt k = 0; k < names->Count(); k++)
		if (names->TodoListName(k).CompareF(name) == 0)
			{
			TAgnTodoListId id = names->TodoListId(k);
			CleanupStack::PopAndDestroy();
			return id;
			}
	CleanupStack::PopAndDestroy();
	CAgnTodoList* list = CAgnTodoList::NewL();
	CleanupStack::PushL(list);
	list->SetName(name);
	TAgnTodoListId id = iModel->AddTodoListL(list);
	CleanupStack::Pop();                   // (the model has it)
	return id;
	}

// the calendar a to-do list is named after, else where new entries go
void CPmCalSync::CalForList(TAgnTodoListId aList, TDes8& aCalId)
	{
	aCalId = iDefaultCal;
	CAgnTodoListNames* names = CAgnTodoListNames::NewL();
	CleanupStack::PushL(names);
	iModel->PopulateTodoListNamesL(names);
	for (TInt k = 0; k < names->Count(); k++)
		{
		if (!(names->TodoListId(k) == aList)) continue;
		TBuf8<64> n;
		n.Copy(Clip(names->TodoListName(k), 60));
		for (TInt i = 0; iCalNames && i < iCalNames->Count(); i++)
			if (Field((*iCalNames)[i], 1).CompareF(n) == 0)
				{
				TPtrC8 id = Field((*iCalNames)[i], 0);
				TInt pos;
				if (iWritable->Find(id, pos) == 0) aCalId = Clip(id, 12);
				}
		break;
		}
	CleanupStack::PopAndDestroy();
	}

// (KPmCalOptSeriesIn) the Agenda's repeat for a calendar repeat
static CAgnRptDef* RptFromL(const CalRepeat& aR, const TTime& aStart)
	{
	CAgnRptDef* r = CAgnRptDef::NewL();
	CleanupStack::PushL(r);
	TTime end = Time::NullTTime();
	if (!aR.forever)
		{
		TPtrC8 u((const TUint8*)aR.until);
		TInt y = 0, m = 0, d = 0;
		TLex8 ly(u.Left(4)), lm(u.Mid(4, 2)), ld(u.Mid(6, 2));
		ly.Val(y); lm.Val(m); ld.Val(d);
		if (m >= 1 && m <= 12 && d >= 1 && d <= 31)
			end = TTime(TDateTime(y, TMonth(m - 1), d - 1, 0, 0, 0, 0));
		}
	TInt i, w;
	// (the repeat's own interval too, as Agenda's dialogs set it: one left
	// at 0 is a bad entry, Agenda Model panic 1)
	TUint iv = aR.interval > 0 ? aR.interval : 1;
	switch (aR.type)
		{
	case CR_DAILY:
		{ TAgnDailyRpt x; x.SetStartDate(aStart); x.SetInterval(iv); r->SetDaily(x); }
		break;
	case CR_WEEKLY:
		{
		TAgnWeeklyRpt x;
		x.SetStartDate(aStart); x.SetInterval(iv);
		for (i = 0; i < 7; i++) if (aR.weekdays & (1 << i)) x.SetDay((TDay)i);
		r->SetWeekly(x);
		}
		break;
	case CR_MONTHLY_DATES:
		{
		TAgnMonthlyByDatesRpt x;
		x.SetStartDate(aStart); x.SetInterval(iv);
		for (i = 0; i < 31; i++) if (aR.dates & (1UL << i)) x.SetDate(i);
		r->SetMonthlyByDates(x);
		}
		break;
	case CR_MONTHLY_DAYS:
		{
		TAgnMonthlyByDaysRpt x;
		x.SetStartDate(aStart); x.SetInterval(iv);
		for (w = 0; w < 5; w++)
			for (i = 0; i < 7; i++)
				if (aR.weeks[w] & (1 << i)) x.SetDay((TDay)i, (TAgnRpt::TWeekInMonth)w);
		r->SetMonthlyByDays(x);
		}
		break;
	case CR_YEARLY_DATE:
		{ TAgnYearlyByDateRpt x; x.SetStartDate(aStart); x.SetInterval(iv); r->SetYearlyByDate(x); }
		break;
	case CR_YEARLY_DAY:
		{
		TAgnYearlyByDayRpt x;
		x.SetStartDay((TDay)aR.yday, (TAgnRpt::TWeekInMonth)aR.yweek, TMonth(aR.ymonth), aStart.DateTime().Year());
		x.SetInterval(iv);
		r->SetYearlyByDay(x);
		}
		break;
	default:
		User::Leave(KErrNotSupported);
		}
	r->SetStartDate(aStart);
	r->SetInterval(aR.interval > 0 ? aR.interval : 1);
	if (end == Time::NullTTime())
		r->SetRepeatForever(ETrue);
	else
		{
		r->SetRepeatForever(EFalse);
		r->SetEndDate(end);
		}
	CleanupStack::Pop();
	return r;
	}

// the S line (a series the Agenda can repeat) for a server event, -1
TInt CPmCalSync::SeriesLine(const TDesC8& aHref) const
	{
	for (TInt i = 0; i < iEvents->Count(); i++)
		{
		const TDesC8& l = *(*iEvents)[i].iLine;
		if (Field(l, 4).Locate('S') >= 0 && Field(l, 1) == aHref) return i;
		}
	return -1;
	}

// (KPmCalOptSeriesIn) a series as one repeating Agenda entry: its first
// instance's times, its rule, the dates taken out of it
TUint32 CPmCalSync::AddSeriesL(const TDesC8& aLine)
	{
	TTime start = ParseTime(Field(aLine, 5)), end = ParseTime(Field(aLine, 6));
	if (start == Time::NullTTime() || end == Time::NullTTime())
		return 0;
	CalRepeat rr;
	TBuf8<170> rule(Clip(Field(aLine, 14), 160));
	rule.Append(0);
	TDateTime sd = start.DateTime();
	if (cal_rrule_parse((const char*)rule.Ptr(), sd.Year(), sd.Month() + 1, sd.Day() + 1, &rr) != 0)
		return 0;
	TBool allday = Field(aLine, 4).Locate('A') >= 0;
	CAgnEntry* e;
	if (allday)
		{
		CAgnEvent* ev = CAgnEvent::NewL(iPara, iChar);
		e = ev;
		CleanupStack::PushL(e);
		ev->SetStartAndEndDate(start, end);
		}
	else
		{
		CAgnAppt* a = CAgnAppt::NewL(iPara, iChar);
		e = a;
		CleanupStack::PushL(e);
		a->SetStartAndEndDateTime(start, end);
		}
	CAgnRptDef* r = RptFromL(rr, Midnight(start));
	CleanupStack::PushL(r);
	// the dates taken out: "20261201,20261208"
	TPtrC8 ex = Field(aLine, 15);
	while (ex.Length() >= 8)
		{
		TTime d = ParseTime(ex.Left(8));
		if (d != Time::NullTTime())
			r->AddExceptionL(TAgnException(AgnDateTime::TTimeToAgnDate(d)));
		TInt comma = ex.Locate(',');
		ex.Set(comma < 0 ? TPtrC8() : ex.Mid(comma + 1));
		}
	e->SetRptDefL(r);
	CleanupStack::PopAndDestroy();       // r (the entry has a copy)
	FillL(e, aLine, start);
	TAgnEntryId id = iModel->AddEntryL(e);
	CleanupStack::PopAndDestroy();       // e
	return iServ->GetUniqueId(id).Id();
	}

// (KPmCalOptSeriesIn) before the single instances: each S line not linked
// yet, and none of whose instances is, becomes one repeating entry; its
// instances are then its own (NewStepL leaves them)
void CPmCalSync::SeriesPrePassL()
	{
	for (TInt i = 0; i < iEvents->Count(); i++)
		{
		TPmCalEvent& ev = (*iEvents)[i];
		if (Field(*ev.iLine, 4).Locate('S') < 0 || ev.iMapped >= 0) continue;
		TPtrC8 href = Field(*ev.iLine, 1);
		TBool linked = EFalse;
		for (TInt k = 0; k < iEvents->Count() && !linked; k++)
			if ((*iEvents)[k].iMapped >= 0 && Field(*(*iEvents)[k].iLine, 1) == href) linked = ETrue;
		if (linked) continue;                // (its instances are linked one by one already)
		TUint32 auid = AddSeriesL(*ev.iLine);
		if (!auid) continue;
		TBuf8<210> key(Clip(href, 200));
		key.Append(_L8("|*"));
		TInt m = AddMapL(auid, key, Fnv(Field(*ev.iLine, 2)), KMapSeries);
		FindSeries(key, m);
		iAdded++;
		}
	}

TUint32 CPmCalSync::AddFromEventL(const TDesC8& aLine)
	{
	if (IsTodo(Field(aLine, 4)))
		{
		// (KPmCalOptTodos) a to-do, in its calendar's list
		CAgnTodo* t = CAgnTodo::NewL(iPara, iChar);
		CleanupStack::PushL(t);
		t->SetTodoListId(TodoListL(Field(aLine, 0)));
		TTime due = ParseTime(Field(aLine, 5));
		if (due != Time::NullTTime()) t->SetDueDate(due);
		else t->MakeUndated();
		FillL(t, aLine, due);
		TAgnEntryId tid = iModel->AddEntryL(t);
		CleanupStack::PopAndDestroy();
		return iServ->GetUniqueId(tid).Id();
		}
	TTime start = ParseTime(Field(aLine, 5)), end = ParseTime(Field(aLine, 6));
	TBool allday = Field(aLine, 4).Locate('A') >= 0;
	CAgnEntry* e;
	if (allday)
		{
		CAgnEvent* ev = CAgnEvent::NewL(iPara, iChar);
		e = ev;
		CleanupStack::PushL(e);
		ev->SetStartAndEndDate(start, end);
		}
	else
		{
		CAgnAppt* a = CAgnAppt::NewL(iPara, iChar);
		e = a;
		CleanupStack::PushL(e);
		a->SetStartAndEndDateTime(start, end);
		}
	FillL(e, aLine, start);
	TAgnEntryId id = iModel->AddEntryL(e);
	CleanupStack::PopAndDestroy();       // e
	return iServ->GetUniqueId(id).Id();
	}

// returns the entry's UID, a new one if it had to be made again
TUint32 CPmCalSync::UpdateFromEventL(TUint32 aAuid, const TDesC8& aLine)
	{
	CAgnEntry* e = FetchL(aAuid);
	TBool allday = Field(aLine, 4).Locate('A') >= 0;
	if (!e)
		return AddFromEventL(aLine);
	CleanupStack::PushL(e);
	if (IsTodo(Field(aLine, 4)) || e->Type() == CAgnEntry::ETodo)
		{
		if (!IsTodo(Field(aLine, 4)) || e->Type() != CAgnEntry::ETodo)
			{
			CleanupStack::PopAndDestroy();
			iServ->DeleteEntry(TAgnUniqueId(aAuid));
			return AddFromEventL(aLine);
			}
		TTime due = ParseTime(Field(aLine, 5));
		if (due != Time::NullTTime()) e->CastToTodo()->SetDueDate(due);
		else e->CastToTodo()->MakeUndated();
		FillL(e, aLine, due);
		iModel->UpdateEntryL(e);
		CleanupStack::PopAndDestroy();
		return aAuid;
		}
	TBool isAppt = e->Type() == CAgnEntry::EAppt;
	if ((e->Type() != CAgnEntry::EAppt && e->Type() != CAgnEntry::EEvent) || isAppt == allday || e->IsRepeating())
		{
		// a different kind of entry now: make it again
		CleanupStack::PopAndDestroy();
		iServ->DeleteEntry(TAgnUniqueId(aAuid));
		return AddFromEventL(aLine);
		}
	TTime start = ParseTime(Field(aLine, 5)), end = ParseTime(Field(aLine, 6));
	if (allday)
		e->CastToEvent()->SetStartAndEndDate(start, end);
	else
		e->CastToAppt()->SetStartAndEndDateTime(start, end);
	FillL(e, aLine, start);
	iModel->UpdateEntryL(e);
	CleanupStack::PopAndDestroy();
	return aAuid;
	}

void CPmCalSync::DeleteL(TUint32 aAuid)
	{
	iServ->DeleteEntry(TAgnUniqueId(aAuid));
	}

// ---------------------------------------------------------------- the steps

// what the engine did with the changes sent last time
void CPmCalSync::ReadPushedL()
	{
	HBufC8* text = ReadFileL(KPushedFile);
	if (!text)
		return;
	CleanupStack::PushL(text);
	TLines lines(*text);
	TPtrC8 line;
	while (lines.Next(line))
		{
		TPtrC8 key = Field(line, 0), what = Field(line, 1), href = Field(line, 2);
		if (key.Length() < 2 || key[0] != 'a') continue;
		TLex8 l(key.Mid(1));
		TUint auid = 0;
		l.Val(auid);
		for (TInt i = 0; i < iMap->Count(); i++)
			{
			TPmCalMap& m = (*iMap)[i];
			if (m.iAuid != auid) continue;
			TBool ok = what == _L8("ok");
			if (m.iFlags & KMapPending)
				{
				if (ok)
					{
					// now it has a place on the server
					HBufC8* k = HBufC8::NewL(href.Length() + 2);
					k->Des().Copy(href);
					k->Des().Append('|');
					if (m.iFlags & KMapSeries) k->Des().Append('*');
					delete m.iKey;
					m.iKey = k;
					m.iHash = 0;
					m.iFlags &= ~KMapPending;
					}
				else
					{
					RemoveMap(i);              // try again next time
					iFailed++;
					}
				}
			else if (m.iFlags & KMapDeleting)
				{
				if (ok || what == _L8("gone")) RemoveMap(i);
				else { m.iFlags = KMapForce | (m.iFlags & KMapSeries); iFailed++; }
				}
			else if (!ok)
				{
				m.iFlags |= KMapForce;        // put the server's version back
				if (what != _L8("gone")) iFailed++;
				}
			else if (m.iFlags & KMapSeries)
				m.iHash = 0;                  // our change moved its etag: take the new one

			break;
			}
		}
	CleanupStack::PopAndDestroy();
	iPushedRead = ETrue;                    // deleted once the links are saved
	}

// one batch of the entries we look after: the facts about each go to
// cal_merge (core/mail/engine/cal/calmerge.c, tested on a PC), which says
// what to do
TBool CPmCalSync::MappedStepL()
	{
	if (iPos == 0)
		GuardCheck();
	TBuf<60> prog;
	prog.Format(_L("Updating the Agenda (%d%%)..."), iMap->Count() ? iPos * 50 / iMap->Count() : 50);
	iObserver.CalProgress(prog);
	TInt options = (iSettings.iSpare[0] & KPmCalOptKeepBoth) ? CM_OPT_KEEPBOTH : 0;
	for (TInt n = 0; n < KStep && iPos < iMap->Count(); n++)
		{
		TPmCalMap& m = (*iMap)[iPos];
		CmFacts f;
		Mem::FillZ(&f, sizeof(f));
		f.flags = m.iFlags;
		if (m.iFlags & KMapPending)
			{
			iPos++;                            // sent, not confirmed yet (offline?)
			continue;
			}
		TBool series = (m.iFlags & KMapSeries) != 0;
		TInt ev;
		if (series)
			ev = FindSeries(*m.iKey, iPos);
		else
			{
			ev = FindEvent(*m.iKey);
			if (ev >= 0 && (*iEvents)[ev].iMapped >= 0)
				ev = -1;                       // two entries for one event: keep the first
			if (ev >= 0) (*iEvents)[ev].iMapped = iPos;
			}
		CAgnEntry* e = FetchL(m.iAuid);
		TBool gone = e == NULL;
		TTime start = Time::NullTTime();
		if (e)
			{
			start = e->Type() == CAgnEntry::EAppt ? e->CastToAppt()->StartDateTime() : e->InstanceStartDate();
			delete e;
			e = NULL;
			}
		TInt64 changed = gone ? TInt64(0) : Changed(m.iAuid);
		f.gone = gone;
		if ((iSettings.iSpare[0] & KPmCalOptStamp) && !gone && m.iChanged.High() != KStampMark)
			{
			// a link from before (PsiMail's, or an older pass): judged by its
			// date this once, then kept by its stamp
			f.local_changed = DateChanged(m.iAuid) != m.iChanged;
			if (!f.local_changed)
				m.iChanged = changed;
			}
		else
			f.local_changed = !gone && changed != m.iChanged;
		f.on_server = ev >= 0;
		f.in_window = start == Time::NullTTime() || (start >= iWinStart && start < iWinEnd) ||
			(iSettings.iSpare[0] & KPmCalOptTodos);       // (to-dos: no window; an event's start is in it anyway)
		const TDesC8* line = NULL;
		TUint32 hash = 0;
		if (ev >= 0)
			{
			line = (*iEvents)[ev].iLine;
			hash = (*iEvents)[ev].iHash;
			f.readable = IsTodo(Field(*line, 4)) ||
				(ParseTime(Field(*line, 5)) != Time::NullTTime() && ParseTime(Field(*line, 6)) != Time::NullTTime());
			f.writable = Writable(*line);
			if (series)
				hash = Fnv(Field(*line, 2));   // a series: changed on the server when its etag moves
			f.no_baseline = m.iHash == 0;
			f.server_changed = hash != m.iHash;
			}
		TInt action = cal_merge(&f, options);
		if (action == CM_DELETE_LOCAL && iGuarded)
			action = CM_SKIP;                  // held back: see GuardCheck
		if (f.on_server && f.readable && !f.gone && f.no_baseline && !(m.iFlags & KMapForce))
			m.iHash = hash;                     // made here: the server's copy is ours
		TInt sline = series && (iSettings.iSpare[0] & KPmCalOptSeriesIn) ? SeriesLine(Field(*m.iKey, 0).Left(Max(0, m.iKey->Length() - 2))) : -1;
		if (series && sline >= 0 && (action == CM_UPDATE_LOCAL || action == CM_KEEP_BOTH || action == CM_READD))
			{
			// the server's series as a repeat again: the old entry goes
			// (unless it was changed here too: then it stays, unlinked, and is sent as new)
			if (action == CM_UPDATE_LOCAL) DeleteL(m.iAuid);
			TUint32 na = AddSeriesL(*(*iEvents)[sline].iLine);
			if (na)
				{
				m.iAuid = na;
				m.iChanged = Changed(na);
				m.iHash = Fnv(Field(*(*iEvents)[sline].iLine, 2));
				m.iFlags = KMapSeries;
				if (action == CM_KEEP_BOTH) iKept++; else if (action == CM_READD) iAdded++; else iChangedN++;
				iPos++;
				continue;
				}
			}
		if (series && (action == CM_UPDATE_LOCAL || action == CM_KEEP_BOTH || action == CM_READD))
			{
			// the Agenda can't be given the server's series back as a repeat:
			// its instances come in as entries of their own (NewStepL), and the
			// Psion's series goes - unless it was changed here too, when it is
			// kept, unlinked, and so sent again as new
			if (action == CM_UPDATE_LOCAL)
				{
				DeleteL(m.iAuid);
				iChangedN++;
				}
			else if (action == CM_KEEP_BOTH)
				iKept++;
			TPtrC8 key(*m.iKey);
			TPtrC8 href = key.Length() >= 2 ? key.Left(key.Length() - 2) : key;
			for (TInt k = 0; k < iEvents->Count(); k++)
				if (Field(*(*iEvents)[k].iLine, 1) == href) (*iEvents)[k].iMapped = -1;
			RemoveMap(iPos);           // (iMapped is only "linked or not": the indexes may move)
			continue;
			}
		switch (action)
			{
		case CM_UNLINK:
			RemoveMap(iPos);
			continue;
		case CM_DELETE_LOCAL:
			DeleteL(m.iAuid);
			iRemoved++;
			RemoveMap(iPos);
			continue;
		case CM_PUSH_DELETE:
			iPushSeries = series;
			PushEntryL('D', m.iAuid, NULL, *line);
			iPushSeries = EFalse;
			m.iFlags = KMapDeleting | (series ? KMapSeries : 0);
			break;
		case CM_READD:
			m.iAuid = AddFromEventL(*line);    // can't delete it there: back it comes
			m.iChanged = Changed(m.iAuid);
			m.iHash = hash;
			m.iFlags = 0;
			iAdded++;
			break;
		case CM_KEEP_BOTH:
			// the Psion's version first, as an entry of its own: it is new
			// to the server, so the next pass sends it
			if (CopyEntryL(m.iAuid))
				iKept++;
			// then as CM_UPDATE_LOCAL
		case CM_UPDATE_LOCAL:
			m.iAuid = UpdateFromEventL(m.iAuid, *line);
			m.iChanged = Changed(m.iAuid);
			m.iHash = hash;
			m.iFlags = 0;
			iChangedN++;
			break;
		case CM_PUSH_CHANGE:
			{
			CAgnEntry* le = FetchL(m.iAuid);
			if (le)
				{
				CleanupStack::PushL(le);
				iPushSeries = series;
				PushEntryL('M', m.iAuid, le, *line);
				iPushSeries = EFalse;
				CleanupStack::PopAndDestroy();
				}
			m.iChanged = changed;
			}
			break;
		default:
			break;
			}
		iPos++;
		}
	return iPos < iMap->Count();
	}

// (KPmCalOptGuard) a half-read calendar, a server that lost its data or a
// wrong account must not empty the Agenda: if more than half the linked
// entries (and more than 10) would be deleted at once, none are, and the
// summary says so. Deleting them by hand, or a sync that finds them back,
// clears it.
void CPmCalSync::GuardCheck()
	{
	iGuarded = EFalse;
	if (!(iSettings.iSpare[0] & KPmCalOptGuard))
		return;
	TInt linked = 0, missing = 0;
	for (TInt i = 0; i < iMap->Count(); i++)
		{
		const TPmCalMap& m = (*iMap)[i];
		if (m.iFlags & (KMapPending | KMapDeleting))
			continue;
		linked++;
		if ((m.iFlags & KMapSeries) ? FindSeries(*m.iKey, -1) < 0 : FindEvent(*m.iKey) < 0)
			missing++;
		}
	iGuarded = missing > 10 && missing * 2 > linked;
	}

// a copy of an Agenda entry, as a new entry (not linked): returns its UID
TUint32 CPmCalSync::CopyEntryL(TUint32 aAuid)
	{
	CAgnEntry* e = FetchL(aAuid);
	if (!e)
		return 0;
	CleanupStack::PushL(e);
	CAgnEntry* copy;
	if (e->Type() == CAgnEntry::EAppt)
		{
		CAgnAppt* a = CAgnAppt::NewL(iPara, iChar);
		copy = a;
		CleanupStack::PushL(copy);
		a->SetStartAndEndDateTime(e->CastToAppt()->StartDateTime(), e->CastToAppt()->EndDateTime());
		}
	else if (e->Type() == CAgnEntry::EEvent)
		{
		CAgnEvent* v = CAgnEvent::NewL(iPara, iChar);
		copy = v;
		CleanupStack::PushL(copy);
		v->SetStartAndEndDate(e->CastToEvent()->StartDate(), e->CastToEvent()->EndDate());
		}
	else
		{
		CleanupStack::PopAndDestroy();       // e
		return 0;
		}
	TBuf<200> text;
	CRichText* rt = e->RichTextL();
	rt->Extract(text, 0, Min(rt->DocumentLength(), text.MaxLength()));
	copy->RichTextL()->InsertL(0, text);
	copy->SetLocationL(e->Location());
	if (e->HasAlarm())
		copy->SetAlarm(e->AlarmDaysWarning(), e->AlarmTime());
	TAgnEntryId id = iModel->AddEntryL(copy);
	CleanupStack::PopAndDestroy(2);      // copy, e
	return iServ->GetUniqueId(id).Id();
	}

// server events the Agenda doesn't have yet
TBool CPmCalSync::NewStepL()
	{
	TBuf<60> prog;
	prog.Format(_L("Updating the Agenda (%d%%)..."), 50 + (iEvents->Count() ? iPos * 40 / iEvents->Count() : 40));
	iObserver.CalProgress(prog);
	TBuf8<260> key;
	if (iPos == 0 && (iSettings.iSpare[0] & KPmCalOptSeriesIn))
		SeriesPrePassL();
	for (TInt n = 0; n < KStep && iPos < iEvents->Count(); iPos++)
		{
		TPmCalEvent& ev = (*iEvents)[iPos];
		if (ev.iMapped >= 0) continue;
		if (Field(*ev.iLine, 4).Locate('S') >= 0) continue;   // (a series' line: SeriesPrePassL's)
		if (!IsTodo(Field(*ev.iLine, 4)) &&
			(ParseTime(Field(*ev.iLine, 5)) == Time::NullTTime() || ParseTime(Field(*ev.iLine, 6)) == Time::NullTTime()))
			continue;                          // a line we can't read
		if (IsTodo(Field(*ev.iLine, 4)) && !(iSettings.iSpare[0] & KPmCalOptTodos))
			continue;
		EventKey(*ev.iLine, key);
		TUint32 auid = AddFromEventL(*ev.iLine);
		ev.iMapped = AddMapL(auid, key, ev.iHash, 0);
		iAdded++;
		n++;
		}
	return iPos < iEvents->Count();
	}

// Psion entries made or changed since sync was turned on
TBool CPmCalSync::LocalStepL()
	{
	CAgnSyncIter* it = (CAgnSyncIter*)iIter;
	if (!it)
		{
		if (iDefaultCal.Length() == 0)
			{
			iObserver.CalLog(_L8("agenda: no calendar for new entries (calendars.txt has none marked D)"));
			return EFalse;                       // nowhere to put them
			}
		iObserver.CalProgress(_L("Looking for new Agenda entries..."));
		it = CAgnSyncIter::NewL(iServ);
		iIter = it;
		it->First();
		}
	for (TInt n = 0; n < 20 && it->Available(); n++, it->Next())
		{
		if (it->HasBeenDeleted()) continue;
		CAgnEntry::TType type = it->Type();
		if (type != CAgnEntry::EAppt && type != CAgnEntry::EEvent &&
			!(type == CAgnEntry::ETodo && (iSettings.iSpare[0] & KPmCalOptTodos))) continue;
		TUint32 auid = it->UniqueId().Id();
		TTime changed = it->LastChangedDate();
		{
		TBuf8<80> l;
		l.Format(_L8("agenda: entry %u type %d changed "), auid, (TInt)type);
		AppendHex64(l, changed.Int64());
		l.Append(_L8(" since "));
		AppendHex64(l, iSince);
		iObserver.CalLog(l);
		}
		// (the Agenda's dates are whole minutes: one changed in the minute
		// sync was turned on is taken too)
		if (iSettings.iSpare[0] & KPmCalOptStamp)
			{
			TDateTime dt = TTime(iSince).DateTime();
			dt.SetSecond(0);
			dt.SetMicroSecond(0);
			if (changed < TTime(dt)) continue;
			}
		else if (changed.Int64() <= iSince) continue;
		TBool known = EFalse;
		for (TInt i = 0; i < iMap->Count() && !known; i++)
			if ((*iMap)[i].iAuid == auid) known = ETrue;
		if (known) continue;
		CAgnEntry* e = FetchL(auid);
		if (!e) continue;
		CleanupStack::PushL(e);
		TTime start = e->Type() == CAgnEntry::EAppt ? e->CastToAppt()->StartDateTime() : e->InstanceStartDate();
		if (e->Type() == CAgnEntry::ETodo)
			{
			PushEntryL('N', auid, e, KNullDesC8);
			AddMapL(auid, KNullDesC8, 0, KMapPending);
			CleanupStack::PopAndDestroy();
			continue;
			}
		// repeating Psion entries stay on the Psion, unless KPmCalOptRepeats:
		// then they go as a series if their repeat can be said as an RRULE
		// and the series has not ended before the window
		if (!e->IsRepeating() && start >= iWinStart && start < iWinEnd)
			{
			PushEntryL('N', auid, e, KNullDesC8);
			AddMapL(auid, KNullDesC8, 0, KMapPending);
			}
		else if (e->IsRepeating() && (iSettings.iSpare[0] & KPmCalOptRepeats) && start < iWinEnd &&
			(e->RptDef()->RepeatForever() || e->RptDef()->EndDate() >= iWinStart))
			{
			TBuf8<120> rule;
			TBuf8<240> ex;
			TBool allday = e->Type() != CAgnEntry::EAppt;
			if ((e->Type() == CAgnEntry::EAppt || e->Type() == CAgnEntry::EEvent) &&
				RepeatOf(e, allday, iSettings.iCal.zone, rule, ex))
				{
				PushEntryL('N', auid, e, KNullDesC8);
				AddMapL(auid, KNullDesC8, 0, KMapPending | KMapSeries);
				}
			}
		CleanupStack::PopAndDestroy();
		}
	return it->Available();
	}

// (KPmCalOptSidecar) sidecar.txt, for Coda's Agenda features: per linked
// entry its calendar, the calendar's colour, the event's time zone, your
// status and the organiser and attendees (psion-netbook's
// docs/agenda-sync-contract.md). Written whole, then put in place.
void CPmCalSync::SidecarL()
	{
	HBufC8* cals = ReadFileL(KCalsFile);
	if (cals) CleanupStack::PushL(cals);
	CDesC8ArrayFlat* out = new(ELeave) CDesC8ArrayFlat(32);
	CleanupStack::PushL(out);
	TBuf8<200> head(_L8("#PSISC1\t"));
	TBuf8<130> file;
	file.Copy(Clip(iSettings.iAgendaFile, 128));
	head.Append(file);
	out->AppendL(head);
	HBufC8* buf = HBufC8::NewLC(1200);
	for (TInt i = 0; i < iMap->Count(); i++)
		{
		const TPmCalMap& m = (*iMap)[i];
		if (m.iFlags & (KMapPending | KMapDeleting)) continue;
		TInt ev = (m.iFlags & KMapSeries) ? FindSeries(*m.iKey, -1) : FindEvent(*m.iKey);
		if (ev < 0) continue;
		const TDesC8& line = *(*iEvents)[ev].iLine;
		TPtrC8 calid = Field(line, 0);
		TPtrC8 name, colour;
		if (cals)
			{
			TLines lines(*cals);
			TPtrC8 l;
			while (lines.Next(l))
				if (l.Length() && l[0] != '#' && Field(l, 0) == calid) { name.Set(Field(l, 5)); colour.Set(Field(l, 6)); break; }
			}
		TPtr8 o = buf->Des();
		o.Zero();
		o.AppendNum((TUint)m.iAuid, EDecimal);
		o.Append('\t'); o.Append(Clip(calid, 12));
		o.Append('\t'); o.Append(Clip(name, 64));
		o.Append('\t'); o.Append(Clip(colour, 6));
		o.Append('\t'); o.Append(Clip(Field(line, 10), 47));
		o.Append('\t'); o.Append(Clip(Field(line, 11), 1));
		o.Append('\t'); o.Append(Clip(Field(line, 12), 119));
		o.Append('\t'); o.Append(Clip(Field(line, 13), 400));
		out->AppendL(o);
		}
	CleanupStack::PopAndDestroy();          // buf
	TFileName path, tmp;
	Path(_L("sidecar.txt"), path);
	Path(_L("sidecar.tmp"), tmp);
	RFile f;
	User::LeaveIfError(f.Replace(iFs, tmp, EFileWrite));
	CleanupClosePushL(f);
	for (TInt k = 0; k < out->Count(); k++)
		{
		User::LeaveIfError(f.Write((*out)[k]));
		User::LeaveIfError(f.Write(_L8("\n")));
		}
	User::LeaveIfError(f.Flush());
	CleanupStack::PopAndDestroy();          // f
	User::LeaveIfError(iFs.Replace(tmp, path));
	CleanupStack::PopAndDestroy();          // out
	if (cals) CleanupStack::PopAndDestroy();
	}

void CPmCalSync::FinishL()
	{
	if (iSettings.iSpare[0] & KPmCalOptSidecar)
		{
		TRAPD(err, SidecarL());               // (Coda's features do without it)
		if (err != KErrNone) { TBuf8<40> l; l.Format(_L8("agenda: sidecar not written (%d)"), err); iObserver.CalLog(l); }
		}
	{
	TBuf8<120> l;
	l.Format(_L8("agenda: done: %d added, %d changed, %d removed, %d to send, %d refused, %d kept"),
		iAdded, iChangedN, iRemoved, iSent, iFailed, iKept);
	iObserver.CalLog(l);
	}
	SaveMapL();
	SavePushL();
	if (iPushedRead)
		{
		TFileName path;
		Path(KPushedFile, path);
		iFs.Delete(path);
		}
	TBool pushed = iPush->Count() > 0;
	// (every part at once is over 200 letters: in a TBuf<120> a sync with
	// changes sent, refused and kept, or with the guard's line, was USER 11
	// - the program gone with no outcome said. Built whole, given clipped
	// to the 120 the callers have room for.)
	TBuf<256> msg;
	if (iAdded || iChangedN || iRemoved)
		{
		msg = _L("Agenda: ");
		TBool any = EFalse;
		if (iAdded) { msg.AppendFormat(_L("%d new"), iAdded); any = ETrue; }
		if (iChangedN) { if (any) msg.Append(_L(", ")); msg.AppendFormat(_L("%d changed"), iChangedN); any = ETrue; }
		if (iRemoved) { if (any) msg.Append(_L(", ")); msg.AppendFormat(_L("%d removed"), iRemoved); }
		}
	else
		msg = _L("Agenda up to date");
	if (iSent)
		msg.AppendFormat(_L("; sending %d change%s"), iSent, iSent == 1 ? _S("") : _S("s"));
	if (iFailed)
		msg.AppendFormat(_L("; %d not accepted by the server"), iFailed);
	if (iKept)
		msg.AppendFormat(_L("; %d changed on both: the Psion's kept as a copy"), iKept);
	if (iGuarded)
		msg.Append(_L("; many events gone from the server - none removed from the Agenda"));
	Close();
	Reset();
	iPhase = EIdle;
	iObserver.CalSyncDone(KErrNone, msg.Left(Min(msg.Length(), 120)), pushed && iSent > 0);
	}

// ---------------------------------------------------------------- a new entry

void CPmCalSync::AddToAgendaL(RFs& aFs, const TDesC& aFile, const TDesC& aTitle, const TDesC& aLocation,
	const TTime& aStart, const TTime& aEnd, TBool aAllDay, TInt aAlarm)
	{
	TEntry entry;
	if (aFs.Entry(aFile, entry) != KErrNone)
		User::Leave(KErrNotFound);
	CParaFormatLayer* para = CParaFormatLayer::NewL();
	CleanupStack::PushL(para);
	CCharFormatLayer* chr = CCharFormatLayer::NewL();
	CleanupStack::PushL(chr);
	RAgendaServ* serv = RAgendaServ::NewL();
	CleanupStack::PushL(serv);
	User::LeaveIfError(serv->Connect());
	CleanupClosePushL(*serv);
	CAgnEntryModel* model = CAgnEntryModel::NewL();
	CleanupStack::PushL(model);
	model->SetServer(serv);
	model->OpenL(aFile, TTimeIntervalMinutes(9 * 60), TTimeIntervalMinutes(9 * 60), TTimeIntervalMinutes(9 * 60));
	serv->WaitUntilLoaded();
	CAgnEntry* e;
	if (aAllDay)
		{
		CAgnEvent* ev = CAgnEvent::NewL(para, chr);
		e = ev;
		CleanupStack::PushL(e);
		ev->SetStartAndEndDate(aStart, aEnd);
		}
	else
		{
		CAgnAppt* a = CAgnAppt::NewL(para, chr);
		e = a;
		CleanupStack::PushL(e);
		a->SetStartAndEndDateTime(aStart, aEnd);
		}
	e->RichTextL()->InsertL(0, aTitle);
	if (aLocation.Length())
		e->SetLocationL(aLocation);
	if (aAlarm >= 0)
		{
		TTime at = aStart - TTimeIntervalMinutes(aAlarm);
		TInt days = Midnight(aStart).DaysFrom(Midnight(at)).Int();
		TDateTime d = at.DateTime();
		e->SetAlarm(TTimeIntervalDays(days), TTimeIntervalMinutes(d.Hour() * 60 + d.Minute()));
		}
	model->AddEntryL(e);
	CleanupStack::PopAndDestroy();       // e
	CleanupStack::PopAndDestroy();       // model
	serv->CloseAgenda();
	CleanupStack::PopAndDestroy();       // serv->Close()
	CleanupStack::Pop();                 // serv
	delete serv;
	CleanupStack::PopAndDestroy(2);      // chr, para
	}
