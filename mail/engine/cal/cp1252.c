/* cp1252.c - UTF-8 and Windows-1252, the Psion's own character set
 * (moved from PsiMail's engine/charset.c, which keeps the rest: the other
 * character sets, RFC 2047 headers, IMAP's modified UTF-7)
 *
 * EIKON on EPOC R5 is 8-bit and uses Windows-1252, so the calendar engine
 * (and PsiMail) convert what servers send (UTF-8) to it here; characters
 * with no cp1252 form become a close ASCII stand-in or '?'.
 */
#include <string.h>
#include "calport.h"

static const unsigned short k1252[32] = {
	0x20ac, 0x81, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
	0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8d, 0x017d, 0x8f,
	0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
	0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x9d, 0x017e, 0x0178
};

unsigned int cs_cp1252_to_ucs(unsigned char c)
{
	if (c >= 0x80 && c < 0xa0) return k1252[c - 0x80];
	return c;
}

int cs_ucs_to_cp1252(unsigned int u)
{
	int i;
	if (u < 0x80 || (u >= 0xa0 && u <= 0xff)) return (int)u;
	for (i = 0; i < 32; i++)
		if (k1252[i] == u) return 0x80 + i;
	return -1;
}

/* a close ASCII stand-in for common characters cp1252 lacks */
static const char *translit(unsigned int u)
{
	switch (u) {
	case 0x2010: case 0x2011: case 0x2012: case 0x2212: return "-";
	case 0x2015: return "--";
	case 0x2032: return "'";
	case 0x2033: return "\"";
	case 0x2043: return "-";
	case 0x2192: return "->";
	case 0x2190: return "<-";
	case 0x2264: return "<=";
	case 0x2265: return ">=";
	case 0x2260: return "!=";
	case 0x2248: return "~";
	case 0x2713: case 0x2714: return "v";
	case 0x2717: case 0x2718: return "x";
	case 0x25cf: case 0x25aa: case 0x25a0: case 0x2023: return "*";
	case 0x00a0: return " ";
	case 0x2002: case 0x2003: case 0x2009: case 0x200a: case 0x202f: case 0x2007: return " ";
	case 0x200b: case 0x200c: case 0x200d: case 0x2060: case 0xfeff: case 0xfe0f: case 0x034f: return "";
	case 0x2028: case 0x2029: return "\n";
	case 0x0131: return "i";
	case 0x0141: return "L";
	case 0x0142: return "l";
	case 0x0107: case 0x010d: return "c";
	case 0x0106: case 0x010c: return "C";
	case 0x0119: case 0x011b: return "e";
	case 0x0144: case 0x0148: return "n";
	case 0x015b: return "s";
	case 0x017a: case 0x017c: return "z";
	case 0x0159: return "r";
	case 0x0103: case 0x0105: return "a";
	case 0x0219: case 0x015f: return "s";
	case 0x021b: case 0x0163: return "t";
	}
	if (u >= 0x1f000 && u < 0x20000) return "";   /* emoji and friends: drop */
	if (u >= 0x2600 && u < 0x2800) return "";     /* symbols, dingbats */
	return 0;
}

int cs_put_ucs(unsigned int u, char *out, int k, int max)
{
	int c = cs_ucs_to_cp1252(u);
	if (c >= 0) {
		if (k < max - 1) out[k++] = (char)c;
		return k;
	} else {
		const char *t = translit(u);
		if (!t) t = "?";
		while (*t && k < max - 1) out[k++] = *t++;
		return k;
	}
}

int cs_utf8_to_cp1252(const char *in, int n, char *out, int max)
{
	const unsigned char *s = (const unsigned char *)in;
	int i = 0, k = 0;
	if (max <= 0) return 0;
	while (i < n && k < max - 1) {
		unsigned int c = s[i++];
		if (c >= 0x80) {
			int extra;
			if (c >= 0xf0 && c < 0xf8) { extra = 3; c &= 0x07; }
			else if (c >= 0xe0) { extra = 2; c &= 0x0f; }
			else if (c >= 0xc0) { extra = 1; c &= 0x1f; }
			else { k = cs_put_ucs(c, out, k, max); continue; }   /* stray byte: treat as Latin-1 */
			while (extra > 0 && i < n && (s[i] & 0xc0) == 0x80) { c = (c << 6) | (s[i++] & 0x3f); extra--; }
			if (extra) c = '?';
		}
		k = cs_put_ucs(c, out, k, max);
	}
	out[k] = 0;
	return k;
}

int cs_cp1252_to_utf8(const char *in, int n, char *out, int max)
{
	const unsigned char *s = (const unsigned char *)in;
	int i, k = 0;
	if (max <= 0) return 0;
	for (i = 0; i < n; i++) {
		unsigned int u = cs_cp1252_to_ucs(s[i]);
		if (u < 0x80) { if (k + 1 >= max) break; out[k++] = (char)u; }
		else if (u < 0x800) { if (k + 2 >= max) break; out[k++] = (char)(0xc0 | (u >> 6)); out[k++] = (char)(0x80 | (u & 0x3f)); }
		else { if (k + 3 >= max) break; out[k++] = (char)(0xe0 | (u >> 12)); out[k++] = (char)(0x80 | ((u >> 6) & 0x3f)); out[k++] = (char)(0x80 | (u & 0x3f)); }
	}
	out[k] = 0;
	return k;
}
