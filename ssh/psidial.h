// psidial.h - the shared connection for a program that talks to the
// network itself (its own RSocketServ, RHostResolver and RSocket) rather
// than through psiglue: Internet time (PsiTime.exe), PsiBoot's updates,
// PsiLinux. Header only; needs c32.lib, nifman.lib, efsrv.lib, euser.lib.
//
// Why: on the Psion Internet route a modem such as the PsiConnect Atom must
// first be sent the "first send" command of Connection settings (ATD777)
// before it speaks PPP. psiglue does that before every dial (StartPpp, in
// psiglue.cpp: PsiMail, PsiWeb, PsiTerm, PsiSync, PsiFetch). A program whose
// own name lookup starts the dial-up left it to EPOC's NetDial, which runs
// the Internet service's modem script ("ATDWT123456"): the Atom takes that
// for a host name and answers NO CARRIER, so nothing connected unless
// another program had brought the link up first.
//
// TPsiDial::Start does what psiglue does, for these programs:
//   - no PsiLink.ini, or the first send empty: nothing (NetDial dials, as
//     before; an Ethernet card needs nothing sent)
//   - the Psion's Internet connection already up (another program's):
//     nothing, and End leaves it up
//   - the modem route: KErrNotSupported (the caller decides: TPsiModem
//     below makes a call over it)
//   - else the serial port at Connection settings' speed, AT (with +++ ATH
//     if the modem is still in an old call), the first send, CONNECT, and
//     the port handed back to EPOC: the caller's first lookup then brings
//     PPP up. Bounded throughout: 30 s at most for CONNECT.
// TPsiDial::End, after use, takes the link down again only if Start brought
// it up and Connection settings has "Stay connected" off; with it on, the
// link is held as by the other programs (psihold.exe).
//
// TPsiModem: one call on the modem route ("ATDT host:port"), for a program
// that needs a few bytes from a plain TCP service (PsiTime's web Date).
#ifndef PSIDIAL_H
#define PSIDIAL_H

#include <e32base.h>
#include <f32file.h>
#include <c32comm.h>
#include <e32hal.h>
#include <netdial.h>          // TNetDialProgress: EStartingHangUp, EFinishedHangUp
#include "psilink.h"

// microseconds since aStart (User::TickCount: monotonic, not the clock)
inline TInt PsiDialSince(TUint aStart)
	{
	TTimeIntervalMicroSeconds32 period;
	TInt tickUs = UserHal::TickPeriod(period) == KErrNone && period.Int() > 0 ? period.Int() : 15625;
	return (TInt)(User::TickCount() - aStart) * tickUs;
	}

// ----- the serial port ----------------------------------------------------

class TPsiSerial
	{
public:
	TPsiSerial() : iOpen(EFalse), iServerOpen(EFalse), iHeld(0) {}
	~TPsiSerial() { Close(); }

	// KErrNone, KErrInUse (a program or the Psion's own Internet connection
	// has the port) or KErrAccessDenied (the Link to desktop, Ctrl+L)
	TInt Open(const TPsiLink& aLink)
		{
		TInt r = User::LoadPhysicalDevice(_L("EUART1"));
		if (r != KErrNone && r != KErrAlreadyExists)
			return r;
		r = User::LoadLogicalDevice(_L("ECOMM"));
		if (r != KErrNone && r != KErrAlreadyExists)
			return r;
		r = StartC32();
		if (r != KErrNone && r != KErrAlreadyExists)
			return r;
		if (!iServerOpen)
			{
			if ((r = iServer.Connect()) != KErrNone)
				return r;
			iServerOpen = ETrue;
			}
		r = iServer.LoadCommModule(_L("ECUART"));
		if (r != KErrNone && r != KErrAlreadyExists)
			return r;
		r = iComm.Open(iServer, _L("COMM::0"), ECommExclusive);
		for (TInt tries = 0; r == KErrInUse && tries < 3; tries++)
			{
			User::After(300000);             // (a port just closed is held a moment)
			r = iComm.Open(iServer, _L("COMM::0"), ECommExclusive);
			}
		if (r != KErrNone)
			return r;
		iOpen = ETrue;
		TCommConfig cfg;
		iComm.Config(cfg);
		static const TBps KRates[] = { EBps9600, EBps19200, EBps38400, EBps57600, EBps115200 };
		cfg().iRate = KRates[aLink.iBaudIndex >= 0 && aLink.iBaudIndex <= 4 ? aLink.iBaudIndex : 4];
		cfg().iDataBits = EData8;
		cfg().iStopBits = EStop1;
		cfg().iParity = EParityNone;
		cfg().iFifo = EFifoEnable;
		cfg().iTerminatorCount = 0;
		cfg().iHandshake = aLink.iRtsCts ? KConfigObeyCTS : 0;   // (as psiglue's OpenSerial)
		iComm.Cancel();
		if ((r = iComm.SetConfig(cfg)) != KErrNone)
			{
			Close();
			return r;
			}
		iComm.SetSignals(KSignalDTR | KSignalRTS, 0);
		TRequestStatus wake;                 // (a zero-length read powers the UART: DTR up)
		TBuf8<4> none;
		iComm.Read(wake, none, 0);
		User::WaitForRequest(wake);
		iHeld = 0;
		return KErrNone;
		}

	void Close()
		{
		if (iOpen)
			{
			iComm.Cancel();
			iComm.Close();
			iOpen = EFalse;
			}
		if (iServerOpen)
			{
			iServer.Close();
			iServerOpen = EFalse;
			}
		}

	TInt Write(const TDesC8& aData)
		{
		TRequestStatus st;
		iComm.Write(st, TTimeIntervalMicroSeconds32(10000000), aData);
		User::WaitForRequest(st);
		return st.Int();
		}

	// what has come in, waiting up to aUs for something: KErrNone with aOut
	// (possibly empty: nothing came), or an error
	TInt ReadSome(TDes8& aOut, TInt aUs)
		{
		aOut.Zero();
		if (iHeld)
			{
			TInt n = Min(iHeld, aOut.MaxLength());
			aOut.Copy(iHold.Left(n));
			iHold.Delete(0, n);
			iHeld -= n;
			return KErrNone;
			}
		TInt avail = iComm.QueryReceiveBuffer();
		TRequestStatus st;
		if (avail > 0)
			{
			TPtr8 p((TUint8*)aOut.Ptr(), 0, Min(avail, aOut.MaxLength()));
			iComm.Read(st, p);
			User::WaitForRequest(st);
			aOut.SetLength(p.Length());
			return st.Int();
			}
		TPtr8 p((TUint8*)aOut.Ptr(), 0, 1);
		iComm.Read(st, TTimeIntervalMicroSeconds32(aUs), p);
		User::WaitForRequest(st);
		aOut.SetLength(p.Length());
		return st.Int() == KErrTimedOut ? KErrNone : st.Int();
		}

	// one line the modem said (without the CR LF), waiting up to aUs:
	// KErrNone, KErrTimedOut or an error
	TInt ReadLine(TDes8& aLine, TInt aUs)
		{
		aLine.Zero();
		TUint start = User::TickCount();
		for (;;)
			{
			TBuf8<64> part;
			TInt elapsed = PsiDialSince(start);
			if (elapsed >= aUs)
				return KErrTimedOut;
			TInt r = ReadSome(part, Min(aUs - elapsed, 250000));
			if (r != KErrNone)
				return r;
			for (TInt i = 0; i < part.Length(); i++)
				{
				TUint8 c = part[i];
				if (c == '\r' || c == '\n')
					{
					if (aLine.Length())
						{
						Unread(part.Mid(i + 1));
						return KErrNone;
						}
					continue;
					}
				if (aLine.Length() < aLine.MaxLength())
					aLine.Append(c);
				}
			}
		}

	// AT -> OK within aUs?
	TBool At(TInt aUs)
		{
		iComm.ResetBuffers();
		iHeld = 0;
		iHold.Zero();
		if (Write(_L8("AT\r")) != KErrNone)
			return EFalse;
		TBuf8<80> line;
		for (TInt i = 0; i < 4; i++)
			{
			if (ReadLine(line, aUs) != KErrNone)
				return EFalse;
			if (line.Left(2) == _L8("OK"))
				return ETrue;
			}
		return EFalse;
		}

	// out of an old call: +++ (with the guard times), ATH
	void Escape()
		{
		User::After(1100000);
		Write(_L8("+++"));
		User::After(1100000);
		Write(_L8("ATH\r"));
		User::After(500000);
		iComm.ResetBuffers();
		iHeld = 0;
		iHold.Zero();
		}

	// sends aCmd (no CR) and waits up to aUs for its result: KErrNone on
	// CONNECT, KErrCouldNotConnect on a refusal (aLine says which),
	// KErrTimedOut on no answer
	TInt Dial(const TDesC8& aCmd, TDes8& aLine, TInt aUs)
		{
		iComm.ResetBuffers();
		iHeld = 0;
		iHold.Zero();
		TBuf8<120> cmd(aCmd.Left(Min(aCmd.Length(), 118)));
		cmd.Append('\r');
		if (Write(cmd) != KErrNone)
			return KErrCouldNotConnect;
		TUint start = User::TickCount();
		for (;;)
			{
			TInt left = aUs - PsiDialSince(start);
			if (left <= 0 || ReadLine(aLine, left) != KErrNone)
				return KErrTimedOut;
			if (aLine.Left(2) == _L8("AT") || aLine.Left(2) == _L8("at"))
				continue;                    // (its echo)
			if (aLine.Left(7) == _L8("CONNECT"))
				return KErrNone;
			if (aLine.Left(5) == _L8("ERROR") || aLine.Left(10) == _L8("NO CARRIER") || aLine.Left(4) == _L8("BUSY")
				|| aLine.Left(9) == _L8("NO ANSWER") || aLine.Left(7) == _L8("NO DIAL"))
				return KErrCouldNotConnect;
			}
		}

private:
	void Unread(const TDesC8& aRest)
		{
		TInt n = Min(aRest.Length(), iHold.MaxLength() - iHold.Length());
		iHold.Append(aRest.Left(n));
		iHeld = iHold.Length();
		}
	RCommServ iServer;
	RComm iComm;
	TBool iOpen, iServerOpen;
	TBuf8<64> iHold;                         // read past the end of a line
	TInt iHeld;
	};

// ----- Psion Internet: the first send ---------------------------------------

// Is the Psion's Internet connection up, or coming up for another program?
// 1 yes, 0 no. A link NIFMAN is hanging up (its idle time ran out, or a
// Stop) still says "active" for a moment: that is waited out, at most 10 s,
// and is "no" - the lookup would otherwise start NetDial's own dial with no
// first send (seen in the emulator: Internet time just after a sync's link
// went down)
inline TInt PsiDialLinkUp()
	{
	RNif nif;
	if (nif.Open() != KErrNone)
		return 0;
	TInt up = 0;
	TUint start = User::TickCount();
	for (;;)
		{
		TBool active = EFalse;
		if (nif.NetworkActive(active) != KErrNone || !active)
			break;
		TNifProgress pr;
		TInt stage = nif.Progress(pr) == KErrNone ? pr.iStage : 0;
		if (stage != EStartingHangUp && stage != EFinishedHangUp)
			{
			up = 1;                          // open, or another program dialling it
			break;
			}
		if (PsiDialSince(start) > 10000000)
			break;
		User::After(500000);
		}
	nif.Close();
	return up;
	}

class TPsiDial
	{
public:
	TPsiDial() : iOpened(EFalse), iKeepUp(EFalse), iNetMode(0), iHave(EFalse) {}

	// Before the program's first lookup or connect. KErrNone: go on (the
	// link is up, or the modem is now in PPP, or there was nothing to
	// send); KErrNotSupported: the modem route; else an error, with aWhy in
	// words. aSay (may be NULL) is told the steps, for a busy message.
	TInt Start(RFs& aFs, TDes& aWhy, void (*aSay)(const TDesC&) = NULL)
		{
		aWhy.Zero();
		iOpened = EFalse;
		iLink.SetDefaults();
		iHave = iLink.Load(aFs);
		iNetMode = iLink.iNetMode;
		iKeepUp = iLink.iKeepUp;
		if (!iHave)
			return KErrNone;                 // (no settings: the Psion's own, as before)
		if (!iNetMode)
			return KErrNotSupported;
		if (PsiDialLinkUp())
			return KErrNone;                 // someone else's link: used, and left up
		iOpened = ETrue;                     // (down now: whatever brings it up is ours)
		if (!iLink.iPppStart.Length())
			return KErrNone;                 // NetDial dials, as it is set up to
		if (aSay)
			aSay(_L("Connecting\x85"));
		TPsiSerial port;
		TInt r = port.Open(iLink);
		if (r == KErrInUse || r == KErrAccessDenied)
			{
			// NIFMAN may be bringing the link up for another program this
			// moment, which holds the port: go on, as psiglue does; if not,
			// the lookup fails and says so
			iOpened = EFalse;
			return KErrNone;
			}
		if (r != KErrNone)
			{
			aWhy.Copy(_L("Could not open the serial port"));
			return r;
			}
		port.Write(_L8("\r"));
		User::After(300000);
		if (!port.At(1500000))
			{
			// no OK: still in an old call, or already in PPP (a WiRSa that
			// ignores +++); one escape, then on regardless, as psiglue
			port.Escape();
			port.At(1500000);
			}
		TBuf8<48> cmd;
		cmd.Copy(iLink.iPppStart);
		TBuf8<80> line;
		r = port.Dial(cmd, line, 30000000);
		port.Close();                        // (the port to the Psion's TCP/IP)
		if (r == KErrTimedOut)
			{
			aWhy.Copy(_L("No answer from the modem to the Psion Internet start command (see Connection settings)"));
			iOpened = EFalse;
			return KErrTimedOut;
			}
		if (r != KErrNone)
			{
			aWhy.Copy(_L("The modem refused the Psion Internet start command (see Connection settings)"));
			iOpened = EFalse;
			return KErrCouldNotConnect;
			}
		User::After(200000);
		return KErrNone;
		}

	// After use: the link down again if Start found it down (this program
	// brought it up) and "Stay connected" is off; held up if it is on
	void End(RFs& aFs)
		{
		if (!iOpened || PsiLinkActive() != 1)
			return;
		if (iKeepUp)
			PsiHoldStart(aFs, RProcess().FileName());
		else
			PsiLinkDisconnect(10000000);
		iOpened = EFalse;
		}

	TBool Opened() const { return iOpened; }
	TBool Modem() const { return iHave && !iNetMode; }
	TPsiLink iLink;
private:
	TBool iOpened, iKeepUp;
	TInt iNetMode;
	TBool iHave;
	};

// ----- the modem route: one call -----------------------------------------

class TPsiModem
	{
public:
	// "ATDT aHost:aPort": KErrNone once connected (then Write, ReadSome,
	// End), else an error with aWhy in words
	TInt Call(const TPsiLink& aLink, const TDesC8& aHost, TInt aPort, TDes& aWhy)
		{
		aWhy.Zero();
		TInt r = iPort.Open(aLink);
		if (r == KErrInUse || r == KErrAccessDenied)
			{
			aWhy.Copy(PsiLinkActive() == 1 ? _L("Psion Internet has the serial port - disconnect it first")
				: _L("The PC link (Ctrl+L) or a program has the serial port"));
			return r;
			}
		if (r != KErrNone)
			{
			aWhy.Copy(_L("Could not open the serial port"));
			return r;
			}
		iPort.Write(_L8("\r"));
		User::After(300000);
		if (!iPort.At(1500000))
			{
			iPort.Escape();
			if (!iPort.At(1500000))
				{
				aWhy.Copy(_L("No answer from the modem - is it on?"));
				iPort.Close();
				return KErrNotReady;
				}
			}
		TBuf8<120> cmd(_L8("ATDT "));
		cmd.Append(aHost.Left(Min(aHost.Length(), 100)));
		cmd.Append(':');
		cmd.AppendNum(aPort);
		TBuf8<80> line;
		r = iPort.Dial(cmd, line, 30000000);
		if (r != KErrNone)
			{
			if (r == KErrTimedOut)
				aWhy.Copy(_L("No answer from the modem"));
			else
				aWhy.Copy(line.Left(Min(line.Length(), aWhy.MaxLength())));
			End();
			return r == KErrTimedOut ? r : KErrCouldNotConnect;
			}
		return KErrNone;
		}
	TInt Write(const TDesC8& aData) { return iPort.Write(aData); }
	TInt ReadSome(TDes8& aOut, TInt aUs) { return iPort.ReadSome(aOut, aUs); }
	// hangs up (+++ ATH) and lets the port go
	void End()
		{
		iPort.Escape();
		iPort.Close();
		}
private:
	TPsiSerial iPort;
	};

#endif
