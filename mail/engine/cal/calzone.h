/* calzone.h - caltz.c's zone for a place's IANA name ("Europe/London"),
 * -1 if none of its zones. Library-free C (calzone.c). */
#ifndef CAL_ZONE_H
#define CAL_ZONE_H
int cal_zone_by_name(const char *iana);
#endif
