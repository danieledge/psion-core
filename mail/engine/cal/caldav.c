/* caldav.c - the calendar engine's CalDAV (RFC 4791) - see cal.h, calport.h
 *
 * (PsiMail's engine/caldav.c until it moved to psion-core, to be shared
 * with PsiSync.)
 *
 * The engine's half of calendar sync. It finds the user's calendars, sends
 * the changes made in the Psion's Agenda and fetches every event in a
 * window around today, expanded by the server into single instances with
 * UTC times. PsiMail.app does the other half: it puts those instances into
 * the Agenda and notices what the user changes there.
 *
 *   <store>cal\calendars.txt  "#PSICAL1 TAB home", then per calendar:
 *                             id TAB sync(0/1) TAB ctag TAB href TAB flags TAB name TAB colour
 *                             (colour: RRGGBB, the calendar's calendar-color, "" none)
 *                             (flags: W writable, D where new events go,
 *                             T (PsiSync) it keeps to-dos)
 *   <store>cal\events.txt     "#PSIEV1 TAB first day TAB zone", then per instance:
 *                             calid TAB href TAB etag TAB recurid TAB flags TAB
 *                             start TAB end TAB alarm TAB summary TAB location
 *                             TAB tzid TAB my status TAB organiser TAB attendees
 *                             (PsiSync's sidecar: the event's TZID; A D T N or ""
 *                             as for "Name <email> S;..." attendees; only the
 *                             first ten fields are hashed, as before)
 *                             (PsiSync) a series the Agenda can repeat: flags S (and A),
 *                             start and end its first instance's, then after the
 *                             attendees TAB rrule TAB exdates; its instances stay too
 *                             (times local, YYYYMMDDHHMM; flags: A all day,
 *                             R one of a series; alarm: minutes before, -1 none)
 *                             (PsiSync) a to-do: flags T (and C when done), start
 *                             and end its due ("" undated), alarm its priority
 *   <store>cal\push.txt       (app) changes to send, one per line:
 *                             N TAB key TAB calid TAB start TAB end TAB flags TAB alarm TAB summary TAB location
 *                             M TAB key TAB href TAB etag TAB recurid TAB start TAB end TAB flags TAB alarm TAB summary TAB location
 *                             D TAB key TAB href TAB etag TAB recurid
 *                             (PsiSync: N and M lines may end TAB R TAB rrule TAB exdates:
 *                             a repeating Agenda entry, see cal_rrule)
 *   <store>cal\pushed.txt     what became of them: key TAB ok|gone|conflict|failed TAB href
 *
 * id is the FNV hash of the calendar's href. All text is Windows-1252.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calport.h"
#include "cal.h"
#include "calmerge.h"        /* cal_clock_ok */

#define MAX_CALS 24
typedef struct
	{
	char id[12];
	int sync;
	char ctag[100];          /* the ctag its events in events.txt are from */
	char newctag[100];       /* the server's, this sync: ctag once its events are saved */
	char href[200];
	char flags[8];
	char name[64];
	char colour[8];          /* (PsiSync) CalDAV's calendar-color as RRGGBB, "" none */
	int seen;                /* in the server's latest list */
	int fetch;               /* events to be fetched this time */
	} Cal;

static int g_ncal;
static char g_home[200];
static char g_where[200];            /* host+path the list came from */

static char g_dir[160];
static CalTarget g_t;                /* where the calendar is (calp_target), for this sync */
static char *g_text;                 /* XML element text (calendar data can be big) */
#define TEXT_MAX (160 * 1024)
static char *g_obj;                  /* a calendar object being changed */
#define OBJ_MAX (128 * 1024)

/* What a sync works in is work memory (psiwork.h): taken by cal_sync and
   given back to the Psion when it ends. The lists were statics (32 KB of
   the program's RAM all day) and the two buffers came from the heap, which
   kept them (EPOC's heap does not shrink by itself). */
#include "../../../ssh/psiwork.h"
#define MAX_SERIES 48
#define MAX_TOUCHED 32
typedef struct
	{
	Cal cal[MAX_CALS];
	char series[MAX_SERIES][200];        /* hrefs whose masters are to be asked for */
	char series_etag[MAX_SERIES][80];
	char touched[MAX_TOUCHED][200];
	char text[TEXT_MAX];
	char obj[OBJ_MAX];
	} CalWork;
static CalWork *g_cw;
#define g_cal         (g_cw->cal)
#define g_series      (g_cw->series)
#define g_series_etag (g_cw->series_etag)
#define g_touched     (g_cw->touched)

/* ------------------------------------------------------------ helpers */

static unsigned long fnv(const char *s)
{
	unsigned long h = 2166136261UL;
	while (*s) { h ^= (unsigned char)*s++; h = (h * 16777619UL) & 0xffffffffUL; }
	return h;
}

static void path_of(const char *name, char *out, int max) { snprintf(out, max, "%s%s", g_dir, name); }

/* "https://host/a/b/" -> "/a/b/" */
static void just_path(const char *href, char *out, int max)
{
	const char *p = href;
	if (!pm_strncasecmp(p, "http://", 7)) p = strchr(p + 7, '/');
	else if (!pm_strncasecmp(p, "https://", 8)) p = strchr(p + 8, '/');
	/* (the scheme skipped by its own length: "http://" alone used to be
	   read one byte past its end) */
	if (!p) p = "/";
	pm_copy(out, p, max);
}

static void trim(char *s)
{
	int n = (int)strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') memmove(s, s + 1, n--);
}

/* tabs and new lines would break our files */
static void clean(char *s)
{
	for (; *s; s++) if (*s == '\t' || *s == '\r' || *s == '\n') *s = ' ';
}

static char *field(char **p)
{
	char *s = *p, *t;
	if (!s) return (char *)"";
	t = strchr(s, '\t');
	if (t) { *t = 0; *p = t + 1; } else *p = 0;
	return s;
}

/* ------------------------------------------------------------ calendars.txt */

static void load_cals(void)
{
	char path[200], line[600];
	FILE *f;
	g_ncal = 0;
	g_home[0] = 0;
	path_of("calendars.txt", path, sizeof(path));
	if (!(f = fopen(path, "r"))) return;
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "#PSICAL1\t", 9)) {
			char *q = line + 9;
			pm_copy(g_home, field(&q), sizeof(g_home));
			if (strcmp(field(&q), g_where)) { g_home[0] = 0; break; }   /* another server */
			continue;
		}
		if (line[0] == '#' || !line[0] || g_ncal >= MAX_CALS) continue;
		{
			Cal *c = &g_cal[g_ncal++];
			memset(c, 0, sizeof(*c));
			pm_copy(c->id, field(&p), sizeof(c->id));
			c->sync = atoi(field(&p));
			pm_copy(c->ctag, field(&p), sizeof(c->ctag));
			pm_copy(c->href, field(&p), sizeof(c->href));
			pm_copy(c->flags, field(&p), sizeof(c->flags));
			pm_copy(c->name, field(&p), sizeof(c->name));
			pm_copy(c->colour, field(&p), sizeof(c->colour));
		}
	}
	fclose(f);
}

static int save_cals(void)
{
	char path[200], tmp[200];
	FILE *f;
	int i;
	path_of("calendars.txt", path, sizeof(path));
	path_of("calendars.tmp", tmp, sizeof(tmp));
	if (!(f = fopen(tmp, "w"))) return -1;
	fprintf(f, "#PSICAL1\t%s\t%s\n", g_home, g_where);
	for (i = 0; i < g_ncal; i++) {
		Cal *c = &g_cal[i];
		fprintf(f, "%s\t%d\t%s\t%s\t%s\t%s\t%s\n", c->id, c->sync, c->ctag, c->href, c->flags, c->name, c->colour);
	}
	if (pm_fclose(f) != 0 || pm_replace(tmp, path) != 0) { remove(tmp); return -1; }
	st_changed();
	return 0;
}

static Cal *cal_by_href(const char *href)
{
	int i;
	for (i = 0; i < g_ncal; i++) if (!strcmp(g_cal[i].href, href)) return &g_cal[i];
	return 0;
}

static Cal *cal_by_id(const char *id)
{
	int i;
	for (i = 0; i < g_ncal; i++) if (!strcmp(g_cal[i].id, id)) return &g_cal[i];
	return 0;
}

/* ------------------------------------------------------------ requests */

static const char *XML_HDR = "Content-Type: application/xml; charset=utf-8\r\n";

typedef struct
	{
	XmlScan x;
	/* the response being read */
	char href[200];
	char etag[100];
	char name[64];
	char ctag[100];
	char color[16];          /* calendar-color, "#RRGGBB" or "#RRGGBBAA" */
	int is_cal, vevent, vtodo, comps, writable;
	int whole;               /* the multistatus ended: the answer is all there */
	int truncated;           /* the server cut its own list short (507) */
	int cut;                 /* an event's calendar data was more than we keep: not all of it read */
	int got_data;
	int ndata;
	/* discovery */
	char principal[200];
	char home[200];
	/* events */
	FILE *out;
	const char *calid;
	int count;
	} Dav;

static Dav *g_dav;

static void sink(const char *data, int n, void *ctx) { xs_feed(&((Dav *)ctx)->x, data, n); }

/* (every list's end handler) a list is only used whole: its multistatus
   ended, and the server did not say it left some out (507, RFC 4918 and
   RFC 6578's truncated results). A cut list read as whole had calendars
   and events "gone from the server", and their Agenda entries deleted */
static void note_whole(Dav *d, const char *name, const char *text)
{
	if (!strcmp(name, "multistatus")) d->whole = 1;
	else if (!strcmp(name, "status") && pm_stristr(text, " 507")) d->truncated = 1;
}

static int not_whole(Dav *d, const char *what, char *why, int whymax)
{
	if (d->cut) {
		/* (used as whole, a cut series lost its later instances, and their
		   Agenda entries went as "deleted on the server") */
		pm_log("caldav: %s: an event too big to read whole", what);
		snprintf(why, whymax, "An event in %s is too big to read", what);
		return 1;
	}
	if (d->whole && !d->truncated) return 0;
	pm_log("caldav: %s: the list is not whole (%s)", what, d->truncated ? "507" : "no end");
	snprintf(why, whymax, "The calendar server's answer was cut short");
	return 1;
}

static int propfind(const char *path, int depth, const char *props, Dav *d, XsEnd end,
                    HttpResp *r, char *why, int whymax)
{
	static char body[900], hdr[120];
	int n = snprintf(body, sizeof(body),
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<D:propfind xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\" "
		"xmlns:CS=\"http://calendarserver.org/ns/\" xmlns:A=\"http://apple.com/ns/ical/\">"
		"<D:prop>%s</D:prop></D:propfind>", props);
	snprintf(hdr, sizeof(hdr), "%sDepth: %d\r\n", XML_HDR, depth);
	xs_init(&d->x, g_text, TEXT_MAX, end, d);
	return http_request("PROPFIND", path, hdr, body, n, r, sink, d, why, whymax);
}

/* ------------------------------------------------------------ discovery */

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
		else if (!strcmp(up, "calendar-home-set")) pm_copy(d->home, t, sizeof(d->home));   /* (with its host: http_follow) */
	}
}

static int discover(char *why, int whymax)
{
	HttpResp r;
	char path[200];
	int res, hops, attempt;
	const char *props = "<D:current-user-principal/><C:calendar-home-set/>";

	for (attempt = 0; attempt < 3 && !g_home[0]; attempt++) {
		if (attempt == 0) pm_copy(path, g_t.path && g_t.path[0] ? g_t.path : "/.well-known/caldav", sizeof(path));
		else if (attempt == 1) pm_copy(path, "/", sizeof(path));
		else {
			/* Fastmail's layout, if the server wouldn't say */
			snprintf(g_home, sizeof(g_home), "/dav/calendars/user/%s/", g_t.user);
			break;
		}
		for (hops = 0; hops < 4; hops++) {
			memset(g_dav, 0, sizeof(*g_dav));
			res = propfind(path, 0, props, g_dav, disc_end, &r, why, whymax);
			if (res != CAL_RES_OK) return res;
			if (r.status >= 300 && r.status < 400 && r.location[0]) { http_follow(r.location, path, sizeof(path)); continue; }
			break;
		}
		if (r.status != 207) continue;
		if (g_dav->home[0]) { pm_copy(g_home, g_dav->home, sizeof(g_home)); break; }
		if (g_dav->principal[0]) {
			http_follow(g_dav->principal, path, sizeof(path));
			memset(g_dav, 0, sizeof(*g_dav));
			res = propfind(path, 0, props, g_dav, disc_end, &r, why, whymax);
			if (res != CAL_RES_OK) return res;
			if (r.status == 207 && g_dav->home[0]) { pm_copy(g_home, g_dav->home, sizeof(g_home)); break; }
		}
	}
	pm_log("caldav: calendar home %s", g_home);
	return CAL_RES_OK;
}

/* the calendars in the home collection */
static void list_end(XmlScan *x, const char *name, const char *text, int len, void *ctx)
{
	Dav *d = (Dav *)ctx;
	char t[200];
	(void)len;
	note_whole(d, name, text);
	if (!strcmp(name, "href") && !strcmp(xs_parent(x, 1), "response")) {
		pm_copy(t, text, sizeof(t)); trim(t);
		just_path(t, d->href, sizeof(d->href));
	} else if (!strcmp(name, "calendar") && !strcmp(xs_parent(x, 1), "resourcetype")) d->is_cal = 1;
	else if (!strcmp(name, "displayname")) { cs_utf8_to_cp1252(text, len, d->name, sizeof(d->name)); trim(d->name); clean(d->name); }
	else if (!strcmp(name, "getctag")) { pm_copy(d->ctag, text, sizeof(d->ctag)); trim(d->ctag); clean(d->ctag); }
	else if (!strcmp(name, "calendar-color")) { pm_copy(d->color, text, sizeof(d->color)); trim(d->color); clean(d->color); }
	else if (!strcmp(name, "comp") && !strcmp(xs_parent(x, 1), "supported-calendar-component-set")) {
		d->comps = 1;
		if (pm_stristr(x->tag, "VEVENT")) d->vevent = 1;
		if (pm_stristr(x->tag, "VTODO")) d->vtodo = 1;
	} else if ((!strcmp(name, "write") || !strcmp(name, "all") || !strcmp(name, "write-content")) &&
	           !strcmp(xs_parent(x, 1), "privilege")) d->writable = 1;
	else if (!strcmp(name, "response")) {
		if (d->is_cal && (d->vevent || !d->comps || (g_t.todos && d->vtodo)) && d->href[0]) {
			Cal *c = cal_by_href(d->href);
			if (!c && g_ncal < MAX_CALS) {
				c = &g_cal[g_ncal++];
				memset(c, 0, sizeof(*c));
				pm_copy(c->href, d->href, sizeof(c->href));
				sprintf(c->id, "%08lX", fnv(d->href));
				c->sync = 1;
			}
			if (c) {
				c->seen = 1;
				if (strcmp(c->ctag, d->ctag) || !d->ctag[0]) c->fetch = 1;
				/* (kept aside: saved as its ctag only once its events are
				   in events.txt. Saved here, one dropped fetch said "No
				   calendar changes" for ever) */
				pm_copy(c->newctag, d->ctag, sizeof(c->newctag));
				pm_copy(c->name, d->name[0] ? d->name : "Calendar", sizeof(c->name));
				c->flags[0] = 0;
				if (d->writable) strcat(c->flags, "W");
				if (g_t.todos && (d->vtodo || !d->comps)) strcat(c->flags, "T");   /* (PsiSync) it keeps to-dos */
				c->colour[0] = 0;
				if (d->color[0] == '#' && strlen(d->color) >= 7) { memcpy(c->colour, d->color + 1, 6); c->colour[6] = 0; }
			}
		}
		d->href[0] = d->name[0] = d->ctag[0] = d->color[0] = 0;
		d->is_cal = d->vevent = d->vtodo = d->comps = d->writable = 0;
	}
}

static int list_calendars(char *why, int whymax)
{
	HttpResp r;
	char home[200];
	int res, i, def = -1;
	pm_progress("Looking for calendars\x85");
	if (!g_home[0]) {
		res = discover(why, whymax);
		if (res != CAL_RES_OK) return res;
	}
	for (i = 0; i < g_ncal; i++) { g_cal[i].seen = 0; g_cal[i].fetch = 0; }
	memset(g_dav, 0, sizeof(*g_dav));
	/* (the home may be on another of the server's hosts: iCloud's partitions) */
	http_follow(g_home, home, sizeof(home));
	res = propfind(home, 1,
		"<D:resourcetype/><D:displayname/><CS:getctag/><C:supported-calendar-component-set/>"
		"<D:current-user-privilege-set/><A:calendar-color/>", g_dav, list_end, &r, why, whymax);
	if (res != CAL_RES_OK) return res;
	if (r.status != 207) {
		if (r.status == 404) { snprintf(why, whymax, "No calendars found at %.60s", g_home); g_home[0] = 0; save_cals(); }
		else snprintf(why, whymax, "The calendar server %s", http_status_words(r.status));
		return CAL_RES_FAILED;
	}
	if (not_whole(g_dav, "the calendars", why, whymax)) return CAL_RES_FAILED;
	/* calendars that have gone */
	for (i = 0; i < g_ncal; ) {
		if (!g_cal[i].seen) { g_cal[i] = g_cal[--g_ncal]; continue; }
		i++;
	}
	/* where new events go: the one marked before, else the first writable */
	for (i = 0; i < g_ncal; i++) if (strchr(g_cal[i].flags, 'W') && def < 0) def = i;
	{
		char path[200], line[300];
		FILE *f;
		path_of("default.txt", path, sizeof(path));
		if ((f = fopen(path, "r")) != 0) {
			if (fgets(line, sizeof(line), f)) {
				Cal *c;
				line[strcspn(line, "\r\n")] = 0;
				c = cal_by_id(line);
				if (c && strchr(c->flags, 'W')) def = (int)(c - g_cal);
			}
			fclose(f);
		}
	}
	if (def >= 0) strcat(g_cal[def].flags, "D");
	save_cals();
	if (!g_ncal) { snprintf(why, whymax, "There are no calendars on %s", g_t.host); return CAL_RES_FAILED; }
	return CAL_RES_OK;
}

/* ------------------------------------------------------------ events */

typedef struct { Dav *d; } EvCtx;

static void note_series(const char *href, const char *etag);

static void one_event(const IcsEvent *e, void *ctx)
{
	Dav *d = (Dav *)ctx;
	int zone = g_t.zone;
	long s = e->start, en = e->end;
	char a[16], b[16], summary[200], location[120];
	if (e->cancelled) return;
	if (g_t.series && (e->recurid[0] || e->recurring)) note_series(d->href, d->etag);
	if (!e->allday && !e->floating) { s = cal_utc_to_local(zone, s); en = cal_utc_to_local(zone, en); }
	if (e->allday && en > s) en -= 86400;          /* DTEND is the day after */
	cal_fmt_local(s, a);
	cal_fmt_local(en, b);
	pm_copy(summary, e->summary[0] ? e->summary : "(no title)", sizeof(summary));
	pm_copy(location, e->location, sizeof(location));
	clean(summary);
	clean(location);
	{
		/* my status: the attendee whose address is the account's */
		char mine[4] = "";
		const char *at = g_t.user && g_t.user[0] ? pm_stristr(e->attendees, g_t.user) : 0;
		if (at) {
			at += strlen(g_t.user);
			if (at[0] == '>' && at[1] == ' ' && at[2]) { mine[0] = at[2]; mine[1] = 0; }
		}
		fprintf(d->out, "%s\t%s\t%s\t%s\t%s%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n", d->calid, d->href, d->etag, e->recurid,
			e->allday ? "A" : "", (e->recurid[0] || e->recurring) ? "R" : "", a, b, e->alarm, summary, location,
			e->tzid, mine, e->organizer, e->attendees);
	}
	d->count++;
}

/* (PsiSync) a to-do as an events.txt line */
static void one_todo(const IcsTodo *t, void *ctx)
{
	Dav *d = (Dav *)ctx;
	char a[16], summary[200];
	if (t->cancelled) return;
	a[0] = 0;
	if (t->has_due) {
		long due = t->due;
		if (t->due_utc) due = cal_utc_to_local(g_t.zone, due);
		cal_fmt_local(due, a);
	}
	pm_copy(summary, t->summary[0] ? t->summary : "(no title)", sizeof(summary));
	clean(summary);
	fprintf(d->out, "%s\t%s\t%s\t\tT%s\t%s\t%s\t%d\t%s\t\n", d->calid, d->href, d->etag,
		t->done ? "C" : "", a, a, t->priority, summary);
	d->count++;
}

static int g_todo_report;      /* the report being read is of to-dos */

/* (PsiSync) series seen in this calendar's report: their masters are asked
   for afterwards, unexpanded, to give the Agenda a repeat */
static int g_nseries;

static void note_series(const char *href, const char *etag)
{
	int i;
	for (i = 0; i < g_nseries; i++) if (!strcmp(g_series[i], href)) return;
	if (g_nseries < MAX_SERIES) { pm_copy(g_series[g_nseries], href, 200); pm_copy(g_series_etag[g_nseries++], etag, 80); }
}

/* a master read (calendar-multiget): the series as one line, if the Agenda can say its rule */
typedef struct { IcsEvent master; int have, overrides; } MasterCtx;
static void one_part(const IcsEvent *e, void *ctx)
{
	MasterCtx *m = (MasterCtx *)ctx;
	if (e->recurid[0]) m->overrides++;
	else if (e->recurring) { m->master = *e; m->have = 1; }
}

static int g_master_report;

static void report_end(XmlScan *x, const char *name, const char *text, int len, void *ctx)
{
	Dav *d = (Dav *)ctx;
	char t[200];
	note_whole(d, name, text);
	if (!strcmp(name, "href") && !strcmp(xs_parent(x, 1), "response")) {
		pm_copy(t, text, sizeof(t)); trim(t);
		just_path(t, d->href, sizeof(d->href));
	} else if (!strcmp(name, "getetag")) { pm_copy(d->etag, text, sizeof(d->etag)); trim(d->etag); clean(d->etag); }
	else if (!strcmp(name, "calendar-data")) {
		/* keep the data until the response ends: the href and etag may
		   come after it. The text buffer is ours until the next element. */
		if (x->over || len >= OBJ_MAX - 1) {
			pm_log("caldav: %s is too big, cut short", d->href);
			d->cut = 1;
		}
		if (len > 0) {
			memmove(g_obj, text, len < OBJ_MAX - 1 ? len : OBJ_MAX - 1);
			d->ndata = len < OBJ_MAX - 1 ? len : OBJ_MAX - 1;
			g_obj[d->ndata] = 0;
			d->got_data = 1;
		}
	} else if (!strcmp(name, "response")) {
		if (d->got_data && d->href[0]) {
			if (g_todo_report) ics_each_todo(g_obj, d->ndata, one_todo, d);
			else if (g_master_report) {
				MasterCtx m;
				memset(&m, 0, sizeof(m));
				ics_each(g_obj, d->ndata, one_part, &m);
				if (m.have && !m.overrides && m.master.rrule[0] && m.master.rrule[0] != '!') {
					const IcsEvent *e = &m.master;
					long s = e->start, en = e->end;
					char a[16], b[16], summary[200], location[120];
					int y, mo, dd;
					CalRepeat rpt;
					if (!e->allday && !e->floating) { s = cal_utc_to_local(g_t.zone, s); en = cal_utc_to_local(g_t.zone, en); }
					if (e->allday && en > s) en -= 86400;
					cal_civil(s / 86400, &y, &mo, &dd);
					if (cal_rrule_parse(e->rrule, y, mo, dd, &rpt) == 0) {
						cal_fmt_local(s, a);
						cal_fmt_local(en, b);
						pm_copy(summary, e->summary[0] ? e->summary : "(no title)", sizeof(summary));
						pm_copy(location, e->location, sizeof(location));
						clean(summary); clean(location);
						fprintf(d->out, "%s\t%s\t%s\t\tS%s\t%s\t%s\t%d\t%s\t%s\t%s\t\t%s\t%s\t%s\t%s\n", d->calid, d->href, d->etag,
							e->allday ? "A" : "", a, b, e->alarm, summary, location, e->tzid, e->organizer, e->attendees,
							e->rrule, e->exdates);
						d->count++;
					}
				}
			}
			else ics_each(g_obj, d->ndata, one_event, d);
		}
		d->href[0] = d->etag[0] = 0;
		d->got_data = 0;
	}
}

static void window(char *start, char *end, char *first_day)
{
	long now = pm_time(), today = now - now % 86400;
	int back = g_t.days_back >= 0 ? g_t.days_back : 30;
	int ahead = g_t.days_ahead > 0 ? g_t.days_ahead : 180;
	cal_fmt_utc(today - back * 86400L, start);
	cal_fmt_utc(today + (ahead + 1) * 86400L, end);
	cal_fmt_date(today - back * 86400L, first_day);
}

static int fetch_events(Cal *c, FILE *out, char *why, int whymax)
{
	static char body[1500];
	HttpResp r;
	char a[24], b[24], day[12];
	int n, res;
	window(a, b, day);
	n = snprintf(body, sizeof(body),
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<C:calendar-query xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
		"<D:prop><D:getetag/><C:calendar-data>"
		"<C:comp name=\"VCALENDAR\"><C:prop name=\"VERSION\"/><C:comp name=\"VEVENT\">"
		"<C:prop name=\"UID\"/><C:prop name=\"SUMMARY\"/><C:prop name=\"LOCATION\"/>"
		"<C:prop name=\"DTSTART\"/><C:prop name=\"DTEND\"/><C:prop name=\"DURATION\"/>"
		"<C:prop name=\"RECURRENCE-ID\"/><C:prop name=\"RRULE\"/><C:prop name=\"RDATE\"/>"
		"<C:prop name=\"STATUS\"/><C:prop name=\"ORGANIZER\"/><C:prop name=\"ATTENDEE\"/>"
		"<C:comp name=\"VALARM\"><C:allprop/></C:comp></C:comp></C:comp>"
		"<C:expand start=\"%s\" end=\"%s\"/></C:calendar-data></D:prop>"
		"<C:filter><C:comp-filter name=\"VCALENDAR\"><C:comp-filter name=\"VEVENT\">"
		"<C:time-range start=\"%s\" end=\"%s\"/></C:comp-filter></C:comp-filter></C:filter>"
		"</C:calendar-query>", a, b, a, b);
	pm_progress("Fetching %s\x85", c->name);
	g_nseries = 0;
	memset(g_dav, 0, sizeof(*g_dav));
	g_dav->out = out;
	g_dav->calid = c->id;
	xs_init(&g_dav->x, g_text, TEXT_MAX, report_end, g_dav);
	res = http_request("REPORT", c->href, "Content-Type: application/xml; charset=utf-8\r\nDepth: 1\r\n",
		body, n, &r, sink, g_dav, why, whymax);
	if (res != CAL_RES_OK) return res;
	if (r.status != 207) { snprintf(why, whymax, "Could not read %s - the server %s", c->name, http_status_words(r.status)); return CAL_RES_FAILED; }
	if (not_whole(g_dav, c->name, why, whymax)) return CAL_RES_FAILED;
	pm_log("caldav: %s: %d events", c->name, g_dav->count);
	if (g_t.series && g_nseries) {
		/* (PsiSync) the series' masters, unexpanded, eight at a time */
		int i = 0, events = g_dav->count;
		while (i < g_nseries) {
			int k;
			n = snprintf(body, sizeof(body), "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
				"<C:calendar-multiget xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
				"<D:prop><D:getetag/><C:calendar-data/></D:prop>");
			/* (as many as fit: snprintf returns the length asked for, and
			   eight long addresses once ran n past the buffer's end, so the
			   next piece was written with a size gone negative) */
			for (k = 0; k < 8 && i < g_nseries; k++, i++) {
				if (n + (int)strlen(g_series[i]) + 60 >= (int)sizeof(body)) break;
				n += snprintf(body + n, sizeof(body) - n, "<D:href>%s</D:href>", g_series[i]);
			}
			n += snprintf(body + n, sizeof(body) - n, "</C:calendar-multiget>");
			if (n >= (int)sizeof(body) - 1) break;
			memset(g_dav, 0, sizeof(*g_dav));
			g_dav->out = out;
			g_dav->calid = c->id;
			g_dav->count = events;
			xs_init(&g_dav->x, g_text, TEXT_MAX, report_end, g_dav);
			g_master_report = 1;
			res = http_request("REPORT", c->href, "Content-Type: application/xml; charset=utf-8\r\nDepth: 1\r\n",
				body, n, &r, sink, g_dav, why, whymax);
			g_master_report = 0;
			if (res != CAL_RES_OK) return res;
			if (r.status == 207 && not_whole(g_dav, c->name, why, whymax)) return CAL_RES_FAILED;
			events = g_dav->count;
		}
		pm_log("caldav: %s: %d series asked for", c->name, g_nseries);
		g_nseries = 0;
	}
	if (g_t.todos && strchr(c->flags, 'T')) {
		/* (PsiSync) its to-dos: all of them, not expanded */
		int events = g_dav->count;
		n = snprintf(body, sizeof(body),
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			"<C:calendar-query xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
			"<D:prop><D:getetag/><C:calendar-data/></D:prop>"
			"<C:filter><C:comp-filter name=\"VCALENDAR\"><C:comp-filter name=\"VTODO\"/></C:comp-filter></C:filter>"
			"</C:calendar-query>");
		pm_progress("Fetching %s's to-dos\x85", c->name);
		memset(g_dav, 0, sizeof(*g_dav));
		g_dav->out = out;
		g_dav->calid = c->id;
		g_dav->count = events;
		xs_init(&g_dav->x, g_text, TEXT_MAX, report_end, g_dav);
		g_todo_report = 1;
		res = http_request("REPORT", c->href, "Content-Type: application/xml; charset=utf-8\r\nDepth: 1\r\n",
			body, n, &r, sink, g_dav, why, whymax);
		g_todo_report = 0;
		if (res != CAL_RES_OK) return res;
		if (r.status != 207) { snprintf(why, whymax, "Could not read %s's to-dos - the server %s", c->name, http_status_words(r.status)); return CAL_RES_FAILED; }
		if (not_whole(g_dav, c->name, why, whymax)) return CAL_RES_FAILED;
		pm_log("caldav: %s: %d to-dos", c->name, g_dav->count - events);
	}
	return CAL_RES_OK;
}

/* ------------------------------------------------------------ changes from the Psion */

static void body_to_obj(const char *data, int n, void *ctx)
{
	int *len = (int *)ctx;
	if (*len + n >= OBJ_MAX) n = OBJ_MAX - 1 - *len;
	if (n > 0) { memcpy(g_obj + *len, data, n); *len += n; }
}

/* resources changed earlier in this batch: their etags have moved on */
static int g_ntouched;

static int touched(const char *href)
{
	int i;
	for (i = 0; i < g_ntouched; i++) if (!strcmp(g_touched[i], href)) return 1;
	return 0;
}

static void touch(const char *href)
{
	if (!touched(href) && g_ntouched < MAX_TOUCHED) pm_copy(g_touched[g_ntouched++], href, sizeof(g_touched[0]));
}

static void pushed(FILE *res, const char *key, const char *what, const char *href)
{
	if (res) fprintf(res, "%s\t%s\t%s\n", key, what, href);
}

static void read_change(char **p, IcsChange *c)
{
	char *flags;
	memset(c, 0, sizeof(*c));
	c->start = cal_parse_local(field(p));
	c->end = cal_parse_local(field(p));
	flags = field(p);
	c->allday = strchr(flags, 'A') != 0;
	c->alarm = atoi(field(p));
	if (strchr(flags, 'T')) {
		/* (PsiSync) a to-do: its due (0 none), done, priority in the alarm's place */
		c->todo = 1;
		c->done = strchr(flags, 'C') != 0;
		c->priority = c->alarm;
		c->alarm = -2;
		if (c->start < 0) c->start = 0;
	}
	pm_copy(c->summary, field(p), sizeof(c->summary));
	pm_copy(c->location, field(p), sizeof(c->location));
}

/* (PsiSync) a series: "R" TAB rrule TAB exdates, after the rest of the line */
static void read_repeat(char **p, IcsChange *c)
{
	char *r = field(p);
	if (r[0] != 'R') return;
	pm_copy(c->rrule, field(p), sizeof(c->rrule));
	pm_copy(c->exdates, field(p), sizeof(c->exdates));
	c->series = c->rrule[0] != 0;
}

static void mark_fetch(const char *href)
{
	int i;
	for (i = 0; i < g_ncal; i++) {
		int n = (int)strlen(g_cal[i].href);
		if (n && !strncmp(href, g_cal[i].href, n)) g_cal[i].fetch = 1;
	}
}

/* (a change sent again: its reply was lost) does the server's copy already
   say what the change says? Then the change was made, and is no conflict:
   called one, "keep both" made a copy of the entry, and every lost reply
   was another duplicate. Compared as events.txt has them. */
typedef struct { const char *recurid; const IcsChange *c; int found, same; } SameCtx;

static void plain(char *out, const char *in, int max)
{
	int n;
	pm_copy(out, in, max);
	for (n = 0; out[n]; n++) if ((unsigned char)out[n] < 32) out[n] = ' ';
	trim(out);
}

static int same_text(const char *server, const char *psion)
{
	char a[200], b[200];
	plain(a, server, sizeof(a));
	plain(b, psion, sizeof(b));
	return !strcmp(a, b);
}

static void same_event(const IcsEvent *e, void *ctx)
{
	SameCtx *m = (SameCtx *)ctx;
	const IcsChange *c = m->c;
	long s = e->start, en = e->end;
	char a1[16], a2[16], b1[16], b2[16];
	if (strcmp(e->recurid, m->recurid) || m->found++) return;
	if (!e->allday && !e->floating) { s = cal_utc_to_local(g_t.zone, s); en = cal_utc_to_local(g_t.zone, en); }
	if (e->allday && en > s) en -= 86400;
	cal_fmt_local(s, a1); cal_fmt_local(c->start, a2);
	cal_fmt_local(en, b1); cal_fmt_local(c->end, b2);
	if (strcmp(a1, a2) || strcmp(b1, b2) || !e->allday != !c->allday) return;
	if (c->alarm >= -1 && e->alarm != c->alarm) return;           /* (-2: the alarm left alone) */
	if (!same_text(e->summary[0] ? e->summary : "(no title)", c->summary)) return;
	if (!same_text(e->location, c->location)) return;
	m->same = 1;
}

static void same_todo(const IcsTodo *t, void *ctx)
{
	SameCtx *m = (SameCtx *)ctx;
	const IcsChange *c = m->c;
	char a1[16], a2[16];
	if (m->found++) return;
	if (t->has_due) {
		long due = t->due_utc ? cal_utc_to_local(g_t.zone, t->due) : t->due;
		if (!c->start) return;
		cal_fmt_local(due, a1); cal_fmt_local(c->start, a2);
		if (strcmp(a1, a2)) return;
	} else if (c->start) return;
	if (!t->done != !c->done || t->priority != c->priority) return;
	if (!same_text(t->summary[0] ? t->summary : "(no title)", c->summary)) return;
	m->same = 1;
}

static int already_there(const char *obj, int len, const char *recurid, const IcsChange *c)
{
	SameCtx m;
	if (c->series) return 0;             /* (a series' rule and dates: not compared) */
	memset(&m, 0, sizeof(m));
	m.recurid = recurid;
	m.c = c;
	if (c->todo) ics_each_todo(obj, len, same_todo, &m);
	else ics_each(obj, len, same_event, &m);
	return m.found == 1 && m.same;
}

/* one change; returns CAL_RES_OK unless the connection is gone */
static int push_one(char *line, FILE *res, char *why, int whymax)
{
	char *p = line, *op = field(&p), *key = field(&p);
	char hdr[200], href[260];
	HttpResp r;
	IcsChange c;
	int rc, len;

	if (op[0] == 'N') {
		unsigned char rnd[8];
		char uid[40];
		Cal *cal = cal_by_id(field(&p));
		int i;
		read_change(&p, &c);
		if (!cal) { pushed(res, key, "failed", ""); return CAL_RES_OK; }
		/* the app names it, so a retry after a lost reply finds it there
		   (If-None-Match) rather than making a second one */
		pm_copy(uid, field(&p), sizeof(uid));
		read_repeat(&p, &c);
		for (i = 0; uid[i]; i++)
			if (!((uid[i] >= '0' && uid[i] <= '9') || (uid[i] >= 'a' && uid[i] <= 'z') || (uid[i] >= 'A' && uid[i] <= 'Z') || uid[i] == '-')) uid[i] = '-';
		if (!uid[0]) {
			genrandom(rnd, sizeof(rnd));
			for (i = 0; i < 8; i++) sprintf(uid + i * 2, "%02x", rnd[i]);
			strcat(uid, "-psion");
		}
		len = c.todo ? ics_todo_new(&c, g_t.zone, uid, g_obj, OBJ_MAX) : ics_new(&c, g_t.zone, uid, g_obj, OBJ_MAX);
		if (len < 0) { pushed(res, key, "failed", ""); return CAL_RES_OK; }
		snprintf(href, sizeof(href), "%s%s.ics", cal->href, uid);
		rc = http_request("PUT", href, "Content-Type: text/calendar; charset=utf-8\r\nIf-None-Match: *\r\n",
			g_obj, len, &r, 0, 0, why, whymax);
		if (rc != CAL_RES_OK) return rc;
		pushed(res, key, r.status / 100 == 2 || r.status == 412 ? "ok" : "failed", href);
		cal->fetch = 1;
		pm_log("caldav: new %s -> %d", href, r.status);
		return CAL_RES_OK;
	}
	if (op[0] == 'P') {
		/* (PsiSync) an invitation's reply: P TAB key TAB href TAB etag TAB recurid TAB A|D|T */
		char *h = field(&p), *etag = field(&p), *recurid = field(&p), *st = field(&p);
		pm_copy(href, h, sizeof(href));
		mark_fetch(href);
		len = 0;
		rc = http_request("GET", href, "", 0, 0, &r, body_to_obj, &len, why, whymax);
		if (rc != CAL_RES_OK) return rc;
		if (r.status == 404) { pushed(res, key, "gone", href); return CAL_RES_OK; }
		if (r.status != 200 || len >= OBJ_MAX - 1) { pushed(res, key, "failed", href); return CAL_RES_OK; }
		(void)etag;                  /* (a reply goes on the server's latest copy) */
		g_obj[len] = 0;
		len = ics_partstat(g_obj, len, OBJ_MAX - 1, recurid, g_t.user, st[0]);
		if (len < 0) { pushed(res, key, "failed", href); return CAL_RES_OK; }
		snprintf(hdr, sizeof(hdr), "Content-Type: text/calendar; charset=utf-8\r\n%s%s%s",
			r.etag[0] ? "If-Match: " : "", r.etag, r.etag[0] ? "\r\n" : "");
		rc = http_request("PUT", href, hdr, g_obj, len, &r, 0, 0, why, whymax);
		if (rc != CAL_RES_OK) return rc;
		pushed(res, key, r.status / 100 == 2 ? "ok" : r.status == 412 ? "conflict" : "failed", href);
		pm_log("caldav: reply %c %s -> %d", st[0], href, r.status);
		return CAL_RES_OK;
	}
	if (op[0] == 'M' || op[0] == 'D') {
		char *h = field(&p), *etag = field(&p), *recurid = field(&p);
		pm_copy(href, h, sizeof(href));
		mark_fetch(href);
		if (touched(href)) etag = (char *)"";
		if (op[0] == 'D' && !recurid[0]) {
			snprintf(hdr, sizeof(hdr), "If-Match: %s\r\n", etag);
			rc = http_request("DELETE", href, etag[0] ? hdr : "", 0, 0, &r, 0, 0, why, whymax);
			if (rc != CAL_RES_OK) return rc;
			pushed(res, key, r.status / 100 == 2 ? "ok" : r.status == 404 ? "gone" : r.status == 412 ? "conflict" : "failed", href);
			pm_log("caldav: delete %s -> %d", href, r.status);
			return CAL_RES_OK;
		}
		/* change the server's own text */
		len = 0;
		rc = http_request("GET", href, "", 0, 0, &r, body_to_obj, &len, why, whymax);
		if (rc != CAL_RES_OK) return rc;
		if (r.status == 404) { pushed(res, key, "gone", href); return CAL_RES_OK; }
		if (r.status != 200 || len >= OBJ_MAX - 1) { pushed(res, key, "failed", href); return CAL_RES_OK; }
		g_obj[len] = 0;
		if (etag[0] && r.etag[0] && strcmp(etag, r.etag)) {
			if (op[0] == 'M') {
				read_change(&p, &c);
				read_repeat(&p, &c);
				if (already_there(g_obj, len, recurid, &c)) {
					pm_log("caldav: %s %s: the server has it already (a reply lost?)", href, recurid);
					pushed(res, key, "ok", href);
					touch(href);
					return CAL_RES_OK;
				}
			}
			pushed(res, key, "conflict", href);
			return CAL_RES_OK;
		}
		if (op[0] == 'M') {
			read_change(&p, &c);
			read_repeat(&p, &c);
			len = c.todo ? ics_todo_change(g_obj, len, OBJ_MAX - 1, &c, g_t.zone)
			             : ics_change(g_obj, len, OBJ_MAX - 1, recurid, &c, g_t.zone);
		} else len = ics_exclude(g_obj, len, OBJ_MAX - 1, recurid, g_t.zone);
		if (len < 0) { pushed(res, key, "failed", href); return CAL_RES_OK; }
		snprintf(hdr, sizeof(hdr), "Content-Type: text/calendar; charset=utf-8\r\n%s%s%s",
			r.etag[0] ? "If-Match: " : "", r.etag, r.etag[0] ? "\r\n" : "");
		rc = http_request("PUT", href, hdr, g_obj, len, &r, 0, 0, why, whymax);
		if (rc != CAL_RES_OK) return rc;
		pm_log("caldav: %s %s %s -> %d", op, href, recurid, r.status);
		if (r.status == 412 && op[0] == 'M') {
			/* the etag moved since the GET: perhaps by this change itself,
			   its reply lost and the request sent again on a new connection
			   (http.c): the server's copy says which */
			len = 0;
			rc = http_request("GET", href, "", 0, 0, &r, body_to_obj, &len, why, whymax);
			if (rc != CAL_RES_OK) return rc;
			if (r.status == 200 && len < OBJ_MAX - 1) {
				g_obj[len] = 0;
				if (already_there(g_obj, len, recurid, &c)) {
					pm_log("caldav: %s %s: the server has it already (a reply lost?)", href, recurid);
					r.status = 204;
				} else r.status = 412;
			} else r.status = r.status == 404 ? 404 : 412;
			if (r.status == 404) { pushed(res, key, "gone", href); return CAL_RES_OK; }
		}
		pushed(res, key, r.status / 100 == 2 ? "ok" : r.status == 412 ? "conflict" : "failed", href);
		if (r.status / 100 == 2) touch(href);
		return CAL_RES_OK;
	}
	return CAL_RES_OK;
}

static int push_changes(int *n, char *why, int whymax)
{
	static char line[2400];      /* (a series' rule and dates, up to CAL_EXDATES: PsiSync) */
	char path[200], rpath[200];
	FILE *f, *res;
	int rc = CAL_RES_OK, total = 0, done = 0;
	*n = 0;
	path_of("push.txt", path, sizeof(path));
	if (!(f = fopen(path, "r"))) return CAL_RES_OK;
	while (fgets(line, sizeof(line), f)) if (line[0] != '#' && line[0] > ' ') total++;
	if (!total) { fclose(f); remove(path); return CAL_RES_OK; }
	fseek(f, 0, SEEK_SET);
	g_ntouched = 0;
	path_of("pushed.txt", rpath, sizeof(rpath));
	res = fopen(rpath, "a");
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0] == '#' || !line[0]) continue;
		pm_progress("Sending calendar changes (%d of %d)\x85", done + 1, total);
		rc = push_one(line, res, why, whymax);
		if (rc != CAL_RES_OK) break;
		done++;
	}
	fclose(f);
	if (res) fclose(res);
	if (rc == CAL_RES_OK) remove(path);
	else if (done > 0) {
		/* keep what wasn't sent for next time */
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
			/* (if the rest can't be written down, the whole file stays:
			   sending a change twice is harmless, losing one is not) */
			if (pm_fclose(out) != 0 || pm_replace(tmp, path) != 0) remove(tmp);
		} else if (in) fclose(in);
	}
	*n = done;
	st_changed();
	return rc;
}

/* ------------------------------------------------------------ the sync */

/* copies the lines of calendars we are not fetching again from the old
   events.txt; returns 0 if it can't be used (a new day, another zone) */
static int keep_old(FILE *out, const char *first_day)
{
	static char line[3000];      /* (a series' line: its dates taken out, up to CAL_EXDATES) */
	char path[200], head[60];
	FILE *f;
	int zone = g_t.zone;
	path_of("events.txt", path, sizeof(path));
	if (!(f = fopen(path, "r"))) return 0;
	snprintf(head, sizeof(head), "#PSIEV1\t%s\t%d", first_day, zone);
	if (!fgets(line, sizeof(line), f) || strncmp(line, head, strlen(head))) { fclose(f); return 0; }
	while (fgets(line, sizeof(line), f)) {
		char id[12];
		Cal *c;
		int n = (int)strcspn(line, "\t");
		if (n >= (int)sizeof(id)) continue;
		memcpy(id, line, n);
		id[n] = 0;
		c = cal_by_id(id);
		if (c && c->sync && !c->fetch) fputs(line, out);
	}
	fclose(f);
	return 1;
}

static int sync_now(int list_only, char *why, int whymax);

/* the buffers are only needed while syncing */
int cal_sync(int list_only, char *why, int whymax)
{
	int r;
	if (!g_cw) g_cw = (CalWork *)psi_work_alloc(sizeof(CalWork) + sizeof(Dav));   /* (still there: the last sync was abandoned) */
	if (!g_cw) { snprintf(why, whymax, "Not enough memory for the calendar"); return CAL_RES_FAILED; }
	g_text = g_cw->text;
	g_obj = g_cw->obj;
	g_dav = (Dav *)(g_cw + 1);
	r = sync_now(list_only, why, whymax);
	g_text = 0; g_obj = 0; g_dav = 0;
	psi_work_free(g_cw); g_cw = 0;
	psi_work_trim();                     /* (and what the objects took from the heap) */
	return r;
}

static int sync_now(int list_only, char *why, int whymax)
{
	char path[200], tmp[200], a1[24], b1[24], day[12];
	FILE *out;
	int res, i, sent = 0, total = 0, fetched = 0;
	const char *pass;

	memset(&g_t, 0, sizeof(g_t));
	calp_target(&g_t);
	if (!g_t.host) g_t.host = "";
	if (!g_t.path) g_t.path = "";
	if (!g_t.user) g_t.user = "";
	pass = g_t.pass ? g_t.pass : "";
	if (!g_t.host[0]) { snprintf(why, whymax, "No calendar server set up"); return CAL_RES_FAILED; }
	/* a clock reset (to 2002: the backup cell flat) would ask for the wrong
	   dates, and an empty answer deletes or doubles the Agenda's entries */
	if (!cal_clock_ok(pm_time(), CAL_BUILD_YEAR)) {
		pm_log("caldav: the clock says %ld, before %d: not synced", pm_time(), CAL_BUILD_YEAR);
		snprintf(why, whymax, "set the Psion's date first");
		return CAL_RES_FAILED;
	}
	if (!pass[0]) { snprintf(why, whymax, "Password needed for the calendar"); return CAL_RES_NEED_PASS; }
	snprintf(g_dir, sizeof(g_dir), "%scal%s", calp_dir(), CAL_SEP);
	pm_mkdir(g_dir);

	http_target(g_t.host, g_t.port ? g_t.port : 443, g_t.tls, g_t.user, pass);
	snprintf(g_where, sizeof(g_where), "%s%s", g_t.host, g_t.path);
	load_cals();
	res = list_calendars(why, whymax);
	if (res != CAL_RES_OK) return res;
	if (list_only) {
		snprintf(why, whymax, "%d calendar%s", g_ncal, g_ncal == 1 ? "" : "s");
		return CAL_RES_OK;
	}
	res = push_changes(&sent, why, whymax);
	if (res != CAL_RES_OK) return res;

	/* fetch what changed; the rest comes from the old file */
	window(a1, b1, day);
	path_of("events.txt", path, sizeof(path));
	path_of("events.tmp", tmp, sizeof(tmp));
	if (!(out = fopen(tmp, "w"))) { pm_write_why(why, whymax, "the calendar", tmp); return CAL_RES_FAILED; }
	fprintf(out, "#PSIEV1\t%s\t%d\n", day, g_t.zone);
	if (!keep_old(out, day)) {
		/* start again */
		fclose(out);
		out = fopen(tmp, "w");
		if (!out) return CAL_RES_FAILED;
		fprintf(out, "#PSIEV1\t%s\t%d\n", day, g_t.zone);
		for (i = 0; i < g_ncal; i++) g_cal[i].fetch = 1;
	}
	for (i = 0; i < g_ncal; i++) {
		Cal *c = &g_cal[i];
		if (!c->sync || !c->fetch) continue;
		res = fetch_events(c, out, why, whymax);
		if (res != CAL_RES_OK) {
			fclose(out);
			remove(tmp);
			/* (the ctags were not moved on: the next sync fetches again) */
			return res;
		}
		total += g_dav->count;
		fetched++;
	}
	if (pm_fclose(out) != 0 || pm_replace(tmp, path) != 0) {
		remove(tmp);
		pm_write_why(why, whymax, "the calendar", path);
		return CAL_RES_FAILED;
	}
	/* the events are saved: now the ctags they are from */
	for (i = 0; i < g_ncal; i++) if (g_cal[i].sync && g_cal[i].fetch) pm_copy(g_cal[i].ctag, g_cal[i].newctag, sizeof(g_cal[i].ctag));
	save_cals();
	st_changed();
	if (fetched) snprintf(why, whymax, "%s%d event%s", sent ? "Sent changes; " : "", total, total == 1 ? "" : "s");
	else snprintf(why, whymax, sent ? "Sent %d change%s" : "No calendar changes", sent, sent == 1 ? "" : "s");
	return CAL_RES_OK;
}
