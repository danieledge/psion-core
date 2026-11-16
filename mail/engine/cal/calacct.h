/* calacct.h - PsiMail's account and calendar settings, as laid out in its
 * shared chunk (psimail.h) and its settings files:
 *   C:\System\Apps\PsiMail\PsiMail.ini    TPmSettings, which ends in PmAccount[4]
 *   C:\System\Apps\PsiMail\Calendar.ini   TPmCalSettings (core/mail/agenda/pmcal.h),
 *                                          which starts with a PmCalendar
 * Here so that PsiSync can use PsiMail's calendar account (read only: the
 * password stays in PsiMail's file, never copied). These are saved whole:
 * never change their size or order.
 */
#ifndef CAL_ACCT_H
#define CAL_ACCT_H

typedef struct
	{
	int used;
	char name[32];               /* shown in the app, e.g. "Fastmail" */
	char fullname[64];           /* "Dan Edge" */
	char email[96];              /* From: address */
	char imap_host[64];
	int imap_port;               /* 993 */
	int imap_tls;
	char smtp_host[64];
	int smtp_port;               /* 465 */
	int smtp_tls;
	char user[96];               /* usually the full address */
	char pass[64];               /* app password; empty = ask (need_pass) */
	int sync_count;              /* newest N messages kept per folder (50) */
	int max_body_kb;             /* text downloaded per message (64) */
	int save_sent;               /* APPEND a copy to Sent after sending */
	char signature[160];         /* added to new messages ("\n" = new line) */
	} PmAccount;

/* the calendar: CalDAV (Fastmail: caldav.fastmail.com), kept in step with
   the Psion's Agenda by the app (engine/caldav.c has the files) */
typedef struct
	{
	int enabled;
	int acct;                    /* whose user name and password, unless below */
	char host[64];               /* "caldav.fastmail.com" */
	int port;                    /* 443 */
	int plain;                   /* tests only: no TLS */
	char path[128];              /* where to look for calendars: "" = discover */
	char user[96];               /* "" = the account's */
	char pass[64];               /* "" = the account's */
	int zone;                    /* the Psion's time zone (engine/caltz.c) */
	int days_back;               /* events from this many days ago (30) */
	int days_ahead;              /* to this many days ahead (180) */
	} PmCalendar;

#endif
