/* calrpt.h - an Agenda repeat as an RRULE (calrpt.c); plain C with no
 * other headers and no C library, so the Agenda half (C++, in PsiMail.app
 * and psisync.exe) includes and links it on its own */
#ifndef CAL_RPT_H
#define CAL_RPT_H

/* tested on a PC: PsiSync's test/rpttest.c */
enum { CR_DAILY = 1, CR_WEEKLY, CR_MONTHLY_DATES, CR_MONTHLY_DAYS, CR_YEARLY_DATE, CR_YEARLY_DAY };
typedef struct
	{
	int type;                /* CR_* (the Agenda's CAgnRptDef::TType) */
	int interval;            /* every n days, weeks, months (1 for yearly) */
	int forever;
	char until[20];          /* the last day, if not forever: "20271231" (all day) or
	                            the end of that day in UTC, "20271231T235959Z" */
	int weekdays;            /* CR_WEEKLY: bit 0 Monday .. bit 6 Sunday */
	unsigned long dates;     /* CR_MONTHLY_DATES: bit 0 the 1st .. bit 30 the 31st */
	int weeks[5];            /* CR_MONTHLY_DAYS: weekday bits for the 1st..4th and last week */
	int yday, yweek, ymonth; /* CR_YEARLY_DAY: weekday (0 Monday), week (0..4, 4 last), month (0..11) */
	} CalRepeat;
/* the RRULE's value ("FREQ=..."), its length, or -1 if the Agenda's repeat
   can't be said as one. No C library: PsiMail.app links it too. */
int  cal_rrule(const CalRepeat *r, char *out, int max);

#endif
