/* tlstest.c - tls13.c on a PC against OpenSSL's s_server (test/tlstest.py
 * drives both): the line is a TCP connection to 127.0.0.1:PORT in place of
 * psiglue's. The client sends "ping", then answers each line the server
 * sends with "got:<line>", until "bye". With KeyUpdates from the server
 * (review CR-26) the lines after them must still come, and after one that
 * asks for ours, the server must still read our answers.
 *   tlstest PORT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "psishared.h"
#include "tls13.h"

static int g_fd = -1;
static PsiShared g_ps;
static unsigned char g_rx[4096];
static int g_rxpos, g_rxlen;

PsiShared *pg_shared(void) { return &g_ps; }
int tls_rng_repeatable(void) { return 0; }
void genrandom(unsigned char *buf, unsigned int len)
{
	FILE *f = fopen("/dev/urandom", "rb");
	if (!f || fread(buf, 1, len, f) != len) { fprintf(stderr, "no /dev/urandom\n"); exit(2); }
	fclose(f);
}
int pg_serial_write(const void *buf, int len) { return (int)send(g_fd, buf, len, 0); }
static void fill(int ms)
{
	fd_set r;
	struct timeval tv;
	int n;
	if (g_rxpos < g_rxlen) return;
	FD_ZERO(&r); FD_SET(g_fd, &r);
	tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
	if (select(g_fd + 1, &r, 0, 0, &tv) <= 0) return;
	n = (int)recv(g_fd, g_rx, sizeof(g_rx), 0);
	if (n > 0) { g_rxpos = 0; g_rxlen = n; }
}
int pg_net_avail(void) { fill(0); return g_rxlen - g_rxpos; }
int pg_net_read(void *buf, int max)
{
	int n = g_rxlen - g_rxpos;
	if (n > max) n = max;
	memcpy(buf, g_rx + g_rxpos, n);
	g_rxpos += n;
	return n;
}
int pg_wait(int ms, int want_net, int want_kbd)
{
	(void)want_kbd;
	if (want_net) fill(ms);
	return g_rxlen > g_rxpos ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct sockaddr_in a;
	char why[120], line[256];
	int k = 0;
	if (argc != 2) { fprintf(stderr, "tlstest PORT\n"); return 2; }
	g_fd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(atoi(argv[1]));
	a.sin_addr.s_addr = inet_addr("127.0.0.1");
	g_ps.port = atoi(argv[1]);
	if (connect(g_fd, (struct sockaddr *)&a, sizeof(a)) != 0) { perror("connect"); return 2; }
	if (tls_connect("localhost", why, sizeof(why)) != 0) { printf("handshake: %s\n", why); return 1; }
	printf("connected\n");
	fflush(stdout);
	if (tls_write("ping\n", 5) != 0) { printf("write failed\n"); return 1; }
	for (;;) {
		char c;
		int n = tls_read(&c, 1, 20000);
		if (n <= 0) { printf("read %d: %s\n", n, tls_error()); fflush(stdout); return 1; }
		if (c != '\n') { if (k < (int)sizeof(line) - 1) line[k++] = c; continue; }
		line[k] = 0;
		k = 0;
		printf("line: %s\n", line);
		fflush(stdout);
		if (!strcmp(line, "bye")) break;
		{
			char out[300];
			int m = snprintf(out, sizeof(out), "got:%s\n", line);
			if (tls_write(out, m) != 0) { printf("write failed\n"); return 1; }
		}
	}
	tls_close();
	close(g_fd);
	printf("done\n");
	return 0;
}
