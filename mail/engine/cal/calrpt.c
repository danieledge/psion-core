/* calrpt.c - an Agenda repeat as an iCalendar RRULE (see calrpt.h)
 *
 * The Agenda's repeats (CAgnRptDef): every n days; every n weeks on some
 * weekdays; every n months on some dates, or on some weekdays of the 1st to
 * 4th or last week; every year on a date, or on a weekday of a week of a
 * month. Each is one RRULE. The Agenda's exceptions become EXDATEs (ics.c).
 * No C library here: PsiMail.app, an EIKON DLL, links it too.
 */
#include "calrpt.h"

static const char *const kDay[7] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };

typedef struct { char *b; int k, max, bad; } Buf;

static void put(Buf *o, const char *s)
{
	while (*s) {
		if (o->k + 1 >= o->max) { o->bad = 1; return; }
		o->b[o->k++] = *s++;
	}
	o->b[o->k] = 0;
}

static void num(Buf *o, int v)
{
	char t[12];
	int n = 0;
	if (v < 0) { put(o, "-"); v = -v; }
	do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
	while (n) { char c[2]; c[0] = t[--n]; c[1] = 0; put(o, c); }
}

int cal_rrule(const CalRepeat *r, char *out, int max)
{
	Buf o;
	int i, w, first, any = 0, interval = r->interval > 0 ? r->interval : 1;
	if (max < 2) return -1;
	o.b = out; o.k = 0; o.max = max; o.bad = 0;
	out[0] = 0;
	switch (r->type) {
	case CR_DAILY: put(&o, "FREQ=DAILY"); break;
	case CR_WEEKLY:
		if (!(r->weekdays & 0x7f)) return -1;
		put(&o, "FREQ=WEEKLY");
		break;
	case CR_MONTHLY_DATES:
		if (!(r->dates & 0x7fffffffUL)) return -1;
		put(&o, "FREQ=MONTHLY");
		break;
	case CR_MONTHLY_DAYS:
		for (w = 0; w < 5; w++) any |= r->weeks[w] & 0x7f;
		if (!any) return -1;
		put(&o, "FREQ=MONTHLY");
		break;
	case CR_YEARLY_DATE: put(&o, "FREQ=YEARLY"); break;
	case CR_YEARLY_DAY:
		if (r->yday < 0 || r->yday > 6 || r->yweek < 0 || r->yweek > 4 || r->ymonth < 0 || r->ymonth > 11) return -1;
		put(&o, "FREQ=YEARLY");
		break;
	default: return -1;
	}
	if (interval > 1 && r->type != CR_YEARLY_DATE && r->type != CR_YEARLY_DAY) {
		put(&o, ";INTERVAL=");
		num(&o, interval);
	}
	if (r->type == CR_WEEKLY) {
		put(&o, ";BYDAY=");
		for (i = 0, first = 1; i < 7; i++)
			if (r->weekdays & (1 << i)) {
				if (!first) put(&o, ",");
				put(&o, kDay[i]);
				first = 0;
			}
	} else if (r->type == CR_MONTHLY_DATES) {
		put(&o, ";BYMONTHDAY=");
		for (i = 0, first = 1; i < 31; i++)
			if (r->dates & (1UL << i)) {
				if (!first) put(&o, ",");
				num(&o, i + 1);
				first = 0;
			}
	} else if (r->type == CR_MONTHLY_DAYS) {
		put(&o, ";BYDAY=");
		for (w = 0, first = 1; w < 5; w++)
			for (i = 0; i < 7; i++)
				if (r->weeks[w] & (1 << i)) {
					if (!first) put(&o, ",");
					num(&o, w == 4 ? -1 : w + 1);
					put(&o, kDay[i]);
					first = 0;
				}
	} else if (r->type == CR_YEARLY_DAY) {
		put(&o, ";BYMONTH=");
		num(&o, r->ymonth + 1);
		put(&o, ";BYDAY=");
		num(&o, r->yweek == 4 ? -1 : r->yweek + 1);
		put(&o, kDay[r->yday]);
	}
	if (!r->forever) {
		int n = 0;
		while (n < (int)sizeof(r->until) && r->until[n]) n++;
		if (n != 8 && n != 16) return -1;     /* 20271231 or 20271231T235959Z */
		put(&o, ";UNTIL=");
		put(&o, r->until);
	}
	return o.bad ? -1 : o.k;
}
