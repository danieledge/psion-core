/* calmerge.c - the rules for one Agenda entry at a sync (see calmerge.h) */
#include "calmerge.h"

int cal_merge(const CmFacts *f, int options)
{
	int keep = (options & CM_OPT_KEEPBOTH) != 0;
	int force = (f->flags & CM_FORCE) != 0;
	int server_changed = f->server_changed && !(f->no_baseline && !force);
	if (f->flags & CM_PENDING)
		return CM_SKIP;                      /* sent, not confirmed yet (offline?) */
	if (f->flags & CM_DELETING)
		return f->on_server ? CM_SKIP : CM_UNLINK;   /* waiting for the server */
	if (!f->on_server) {
		/* gone, or just old: the Psion keeps it */
		if (f->gone || !f->in_window) return CM_UNLINK;
		/* changed here since: keep it (it goes to the server as new) */
		if (keep && f->local_changed) return CM_UNLINK;
		return CM_DELETE_LOCAL;
	}
	if (!f->readable)
		return CM_SKIP;                      /* a line we can't read: leave the entry be */
	if (f->gone)
		return f->writable && !force ? CM_PUSH_DELETE : CM_READD;
	if (server_changed || force || (f->local_changed && !f->writable)) {
		if (keep && server_changed && f->local_changed && f->writable && !force)
			return CM_KEEP_BOTH;
		return CM_UPDATE_LOCAL;
	}
	if (f->local_changed)
		return CM_PUSH_CHANGE;
	return CM_SKIP;
}

const char *cal_merge_name(int a)
{
	static const char *const n[] = { "skip", "unlink", "delete-local", "push-delete", "readd",
		"update-local", "push-change", "keep-both" };
	return a >= 0 && a <= CM_KEEP_BOTH ? n[a] : "?";
}
