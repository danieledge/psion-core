/* calport.h - what a program gives the calendar engine (see cal.h)
 *
 * The calendar engine here - CalDAV (caldav.c) over HTTP (http.c), iCalendar
 * (ics.c), time zones (caltz.c), a little XML (xmlscan.c) - was PsiMail's
 * (engine/). It is shared so that PsiMail and PsiSync (the Psion's Agenda
 * kept in step with an online calendar) run one engine, not two copies.
 *
 * The engine is plain C on ESTLIB (or a PC's C library, for the host
 * tests). A program links it with:
 *   - cp1252.c, calutil.c here (character set, string helpers);
 *   - core/mail/engine/tlsv/ (the TLS certificate check) if it uses TLS;
 *   - its own definitions of the functions marked "program" below: where
 *     the calendar is (calp_target), its folder, the network line (pmn_*),
 *     logging and progress. PsiMail: engine/pmmain.c, pmnet.c, store.c,
 *     pmepoc.cpp; PsiSync: engine/psport.c.
 *
 * Every function keeps the name it had in PsiMail, so PsiMail's own code
 * did not change when this moved.
 */
#ifndef CAL_PORT_H
#define CAL_PORT_H
#include <stdio.h>

/* results (the same numbers as PsiMail's PM_RES_*: psimail.h checks it) */
enum
	{
	CAL_RES_OK = 0,
	CAL_RES_FAILED,          /* why says why */
	CAL_RES_OFFLINE,         /* no connection: try again later */
	CAL_RES_CANCELLED,
	CAL_RES_UNTRUSTED,       /* the server's certificate is not trusted: calp_untrusted was told */
	CAL_RES_NEED_PASS,       /* no password */
	CAL_RES_LOGIN_FAILED     /* the server refused the user name or password */
	};

/* ---- where the calendar is (program) */
typedef struct
	{
	const char *host;        /* "caldav.fastmail.com" */
	int port;                /* 443 */
	int tls;
	const char *path;        /* where to look for calendars, "" = discover */
	const char *user;        /* the user name to log in with */
	const char *pass;        /* "" = none: CAL_RES_NEED_PASS */
	int zone;                /* the Psion's time zone (caltz.c) */
	int days_back;           /* events from this many days ago (< 0: 30) */
	int days_ahead;          /* to this many days ahead (<= 0: 180) */
	int todos;               /* (PsiSync) the calendars' to-dos too (0: PsiMail, events only) */
	int series;              /* (PsiSync) series the Agenda can repeat as S lines too */
	} CalTarget;
void calp_target(CalTarget *t);
/* the program's data folder, ending in the separator: the engine keeps its
   files in <dir>cal<sep> (caldav.c lists them); certs.txt goes in <dir> */
const char *calp_dir(void);
int  calp_offline(void);                   /* 1: never connect (working offline) */
/* the certificate of host:port could not be checked: what to ask the user */
void calp_untrusted(const char *hostport, const char *why, const char *fingerprint);

/* ---- the platform (program) */
long pm_time(void);                        /* seconds since 1970, UTC */
int  pm_mkdir(const char *path);           /* 0 ok or exists */
int  pm_replace(const char *tmp, const char *path); /* tmp takes path's place, in one step; 0 ok */
void pm_log(const char *fmt, ...);
void pm_progress(const char *fmt, ...);    /* "Fetching Home..." for the user */
int  pm_cancelled(void);                   /* the user stopped it */
/* "Could not save <what>: no room left on D:" */
void pm_write_why(char *why, int whymax, const char *what, const char *path);
void st_changed(void);                     /* files in calp_dir changed */
void genrandom(unsigned char *buf, unsigned int len);
#ifdef __EPOC32__
#define CAL_SEP "\\"
#else
#define CAL_SEP "/"
#endif

/* ---- the network line (program): one connection at a time */
#define PMN_TIMEOUT (-3)
#define PMN_CANCEL  (-2)
int  pmn_connect(const char *host, int port, int tls, char *why, int whymax);  /* 0 ok */
int  pmn_is_open(void);
int  pmn_conn_id(void);                    /* changes with every new connection */
int  pmn_write(const void *buf, int len);  /* 0 ok */
int  pmn_read(void *buf, int max, int timeout_ms);       /* >0, 0 closed, <0 */
int  pmn_readline(char *buf, int max, int timeout_ms);   /* length without CRLF, <0 error */
void pmn_close(int hangup);

/* ---- the certificate check (core/mail/engine/tlsv/certcheck.c) */
const char *tlsv_problem(void);            /* why the last connection's certificate was refused, "" */
const char *tlsv_fingerprint(void);
/* (program) pins.txt: 1 if the user trusted this key for host:port before */
int  st_pin_check(const char *hostport, const char *fp);

/* ---- here (calutil.c, cp1252.c) */
void pm_copy(char *dst, const char *src, int max);
int  pm_strcasecmp(const char *a, const char *b);
int  pm_strncasecmp(const char *a, const char *b, int n);
const char *pm_stristr(const char *hay, const char *needle);
int  pm_fclose(FILE *f);                   /* fclose that also checks the error flag: 0 = all written */
int  cs_utf8_to_cp1252(const char *in, int n, char *out, int max);
int  cs_cp1252_to_utf8(const char *in, int n, char *out, int max);
unsigned int cs_cp1252_to_ucs(unsigned char c);
int  cs_ucs_to_cp1252(unsigned int u);     /* -1 if none */
int  cs_put_ucs(unsigned int u, char *out, int k, int max);   /* one character at out[k]: the new k */

#endif
