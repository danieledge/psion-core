/* calmerge.h - what to do with one Agenda entry the calendar looks after
 * (core/mail/agenda/pmcal.cpp asks, once per entry, every sync)
 *
 * Plain C, so the rules can be tested on a PC (PsiSync's test/mergetest.c).
 * The facts about an entry go in, the action comes out; pmcal.cpp does it.
 *
 * The rules (PsiMail's since 0.6x; the CM_OPT_* ones are PsiSync's):
 *   - a change made on the Psion and not yet confirmed is left alone;
 *   - deleted on the server: deleted in the Agenda too, unless the entry is
 *     outside the window (the Psion keeps old entries) or, with
 *     CM_OPT_KEEPBOTH, the Agenda entry was changed since the last sync:
 *     then it is kept (unlinked: it goes to the server again as new);
 *   - deleted in the Agenda: deleted on the server (if the calendar can be
 *     written), else put back;
 *   - changed on the server only: the Agenda entry is updated;
 *   - changed on the Psion only: the change is sent;
 *   - changed on both: the server wins; with CM_OPT_KEEPBOTH the Psion's
 *     version is kept as a new entry beside it (and so sent as new), so
 *     neither change is lost.
 */
#ifndef CAL_MERGE_H
#define CAL_MERGE_H

/* the map entry's flags (pmcal.cpp KMap*) */
#define CM_PENDING   1       /* made on the Psion, sent, not yet confirmed */
#define CM_DELETING  2       /* deleted on the Psion, deletion sent */
#define CM_FORCE     4       /* the server refused a change: put its version back */

/* options */
#define CM_OPT_KEEPBOTH 1

typedef struct
	{
	unsigned flags;          /* CM_* */
	int on_server;           /* the event is in the server's list */
	int readable;            /* ... and its line could be read */
	int gone;                /* the Agenda entry has been deleted */
	int local_changed;       /* its last-changed date moved since the last sync */
	int in_window;           /* its start is inside the synced dates (or unknown) */
	int server_changed;      /* the server's fields differ from the last sync's */
	int no_baseline;         /* made on the Psion, just confirmed: the server's copy is ours
	                            (the caller takes its fields as the baseline unless CM_FORCE) */
	int writable;            /* the calendar can be written */
	} CmFacts;

enum
	{
	CM_SKIP = 0,             /* nothing now */
	CM_UNLINK,               /* forget the link, keep the entry */
	CM_DELETE_LOCAL,         /* delete the Agenda entry and the link */
	CM_PUSH_DELETE,          /* send the deletion (flags = CM_DELETING) */
	CM_READD,                /* make the entry again from the server's */
	CM_UPDATE_LOCAL,         /* the server's version into the Agenda */
	CM_PUSH_CHANGE,          /* send the Psion's change */
	CM_KEEP_BOTH             /* copy the Psion's version to a new entry, then CM_UPDATE_LOCAL */
	};

int cal_merge(const CmFacts *f, int options);
const char *cal_merge_name(int action);

/* CmFacts.in_window: would the server's answer have the entry, were it
   still there? The engine asks for events inside the synced dates and, with
   to-dos on, for every to-do (they have no window). An entry the answer
   could not have had is only unlinked when it is missing, never deleted. */
typedef struct
	{
	int no_start;            /* gone, or no date to judge it by */
	int todo;                /* an Agenda to-do */
	int todos;               /* to-dos are synced (KPmCalOptTodos) */
	int in_range;            /* its start is inside the synced dates */
	} CmWhen;
int cal_in_window(const CmWhen *w);

/* the mass-deletion guard, both ways (KPmCalOptGuard, and contacts): more
   than 10 of the linked entries, and more than half, would be deleted at
   once (gone from the server, or from the Psion) - then none are */
int cal_guard(int linked, int deleting);

/* A sync with the clock reset (the backup cell flat: 1 January 2002) asks
   for the wrong dates: an empty answer deletes or doubles entries. The
   clock is taken as wrong if it says a year before the program was built.
   now: seconds since 1970 (UTC). (Here, not in a .c file: the engines that
   link no calmerge.c use it too.) */
#define CAL_BUILD_YEAR ((__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 + \
	(__DATE__[9] - '0') * 10 + (__DATE__[10] - '0'))
static int cal_clock_ok(long now, int build_year)
	{
	long y = build_year - 1;
	long days = 365L * (build_year - 1970) + (y / 4 - y / 100 + y / 400) - 477;   /* 1 January, from 1970 */
	return now >= days * 86400L;
	}

#endif
