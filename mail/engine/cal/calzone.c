/* calzone.c - caltz.c's zones by a place's IANA name (calzone.h). No C
 * library: the Control panel item (an EIKON DLL) links it too. */
#include "calzone.h"

/* (PsiSync) the zone for a place's IANA name, as the netBook's Internet time
   finds it ("Europe/London"), -1 if none of ours: the list's own places
   and the common ones that keep the same rules */
static const struct { const char *name; signed char zone; } kNames[] =
	{
	{ "UTC", 0 }, { "Etc/UTC", 0 }, { "GMT", 0 },
	{ "Europe/London", 1 }, { "Europe/Dublin", 1 }, { "Europe/Lisbon", 1 }, { "Atlantic/Canary", 1 },
	{ "Europe/Paris", 2 }, { "Europe/Berlin", 2 }, { "Europe/Rome", 2 }, { "Europe/Madrid", 2 },
	{ "Europe/Amsterdam", 2 }, { "Europe/Brussels", 2 }, { "Europe/Vienna", 2 }, { "Europe/Zurich", 2 },
	{ "Europe/Stockholm", 2 }, { "Europe/Oslo", 2 }, { "Europe/Copenhagen", 2 }, { "Europe/Prague", 2 },
	{ "Europe/Warsaw", 2 }, { "Europe/Budapest", 2 }, { "Europe/Luxembourg", 2 }, { "Europe/Belgrade", 2 },
	{ "Europe/Athens", 3 }, { "Europe/Helsinki", 3 }, { "Europe/Kiev", 3 }, { "Europe/Kyiv", 3 },
	{ "Europe/Bucharest", 3 }, { "Europe/Sofia", 3 }, { "Europe/Riga", 3 }, { "Europe/Tallinn", 3 }, { "Europe/Vilnius", 3 },
	{ "Europe/Moscow", 4 }, { "Europe/Istanbul", 4 }, { "Asia/Istanbul", 4 },
	{ "Asia/Dubai", 5 }, { "Asia/Kolkata", 6 }, { "Asia/Calcutta", 6 },
	{ "Asia/Singapore", 7 }, { "Asia/Hong_Kong", 7 }, { "Asia/Shanghai", 7 }, { "Asia/Taipei", 7 }, { "Australia/Perth", 7 },
	{ "Asia/Tokyo", 8 }, { "Asia/Seoul", 8 }, { "Australia/Brisbane", 9 },
	{ "Australia/Sydney", 10 }, { "Australia/Melbourne", 10 }, { "Australia/Canberra", 10 }, { "Australia/Hobart", 10 },
	{ "Pacific/Auckland", 11 }, { "Pacific/Honolulu", 12 }, { "America/Anchorage", 13 },
	{ "America/Los_Angeles", 14 }, { "America/Vancouver", 14 }, { "America/Phoenix", 15 },
	{ "America/Denver", 16 }, { "America/Edmonton", 16 }, { "America/Chicago", 17 }, { "America/Winnipeg", 17 },
	{ "America/New_York", 18 }, { "America/Toronto", 18 }, { "America/Detroit", 18 }, { "America/Halifax", 19 },
	{ "America/Sao_Paulo", 20 }, { "America/Argentina/Buenos_Aires", 20 }, { "America/Buenos_Aires", 20 }
	};

int cal_zone_by_name(const char *iana)
{
	int i, k;
	if (!iana) return -1;
	for (i = 0; i < (int)(sizeof(kNames) / sizeof(kNames[0])); i++) {
		const char *n = kNames[i].name;
		for (k = 0; n[k] && n[k] == iana[k]; k++) ;
		if (!n[k] && !iana[k]) return kNames[i].zone;
	}
	return -1;
}

