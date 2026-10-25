/* pgdevbox.h - where PsiProxy's own services are (Send log, and a dev tool's
   builds). That is the shared proxy (PsiLink.ini). If it is the Atom modem's
   own web proxy (192.168.7.1 on its PPP link, or "psiproxy" through the
   modem), they go to the address in the optional C:\System\Data\PsiDevBox.ini
   ("<host>\r\n<port>\r\n", written by an optional dev tool's Dev box...
   dialog) if there is one, else to the Atom's proxy itself: a modem set to
   a PsiProxy on a computer (AT$PU) passes them on to it. With no proxy they
   are not available. No address is built in. Plain C, for the engines;
   TPsiLink::DevBox (psilink.h) is the same rule for the apps.

   The answer counts only if it is PsiProxy's: the Atom's proxy says it has
   no PsiProxy behind it with a 501 and "X-PsiConnect: no-psiproxy", and an
   older Atom answers with its own status page (200, HTML). Both are "Needs
   PsiProxy on a computer" (pg_psiproxy_answer). */
#ifndef PGDEVBOX_H
#define PGDEVBOX_H

#include <stdio.h>
#include <string.h>

#define PG_DEVBOX_FILE "C:\\System\\Data\\PsiDevBox.ini"
#define PG_DEVBOX_PORT 8081            /* PsiProxy's standard port */

/* 1 if host is empty or the Atom's own proxy */
static int pg_is_atom_proxy(const char *host)
{
	static const char *const atom[2] = { "192.168.7.1", "psiproxy" };
	int i, k;
	if (!host || !host[0])
		return 1;
	for (i = 0; i < 2; i++) {
		for (k = 0; atom[i][k]; k++) {
			char c = host[k];
			if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
			if (c != atom[i][k]) break;
		}
		if (!atom[i][k] && !host[k])
			return 1;
	}
	return 0;
}

/* PsiDevBox.ini: 1 and the address if it is there and sound */
static int pg_read_devbox(char *host, int hostmax, int *port)
{
	char d[100];
	int n = 0, k, p, v = 0, digits = 0;
	FILE *f = fopen(PG_DEVBOX_FILE, "rb");
	if (!f)
		return 0;
	k = (int)fread(d, 1, sizeof(d) - 1, f);
	fclose(f);
	if (k <= 0)
		return 0;
	d[k] = 0;
	while (n < k && d[n] != '\r' && d[n] != '\n')
		n++;
	if (n == 0 || n >= hostmax || n > 60)
		return 0;
	for (p = 0; p < n; p++)                  /* (a name or an address: no spaces or controls) */
		if ((unsigned char)d[p] <= ' ' || (unsigned char)d[p] > '~')
			return 0;
	p = n;
	while (p < k && (d[p] == '\r' || d[p] == '\n'))
		p++;
	while (p < k && d[p] >= '0' && d[p] <= '9' && digits < 6) {
		v = v * 10 + (d[p++] - '0');
		digits++;
	}
	if (v <= 0 || v >= 65536)
		return 0;
	for (p = 0; p < n; p++)
		host[p] = d[p];
	host[n] = 0;
	*port = v;
	return 1;
}

/* Where PsiProxy's services are, from the shared proxy: 1 and host and port
   set, or 0 if there is nowhere ("Not available - set a proxy ...") */
static int pg_devbox(const char *proxy_host, int proxy_port, const char **host, int *port)
{
	static char devbox[64];
	if (!proxy_host || !proxy_host[0])
		return 0;
	if (pg_is_atom_proxy(proxy_host) && pg_read_devbox(devbox, (int)sizeof(devbox), port)) {
		*host = devbox;
		return 1;
	}
	*host = proxy_host;                      /* (the Atom's: PsiProxy behind it, if it has one) */
	*port = proxy_port > 0 && proxy_port < 65536 ? proxy_port : PG_DEVBOX_PORT;
	return 1;
}

/* The name for the Host: line. To the Atom's own proxy it is "psiproxy",
   either way it is reached: an older Atom then answers itself (its status
   page) and does not try to fetch the address from the Internet. */
static const char *pg_devbox_hostname(const char *host)
{
	return host && host[0] && pg_is_atom_proxy(host) ? "psiproxy" : host;
}

#define PG_NEEDS_PSIPROXY "Needs PsiProxy on a computer"

/* 1 if a header line of the reply is the Atom's "X-PsiConnect: no-psiproxy" */
static int pg_no_psiproxy_line(const char *line)
{
	static const char name[] = "x-psiconnect:", value[] = "no-psiproxy";
	int i, k;
	for (i = 0; name[i]; i++) {
		char c = line[i];
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		if (c != name[i]) return 0;
	}
	while (line[i] == ' ' || line[i] == '\t')
		i++;
	for (k = 0; value[k]; k++) {
		char c = line[i + k];
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		if (c != value[k]) return 0;
	}
	return 1;
}

/* What an answer from where PsiProxy's services are means: status, whether
   a header said no-psiproxy, and whether the body was the service's own
   (PsiProxy's "Log saved: ..." for /_putlog, a "#PSIDIST1" list...) */
#define PG_ANSWER_OK      0              /* PsiProxy's: done */
#define PG_ANSWER_NEEDS   1              /* not PsiProxy: PG_NEEDS_PSIPROXY */
#define PG_ANSWER_FAILED  2              /* a failure: the status says which */
static int pg_psiproxy_answer(int status, int no_psiproxy, int ours)
{
	if (no_psiproxy)
		return PG_ANSWER_NEEDS;
	if (status >= 200 && status < 300)      /* (an older Atom's status page is 200) */
		return ours ? PG_ANSWER_OK : PG_ANSWER_NEEDS;
	return PG_ANSWER_FAILED;
}

/* 1 if the first line of a /_putlog answer is PsiProxy's "Log saved: ..." */
static int pg_putlog_saved(const char *body)
{
	return body && !strncmp(body, "Log saved", 9);
}

#endif
