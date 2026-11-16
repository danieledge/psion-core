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

#endif
