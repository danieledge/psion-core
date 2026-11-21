// psierr.h's messages on a PC (test/errstub's descriptors panic as ER5's
// do): the words, no tail when the reason is not known, and every length
// cut to the buffer
#include "psierr.h"

static int gFails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); gFails++; } } while (0)

static bool Is(TDes& aMsg, const char* aWant)
	{
	bool ok = std::strcmp(aMsg.Str(), aWant) == 0;
	if (!ok)
		std::printf("  got \"%s\", want \"%s\"\n", aMsg.Str(), aWant);
	return ok;
	}

int main()
	{
	TBuf<80> m;
	PsiErrMsg(m, _L("Not saved"), KErrDiskFull);
	CHECK(Is(m, "Not saved - the disk is full"));
	PsiErrMsg(m, _L("Not saved"), KErrNotSupported);
	CHECK(Is(m, "Not saved - not supported"));
	PsiErrMsg(m, _L("Not connected"), KErrCouldNotConnect);
	CHECK(Is(m, "Not connected - no connection"));
	// a reason not in the table: no tail
	PsiErrMsg(m, _L("Not saved"), KErrGeneral);
	CHECK(Is(m, "Not saved"));
	PsiErrMsg(m, _L("Not saved"), -9999);
	CHECK(Is(m, "Not saved"));

	// PsiErrTail after the caller's own text
	m.Copy(_L("Cannot play"));
	PsiErrTail(m, KErrNoMemory);
	CHECK(Is(m, "Cannot play - not enough memory"));
	m.Copy(_L("Cannot play"));
	PsiErrTail(m, KErrArgument);
	CHECK(Is(m, "Cannot play"));

	// short buffers: what fits, never a panic
	TBuf<12> s;
	PsiErrMsg(s, _L("Not saved"), KErrDiskFull);   // 12 - 3 - 16 < 0: no room for the text
	CHECK(s.Length() <= 12);
	TBuf<20> t;
	PsiErrMsg(t, _L("Not saved"), KErrNotFound);    // 9 + 3 + 9 = 21 > 20
	CHECK(t.Length() <= 20);
	CHECK(Is(t, "Not save - not found"));       // (the text is cut, the reason kept)
	TBuf<10> u;
	u.Copy(_L("0123456"));
	PsiErrTail(u, KErrDiskFull);                    // room for 3 only: nothing
	CHECK(Is(u, "0123456"));
	u.Copy(_L("012345"));
	PsiErrTail(u, KErrDiskFull);                    // room for 4: " - t"
	CHECK(Is(u, "012345 - t"));
	TBuf<40> w;
	PsiErrMsg(w, _L("A very long description of what did not happen here"), KErrNotReady);
	CHECK(Is(w, "A very long descrip - the card is not in"));

	// on the card
	PsiOnCardMsg(m, _L("Help"));
	CHECK(Is(m, "Help is on the card - put the card in"));
	TBuf<30> c;
	PsiOnCardMsg(c, _L("PsiWeb"));                 // the tail alone is 33
	CHECK(c.Length() <= 30);

	if (gFails)
		{
		std::printf("psierrtest: %d failed\n", gFails);
		return 1;
		}
	std::printf("psierrtest: all passed\n");
	return 0;
	}
