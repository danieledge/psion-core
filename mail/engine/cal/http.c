/* http.c - HTTP/1.1 requests for CalDAV (see cal.h)
 *
 * One keep-alive connection on the engine's single network line (pmnet.c):
 * after the first request the TLS handshake - the slow part on a Psion - is
 * not repeated. If IMAP or SMTP used the line in between, or the server
 * closed it, the next request connects again.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calport.h"
#include "cal.h"

static char g_host[80], g_auth[300];
static char g_base[80];               /* the host set up (http_target): http_follow stays in its domain */
/* the User-Agent: a program's own (http_user_agent), or ("" until then)
   the one every caller sent before there was a choice. (Zeroed, not
   initialised data.) */
static char g_ua[80];
#define UA_DEFAULT "PsiMail (Psion Series 5mx)"
static int  g_port, g_tls;
static int  g_conn = -1;              /* pmn_conn_id() of our connection */

static void b64(const unsigned char *in, int n, char *out)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int i, k = 0;
	for (i = 0; i < n; i += 3) {
		unsigned long v = (unsigned long)in[i] << 16;
		if (i + 1 < n) v |= (unsigned long)in[i + 1] << 8;
		if (i + 2 < n) v |= in[i + 2];
		out[k++] = t[(v >> 18) & 63];
		out[k++] = t[(v >> 12) & 63];
		out[k++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
		out[k++] = i + 2 < n ? t[v & 63] : '=';
	}
	out[k] = 0;
}

void http_user_agent(const char *ua)
{
	int i, k = 0;
	/* (one header line: no line breaks or other controls) */
	for (i = 0; ua && ua[i] && k < (int)sizeof(g_ua) - 1; i++)
		if ((unsigned char)ua[i] >= 32 && ua[i] != 127) g_ua[k++] = ua[i];
	g_ua[k] = 0;
}

void http_target(const char *host, int port, int tls, const char *user, const char *pass)
{
	char raw[200];
	if (strcmp(host, g_host) || port != g_port) g_conn = -1;
	pm_copy(g_host, host, sizeof(g_host));
	pm_copy(g_base, host, sizeof(g_base));
	g_port = port;
	g_tls = tls;
	snprintf(raw, sizeof(raw), "%s:%s", user, pass);
	b64((const unsigned char *)raw, (int)strlen(raw), g_auth);
	memset(raw, 0, sizeof(raw));
}

/* (review CR-19) a domain someone can register: two labels or more, and
   three where the last two are a country's second level for everyone
   (co.uk, com.au, co.jp...: anyone may have a name under them) */
static int registrable(const char *d)
{
	static const char *const k_second[] = {
		"co", "com", "net", "org", "ac", "gov", "edu", "ne", "or", "go", "gob", "gv",
		"mil", "nic", "ltd", "plc", "sch", "nhs", "me", "nom", "info", "biz", "web", 0 };
	const char *last = strrchr(d, '.'), *p;
	int labels = 1, i, n;
	for (p = d; *p; p++) if (*p == '.') labels++;
	if (!last || labels < 2 || d[0] == '.' || !last[1]) return 0;
	if (labels >= 3 || strlen(last + 1) != 2) return 1;
	/* two labels, a country's: "co.uk" is not one, "example.uk" is */
	n = (int)(last - d);
	for (i = 0; k_second[i]; i++)
		if ((int)strlen(k_second[i]) == n && !pm_strncasecmp(d, k_second[i], n)) return 0;
	return 1;
}

/* host ends ".dom" (and is longer)? */
static int under(const char *host, const char *dom)
{
	int hl = (int)strlen(host), dl = (int)strlen(dom);
	return dl > 0 && hl > dl + 1 && host[hl - dl - 1] == '.' && !pm_strncasecmp(host + hl - dl, dom, dl);
}

/* is host the one set up, a name under it, or in its domain?
   ("caldav.icloud.com": any *.icloud.com; "example.co.uk": only names under
   it, never another *.co.uk; a name without a dot, or an address, only
   itself) */
static int same_domain(const char *host)
{
	const char *dom = strchr(g_base, '.');
	if (!pm_strncasecmp(host, g_base, 80) && strlen(host) == strlen(g_base)) return 1;
	if (!dom || strspn(g_base, "0123456789.") == strlen(g_base)) return 0;
	if (under(host, g_base)) return 1;      /* (dav.example.co.uk for example.co.uk) */
	return registrable(dom + 1) && under(host, dom + 1);
}

void http_follow(const char *url, char *out, int max)
{
	const char *p = url, *h, *e, *slash;
	char host[80];
	int tls = -1, port, n;
	if (!pm_strncasecmp(p, "https://", 8)) { tls = 1; p += 8; }
	else if (!pm_strncasecmp(p, "http://", 7)) { tls = 0; p += 7; }
	if (tls < 0) { pm_copy(out, url[0] ? url : "/", max); return; }       /* a path already */
	slash = strchr(p, '/');
	pm_copy(out, slash ? slash : "/", max);
	h = p;
	e = slash ? slash : p + strlen(p);
	for (n = 0; h + n < e && h[n] != ':' && h[n] != '@'; n++) {}
	if (h + n < e && h[n] == '@') return;   /* (user@host: not followed) */
	if (n <= 0 || n >= (int)sizeof(host)) return;
	memcpy(host, h, n);
	host[n] = 0;
	port = tls ? 443 : 80;
	if (h[n] == ':') {
		port = atoi(h + n + 1);
		if (port <= 0 || port > 65535) return;
	}
	if (tls != g_tls || !same_domain(host)) {
		if (pm_strncasecmp(host, g_host, 80) || port != g_port)
			pm_log("http: %s:%d not followed (another domain or scheme): only the path", host, port);
		return;
	}
	if (pm_strncasecmp(host, g_host, 80) || (int)strlen(host) != (int)strlen(g_host) || port != g_port) {
		pm_log("http: now to %s:%d", host, port);
		pm_copy(g_host, host, sizeof(g_host));
		g_port = port;
		g_conn = -1;                             /* (the next request connects there) */
	}
}

const char *http_status_words(int status)
{
	pm_log("http: status %d", status);
	if (status == 401) return "would not take the user name or password";
	if (status == 403) return "does not allow it";
	if (status == 404 || status == 410) return "found nothing at that address";
	if (status == 405 || status == 501) return "does not offer it at that address";
	if (status == 429 || status == 503) return "is busy - try again later";
	if (status >= 500) return "had a problem - try again later";
	if (status >= 300 && status < 400) return "sent it to another address";
	return "did not accept the request";
}

void http_close(void)
{
	if (g_conn >= 0 && pmn_conn_id() == g_conn) pmn_close(0);
	g_conn = -1;
}

static int connect_now(char *why, int whymax)
{
	if (g_conn >= 0 && pmn_conn_id() == g_conn && pmn_is_open()) return CAL_RES_OK;
	if (calp_offline()) { snprintf(why, whymax, "Working offline"); return CAL_RES_OFFLINE; }
	if (pmn_connect(g_host, g_port, g_tls, why, whymax) != 0) {
		if (tlsv_problem()[0]) {
			char hp[80];
			snprintf(hp, sizeof(hp), "%s:%d", g_host, g_port);
			calp_untrusted(hp, tlsv_problem(), tlsv_fingerprint());
			return CAL_RES_UNTRUSTED;
		}
		return pm_cancelled() ? CAL_RES_CANCELLED : CAL_RES_OFFLINE;
	}
	g_conn = pmn_conn_id();
	return CAL_RES_OK;
}

static int lost(int r, char *why, int whymax)
{
	g_conn = -1;
	if (r == PMN_CANCEL || pm_cancelled()) { snprintf(why, whymax, "Stopped"); return CAL_RES_CANCELLED; }
	snprintf(why, whymax, r == PMN_TIMEOUT ? "The calendar server stopped answering" : "Lost the connection to the calendar server");
	pmn_close(1);
	return CAL_RES_OFFLINE;
}

/* reads exactly n body bytes (n < 0: until the connection closes) */
static int body_bytes(long n, HttpResp *r, HttpSink sink, void *ctx)
{
	static char b[1024];
	while (n != 0) {
		int want = n < 0 || n > (long)sizeof(b) ? (int)sizeof(b) : (int)n;
		int k = pmn_read(b, want, 60000);
		if (k == 0 && n < 0) return 0;
		if (k <= 0) return k == 0 ? -1 : k;
		if (sink) sink(b, k, ctx);
		r->length += k;
		if (n > 0) n -= k;
	}
	return 0;
}

int http_request(const char *method, const char *path, const char *headers,
                 const char *body, int blen, HttpResp *r, HttpSink sink, void *ctx,
                 char *why, int whymax)
{
	static char line[600], req[1400];
	int attempt, res, k;
	memset(r, 0, sizeof(*r));
	for (attempt = 0; attempt < 2; attempt++) {
		int chunked = 0, close_after = 0, got_status = 0;
		long clen = -1;
		res = connect_now(why, whymax);
		if (res != CAL_RES_OK) return res;
		k = snprintf(req, sizeof(req),
			"%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n"
			"Authorization: Basic %s\r\nContent-Length: %d\r\n%s\r\n",
			method, path, g_host, g_ua[0] ? g_ua : UA_DEFAULT, g_auth, blen, headers ? headers : "");
		if (k >= (int)sizeof(req)) { snprintf(why, whymax, "Request too long"); return CAL_RES_FAILED; }
		if (pmn_write(req, k) != 0 || (blen > 0 && pmn_write(body, blen) != 0)) {
			g_conn = -1;
			if (attempt == 0) continue;       /* the server had closed the connection */
			return lost(-1, why, whymax);
		}
		/* status line and headers */
		for (;;) {
			int n = pmn_readline(line, sizeof(line), 60000);
			if (n < 0) {
				if (!got_status && attempt == 0 && n != PMN_CANCEL) { g_conn = -1; pmn_close(0); break; }
				return lost(n, why, whymax);
			}
			if (!got_status) {
				if (n == 0) continue;
				if (n < 12 || strncmp(line, "HTTP/1.", 7)) { snprintf(why, whymax, "Not a web server: %.40s", line); return CAL_RES_FAILED; }
				r->status = atoi(line + 9);
				got_status = 1;
				/* an HTTP/1.0 server closes after each response unless it says
				   keep-alive (Radicale's own server does): reusing the line
				   would send the next request into a closed connection - on a
				   modem, into its command mode */
				if (line[7] == '0') close_after = 1;
				continue;
			}
			if (n == 0) break;
			if (!pm_strncasecmp(line, "Content-Length:", 15)) clen = atol(line + 15);
			else if (!pm_strncasecmp(line, "Transfer-Encoding:", 18) && pm_stristr(line, "chunked")) chunked = 1;
			else if (!pm_strncasecmp(line, "Connection:", 11) && pm_stristr(line, "close")) close_after = 1;
			else if (!pm_strncasecmp(line, "Connection:", 11) && pm_stristr(line, "keep-alive")) close_after = 0;
			else if (!pm_strncasecmp(line, "ETag:", 5)) {
				const char *v = line + 5;
				while (*v == ' ') v++;
				pm_copy(r->etag, v, sizeof(r->etag));
			} else if (!pm_strncasecmp(line, "Location:", 9)) {
				const char *v = line + 9;
				while (*v == ' ') v++;
				pm_copy(r->location, v, sizeof(r->location));
			}
		}
		if (!got_status) continue;             /* retry on a new connection */
		/* the body */
		if (r->status == 204 || r->status == 304 || !strcmp(method, "HEAD")) clen = 0;
		if (chunked) {
			for (;;) {
				long size;
				char *e;
				int n = pmn_readline(line, sizeof(line), 60000);
				if (n < 0) return lost(n, why, whymax);
				if (n == 0) continue;
				size = strtol(line, &e, 16);
				if (e == line) return lost(-1, why, whymax);   /* (not a chunk's size: a damaged answer, not its end) */
				if (size <= 0) {
					while ((n = pmn_readline(line, sizeof(line), 60000)) > 0) {}   /* trailers */
					break;
				}
				if ((n = body_bytes(size, r, sink, ctx)) != 0) return lost(n, why, whymax);
			}
		} else if (clen >= 0) {
			if ((k = body_bytes(clen, r, sink, ctx)) != 0) return lost(k, why, whymax);
		} else {
			/* (to the close: a timeout part-way is a failure, not a whole
			   answer - a cut calendar list lost the calendars missing from
			   it. So is a close that was not the server's own end: a reset,
			   the line lost, the modem's NO CARRIER (pmn_clean_end). A clean
			   close without TLS's close_notify reads as -1 and is the end,
			   as many servers end so) */
			k = body_bytes(-1, r, sink, ctx);
			if (k == PMN_TIMEOUT || k == PMN_CANCEL) return lost(k, why, whymax);
			if (!pmn_clean_end()) {
				pm_log("http: the answer (%ld bytes, no length) ended without a clean close: not used", r->length);
				return lost(-1, why, whymax);
			}
			close_after = 1;
		}
		if (close_after) { pmn_close(0); g_conn = -1; }
		if (r->status == 401) {
			snprintf(why, whymax, "The calendar server did not accept the password");
			return CAL_RES_LOGIN_FAILED;
		}
		return CAL_RES_OK;
	}
	return lost(-1, why, whymax);
}
