#!/usr/bin/env python3
"""fakeppp.py SOCKET [--tcp PORT=HOST:PORT ...] [--ntp-offset S] [--echo S] [--no-ppp]
- the Atom modem's Psion Internet route on a unix socket, for the emulator's
serial bridge (run.sh EMU_SERIAL=SOCKET, nbrun.py --modem SOCKET): AT
commands as the Atom (echo, OK), a numeric dial (ATD777, ATDT*99#) answers
CONNECT and the line becomes a PPP link, as psiconnect's firmware with
AT$PPP=1 (README "PPP: the Psion's own Internet"). hostmodem has no PPP on
the PC (ppp.cpp's host half is a stub), so the route could not be tested in
the emulator before.

The PPP peer is just enough for the Psion: LCP (no authentication), IPCP
(the Psion's static 192.168.7.2, this end 192.168.7.1, DNS 192.168.7.1),
LCP echo, Protocol-Reject for the rest. Over it:
  - DNS (UDP 53 to any address): every name is 192.168.7.10 (A records only;
    anything else is NXDOMAIN)
  - SNTP (UDP 123): the PC's clock (+ --ntp-offset seconds)
  - TCP: a small, in-order stack (the serial bridge loses nothing): a
    connection to any address on PORT goes to the --tcp PORT=HOST:PORT
    mapping on the PC (127.0.0.1 only); with no mapping it is refused (RST)
--no-ppp: a numeric dial answers NO CARRIER (a modem without PPP).
--echo S: an LCP echo every S seconds of the PC's clock (3); no answer to
three means the Psion's end has gone ("ppp: down"): a hang-up drops DTR,
which the bridge does not carry. The emulator runs faster than the clock on
the wall, so short values suit it (0.5).
Everything is logged to stdout: "dial", "ppp: lcp up", "ppp: ipcp up",
"dns <name>", "ntp", "tcp <port> open/close", "ppp: down"."""
import os, select, socket, struct, sys, time

OUR_IP = bytes([192, 168, 7, 1])
PSION_IP = bytes([192, 168, 7, 2])
FAKE_IP = bytes([192, 168, 7, 10])
ECHO = 3.0                 # seconds (wall clock) between LCP echoes: --echo


def log(*a):
    print('%.1f' % (time.time() - T0), *a, flush=True)


T0 = time.time()

# ----- HDLC framing (RFC 1662) --------------------------------------------------
FCSTAB = []
for b in range(256):
    v = b
    for _ in range(8):
        v = (v >> 1) ^ 0x8408 if v & 1 else v >> 1
    FCSTAB.append(v)


def fcs16(data):
    f = 0xffff
    for c in data:
        f = (f >> 8) ^ FCSTAB[(f ^ c) & 0xff]
    return f


def frame(proto, payload):
    body = b'\xff\x03' + struct.pack('>H', proto) + payload
    f = fcs16(body) ^ 0xffff
    body += struct.pack('<H', f)
    out = bytearray(b'\x7e')
    for c in body:
        if c < 0x20 or c in (0x7d, 0x7e):
            out += bytes([0x7d, c ^ 0x20])
        else:
            out.append(c)
    out.append(0x7e)
    return bytes(out)


def ip_sum(data):
    if len(data) & 1:
        data += b'\0'
    s = sum(struct.unpack('>%dH' % (len(data) // 2), data))
    while s >> 16:
        s = (s & 0xffff) + (s >> 16)
    return (~s) & 0xffff


class Tcp:
    """One TCP connection, the Psion's end active."""

    def __init__(self, peer, sport, dport, dst, seq, sock):
        self.peer, self.sport, self.dport, self.dst = peer, sport, dport, dst
        self.rcv_nxt = (seq + 1) & 0xffffffff
        self.iss = int(time.time() * 1000) & 0xffffff
        self.snd_una = self.iss
        self.snd_nxt = (self.iss + 1) & 0xffffffff
        self.out = b''           # unacked + unsent, from snd_una + (1 while SYN unacked)
        self.sock = sock
        self.local_eof = False
        self.fin_sent = False
        self.fin_seen = False
        self.last_tx = 0
        self.wnd = 2048
        self.closed = False
        self.syn_acked = False

    def seg(self, flags, seq, payload=b'', opts=b''):
        hl = (20 + len(opts)) // 4
        tcp = struct.pack('>HHIIBBHHH', self.dport, self.sport, seq, self.rcv_nxt, hl << 4, flags,
                          4096, 0, 0) + opts + payload
        pseudo = self.dst + PSION_IP + struct.pack('>BBH', 0, 6, len(tcp))
        c = ip_sum(pseudo + tcp)
        tcp = tcp[:16] + struct.pack('>H', c) + tcp[18:]
        self.peer.send_ip(6, self.dst, tcp)


class Ppp:
    def __init__(self, conn, tcpmap, ntp_offset):
        self.conn = conn
        self.tcpmap = tcpmap
        self.ntp_offset = ntp_offset
        self.buf = bytearray()
        self.esc = False
        self.inframe = False
        self.lcp_up = self.lcp_acked = self.lcp_ackrecv = False
        self.ipcp_acked = self.ipcp_ackrecv = False
        self.ident = 1
        self.tcps = {}
        self.ended = False
        self.last_req = 0
        self.echo_at = self.echo_ok = time.time()
        self.echo_lost = False
        self.send_lcp_req()

    # -- output
    def write(self, data):
        # (paced a little: the emulator's UART takes what fits its FIFO)
        for i in range(0, len(data), 256):
            self.conn.sendall(data[i:i + 256])
            if len(data) > 256:
                time.sleep(0.01)

    def send(self, proto, payload):
        self.write(frame(proto, payload))

    def send_lcp_req(self):
        self.ident = (self.ident + 1) & 0xff
        self.send(0xc021, struct.pack('>BBH', 1, self.ident, 4))
        self.last_req = time.time()

    def send_ipcp_req(self):
        self.ident = (self.ident + 1) & 0xff
        opt = bytes([3, 6]) + OUR_IP
        self.send(0x8021, struct.pack('>BBH', 1, self.ident, 4 + len(opt)) + opt)
        self.last_req = time.time()

    def send_ip(self, proto, src, payload):
        hdr = struct.pack('>BBHHHBBH4s4s', 0x45, 0, 20 + len(payload), 0, 0, 64, proto, 0, src, PSION_IP)
        hdr = hdr[:10] + struct.pack('>H', ip_sum(hdr)) + hdr[12:]
        self.send(0x0021, hdr + payload)

    # -- input
    def feed(self, data):
        for c in data:
            if c == 0x7e:
                if len(self.buf) >= 4:
                    self.got(bytes(self.buf))
                self.buf = bytearray()
                self.esc = False
            elif c == 0x7d:
                self.esc = True
            else:
                if self.esc:
                    c ^= 0x20
                    self.esc = False
                self.buf.append(c)

    def got(self, f):
        if fcs16(f) != 0xf0b8:
            log('ppp: bad FCS, %d bytes' % len(f))
            return
        f = f[:-2]
        if f[:2] == b'\xff\x03':
            f = f[2:]
        if f[0] & 1:
            proto, p = f[0], f[1:]
        else:
            proto, p = struct.unpack('>H', f[:2])[0], f[2:]
        if proto == 0xc021:
            self.lcp(p)
        elif proto == 0x8021:
            self.ipcp(p)
        elif proto == 0x0021:
            self.ip(p)
        else:
            log('ppp: protocol %04x rejected' % proto)
            self.ident = (self.ident + 1) & 0xff
            body = struct.pack('>H', proto) + p[:200]
            self.send(0xc021, struct.pack('>BBH', 8, self.ident, 4 + len(body)) + body)

    def options(self, p):
        code, ident, ln = struct.unpack('>BBH', p[:4])
        opts, i = [], 4
        while i + 2 <= ln and i < len(p):
            t, l = p[i], p[i + 1]
            if l < 2:
                break
            opts.append((t, p[i + 2:i + l]))
            i += l
        return code, ident, opts

    def lcp(self, p):
        code, ident, opts = self.options(p)
        if code == 1:
            rej = [(t, v) for t, v in opts if t == 3]           # authentication: none here
            if rej:
                body = b''.join(bytes([t, len(v) + 2]) + v for t, v in rej)
                self.send(0xc021, struct.pack('>BBH', 4, ident, 4 + len(body)) + body)
                log('lcp: rejected authentication')
                return
            self.send(0xc021, struct.pack('>BBH', 2, ident, len(p)) + p[4:])
            self.lcp_acked = True
            log('lcp: their request acked: options %s' % [t for t, v in opts])
        elif code == 2:
            self.lcp_ackrecv = True
        elif code in (3, 4):
            self.send_lcp_req()
        elif code == 5:
            self.send(0xc021, struct.pack('>BBH', 6, ident, 4))
            log('ppp: down (the Psion ended the link: LCP Terminate-Request)')
            self.ended = True
            return
        elif code == 9:
            self.send(0xc021, struct.pack('>BBH', 10, ident, len(p)) + b'\0\0\0\0' + p[8:])
        elif code == 10:
            self.echo_ok = time.time()
            if self.echo_lost:
                log('ppp: echo answered again')
                self.echo_lost = False
        if self.lcp_acked and self.lcp_ackrecv and not self.lcp_up:
            self.lcp_up = True
            log('ppp: lcp up')
            self.send_ipcp_req()

    def ipcp(self, p):
        code, ident, opts = self.options(p)
        if code == 1:
            rej = [(t, v) for t, v in opts if t not in (3, 129, 131)]
            if rej:
                body = b''.join(bytes([t, len(v) + 2]) + v for t, v in rej)
                self.send(0x8021, struct.pack('>BBH', 4, ident, 4 + len(body)) + body)
                return
            nak = []
            for t, v in opts:
                if t == 3 and v != PSION_IP:
                    nak.append((t, PSION_IP))
                if t in (129, 131) and v != OUR_IP:
                    nak.append((t, OUR_IP))
            if nak:
                body = b''.join(bytes([t, len(v) + 2]) + v for t, v in nak)
                self.send(0x8021, struct.pack('>BBH', 3, ident, 4 + len(body)) + body)
                return
            self.send(0x8021, struct.pack('>BBH', 2, ident, len(p)) + p[4:])
            self.ipcp_acked = True
        elif code == 2:
            self.ipcp_ackrecv = True
        elif code in (3, 4):
            self.send_ipcp_req()
        elif code == 5:
            self.send(0x8021, struct.pack('>BBH', 6, ident, 4))
        if self.ipcp_acked and self.ipcp_ackrecv:
            if not getattr(self, 'ip_up', False):
                self.ip_up = True
                log('ppp: ipcp up (Psion 192.168.7.2, peer 192.168.7.1)')

    def ip(self, p):
        if len(p) < 20 or p[0] >> 4 != 4:
            return
        ihl = (p[0] & 15) * 4
        proto = p[9]
        src, dst = p[12:16], p[16:20]
        body = p[ihl:struct.unpack('>H', p[2:4])[0]]
        if proto == 17:
            sport, dport, ln = struct.unpack('>HHH', body[:6])
            data = body[8:ln]
            reply = None
            if dport == 53:
                reply = self.dns(data)
            elif dport == 123:
                reply = self.ntp(data)
            if reply is not None:
                u = struct.pack('>HHHH', dport, sport, 8 + len(reply), 0) + reply
                pseudo = dst + PSION_IP + struct.pack('>BBH', 0, 17, len(u))
                u = u[:6] + struct.pack('>H', ip_sum(pseudo + u) or 0xffff) + u[8:]
                self.send_ip(17, dst, u)
        elif proto == 6:
            self.tcp(dst, body)
        elif proto == 1 and body[:1] == b'\x08':
            r = b'\0' + body[1:2] + b'\0\0' + body[4:]
            r = r[:2] + struct.pack('>H', ip_sum(r)) + r[4:]
            self.send_ip(1, dst, r)

    def dns(self, q):
        ident, flags, qd = struct.unpack('>HHH', q[:6])
        i, labels = 12, []
        while i < len(q) and q[i]:
            labels.append(q[i + 1:i + 1 + q[i]].decode('latin-1'))
            i += 1 + q[i]
        qtype = struct.unpack('>H', q[i + 1:i + 3])[0] if i + 3 <= len(q) else 0
        question = q[12:i + 5]
        name = '.'.join(labels)
        log('dns %s (type %d)' % (name, qtype))
        if qtype == 1:
            ans = b'\xc0\x0c' + struct.pack('>HHIH', 1, 1, 60, 4) + FAKE_IP
            return struct.pack('>HHHHHH', ident, 0x8180, 1, 1, 0, 0) + question + ans
        return struct.pack('>HHHHHH', ident, 0x8183, 1, 0, 0, 0) + question

    def ntp(self, q):
        if len(q) < 48:
            return None
        now = time.time() + self.ntp_offset + 2208988800
        ts = struct.pack('>II', int(now), int((now % 1) * 2 ** 32))
        vn = (q[0] >> 3) & 7
        log('ntp: answered (version %d)' % vn)
        return bytes([(vn << 3) | 4, 2, 6, 0xec]) + b'\0' * 8 + b'LOCL' + ts + q[40:48] + ts + ts

    # -- TCP
    def tcp(self, dst, b):
        sport, dport, seq, ack, off, flags, wnd = struct.unpack('>HHIIBBH', b[:16])
        data = b[(off >> 4) * 4:]
        key = (sport, dport, dst)
        c = self.tcps.get(key)
        if flags & 0x04:                       # RST
            if c:
                log('tcp %d: reset by the Psion' % dport)
                self.drop(key)
            return
        if flags & 0x02 and not c:             # SYN
            target = self.tcpmap.get(dport)
            s = None
            if target:
                try:
                    s = socket.create_connection(target, timeout=5)
                    s.setblocking(False)
                except OSError as e:
                    log('tcp %d: %s:%d: %s' % (dport, target[0], target[1], e))
                    s = None
            c = Tcp(self, sport, dport, dst, seq, s)
            if not s:
                log('tcp %d: refused (nothing mapped)' % dport)
                c.seg(0x14, 0)
                return
            self.tcps[key] = c
            c.wnd = wnd
            c.seg(0x12, c.iss, opts=b'\x02\x04\x02\x18')     # SYN+ACK, MSS 536
            c.last_tx = time.time()
            log('tcp %d: open -> %s:%d' % (dport, target[0], target[1]))
            return
        if not c:
            if not flags & 0x04:
                t = Tcp(self, sport, dport, dst, (seq - 1) & 0xffffffff, None)
                t.rcv_nxt = (seq + len(data)) & 0xffffffff
                t.seg(0x04, ack)
            return
        c.wnd = wnd
        if flags & 0x10:                       # ACK
            acked = (ack - c.snd_una) & 0xffffffff
            inflight = (c.snd_nxt - c.snd_una) & 0xffffffff
            if 0 < acked <= inflight:
                if not c.syn_acked:
                    c.syn_acked = True
                    acked -= 1
                c.out = c.out[acked:]
                c.snd_una = ack
                if c.fin_sent and c.snd_una == c.snd_nxt and c.fin_seen:
                    self.drop(key)
                    return
        if data:
            if seq == c.rcv_nxt:
                c.rcv_nxt = (c.rcv_nxt + len(data)) & 0xffffffff
                try:
                    c.sock.sendall(data)
                except OSError:
                    pass
            c.seg(0x10, c.snd_nxt)
        if flags & 0x01 and seq + len(data) == c.rcv_nxt and not c.fin_seen:   # FIN
            c.fin_seen = True
            c.rcv_nxt = (c.rcv_nxt + 1) & 0xffffffff
            c.seg(0x10, c.snd_nxt)
            try:
                c.sock.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            if c.fin_sent and c.snd_una == c.snd_nxt:
                self.drop(key)

    def drop(self, key):
        c = self.tcps.pop(key, None)
        if c and c.sock:
            log('tcp %d: closed' % c.dport)
            c.sock.close()

    def pump(self):
        now = time.time()
        if not self.lcp_up and now - self.last_req > 3:
            self.send_lcp_req()
        if self.lcp_up and not getattr(self, 'ip_up', False) and now - self.last_req > 3:
            self.send_ipcp_req()
        # LCP echo every 3 s once up: no answer for 8 s means the Psion's
        # end of the link has gone (a hang-up by RNif::Stop drops DTR, which
        # the emulator's bridge does not carry)
        if self.lcp_up and now - self.echo_at > ECHO:
            self.echo_at = now
            self.ident = (self.ident + 1) & 0xff
            self.send(0xc021, struct.pack('>BBH', 9, self.ident, 8) + b'\0\0\0\0')
            if not self.echo_lost and now - self.echo_ok > ECHO * 3:
                self.echo_lost = True
                log('ppp: down (no answer to LCP echo: the Psion hung up)')
        for key, c in list(self.tcps.items()):
            if c.sock and not c.local_eof:
                try:
                    d = c.sock.recv(8192)
                    if d:
                        c.out += d
                    else:
                        c.local_eof = True
                except BlockingIOError:
                    pass
                except OSError:
                    c.local_eof = True
            if not c.syn_acked:
                if now - c.last_tx > 2:
                    c.seg(0x12, c.iss, opts=b'\x02\x04\x02\x18')
                    c.last_tx = now
                continue
            sent = (c.snd_nxt - c.snd_una) & 0xffffffff
            if sent and now - c.last_tx > 2:         # retransmit from snd_una
                c.snd_nxt = c.snd_una
                if c.fin_sent:
                    c.fin_sent = False
                sent = 0
            limit = min(c.wnd, 2048)
            while sent < len(c.out) and sent < limit:
                chunk = c.out[sent:sent + min(536, limit - sent)]
                c.seg(0x18, c.snd_nxt, chunk)
                c.snd_nxt = (c.snd_nxt + len(chunk)) & 0xffffffff
                sent += len(chunk)
                c.last_tx = now
            if c.local_eof and not c.fin_sent and sent == len(c.out):
                c.seg(0x11, c.snd_nxt)
                c.snd_nxt = (c.snd_nxt + 1) & 0xffffffff
                c.fin_sent = True
                c.last_tx = now
                c.out += b''

    def socks(self):
        return [c.sock for c in self.tcps.values() if c.sock and not c.local_eof]


def main():
    args = sys.argv[1:]
    path = args[0]
    tcpmap, offset, noppp = {}, 0.0, False
    i = 1
    while i < len(args):
        if args[i] == '--tcp':
            k, v = args[i + 1].split('=')
            h, p = v.rsplit(':', 1)
            tcpmap[int(k)] = (h, int(p))
            i += 2
        elif args[i] == '--ntp-offset':
            offset = float(args[i + 1])
            i += 2
        elif args[i] == '--echo':
            global ECHO
            ECHO = float(args[i + 1])
            i += 2
        elif args[i] == '--no-ppp':
            noppp = True
            i += 1
        else:
            raise SystemExit('unknown: ' + args[i])
    if os.path.exists(path):
        os.remove(path)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(1)
    log('fakeppp on %s, tcp %s' % (path, tcpmap))
    while True:
        conn, _ = srv.accept()
        log('emulator connected')
        line = b''
        ppp = None
        while True:
            rs = [conn] + (ppp.socks() if ppp else [])
            r, _, _ = select.select(rs, [], [], 0.05)
            if conn in r:
                d = conn.recv(4096)
                if not d:
                    log('emulator closed the bridge')
                    break
                if ppp and ppp.echo_lost and b'\x7e' not in d:
                    # the Psion hung up (its DTR, which the bridge does not
                    # carry): the Atom is back at its command prompt
                    log('back to commands (the call ended)')
                    ppp = None
                    line = b''
                if ppp:
                    # AT commands while the call is up and before LCP: EPOC's
                    # NetDial running its own modem script (Hayes) over the
                    # call psiglue's first send started. The real netBook
                    # sees the carrier (DCD) up and the Atom takes only PPP
                    # frames; the emulator's bridge has no DCD, so here each
                    # line is answered as a modem would (OK, a dial CONNECT)
                    if b'\x7e' not in d and not ppp.lcp_up and not ppp.buf:
                        line += d
                        while b'\r' in line:
                            cmd, line = line.split(b'\r', 1)
                            cmd = cmd.strip().decode('latin-1')
                            if cmd:
                                log('during the call: %s' % cmd)
                                conn.sendall(b'\r\nCONNECT 115200\r\n' if cmd.upper().startswith('ATD') else b'\r\nOK\r\n')
                        continue
                    line = b''
                    ppp.feed(d)
                    if ppp.ended:
                        ppp = None
                        conn.sendall(b'\r\nNO CARRIER\r\n')
                        log('back to commands')
                    continue
                conn.sendall(d)                     # echo
                line += d
                while b'\r' in line:
                    cmd, line = line.split(b'\r', 1)
                    cmd = cmd.strip().decode('latin-1')
                    if not cmd:
                        continue
                    u = cmd.upper().replace(' ', '')
                    if u.startswith('ATD') and u[3:].lstrip('TP').replace('*', '').replace('#', '').isdigit():
                        log('dial %s' % cmd)
                        if noppp:
                            conn.sendall(b'\r\nNO CARRIER\r\n')
                            continue
                        conn.sendall(b'\r\nCONNECT 115200\r\n')
                        time.sleep(0.05)
                        ppp = Ppp(conn, tcpmap, offset)
                        line = b''
                        break
                    elif u.startswith('ATD'):
                        log('dial %s: not a PPP dial, NO CARRIER' % cmd)
                        conn.sendall(b'\r\nNO CARRIER\r\n')
                    else:
                        log('cmd %s' % cmd)
                        conn.sendall(b'\r\nOK\r\n')
            if ppp:
                for c in list(ppp.tcps.values()):
                    pass
                ppp.pump()
        conn.close()


if __name__ == '__main__':
    main()
