/* calutil.c - small string and file helpers of the calendar engine
 * (calport.h). They were PsiMail's (engine/pmmain.c), which uses them
 * everywhere and now links them from here.
 */
#include <stdio.h>
#include <string.h>
#include "calport.h"

void pm_copy(char *dst, const char *src, int max)
{
	int i = 0;
	if (max <= 0) return;
	if (src) while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
	dst[i] = 0;
}

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int pm_strcasecmp(const char *a, const char *b)
{
	while (*a && lc((unsigned char)*a) == lc((unsigned char)*b)) { a++; b++; }
	return lc((unsigned char)*a) - lc((unsigned char)*b);
}

int pm_strncasecmp(const char *a, const char *b, int n)
{
	while (n > 0 && *a && lc((unsigned char)*a) == lc((unsigned char)*b)) { a++; b++; n--; }
	if (n == 0) return 0;
	return lc((unsigned char)*a) - lc((unsigned char)*b);
}

const char *pm_stristr(const char *hay, const char *needle)
{
	int n = (int)strlen(needle);
	if (!n) return hay;
	for (; *hay; hay++) if (!pm_strncasecmp(hay, needle, n)) return hay;
	return 0;
}

/* fclose() alone can say 0 after a write failed earlier (the failed flush
   set the error flag; the final one had nothing left to write) */
int pm_fclose(FILE *f)
{
	int bad = ferror(f) != 0;
	if (fclose(f) != 0) bad = 1;
	return bad ? -1 : 0;
}
