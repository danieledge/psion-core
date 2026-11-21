/* e32std.h for psierrtest.cpp only: the few 8-bit descriptors psierr.h
   uses, with ER5's panics turned into a failed test (Left past the end,
   USER 22; Copy or Append past MaxLength, USER 11) */
#ifndef PSIERR_TEST_E32STD_H
#define PSIERR_TEST_E32STD_H
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef int TInt;
typedef unsigned char TUint8;

static inline void Panic(const char* aWhat, int aCode)
	{
	std::printf("PANIC %s %d\n", aWhat, aCode);
	std::exit(1);
	}

template <class T> inline T Min(T a, T b) { return a < b ? a : b; }
template <class T> inline T Max(T a, T b) { return a > b ? a : b; }

class TPtrC;
class TDesC
	{
public:
	TInt Length() const { return iLength; }
	const TUint8* Ptr() const { return iPtr; }
	inline TPtrC Left(TInt aLength) const;
protected:
	TDesC(const TUint8* aPtr, TInt aLength) : iPtr(aPtr), iLength(aLength) {}
	const TUint8* iPtr;
	TInt iLength;
	};

class TPtrC : public TDesC
	{
public:
	TPtrC(const TUint8* aPtr, TInt aLength) : TDesC(aPtr, aLength) {}
	TPtrC(const TDesC& aDes) : TDesC(aDes.Ptr(), aDes.Length()) {}
	};

inline TPtrC TDesC::Left(TInt aLength) const
	{
	if (aLength < 0 || aLength > iLength)
		Panic("USER", 22);
	return TPtrC(iPtr, aLength);
	}

class TDes : public TDesC
	{
public:
	TInt MaxLength() const { return iMax; }
	void Copy(const TDesC& aDes)
		{
		if (aDes.Length() > iMax)
			Panic("USER", 11);
		std::memmove(iBuf, aDes.Ptr(), aDes.Length());
		iLength = aDes.Length();
		}
	void Append(const TDesC& aDes)
		{
		if (iLength + aDes.Length() > iMax)
			Panic("USER", 11);
		std::memmove(iBuf + iLength, aDes.Ptr(), aDes.Length());
		iLength += aDes.Length();
		}
	const char* Str() { iBuf[iLength] = 0; return (const char*)iBuf; }
protected:
	TDes(TUint8* aBuf, TInt aMax) : TDesC(aBuf, 0), iBuf(aBuf), iMax(aMax) {}
	TUint8* iBuf;
	TInt iMax;
	};

template <TInt S> class TBuf : public TDes
	{
public:
	TBuf() : TDes(iData, S) {}
private:
	TUint8 iData[S + 1];
	};

struct TLitC
	{
	const char* iText;
	TPtrC operator()() const { return TPtrC((const TUint8*)iText, (TInt)std::strlen(iText)); }
	};
#define _L(s) TPtrC((const TUint8*)(s), (TInt)std::strlen(s))
#define _LIT(aName, aText) static const TLitC aName = { aText }

enum
	{
	KErrNone = 0, KErrNotFound = -1, KErrGeneral = -2, KErrCancel = -3, KErrNoMemory = -4,
	KErrNotSupported = -5, KErrArgument = -6, KErrOverflow = -9, KErrAlreadyExists = -11,
	KErrPathNotFound = -12, KErrDied = -13, KErrInUse = -14, KErrServerBusy = -16,
	KErrNotReady = -18, KErrCorrupt = -20, KErrAccessDenied = -21, KErrLocked = -22,
	KErrDisMounted = -24, KErrEof = -25, KErrDiskFull = -26, KErrBadName = -28,
	KErrTimedOut = -33, KErrCouldNotConnect = -34, KErrTooBig = -40
	};

#endif
