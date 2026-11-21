/* cal.h - the calendar engine (psion-core; it was PsiMail's): CalDAV
 * (caldav.c) over HTTP (http.c), with iCalendar (ics.c), a little XML
 * (xmlscan.c) and time zones (caltz.c). calport.h is what the program gives it.
 *
 * The engine keeps the server's side in <dir>cal\ (see caldav.c for the
 * files); the Agenda half (core/mail/agenda/pmcal.cpp, in PsiMail.app or
 * PsiSync) puts those events into the Psion's Agenda and hands back what
 * changed there (push.txt).
 */
#ifndef PM_CAL_H
#define PM_CAL_H

/* ---- dates and time zones (caltz.c). Times are seconds since 1970 UTC
   ("utc") or the same count on the local wall clock ("local"). */
long cal_days(int y, int m, int d);                  /* days since 1970-01-01 */
void cal_civil(long days, int *y, int *m, int *d);
int  cal_wday(long days);                            /* 0 = Sunday */
long cal_utc_to_local(int zone, long utc);
long cal_local_to_utc(int zone, long local);
int  cal_zone_count(void);
/* "20260930T101500Z" / "20260930T101500" / "20260930" -> seconds; sets
   *utc (Z), *date_only (no time); 0 ok, -1 bad */
int  cal_parse_time(const char *s, long *t, int *utc, int *date_only);
void cal_fmt_utc(long utc, char *out);               /* 20260930T101500Z */
void cal_fmt_date(long t, char *out);                /* 20260930 */
void cal_fmt_local(long t, char *out);               /* 202609301015 (the app's form) */
long cal_parse_local(const char *s);                 /* 202609301015 -> local seconds */

/* ---- a small streaming XML reader (xmlscan.c): element names without
   their namespace prefix, text with entities decoded (UTF-8) */
#define XS_DEPTH 12
typedef struct XmlScan XmlScan;
typedef void (*XsEnd)(XmlScan *x, const char *name, const char *text, int len, void *ctx);
struct XmlScan
	{
	int st;                  /* parser state */
	char tag[160];
	int tlen;
	int quote;
	char last;               /* the tag's last character ("/": empty element) */
	char ent[12];
	int elen;
	char path[XS_DEPTH][48];
	int depth;
	char *text;              /* the current element's text */
	int len, max;
	int over;                /* text was cut short */
	XsEnd end;
	void *ctx;
	/* (21 November 2026) a text buffer that grows in place (psiwork.h's
	   psi_work_grow): *room bytes of max are there now, grow(text, n) takes
	   more (0 ok). room 0 (as xs_init leaves it): all max are there */
	int *room;
	int (*grow)(void *text, unsigned long bytes);
	};
void xs_init(XmlScan *x, char *textbuf, int textmax, XsEnd end, void *ctx);
void xs_feed(XmlScan *x, const char *in, int n);
/* name of the element 'up' levels above the one ending (1 = its parent) */
const char *xs_parent(XmlScan *x, int up);

/* ---- iCalendar (ics.c) */
/* (review CR-25) a series' dates taken out, "20261201,20261208": room for 100
   (9 bytes each). pmcal.cpp's KPmExDates is the same; a series with more is
   not sent as a series (its rule and dates on the server are left alone) */
#define CAL_EXDATES 900
typedef struct
	{
	char uid[120];
	char summary[200];       /* cp1252 */
	char location[120];      /* cp1252 */
	char recurid[20];        /* RECURRENCE-ID as UTC (or a date), "" if none */
	long start, end;         /* utc, or local for floating / all-day */
	int allday;
	int floating;            /* times without a zone: already local */
	int alarm;               /* minutes before the start, -1 = none */
	int recurring;           /* has RRULE/RDATE (a master) */
	int cancelled;
	/* (PsiSync's sidecar) */
	char tzid[48];           /* DTSTART's TZID, "" if UTC, a date or floating */
	char organizer[120];     /* "Name <email>" (cp1252) */
	char attendees[400];     /* "Name <email> S;..." S: A accepted, D declined, T tentative, N needs action */
	char rrule[160];         /* (a master) its RRULE's value */
	char exdates[CAL_EXDATES];   /* ... its EXDATEs as dates, "20261201,20261208" (UTC ones: the date in UTC) */
	} IcsEvent;
/* a PARTSTAT as A, D, T or N (0 if none) */
char ics_partstat_letter(const char *partstat);
/* calls fn for every VEVENT in the (UTF-8) text; returns how many */
int  ics_each(const char *text, int len, void (*fn)(const IcsEvent *e, void *ctx), void *ctx);

/* the reader's pieces (for invite.c): the next unfolded line (-1 at the
   end), a line split into its name (upper case), parameters and value, and
   a TEXT value unescaped into cp1252 */
int  ics_line(const char *text, int len, int *pos, char *line, int max);
const char *ics_prop(const char *line, char *name, int nmax, char *params, int pmax);
void ics_text(const char *v, char *out, int max);

/* an event's details from the Psion, for a new event or a change */
typedef struct
	{
	long start, end;         /* local wall-clock seconds */
	int allday;
	int alarm;               /* minutes before, -1 none */
	char summary[200];       /* cp1252 */
	char location[120];      /* cp1252 */
	/* (PsiSync) a repeating Agenda entry: the master's rule, replacing the
	   server's RRULE, RDATE and EXDATE. series 0: leave them as they are */
	int series;
	char rrule[120];         /* "FREQ=WEEKLY;BYDAY=MO,TH" (cal_rrule) */
	char exdates[CAL_EXDATES];   /* the instances taken out: local dates "20261201,20261208" */
	/* (PsiSync) a to-do (VTODO): start = its due date (local, 0 none) */
	int todo;
	int done;                /* crossed out: COMPLETED */
	int priority;            /* 1 (high) .. 9, 0 none */
	} IcsChange;
/* (PsiSync) a to-do from the server: start = its due (local wall clock for
   a date or a floating time, else UTC), has_due 0 = undated */
typedef struct
	{
	char uid[120];
	char summary[200];       /* cp1252 */
	long due;
	int has_due, due_date_only, due_utc;
	int priority;            /* 0..9 */
	int done;                /* STATUS:COMPLETED or a COMPLETED date */
	int cancelled;
	} IcsTodo;
int  ics_each_todo(const char *text, int len, void (*fn)(const IcsTodo *t, void *ctx), void *ctx);
/* a new VCALENDAR with one VTODO; and the change of the first VTODO of an
   object (its summary, due date, priority and done-ness; the rest kept) */
int  ics_todo_new(const IcsChange *c, int zone, const char *uid, char *out, int max);
int  ics_todo_change(char *buf, int len, int max, const IcsChange *c, int zone);
/* A new VCALENDAR (UTF-8) with one event. Returns length, -1 if too big. */
int  ics_new(const IcsChange *c, int zone, const char *uid, char *out, int max);
/* Edits a calendar object (UTF-8, in place in buf of size max): the master
   event if recurid is "", else that instance (added as an exception if the
   object has none). Returns the new length, -1 if impossible. */
int  ics_change(char *buf, int len, int max, const char *recurid, const IcsChange *c, int zone);
/* (PsiSync: an invitation's reply) sets the PARTSTAT of the ATTENDEE whose
   address is email (A, D or T) in the instance recurid ("" the master, or
   the master if that instance has no exception); RSVP is dropped.
   Returns the new length, -1 if that attendee isn't there. */
int  ics_partstat(char *buf, int len, int max, const char *recurid, const char *email, char status);
/* Removes one instance of a recurring event (EXDATE). Returns new length. */
int  ics_exclude(char *buf, int len, int max, const char *recurid, int zone);

/* ---- repeats (calrpt.c): calrpt.h, which C++ can include on its own */
#include "calrpt.h"

/* ---- time zones by name (calzone.c) */
#include "calzone.h"

/* ---- HTTP/1.1 over the engine's connection (http.c) */
typedef struct
	{
	int status;
	char etag[100];
	char location[240];
	long length;             /* body bytes received */
	} HttpResp;
typedef void (*HttpSink)(const char *data, int n, void *ctx);
/* where requests go; user/pass for Basic authentication */
void http_target(const char *host, int port, int tls, const char *user, const char *pass);
/* the User-Agent sent from now on ("PsiMail/1.05 (Psion netBook)"); without a
   call, "PsiMail (Psion Series 5mx)" as before */
void http_user_agent(const char *ua);
/* one request; the response body goes to sink (if any). Returns PM_RES_*. */
int  http_request(const char *method, const char *path, const char *headers,
                  const char *body, int blen, HttpResp *r, HttpSink sink, void *ctx,
                  char *why, int whymax);
void http_close(void);
/* an HTTP status the engine cannot use, as words to follow "The calendar
   server ..." ("is busy - try again later"); the number goes to the log
   (review R105: no bare numbers on screen) */
const char *http_status_words(int status);
/* a URL the server gave (a redirect's Location, a home collection's href):
   its path into out. If it names another host in the domain of the one set
   up (iCloud: caldav.icloud.com hands out p68-caldav.icloud.com), with the
   same scheme, requests go to that host from then on (until http_target).
   Any other host, or http:// for https://, is not followed: only the path
   is used, so a password is never sent outside the server's domain. */
void http_follow(const char *url, char *out, int max);

/* ---- CalDAV (caldav.c) */
int  cal_sync(int list_only, char *why, int whymax);

#endif
