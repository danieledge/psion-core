#!/usr/bin/env python3
"""tlstest.py TLSTEST - tls13.c (built for the PC as TLSTEST, test/tlstest.c)
against OpenSSL's s_server on 127.0.0.1, with a throwaway self-signed key
made here. For each cipher: lines both ways; the server's KeyUpdate (s_server
"k"), after which its lines must still be read; and a KeyUpdate asking for
ours ("K"), after which the server must still read our answers (review
CR-26: a KeyUpdate was ignored and the next record failed). Skipped (exit 0)
when there is no openssl."""
import os, sys, subprocess, socket, shutil, tempfile, time, threading

EXE = os.path.abspath(sys.argv[1])
if not shutil.which('openssl'):
    print('tlstest: no openssl, skipped')
    sys.exit(0)
work = tempfile.mkdtemp(prefix='tlstest-')
key, crt = os.path.join(work, 'k.pem'), os.path.join(work, 'c.pem')
subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', key, '-out', crt,
                '-days', '2', '-subj', '/CN=localhost'], check=True, capture_output=True)
fails = 0

def check(what, good):
    global fails
    print(('ok   ' if good else 'FAIL ') + what)
    if not good: fails += 1

def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p

def run(cipher):
    port = free_port()
    srv = subprocess.Popen(['openssl', 's_server', '-accept', str(port), '-cert', crt, '-key', key, '-tls1_3',
                            '-ciphersuites', cipher, '-ign_eof'], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    got = []
    threading.Thread(target=lambda: [got.append(l.rstrip('\n')) for l in srv.stdout], daemon=True).start()
    for _ in range(50):
        if any('ACCEPT' in l for l in got): break
        time.sleep(0.1)
    cli = subprocess.Popen([EXE, str(port)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    def wait_for(text, secs=10):
        t = time.time() + secs
        while time.time() < t:
            if any(l == text for l in got): return True
            time.sleep(0.05)
        return False
    def say(line):
        srv.stdin.write(line + '\n'); srv.stdin.flush()
    check(cipher + ': connected, ping', wait_for('ping'))
    say('one'); check(cipher + ': a line each way', wait_for('got:one'))
    say('k'); time.sleep(0.5)
    say('two'); check(cipher + ": the server's KeyUpdate: its next line read", wait_for('got:two'))
    say('K'); time.sleep(0.5)
    say('three'); check(cipher + ': a KeyUpdate asking for ours: our answer read by the server', wait_for('got:three'))
    say('k'); time.sleep(0.3); say('K'); time.sleep(0.3)
    say('four'); check(cipher + ': two more: still both ways', wait_for('got:four'))
    say('bye')
    try: out = cli.communicate(timeout=10)[0]
    except subprocess.TimeoutExpired: cli.kill(); out = cli.communicate()[0]
    check(cipher + ': the client ended cleanly', cli.returncode == 0 and 'done' in out)
    if cli.returncode != 0: print(out)
    srv.kill(); srv.wait()

run('TLS_CHACHA20_POLY1305_SHA256')
run('TLS_AES_128_GCM_SHA256')
shutil.rmtree(work, ignore_errors=True)
print('tlstest: %d failed' % fails)
sys.exit(1 if fails else 0)
