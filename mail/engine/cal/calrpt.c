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

/* ---------------------------------------------------------------- the other way */

static long days_of(int y, int m, int d)           /* Hinnant, as caltz.c */
{
	long era, yoe, doy, doe;
	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void civil_of(long z, int *y, int *m, int *d)
{
	long era, doe, yoe, doy, mp;
	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yoe + era * 400 + (*m <= 2));
}

static void put_date(char *out, int y, int m, int d)
{
	int v[3], w[3] = { 4, 2, 2 }, i, k = 0, j;
	v[0] = y; v[1] = m; v[2] = d;
	for (i = 0; i < 3; i++)
		for (j = w[i] - 1; j >= 0; j--) {
			int p = 1, q;
			for (q = 0; q < j; q++) p *= 10;
			out[k++] = (char)('0' + (v[i] / p) % 10);
		}
	out[k] = 0;
}

static int word_eq(const char *a, int n, const char *b)
{
	int i;
	for (i = 0; i < n; i++) if (!b[i] || ((a[i] | 32) != (b[i] | 32))) return 0;
	return b[n] == 0;
}

static int atoi_n(const char *s, int n, int *ok)
{
	int v = 0, i = 0, neg = 0;
	if (n > 0 && (s[0] == '-' || s[0] == '+')) { neg = s[0] == '-'; i = 1; }
	if (i >= n) { *ok = 0; return 0; }
	for (; i < n; i++) { if (s[i] < '0' || s[i] > '9') { *ok = 0; return 0; } v = v * 10 + s[i] - '0'; }
	return neg ? -v : v;
}

static int day_index(const char *s)
{
	int i;
	for (i = 0; i < 7; i++) if ((s[0] | 32) == (kDay[i][0] | 32) && (s[1] | 32) == (kDay[i][1] | 32)) return i;
	return -1;
}

int cal_rrule_parse(const char *rrule, int y, int m, int d, CalRepeat *r)
{
	const char *p = rrule;
	int freq = 0, count = 0, ok = 1, i, nbyday = 0, nmonthday = 0, bymonth = 0;
	char byday[64], bymd[64], until[20];
	byday[0] = bymd[0] = until[0] = 0;
	for (i = 0; i < (int)sizeof(*r); i++) ((char *)r)[i] = 0;
	r->interval = 1;
	r->forever = 1;
	while (*p) {
		const char *k = p, *v, *e;
		int kn, vn;
		while (*p && *p != '=' && *p != ';') p++;
		kn = (int)(p - k);
		if (*p != '=') return -1;
		v = ++p;
		while (*p && *p != ';') p++;
		e = p;
		vn = (int)(e - v);
		if (*p == ';') p++;
		if (word_eq(k, kn, "FREQ")) {
			if (word_eq(v, vn, "DAILY")) freq = CR_DAILY;
			else if (word_eq(v, vn, "WEEKLY")) freq = CR_WEEKLY;
			else if (word_eq(v, vn, "MONTHLY")) freq = CR_MONTHLY_DATES;
			else if (word_eq(v, vn, "YEARLY")) freq = CR_YEARLY_DATE;
			else return -1;
		} else if (word_eq(k, kn, "INTERVAL")) {
			r->interval = atoi_n(v, vn, &ok);
			if (!ok || r->interval < 1 || r->interval > 999) return -1;
		} else if (word_eq(k, kn, "COUNT")) {
			count = atoi_n(v, vn, &ok);
			if (!ok || count < 1) return -1;
		} else if (word_eq(k, kn, "UNTIL")) {
			if (vn < 8) return -1;
			for (i = 0; i < 8; i++) until[i] = v[i];
			until[8] = 0;
		} else if (word_eq(k, kn, "BYDAY")) {
			if (vn >= (int)sizeof(byday)) return -1;
			for (i = 0; i < vn; i++) byday[i] = v[i];
			byday[vn] = 0;
		} else if (word_eq(k, kn, "BYMONTHDAY")) {
			if (vn >= (int)sizeof(bymd)) return -1;
			for (i = 0; i < vn; i++) bymd[i] = v[i];
			bymd[vn] = 0;
		} else if (word_eq(k, kn, "BYMONTH")) {
			bymonth = atoi_n(v, vn, &ok);
			if (!ok || bymonth < 1 || bymonth > 12) return -1;
		} else if (word_eq(k, kn, "WKST")) {
			/* the week's start: the Agenda's own setting decides */
		} else return -1;                 /* BYSETPOS, BYHOUR, BYWEEKNO...: not the Agenda's */
	}
	if (!freq) return -1;
	r->type = freq;
	/* the days, the dates */
	if (byday[0]) {
		const char *q = byday;
		while (*q) {
			int ord = 0, neg = 0, di;
			if (*q == '-') { neg = 1; q++; } else if (*q == '+') q++;
			while (*q >= '0' && *q <= '9') ord = ord * 10 + *q++ - '0';
			di = day_index(q);
			if (di < 0) return -1;
			q += 2;
			if (*q == ',') q++;
			nbyday++;
			if (freq == CR_WEEKLY) {
				if (ord) return -1;
				r->weekdays |= 1 << di;
			} else if (freq == CR_MONTHLY_DATES || freq == CR_MONTHLY_DAYS) {
				int w;
				if (!ord || (neg && ord != 1) || (!neg && ord > 4)) return -1;   /* the Agenda: 1st..4th, last */
				w = neg ? 4 : ord - 1;
				r->type = CR_MONTHLY_DAYS;
				r->weeks[w] |= 1 << di;
			} else if (freq == CR_YEARLY_DATE || freq == CR_YEARLY_DAY) {
				if (!ord || nbyday > 1 || !bymonth || (neg && ord != 1) || (!neg && ord > 4)) return -1;
				r->type = CR_YEARLY_DAY;
				r->yday = di;
				r->yweek = neg ? 4 : ord - 1;
				r->ymonth = bymonth - 1;
			} else return -1;
		}
	}
	if (bymd[0]) {
		const char *q = bymd;
		if (freq != CR_MONTHLY_DATES || r->type != CR_MONTHLY_DATES) return -1;
		while (*q) {
			int v = 0;
			if (*q == '-') return -1;
			while (*q >= '0' && *q <= '9') v = v * 10 + *q++ - '0';
			if (v < 1 || v > 31) return -1;
			r->dates |= 1UL << (v - 1);
			nmonthday++;
			if (*q == ',') q++; else if (*q) return -1;
		}
	}
	if (bymonth && r->type == CR_YEARLY_DATE && bymonth != m) return -1;
	if (freq == CR_YEARLY_DATE && r->type == CR_YEARLY_DATE && r->interval != 1) return -1;   /* the Agenda: every year */
	/* the defaults from the start date */
	if (r->type == CR_WEEKLY && !r->weekdays) {
		int wd = (int)((days_of(y, m, d) % 7 + 11) % 7);   /* 0 Sunday */
		r->weekdays = 1 << ((wd + 6) % 7);                   /* bit 0 Monday */
	}
	if (r->type == CR_MONTHLY_DATES && !r->dates) { r->dates = 1UL << (d - 1); nmonthday = 1; }
	/* the end */
	if (until[0]) {
		int ok2 = 1;
		atoi_n(until, 8, &ok2);
		if (!ok2) return -1;
		for (i = 0; i < 9; i++) r->until[i] = until[i];
		r->forever = 0;
	} else if (count) {
		long day0 = days_of(y, m, d), last;
		int ly, lm, ld;
		if (r->type == CR_DAILY) last = day0 + (long)(count - 1) * r->interval;
		else if (r->type == CR_WEEKLY) {
			if (nbyday > 1) return -1;
			last = day0 + (long)(count - 1) * 7 * r->interval;
		} else if (r->type == CR_MONTHLY_DATES && nmonthday <= 1) {
			long months = (long)(count - 1) * r->interval + (m - 1);
			ly = y + (int)(months / 12); lm = (int)(months % 12) + 1; ld = d;
			last = days_of(ly, lm, ld);
		} else if (r->type == CR_YEARLY_DATE) last = days_of(y + count - 1, m, d);
		else return -1;
		civil_of(last, &ly, &lm, &ld);
		put_date(r->until, ly, lm, ld);
		r->forever = 0;
	}
	return 0;
}
