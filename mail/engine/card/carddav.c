/* carddav.c - contacts: CardDAV (RFC 6352) - see card.h
 *
 * The engine's half of contacts sync, as caldav.c is the calendar's: it
 * finds the user's address books, sends the changes made in the Psion's
 * Contacts, and fetches the cards that changed (only books whose ctag
 * moved; in them only cards whose ETag moved: a PROPFIND for the ETags,
 * then addressbook-multiget for the changed ones, ten at a time). The
 * Contacts half (core/mail/contacts/pmcard.cpp) does the rest.
 *
 *   <dir>card\books.txt   "#PSIBOOK1 TAB home TAB where", then per book:
 *                         id TAB sync(0/1) TAB ctag TAB href TAB flags TAB name
 *                         (flags: W writable, D where new contacts go)
 *   <dir>card\index.txt   per card: bookid TAB href TAB etag TAB file TAB hash
 *                         (file: cXXXXXXXX, the FNV hash of the href;
 *                         hash: vc_psion_hash, what the Psion would see)
 *   <dir>card\cXXXXXXXX.v21  the Psion's (vc_to_psion), for Contacts to read
 *                         (21 November 2026: the server's own card, which
 *                         was kept beside it as cXXXXXXXX.vcf, up to 48 KB
 *                         each with a photo, is no longer: nothing read it,
 *                         a change fetches the card again; index.txt's
 *                         file column still names it, as pmcard expects)
 *   <dir>card\push.txt    (Contacts half) changes to send, one per line:
 *                         N TAB key TAB bookid TAB v21 file TAB uid
 *                         M TAB key TAB href TAB etag TAB v21 file
 *                         D TAB key TAB href TAB etag
 *   <dir>card\pushed.txt  key TAB ok|gone|conflict|failed TAB href
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calport.h"
#include "cal.h"
#include "card.h"
#include "../../../ssh/psiwork.h"

#define MAX_BOOKS 8
#define MAX_CARDS 800
#define TEXT_MAX (64 * 1024)
#define CARD_MAX (48 * 1024)

typedef struct
	{
	char id[12];
	int sync;
	char ctag[100];          /* the ctag its cards in index.txt are from */
	char newctag[100];       /* the server's, this sync: ctag once its cards are saved */
	char href[200];
	char flags[8];
	char name[64];
	int seen, fetch;
	} Book;

typedef struct
	{
	char book[12];
	char href[200];
	char etag[80];
	unsigned long hash;
	int seen;                /* in the server's latest list */
	int want;                /* to fetch */
	} Card;

static Book g_book[MAX_BOOKS];
static int g_nbook;
static Card *g_card;
static int g_ncard;
static int g_cardcap;            /* (21 November 2026) cards g_card has room for */
static int g_cardshort;          /* no memory for one more */
static char g_home[200], g_where[200], g_dir[160];
static char *g_text, *g_data;
static CalTarget g_t;

static unsigned long fnv(const char *s)
{
	unsigned long h = 2166136261UL;
	while (*s) { h ^= (unsigned char)*s++; h = (h * 16777619UL) & 0xffffffffUL; }
	return h;
}

static void path_of(const char *name, char *out, int max) { snprintf(out, max, "%s%s", g_dir, name); }

static void just_path(const char *href, char *out, int max)
{
	const char *p = href;
	if (!pm_strncasecmp(p, "http://", 7)) p = strchr(p + 7, '/');
	else if (!pm_strncasecmp(p, "https://", 8)) p = strchr(p + 8, '/');
	if (!p) p = "/";
	pm_copy(out, p, max);
}

static void trim(char *s)
{
	int n = (int)strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') memmove(s, s + 1, n--);
}

static void clean(char *s) { for (; *s; s++) if (*s == '\t' || *s == '\r' || *s == '\n') *s = ' '; }

static char *field(char **p)
{
	char *s = *p, *t;
	if (!s) return (char *)"";
	t = strchr(s, '\t');
	if (t) { *t = 0; *p = t + 1; } else *p = 0;
	return s;
}

static void card_file(const char *href, const char *ext, char *out, int max)
{
	char name[24];
	snprintf(name, sizeof(name), "c%08lX.%s", fnv(href), ext);
	path_of(name, out, max);
}

/* (21 November 2026, MEM-30) room for one more card: the list is work
   memory that grows in place, 64 cards (19 KB) at a time, up to MAX_CARDS
   (it was 800 cards, 243 KB, whatever the book held). 0 when there is no
   memory for it: the sync then stops with "not enough memory" rather than
   go on with a list cut short */
#define CARD_STEP 64
static int card_room(void)
{
	int n;
	if (g_ncard < g_cardcap) return 1;
	if (g_cardcap >= MAX_CARDS) return 0;
	n = g_cardcap + CARD_STEP < MAX_CARDS ? g_cardcap + CARD_STEP : MAX_CARDS;
	if (psi_work_grow(g_card, (unsigned long)n * sizeof(Card)) != 0) {
		pm_log("carddav: no memory for %d cards", n);
		g_cardshort = 1;
		return 0;
	}
	g_cardcap = n;
	return 1;
}

/* ------------------------------------------------------------ the files */

static void load_books(void)
{
	char path[200], line[600];
	FILE *f;
	g_nbook = 0;
	g_home[0] = 0;
	path_of("books.txt", path, sizeof(path));
	if (!(f = fopen(path, "r"))) return;
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "#PSIBOOK1\t", 10)) {
			char *q = line + 10;
			pm_copy(g_home, field(&q), sizeof(g_home));
			if (strcmp(field(&q), g_where)) { g_home[0] = 0; break; }
			continue;
		}
		if (line[0] == '#' || !line[0] || g_nbook >= MAX_BOOKS) continue;
		{
			Book *b = &g_book[g_nbook++];
			memset(b, 0, sizeof(*b));
			pm_copy(b->id, field(&p), sizeof(b->id));
			b->sync = atoi(field(&p));
			pm_copy(b->ctag, field(&p), sizeof(b->ctag));
			pm_copy(b->href, field(&p), sizeof(b->href));
			pm_copy(b->flags, field(&p), sizeof(b->flags));
			pm_copy(b->name, field(&p), sizeof(b->name));
		}
	}
	fclose(f);
}

static int save_books(void)
{
	char path[200], tmp[200];
	FILE *f;
	int i;
	path_of("books.txt", path, sizeof(path));
	path_of("books.tmp", tmp, sizeof(tmp));
	if (!(f = fopen(tmp, "w"))) return -1;
	fprintf(f, "#PSIBOOK1\t%s\t%s\n", g_home, g_where);
	for (i = 0; i < g_nbook; i++) {
		Book *b = &g_book[i];
		fprintf(f, "%s\t%d\t%s\t%s\t%s\t%s\n", b->id, b->sync, b->ctag, b->href, b->flags, b->name);
	}
	if (pm_fclose(f) != 0 || pm_replace(tmp, path) != 0) { remove(tmp); return -1; }
	return 0;
}

static Book *book_by_href(const char *href)
{
	int i;
	for (i = 0; i < g_nbook; i++) if (!strcmp(g_book[i].href, href)) return &g_book[i];
	return 0;
}

static Book *book_by_id(const char *id)
{
	int i;
	for (i = 0; i < g_nbook; i++) if (!strcmp(g_book[i].id, id)) return &g_book[i];
	return 0;
}

/* (21 November 2026) index.txt's first line says "novcf" once the old
   cXXXXXXXX.vcf files have been removed (sync_now, once) */
static int g_vcfgone;

static void load_index(void)
{
	char path[200], line[600];
	FILE *f;
	g_ncard = 0;
	g_vcfgone = 0;
	path_of("index.txt", path, sizeof(path));
	if (!(f = fopen(path, "r"))) return;
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		Card *c;
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "#PSICARD1", 9) && strstr(line, "novcf")) g_vcfgone = 1;
		if (line[0] == '#' || !line[0]) continue;
		if (!card_room()) break;
		c = &g_card[g_ncard];
		memset(c, 0, sizeof(*c));
		pm_copy(c->book, field(&p), sizeof(c->book));
		pm_copy(c->href, field(&p), sizeof(c->href));
		pm_copy(c->etag, field(&p), sizeof(c->etag));
		field(&p);                                    /* (the file: made from the href) */
		c->hash = strtoul(field(&p), 0, 16);
		g_ncard++;
	}
	fclose(f);
}

static int save_index(void)
{
	char path[200], tmp[200], file[200];
	FILE *f;
	int i;
	path_of("index.txt", path, sizeof(path));
	path_of("index.tmp", tmp, sizeof(tmp));
	if (!(f = fopen(tmp, "w"))) return -1;
	fprintf(f, "#PSICARD1\tnovcf\n");
	for (i = 0; i < g_ncard; i++) {
		Card *c = &g_card[i];
		char *slash;
		card_file(c->href, "vcf", file, sizeof(file));
		slash = strrchr(file, CAL_SEP[0]);
		fprintf(f, "%s\t%s\t%s\t%s\t%08lX\n", c->book, c->href, c->etag, slash ? slash + 1 : file, c->hash);
	}
	if (pm_fclose(f) != 0 || pm_replace(tmp, path) != 0) { remove(tmp); return -1; }
	st_changed();
	return 0;
}

static Card *card_by_href(const char *href)
{
	int i;
	for (i = 0; i < g_ncard; i++) if (!strcmp(g_card[i].href, href)) return &g_card[i];
	return 0;
}

/* a whole file, NUL-terminated, in buf (max); its length or -1 */
static int read_file(const char *path, char *buf, int max)
{
	FILE *f = fopen(path, "rb");
	int n;
	if (!f) return -1;
	n = (int)fread(buf, 1, max - 1, f);
	fclose(f);
	if (n < 0) return -1;
	buf[n] = 0;
	return n;
}

static int write_file(const char *path, const char *data, int n)
{
	char tmp[220];
	FILE *f;
	snprintf(tmp, sizeof(tmp), "%s~", path);
	if (!(f = fopen(tmp, "wb"))) return -1;
	fwrite(data, 1, n, f);
	if (pm_fclose(f) != 0 || pm_replace(tmp, path) != 0) { remove(tmp); return -1; }
	return 0;
}

/* ------------------------------------------------------------ requests */

typedef struct
	{
	XmlScan x;
	char href[200], etag[80], name[64], ctag[100];
	int is_book, writable, got_data, ndata;
	int whole, truncated;    /* the multistatus ended; the server left some out (507) */
	char principal[200], home[200];
	int count;
	} Dav;
static Dav *g_dav;

/* What a sync works in is work memory (psiwork.h), taken by card_sync and
   given back when it ends: the buffers below were statics (95 KB of
   psisync.exe's RAM all day), and the list of cards and the two texts came
   from the heap, which kept them. (21 November 2026) The cards are a block
   of their own that grows (card_room), and push_one's two buffers (70 KB)
   are taken only when there are changes to send: a sync of a few contacts
   took 646 KB at once, now about 160 KB. */
typedef struct
	{
	char pf_body[700], pf_hdr[120];              /* propfind */
	char me_v21[16384];                          /* multi_end */
	char fb_body[4096];                          /* fetch_book */
	char text[TEXT_MAX];
	char data[CARD_MAX];
	Dav dav;
	} CardWork;
static CardWork *g_kw;
typedef struct
	{
	char po_psion[16384], po_out[CARD_MAX + 4096];   /* push_one */
	} PushWork;
static PushWork *g_pw;

static void sink(const char *data, int n, void *ctx) { xs_feed(&((Dav *)ctx)->x, data, n); }

/* (every list's end handler) a list is only used whole: its multistatus
   ended and the server did not say it left some out (507). A cut list read
   as whole had cards "gone from the server", and their contacts deleted */
static void note_whole(Dav *d, const char *name, const char *text)
{
	if (!strcmp(name, "multistatus")) d->whole = 1;
	else if (!strcmp(name, "status") && pm_stristr(text, " 507")) d->truncated = 1;
}

static int not_whole(const char *what, char *why, int whymax)
{
	if (g_dav->whole && !g_dav->truncated) return 0;
	pm_log("carddav: %s: the list is not whole (%s)", what, g_dav->truncated ? "507" : "no end");
	snprintf(why, whymax, "the contacts server's answer was cut short");
	return 1;
}

/* (20 November 2026) why the contacts server said no, in words. The
   contacts pass runs after the calendar's, with the same user name and
   password: a 401 or 403 here, when the calendar took them, is most often
   an app password made for calendars only (Fastmail's app passwords each
   name what they may reach: "Calendars", "Contacts", "Mail"...) */
static void card_refused(int status, const char *what, char *why, int whymax)
{
	pm_log("carddav: %s: status %d", what, status);
	if (status == 401 || status == 403)
		snprintf(why, whymax, "the password is not allowed to read contacts (check the app password's access)");
	else if (status == 404 || status == 410)
		snprintf(why, whymax, "the address book was not found on %s", g_t.host ? g_t.host : "the server");
	else
		snprintf(why, whymax, "the contacts server %s", http_status_words(status));
}

static int propfind(const char *path, int depth, const char *props, XsEnd end, HttpResp *r, char *why, int whymax)
{
#define body (g_kw->pf_body)
#define hdr  (g_kw->pf_hdr)
	int n = snprintf(body, sizeof(body),
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<D:propfind xmlns:D=\"DAV:\" xmlns:A=\"urn:ietf:params:xml:ns:carddav\" "
		"xmlns:CS=\"http://calendarserver.org/ns/\"><D:prop>%s</D:prop></D:propfind>", props);
	snprintf(hdr, sizeof(hdr), "Content-Type: application/xml; charset=utf-8\r\nDepth: %d\r\n", depth);
	xs_init(&g_dav->x, g_text, TEXT_MAX, end, g_dav);
	return http_request("PROPFIND", path, hdr, body, n, r, sink, g_dav, why, whymax);
}
#undef body
#undef hdr

static void disc_end(XmlScan *x, const char *name, const char *text, int len, void *ctx)
{
	Dav *d = (Dav *)ctx;
	(void)len;
	if (!strcmp(name, "href")) {
		const char *up = xs_parent(x, 1);
		char t[200];
		pm_copy(t, text, sizeof(t));
		trim(t);
		if (!strcmp(up, "current-user-principal")) pm_copy(d->principal, t, sizeof(d->principal));
		else if (!strcmp(up, "addressbook-home-set")) pm_copy(d->home, t, sizeof(d->home));   /* (with its host: http_follow) */
	}
}

static int discover(char *why, int whymax)
{
	HttpResp r;
	char path[200];
	int res, hops, attempt, refused = 0;
	const char *props = "<D:current-user-principal/><A:addressbook-home-set/>";
	for (attempt = 0; attempt < 3 && !g_home[0]; attempt++) {
		if (attempt == 0) pm_copy(path, "/.well-known/carddav", sizeof(path));
		else if (attempt == 1) pm_copy(path, g_t.path && g_t.path[0] ? g_t.path : "/", sizeof(path));
		else {
			/* (20 November 2026) every place asked said "not with that
			   password": said so, rather than guessing a path that can
			   only be refused too */
			if (refused) { card_refused(refused, "discovery", why, whymax); return CAL_RES_FAILED; }
			snprintf(g_home, sizeof(g_home), "/dav/addressbooks/user/%s/", g_t.user);   /* Fastmail's */
			pm_log("carddav: no address book home named: trying Fastmail's");
			break;
		}
		for (hops = 0; hops < 4; hops++) {
			memset(g_dav, 0, sizeof(*g_dav));
			res = propfind(path, 0, props, disc_end, &r, why, whymax);
			if (res != CAL_RES_OK) return res;
			pm_log("carddav: discovery %s: status %d", path, r.status);
			if (r.status >= 300 && r.status < 400 && r.location[0]) { http_follow(r.location, path, sizeof(path)); continue; }
			break;
		}
		if (r.status == 401 || r.status == 403) refused = r.status;
		else if (r.status == 207) refused = 0;
		if (r.status != 207) continue;
		if (g_dav->home[0]) { pm_copy(g_home, g_dav->home, sizeof(g_home)); break; }
		if (g_dav->principal[0]) {
			http_follow(g_dav->principal, path, sizeof(path));
			memset(g_dav, 0, sizeof(*g_dav));
			res = propfind(path, 0, props, disc_end, &r, why, whymax);
			if (res != CAL_RES_OK) return res;
			pm_log("carddav: principal %s: status %d", path, r.status);
			if (r.status == 207 && g_dav->home[0]) { pm_copy(g_home, g_dav->home, sizeof(g_home)); break; }
			if (r.status == 401 || r.status == 403) { card_refused(r.status, "the principal", why, whymax); return CAL_RES_FAILED; }
		}
	}
	pm_log("carddav: address book home %s", g_home);
	return CAL_RES_OK;
}

static void list_end(XmlScan *x, const char *name, const char *text, int len, void *ctx)
{
	Dav *d = (Dav *)ctx;
	char t[200];
	note_whole(d, name, text);
	if (!strcmp(name, "href") && !strcmp(xs_parent(x, 1), "response")) {
		pm_copy(t, text, sizeof(t)); trim(t);
		just_path(t, d->href, sizeof(d->href));
	} else if (!strcmp(name, "addressbook") && !strcmp(xs_parent(x, 1), "resourcetype")) d->is_book = 1;
	else if (!strcmp(name, "displayname")) { cs_utf8_to_cp1252(text, len, d->name, sizeof(d->name)); trim(d->name); clean(d->name); }
	else if (!strcmp(name, "getctag")) { pm_copy(d->ctag, text, sizeof(d->ctag)); trim(d->ctag); clean(d->ctag); }
	else if ((!strcmp(name, "write") || !strcmp(name, "all") || !strcmp(name, "write-content")) &&
	         !strcmp(xs_parent(x, 1), "privilege")) d->writable = 1;
	else if (!strcmp(name, "response")) {
		if (d->is_book && d->href[0]) {
			Book *b = book_by_href(d->href);
			if (!b && g_nbook < MAX_BOOKS) {
				b = &g_book[g_nbook++];
				memset(b, 0, sizeof(*b));
				pm_copy(b->href, d->href, sizeof(b->href));
				sprintf(b->id, "%08lX", fnv(d->href));
				b->sync = 1;
			}
			if (b) {
				b->seen = 1;
				if (strcmp(b->ctag, d->ctag) || !d->ctag[0]) b->fetch = 1;
				/* (saved as its ctag only once its cards are: saved here,
				   one dropped fetch said "No contact changes" for ever) */
				pm_copy(b->newctag, d->ctag, sizeof(b->newctag));
				pm_copy(b->name, d->name[0] ? d->name : "Contacts", sizeof(b->name));
				b->flags[0] = 0;
				if (d->writable) strcat(b->flags, "W");
			}
		}
		d->href[0] = d->name[0] = d->ctag[0] = 0;
		d->is_book = d->writable = 0;
	}
}

static int list_books(char *why, int whymax)
{
	HttpResp r;
	char home[200];
	int res, i, def = -1;
	pm_progress("Looking for address books\x85");
	if (!g_home[0]) {
		res = discover(why, whymax);
		if (res != CAL_RES_OK) return res;
	}
	for (i = 0; i < g_nbook; i++) g_book[i].seen = g_book[i].fetch = 0;
	memset(g_dav, 0, sizeof(*g_dav));
	/* (the home may be on another of the server's hosts: iCloud's partitions) */
	http_follow(g_home, home, sizeof(home));
	res = propfind(home, 1, "<D:resourcetype/><D:displayname/><CS:getctag/><D:current-user-privilege-set/>",
		list_end, &r, why, whymax);
	if (res != CAL_RES_OK) return res;
	if (r.status != 207) {
		if (r.status == 404) { g_home[0] = 0; save_books(); }
		card_refused(r.status, "the address books", why, whymax);
		return CAL_RES_FAILED;
	}
	if (not_whole("the address books", why, whymax)) return CAL_RES_FAILED;
	for (i = 0; i < g_nbook; ) {
		if (!g_book[i].seen) { g_book[i] = g_book[--g_nbook]; continue; }
		i++;
	}
	/* where new contacts go: the one marked before (bookdef.txt), else the first writable */
	for (i = 0; i < g_nbook; i++) if (strchr(g_book[i].flags, 'W') && def < 0) def = i;
	{
		char path[200], line[60];
		FILE *f;
		path_of("bookdef.txt", path, sizeof(path));
		if ((f = fopen(path, "r")) != 0) {
			if (fgets(line, sizeof(line), f)) {
				Book *b;
				line[strcspn(line, "\r\n")] = 0;
				b = book_by_id(line);
				if (b && strchr(b->flags, 'W')) def = (int)(b - g_book);
			}
			fclose(f);
		}
	}
	if (def >= 0) strcat(g_book[def].flags, "D");
	save_books();
	if (!g_nbook) { snprintf(why, whymax, "the address book was not found on %s", g_t.host); return CAL_RES_FAILED; }
	return CAL_RES_OK;
}

/* the cards in a book and their ETags */
static Book *g_listing;
static void etags_end(XmlScan *x, const char *name, const char *text, int len, void *ctx)
{
	Dav *d = (Dav *)ctx;
	char t[200];
	(void)len;
	note_whole(d, name, text);
	if (!strcmp(name, "href") && !strcmp(xs_parent(x, 1), "response")) {
		pm_copy(t, text, sizeof(t)); trim(t);
		just_path(t, d->href, sizeof(d->href));
	} else if (!strcmp(name, "getetag")) { pm_copy(d->etag, text, sizeof(d->etag)); trim(d->etag); clean(d->etag); }
	else if (!strcmp(name, "collection") && !strcmp(xs_parent(x, 1), "resourcetype")) d->is_book = 1;
	else if (!strcmp(name, "response")) {
		if (d->href[0] && !d->is_book && strcmp(d->href, g_listing->href)) {
			Card *c = card_by_href(d->href);
			if (!c && card_room()) {
				c = &g_card[g_ncard++];
				memset(c, 0, sizeof(*c));
				pm_copy(c->href, d->href, sizeof(c->href));
				pm_copy(c->book, g_listing->id, sizeof(c->book));
				c->want = 1;
			}
			if (c) {
				c->seen = 1;
				/* (its ETag moves on when its card is saved, in multi_end:
				   taken from this list, a card whose fetch was cut was
				   never asked for again) */
				if (strcmp(c->etag, d->etag) || !d->etag[0]) c->want = 1;
			}
		}
		d->href[0] = d->etag[0] = 0;
		d->is_book = 0;
	}
}

/* addressbook-multiget: a card's data */
static void multi_end(XmlScan *x, const char *name, const char *text, int len, void *ctx)
{
	Dav *d = (Dav *)ctx;
	char t[200];
	note_whole(d, name, text);
	if (!strcmp(name, "href") && !strcmp(xs_parent(x, 1), "response")) {
		pm_copy(t, text, sizeof(t)); trim(t);
		just_path(t, d->href, sizeof(d->href));
	} else if (!strcmp(name, "getetag")) { pm_copy(d->etag, text, sizeof(d->etag)); trim(d->etag); clean(d->etag); }
	else if (!strcmp(name, "address-data")) {
		if (x->over) pm_log("carddav: %s is too big, cut short", d->href);
		if (len > 0) {
			d->ndata = len < CARD_MAX - 1 ? len : CARD_MAX - 1;
			memmove(g_data, text, d->ndata);
			g_data[d->ndata] = 0;
			d->got_data = 1;
		}
	} else if (!strcmp(name, "response")) {
		Card *c = d->href[0] ? card_by_href(d->href) : 0;
		if (c && d->got_data) {
			char path[200];
#define v21 (g_kw->me_v21)
			int m = vc_to_psion(g_data, d->ndata, v21, sizeof(v21));
			int bad = 0;
			card_file(c->href, "v21", path, sizeof(path));
			if (m > 0) bad = write_file(path, v21, m) != 0;
			else remove(path);
			c->hash = vc_psion_hash(g_data, d->ndata);
			/* the card is saved: its ETag (none, or not saved: "", so the
			   next sync asks again) */
			pm_copy(c->etag, bad ? "" : d->etag, sizeof(c->etag));
			c->want = 0;
			d->count++;
		}
		d->href[0] = d->etag[0] = 0;
		d->got_data = 0;
	}
}
#undef v21

static int fetch_book(Book *b, int *count, char *why, int whymax)
{
#define body (g_kw->fb_body)
	HttpResp r;
	int res, i, n, k;
	pm_progress("Fetching %s\x85", b->name);
	for (i = 0; i < g_ncard; i++) if (!strcmp(g_card[i].book, b->id)) g_card[i].seen = 0;
	memset(g_dav, 0, sizeof(*g_dav));
	g_listing = b;
	res = propfind(b->href, 1, "<D:resourcetype/><D:getetag/>", etags_end, &r, why, whymax);
	if (res != CAL_RES_OK) return res;
	if (r.status != 207) { card_refused(r.status, b->name, why, whymax); return CAL_RES_FAILED; }
	if (not_whole(b->name, why, whymax)) return CAL_RES_FAILED;
	if (g_cardshort) { snprintf(why, whymax, "not enough memory for the contacts"); return CAL_RES_FAILED; }
	/* cards gone from the server */
	for (i = 0; i < g_ncard; ) {
		Card *c = &g_card[i];
		if (!strcmp(c->book, b->id) && !c->seen) {
			char path[200];
			if (!g_vcfgone) { card_file(c->href, "vcf", path, sizeof(path)); remove(path); }
			card_file(c->href, "v21", path, sizeof(path)); remove(path);
			g_card[i] = g_card[--g_ncard];
			continue;
		}
		i++;
	}
	/* the changed ones, ten at a time */
	for (;;) {
		k = 0;
		n = snprintf(body, sizeof(body), "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			"<A:addressbook-multiget xmlns:D=\"DAV:\" xmlns:A=\"urn:ietf:params:xml:ns:carddav\">"
			"<D:prop><D:getetag/><A:address-data/></D:prop>");
		for (i = 0; i < g_ncard && k < 10; i++) {
			Card *c = &g_card[i];
			if (strcmp(c->book, b->id) || !c->want || c->want == 2) continue;
			if (n + (int)strlen(c->href) + 40 >= (int)sizeof(body)) break;
			n += snprintf(body + n, sizeof(body) - n, "<D:href>%s</D:href>", c->href);
			c->want = 2;                     /* asked for (a card the server leaves out is not asked again) */
			k++;
		}
		if (!k) break;
		n += snprintf(body + n, sizeof(body) - n, "</A:addressbook-multiget>");
		memset(g_dav, 0, sizeof(*g_dav));
		xs_init(&g_dav->x, g_text, TEXT_MAX, multi_end, g_dav);
		res = http_request("REPORT", b->href, "Content-Type: application/xml; charset=utf-8\r\nDepth: 1\r\n",
			body, n, &r, sink, g_dav, why, whymax);
		if (res != CAL_RES_OK) return res;
		if (r.status != 207) { card_refused(r.status, b->name, why, whymax); return CAL_RES_FAILED; }
		if (not_whole(b->name, why, whymax)) return CAL_RES_FAILED;
		*count += g_dav->count;
		pm_progress("Fetching %s (%d)\x85", b->name, *count);
	}
	return CAL_RES_OK;
}
#undef body

/* ------------------------------------------------------------ changes from the Psion */

static void pushed(FILE *res, const char *key, const char *what, const char *href)
{
	if (res) fprintf(res, "%s\t%s\t%s\n", key, what, href);
}

static void to_data(const char *d, int n, void *ctx)
{
	int *len = (int *)ctx;
	if (*len + n >= CARD_MAX) n = CARD_MAX - 1 - *len;
	if (n > 0) { memcpy(g_data + *len, d, n); *len += n; }
}

static int push_one(char *line, FILE *res, char *why, int whymax)
{
#define psion (g_pw->po_psion)
#define out   (g_pw->po_out)
	char *p = line, *op = field(&p), *key = field(&p);
	char hdr[200], href[260], path[200];
	HttpResp r;
	int rc, len, plen, olen;
	if (op[0] == 'N') {
		Book *b = book_by_id(field(&p));
		char *v21 = field(&p), *uid = field(&p);
		int i;
		if (!b) { pushed(res, key, "failed", ""); return CAL_RES_OK; }
		for (i = 0; uid[i]; i++)
			if (!((uid[i] >= '0' && uid[i] <= '9') || (uid[i] >= 'a' && uid[i] <= 'z') || (uid[i] >= 'A' && uid[i] <= 'Z') || uid[i] == '-')) uid[i] = '-';
		path_of(v21, path, sizeof(path));
		plen = read_file(path, psion, sizeof(psion));
		if (plen < 0 || !uid[0]) { pushed(res, key, "failed", ""); return CAL_RES_OK; }
		olen = vc_merge(0, 0, psion, plen, uid, out, sizeof(out));
		if (olen < 0) { pushed(res, key, "failed", ""); return CAL_RES_OK; }
		snprintf(href, sizeof(href), "%s%s.vcf", b->href, uid);
		rc = http_request("PUT", href, "Content-Type: text/vcard; charset=utf-8\r\nIf-None-Match: *\r\n",
			out, olen, &r, 0, 0, why, whymax);
		if (rc != CAL_RES_OK) return rc;
		pushed(res, key, r.status / 100 == 2 || r.status == 412 ? "ok" : "failed", href);
		b->fetch = 1;
		remove(path);
		pm_log("carddav: new %s -> %d", href, r.status);
		return CAL_RES_OK;
	}
	if (op[0] == 'M' || op[0] == 'D') {
		char *h = field(&p), *etag = field(&p), *v21 = op[0] == 'M' ? field(&p) : (char *)"";
		Book *b;
		int i;
		pm_copy(href, h, sizeof(href));
		for (i = 0; i < g_nbook; i++) {
			int n = (int)strlen(g_book[i].href);
			if (n && !strncmp(href, g_book[i].href, n)) g_book[i].fetch = 1;
		}
		b = 0; (void)b;
		if (op[0] == 'D') {
			snprintf(hdr, sizeof(hdr), "If-Match: %s\r\n", etag);
			rc = http_request("DELETE", href, etag[0] ? hdr : "", 0, 0, &r, 0, 0, why, whymax);
			if (rc != CAL_RES_OK) return rc;
			pushed(res, key, r.status / 100 == 2 ? "ok" : r.status == 404 ? "gone" : r.status == 412 ? "conflict" : "failed", href);
			pm_log("carddav: delete %s -> %d", href, r.status);
			return CAL_RES_OK;
		}
		/* the server's own card, the Psion's change merged into it */
		len = 0;
		rc = http_request("GET", href, "", 0, 0, &r, to_data, &len, why, whymax);
		if (rc != CAL_RES_OK) return rc;
		if (r.status == 404) { pushed(res, key, "gone", href); return CAL_RES_OK; }
		if (r.status != 200 || len >= CARD_MAX - 1) { pushed(res, key, "failed", href); return CAL_RES_OK; }
		if (etag[0] && r.etag[0] && strcmp(etag, r.etag)) { pushed(res, key, "conflict", href); return CAL_RES_OK; }
		g_data[len] = 0;
		path_of(v21, path, sizeof(path));
		plen = read_file(path, psion, sizeof(psion));
		if (plen < 0) { pushed(res, key, "failed", href); return CAL_RES_OK; }
		olen = vc_merge(g_data, len, psion, plen, 0, out, sizeof(out));
		if (olen < 0) { pushed(res, key, "failed", href); return CAL_RES_OK; }
		snprintf(hdr, sizeof(hdr), "Content-Type: text/vcard; charset=utf-8\r\n%s%s%s",
			r.etag[0] ? "If-Match: " : "", r.etag, r.etag[0] ? "\r\n" : "");
		rc = http_request("PUT", href, hdr, out, olen, &r, 0, 0, why, whymax);
		if (rc != CAL_RES_OK) return rc;
		pushed(res, key, r.status / 100 == 2 ? "ok" : r.status == 412 ? "conflict" : "failed", href);
		remove(path);
		pm_log("carddav: change %s -> %d", href, r.status);
		return CAL_RES_OK;
	}
	return CAL_RES_OK;
}
#undef psion
#undef out

static int push_changes(int *n, char *why, int whymax)
{
	char path[200], rpath[200], line[700];
	FILE *f, *res;
	int rc = CAL_RES_OK, total = 0, done = 0;
	*n = 0;
	path_of("push.txt", path, sizeof(path));
	if (!(f = fopen(path, "r"))) return CAL_RES_OK;
	while (fgets(line, sizeof(line), f)) if (line[0] != '#' && line[0] > ' ') total++;
	if (!total) { fclose(f); remove(path); return CAL_RES_OK; }
	if (!(g_pw = (PushWork *)psi_work_alloc(sizeof(PushWork)))) {
		fclose(f);
		snprintf(why, whymax, "not enough memory for the contacts");
		return CAL_RES_FAILED;
	}
	fseek(f, 0, SEEK_SET);
	path_of("pushed.txt", rpath, sizeof(rpath));
	res = fopen(rpath, "a");
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0] == '#' || !line[0]) continue;
		pm_progress("Sending contact changes (%d of %d)\x85", done + 1, total);
		rc = push_one(line, res, why, whymax);
		if (rc != CAL_RES_OK) break;
		done++;
	}
	fclose(f);
	if (res) fclose(res);
	psi_work_free(g_pw); g_pw = 0;
	if (rc == CAL_RES_OK) remove(path);
	else if (done > 0) {
		char tmp[200];
		FILE *in, *out;
		int k = 0;
		path_of("push.tmp", tmp, sizeof(tmp));
		if ((in = fopen(path, "r")) != 0 && (out = fopen(tmp, "w")) != 0) {
			while (fgets(line, sizeof(line), in)) {
				if (line[0] == '#' || line[0] <= ' ') continue;
				if (k++ >= done) fputs(line, out);
			}
			fclose(in);
			if (pm_fclose(out) != 0 || pm_replace(tmp, path) != 0) remove(tmp);
		} else if (in) fclose(in);
	}
	*n = done;
	return rc;
}

/* ------------------------------------------------------------ the sync */

static int sync_now(int list_only, char *why, int whymax)
{
	int res, i, sent = 0, total = 0, fetched = 0;
	memset(&g_t, 0, sizeof(g_t));
	calp_target(&g_t);
	if (!g_t.host || !g_t.host[0]) { snprintf(why, whymax, "no contacts server set up"); return CAL_RES_FAILED; }
	if (!g_t.pass || !g_t.pass[0]) { snprintf(why, whymax, "a password is needed for the contacts"); return CAL_RES_NEED_PASS; }
	if (!g_t.user) g_t.user = "";
	snprintf(g_dir, sizeof(g_dir), "%scard%s", calp_dir(), CAL_SEP);
	pm_mkdir(g_dir);
	http_target(g_t.host, g_t.port ? g_t.port : 443, g_t.tls, g_t.user, g_t.pass);
	snprintf(g_where, sizeof(g_where), "%s", g_t.host);
	load_books();
	load_index();
	if (g_cardshort) { snprintf(why, whymax, "not enough memory for the contacts"); return CAL_RES_FAILED; }
	/* (21 November 2026) the server's cards kept as .vcf by older builds:
	   removed once (save_index then writes "novcf") */
	if (!g_vcfgone) {
		char path[200];
		for (i = 0; i < g_ncard; i++) { card_file(g_card[i].href, "vcf", path, sizeof(path)); remove(path); }
		pm_log("carddav: the old .vcf copies of %d cards removed", g_ncard);
	}
	res = list_books(why, whymax);
	if (res != CAL_RES_OK) return res;
	if (list_only) { snprintf(why, whymax, "%d address book%s", g_nbook, g_nbook == 1 ? "" : "s"); return CAL_RES_OK; }
	res = push_changes(&sent, why, whymax);
	if (res != CAL_RES_OK) return res;
	for (i = 0; i < g_nbook; i++) {
		Book *b = &g_book[i];
		if (!b->sync || !b->fetch) continue;
		res = fetch_book(b, &total, why, whymax);
		if (res != CAL_RES_OK) {
			/* (the ctags were not moved on: the next sync lists the books
			   again, and asks for the cards whose ETags are not yet saved) */
			save_index();                    /* (what came in so far is kept) */
			return res;
		}
		fetched++;
	}
	/* cards of books that have gone */
	for (i = 0; i < g_ncard; ) {
		if (!book_by_id(g_card[i].book)) { g_card[i] = g_card[--g_ncard]; continue; }
		i++;
	}
	if (save_index() != 0) { pm_write_why(why, whymax, "the contacts", g_dir); return CAL_RES_FAILED; }
	/* the cards are saved: now the ctags they are from */
	for (i = 0; i < g_nbook; i++) if (g_book[i].sync && g_book[i].fetch) pm_copy(g_book[i].ctag, g_book[i].newctag, sizeof(g_book[i].ctag));
	if (save_books() != 0) { pm_write_why(why, whymax, "the contacts", g_dir); return CAL_RES_FAILED; }
	if (fetched) snprintf(why, whymax, "%s%d contact%s changed", sent ? "Sent changes; " : "", total, total == 1 ? "" : "s");
	else snprintf(why, whymax, sent ? "Sent %d change%s" : "No contact changes", sent, sent == 1 ? "" : "s");
	return CAL_RES_OK;
}

int card_sync(int list_only, char *why, int whymax)
{
	int r;
	if (!g_kw) g_kw = (CardWork *)psi_work_alloc(sizeof(CardWork));
	g_cardcap = 0; g_cardshort = 0; g_ncard = 0;
	if (g_kw && !g_card) g_card = (Card *)psi_work_alloc_max(0, (unsigned long)MAX_CARDS * sizeof(Card));
	if (!g_kw || !g_card || vc_work(1) != 0) {
		snprintf(why, whymax, "not enough memory for the contacts");
		r = CAL_RES_FAILED;
	} else {
		g_text = g_kw->text;
		g_data = g_kw->data;
		g_dav = &g_kw->dav;
		r = sync_now(list_only, why, whymax);
		/* (20 November 2026) http.c says a 401 is "The calendar server did
		   not accept the password": here it is the contacts', after the
		   calendar took the same password (an app password for calendars
		   only, most often) */
		if (r == CAL_RES_LOGIN_FAILED) card_refused(401, "a request", why, whymax);
	}
	g_text = 0; g_data = 0; g_dav = 0;
	psi_work_free(g_card); g_card = 0; g_cardcap = 0; g_ncard = 0;
	psi_work_free(g_kw); g_kw = 0;
	vc_work(0);
	psi_work_trim();
	return r;
}
