/* vcard.c - vCards between the server (3.0, 4.0) and the Psion's Contacts
 * (2.1, Windows-1252): see card.h. All input is untrusted: bounded copies.
 */
#include <stdio.h>
#include <string.h>
#include "calport.h"
#include "cal.h"
#include "card.h"

#define VLINE 1200
#define MAXP 64

/* the kinds the Psion holds */
static const char *const kKinds[] = { "N", "FN", "TEL", "EMAIL", "ADR", "ORG", "TITLE", "NOTE", "URL", "BDAY" };
#define NKINDS ((int)(sizeof(kKinds) / sizeof(kKinds[0])))

typedef struct { char *b; int n, max, bad; } Buf;

static void put(Buf *o, const char *s, int n)
{
	if (n < 0) n = (int)strlen(s);
	if (o->n + n >= o->max) { o->bad = 1; return; }
	memcpy(o->b + o->n, s, n);
	o->n += n;
	o->b[o->n] = 0;
}

/* a property: its group ("item1"), name, parameters and value */
typedef struct
	{
	char group[24];
	char name[32];
	char params[200];
	const char *value;
	} Prop;

static void parse(const char *line, Prop *p)
{
	char full[64];
	const char *dot;
	p->value = ics_prop(line, full, sizeof(full), p->params, sizeof(p->params));
	dot = strchr(full, '.');
	if (dot) {
		int g = (int)(dot - full);
		if (g >= (int)sizeof(p->group)) g = sizeof(p->group) - 1;
		memcpy(p->group, full, g); p->group[g] = 0;
		pm_copy(p->name, dot + 1, sizeof(p->name));
	} else { p->group[0] = 0; pm_copy(p->name, full, sizeof(p->name)); }
}

static int kind_of(const char *name)
{
	int i;
	for (i = 0; i < NKINDS; i++) if (!strcmp(name, kKinds[i])) return i;
	return -1;
}

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* the types a property has, as bits: from TYPE=a,b;TYPE=c (3.0, 4.0) or
   bare 2.1 parameters (HOME;CELL) */
enum { T_HOME = 1, T_WORK = 2, T_CELL = 4, T_FAX = 8, T_PAGER = 16, T_VOICE = 32, T_PREF = 64, T_INTERNET = 128 };
static const struct { const char *w; int bit; } kTypes[] = {
	{ "home", T_HOME }, { "work", T_WORK }, { "cell", T_CELL }, { "mobile", T_CELL }, { "iphone", T_CELL },
	{ "fax", T_FAX }, { "pager", T_PAGER }, { "voice", T_VOICE }, { "pref", T_PREF }, { "internet", T_INTERNET } };

static int types(const char *params)
{
	int bits = 0, i;
	const char *p = params;
	while (*p) {
		char w[24];
		int n = 0;
		while (*p == ';' || *p == ',' || *p == ' ') p++;
		/* skip "TYPE=" */
		if (!pm_strncasecmp(p, "TYPE=", 5)) p += 5;
		while (*p && *p != ';' && *p != ',' && n < (int)sizeof(w) - 1) w[n++] = (char)lc((unsigned char)*p++);
		w[n] = 0;
		while (*p && *p != ';' && *p != ',') p++;
		if (w[0] == '"') memmove(w, w + 1, strlen(w));
		if (w[0] && w[strlen(w) - 1] == '"') w[strlen(w) - 1] = 0;    /* (w may be empty now: it was a lone quote) */
		if (!strncmp(w, "pref=", 5)) { bits |= T_PREF; continue; }    /* 4.0 PREF=1 */
		for (i = 0; i < (int)(sizeof(kTypes) / sizeof(kTypes[0])); i++)
			if (!strcmp(w, kTypes[i].w)) bits |= kTypes[i].bit;
	}
	return bits;
}

/* quoted-printable (2.1) to bytes; soft line breaks are gone already
   (ics_line unfolds; a QP value's "=" at a line's end is joined below) */
static void qp_decode(const char *in, char *out, int max)
{
	int n = 0;
	while (*in && n < max - 1) {
		if (in[0] == '=' && in[1] && in[2]) {
			int h = 0, k;
			for (k = 1; k <= 2; k++) {
				char c = in[k];
				h = h * 16 + (c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0);
			}
			out[n++] = (char)h;
			in += 3;
		} else out[n++] = *in++;
	}
	out[n] = 0;
}

/* a TEXT component: unescape \, \; \\ \n; stop at an unescaped ';' (when
   split) - returns where the next component starts, or 0 at the end */
static const char *component(const char *v, char *out, int max, int split)
{
	int n = 0;
	while (*v) {
		if (*v == '\\' && v[1]) {
			char c = v[1];
			if (c == 'n' || c == 'N') c = '\n';
			if (n < max - 1) out[n++] = c;
			v += 2;
			continue;
		}
		if (split && *v == ';') { out[n] = 0; return v + 1; }
		if (n < max - 1) out[n++] = *v;
		v++;
	}
	out[n] = 0;
	return 0;
}

/* ---------------------------------------------------------------- to the Psion */

/* is s all well-formed UTF-8 (plain ASCII is)? */
static int valid_utf8(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;
	while (*p) {
		int k;
		if (*p < 0x80) { p++; continue; }
		if (*p >= 0xc2 && *p < 0xe0) k = 1;
		else if (*p >= 0xe0 && *p < 0xf0) k = 2;
		else if (*p >= 0xf0 && *p < 0xf5) k = 3;
		else return 0;
		p++;
		while (k--) { if ((*p & 0xc0) != 0x80) return 0; p++; }
	}
	return 1;
}

/* a 2.1 value in cp1252: quoted-printable if it has 8-bit text or line ends */
static void put21(Buf *o, const char *name, const char *params, const char *cp1252)
{
	int i, qp = 0;
	char l[VLINE];
	int n;
	for (i = 0; cp1252[i]; i++) if ((unsigned char)cp1252[i] >= 0x80 || cp1252[i] == '\n' || cp1252[i] == '\r' || cp1252[i] == '=') qp = 1;
	n = snprintf(l, sizeof(l), "%s%s%s", name, params[0] ? ";" : "", params);
	if (qp) n += snprintf(l + n, sizeof(l) - n, ";CHARSET=ISO-8859-1;ENCODING=QUOTED-PRINTABLE");
	n += snprintf(l + n, sizeof(l) - n, ":");
	for (i = 0; cp1252[i] && n < (int)sizeof(l) - 8; i++) {
		unsigned char c = (unsigned char)cp1252[i];
		if (c == '\r') continue;                     /* (a line end is CR LF: written for the LF) */
		if (qp && c == '\n') n += snprintf(l + n, sizeof(l) - n, "=0D=0A");
		else if (qp && (c >= 0x80 || c == '=')) n += snprintf(l + n, sizeof(l) - n, "=%02X", c);
		else l[n++] = (char)c;
	}
	l[n] = 0;
	put(o, l, n);
	put(o, "\r\n", 2);
}

/* one value (all its components) to cp1252, the components joined by ';'
   with a ';' inside one escaped as 2.1 has it */
static void value21(const Prop *p, int is21, int qp, char *out, int max, int split)
{
	char raw[VLINE], comp[VLINE], cp[VLINE];
	const char *v = p->value;
	int n = 0;
	if (qp) { qp_decode(v, raw, sizeof(raw)); v = raw; }
	out[0] = 0;
	for (;;) {
		const char *next = component(v, comp, sizeof(comp), split);
		int k;
		/* UTF-8 (3.0, 4.0, and 2.1 as servers keep it), unless it isn't: a
		   2.1 card from Contacts or Outlook is in Windows-1252 already */
		if (is21 && !valid_utf8(comp)) pm_copy(cp, comp, sizeof(cp));
		else cs_utf8_to_cp1252(comp, (int)strlen(comp), cp, sizeof(cp));
		for (k = 0; cp[k] && n < max - 2; k++) {
			if (cp[k] == ';') out[n++] = '\\';
			out[n++] = cp[k];
		}
		if (!next) break;
		if (n < max - 1) out[n++] = ';';
		v = next;
	}
	out[n] = 0;
}

static void params21(int kind, int t, char *out, int max)
{
	out[0] = 0;
	if (kind == 3) pm_copy(out, "INTERNET", max);        /* EMAIL */
#define ADD(bit, s) if (t & bit) { if (out[0]) strncat(out, ";", max - strlen(out) - 1); strncat(out, s, max - strlen(out) - 1); }
	ADD(T_HOME, "HOME") ADD(T_WORK, "WORK")
	if (kind == 2) { ADD(T_CELL, "CELL") ADD(T_FAX, "FAX") ADD(T_PAGER, "PAGER") ADD(T_VOICE, "VOICE") }
	ADD(T_PREF, "PREF")
#undef ADD
}

/* a property of the Psion's card, ready to compare and write */
typedef struct
	{
	int kind;
	int types;
	char value[VLINE];       /* cp1252, components joined by ';' (a ';' in one escaped) */
	} PsProp;

/* What the functions here work in: 194 KB, which were statics (the stack
   is too small for them) and so part of psisync.exe's RAM all day. Now work
   memory (psiwork.h): taken at the first use, given back by vc_work(0)
   when the sync or the import is over. */
#include "../../../ssh/psiwork.h"
typedef struct
	{
	char tp_line[VLINE], tp_val[VLINE], tp_pr[80], tp_fn[VLINE];   /* vc_to_psion */
	char hash_tmp[16384];                                          /* vc_psion_hash */
	char rp_line[VLINE];                                           /* read_psion */
	char sk_tmp[16384];                                            /* same_kind */
	PsProp sk_mine[MAXP];
	PsProp mg_ps[MAXP];                                            /* vc_merge */
	char mg_line[VLINE];
	PsProp mg_f;
	} VcWork;
static VcWork *g_vw;

int vc_work(int on)
{
	if (on) {
		if (!g_vw) g_vw = (VcWork *)psi_work_alloc(sizeof(VcWork));
		return g_vw ? 0 : -1;
	}
	if (g_vw) { psi_work_free(g_vw); g_vw = 0; }
	return 0;
}

#define line (g_vw->tp_line)
#define val  (g_vw->tp_val)
#define pr   (g_vw->tp_pr)
#define fn   (g_vw->tp_fn)
int vc_to_psion(const char *in, int len, char *out, int max)
{
	Buf o;
	int pos = 0, n, in_card = 0, is21 = 0, got = 0;
	Prop p;
	int have_n = 0;
	o.b = out; o.n = 0; o.max = max; o.bad = 0;
	if (max < 2) return -1;
	out[0] = 0;
	if (vc_work(1) != 0) return -1;
	fn[0] = 0;
	while ((n = ics_line(in, len, &pos, line, sizeof(line))) >= 0) {
		int kind, qp;
		if (!n) continue;
		/* a 2.1 quoted-printable value goes on over lines ending in "=" */
		while (n > 0 && line[n - 1] == '=' && pm_stristr(line, "QUOTED-PRINTABLE")) {
			char more[VLINE];
			int m = ics_line(in, len, &pos, more, sizeof(more));
			if (m < 0) break;
			line[--n] = 0;
			if (n + m >= (int)sizeof(line)) break;
			memcpy(line + n, more, m + 1);
			n += m;
		}
		parse(line, &p);
		if (!strcmp(p.name, "BEGIN") && !pm_strcasecmp(p.value, "VCARD")) {
			in_card = 1;
			put(&o, "BEGIN:VCARD\r\nVERSION:2.1\r\n", -1);
			continue;
		}
		if (!in_card) continue;
		if (!strcmp(p.name, "END")) {
			/* a card with only FN: the Psion has its name as the given name */
			if (!have_n && fn[0]) {
				char n21[VLINE + 2];
				snprintf(n21, sizeof(n21), ";%s", fn);
				put21(&o, "N", "", n21);
			}
			put(&o, "END:VCARD\r\n", -1);
			got = 1;
			break;
		}
		if (!strcmp(p.name, "VERSION")) { is21 = !strcmp(p.value, "2.1"); continue; }
		kind = kind_of(p.name);
		if (kind < 0) continue;
		qp = pm_stristr(p.params, "QUOTED-PRINTABLE") != 0;
		if (kind == 2 && !pm_strncasecmp(p.value, "tel:", 4)) p.value += 4;      /* 4.0 tel: URI */
		if (kind == 9) {
			/* BDAY: 1985-04-12, 19850412, 1985-04-12T00:00:00Z; no year (--0412): not for the Psion */
			char d[12];
			int k = 0, i;
			for (i = 0; p.value[i] && p.value[i] != 'T' && k < 8; i++)
				if (p.value[i] >= '0' && p.value[i] <= '9') d[k++] = p.value[i];
			d[k] = 0;
			if (k != 8 || p.value[0] == '-') continue;
			put21(&o, "BDAY", "", d);
			continue;
		}
		value21(&p, is21, qp, val, sizeof(val), kind == 0 || kind == 4 || kind == 5);
		if (!val[0]) continue;
		/* FN is not Contacts': it shows the name from N (vc_merge makes FN
		   again from N when the name changes) */
		if (kind == 1) { pm_copy(fn, val, sizeof(fn)); continue; }
		if (kind == 8) {
			/* a web address is a URI, but Google writes "https\\://" and a server
			   that takes it as text escapes the backslash again: no backslash
			   before a ':' or ',' belongs in one */
			int r = 0, w = 0;
			for (; val[r]; r++)
				if (!(val[r] == '\\' && (val[r + 1] == ':' || val[r + 1] == ',' || val[r + 1] == '\\'))) val[w++] = val[r];
			val[w] = 0;
		}
		if (kind == 0) have_n = 1;
		params21(kind, types(p.params), pr, sizeof(pr));
		put21(&o, kKinds[kind], pr, val);
	}
	if (!got || o.bad) return -1;
	return o.n;
}

#undef line
#undef val
#undef pr
#undef fn

#define tmp (g_vw->hash_tmp)
unsigned long vc_psion_hash(const char *in, int len)
{
	unsigned long h = 2166136261UL;
	int n, i;
	if (vc_work(1) != 0) return 0;
	n = vc_to_psion(in, len, tmp, sizeof(tmp));
	if (n < 0) return 0;
	for (i = 0; i < n; i++) {
		if (tmp[i] == '\r' || tmp[i] == '\n') continue;
		h ^= (unsigned char)tmp[i];
		h = (h * 16777619UL) & 0xffffffffUL;
	}
	return h;
}
#undef tmp

/* ---------------------------------------------------------------- back to the server */

/* the Psion's 2.1 card read (QP decoded, cp1252) */
#define line (g_vw->rp_line)
static int read_psion(const char *in, int len, PsProp *out, int max)
{
	int pos = 0, n, k = 0;
	Prop p;
	while ((n = ics_line(in, len, &pos, line, sizeof(line))) >= 0 && k < max) {
		int kind;
		/* a QP value may go on over lines ending in "=" */
		while (n > 0 && line[n - 1] == '=' && pm_stristr(line, "QUOTED-PRINTABLE")) {
			char more[VLINE];
			int m = ics_line(in, len, &pos, more, sizeof(more));
			if (m < 0) break;
			line[--n] = 0;
			if (n + m >= (int)sizeof(line)) break;
			memcpy(line + n, more, m + 1);
			n += m;
		}
		parse(line, &p);
		kind = kind_of(p.name);
		if (kind < 0) continue;
		out[k].kind = kind;
		out[k].types = types(p.params) & ~T_INTERNET;
		value21(&p, 1, pm_stristr(p.params, "QUOTED-PRINTABLE") != 0, out[k].value, sizeof(out[k].value),
			kind == 0 || kind == 4 || kind == 5);
		if (out[k].value[0]) k++;
	}
	return k;
}
#undef line

/* cp1252 component text -> a 3.0/4.0 TEXT value in UTF-8 (escaped) */
static void text30(const char *cp, int split, char *out, int max)
{
	char u[VLINE * 3];
	int n = 0, i;
	cs_cp1252_to_utf8(cp, (int)strlen(cp), u, sizeof(u));
	for (i = 0; u[i] && n < max - 3; i++) {
		char c = u[i];
		if (c == '\\' && u[i + 1] == ';' && split) { out[n++] = '\\'; out[n++] = ';'; i++; continue; }
		if (c == ';' && split) { out[n++] = ';'; continue; }       /* a component break */
		if (c == ';' || c == ',' || c == '\\') { out[n++] = '\\'; out[n++] = c; continue; }
		if (c == '\n') { out[n++] = '\\'; out[n++] = 'n'; continue; }
		if (c == '\r') continue;
		out[n++] = c;
	}
	out[n] = 0;
}

static void put_line(Buf *o, const char *s)
{
	/* folded at 75 octets, as ics.c does */
	int col = 0, i = 0, n = (int)strlen(s);
	while (i < n) {
		int k = 1;
		unsigned char c = (unsigned char)s[i];
		if (c >= 0xc0) while (i + k < n && ((unsigned char)s[i + k] & 0xc0) == 0x80) k++;
		if (col + k > 75) { put(o, "\r\n ", 3); col = 1; }
		put(o, s + i, k);
		col += k;
		i += k;
	}
	put(o, "\r\n", 2);
}

/* a line of the server's card that stays. One longer than a line here holds
   (a photo, a key, a long note) is written as the server had it, byte for
   byte: read into line[] it was cut at VLINE and the rest was lost */
static void keep_line(Buf *o, const char *line, int n, const char *raw, int rawlen)
{
	if (n < VLINE - 1) { put_line(o, line); return; }
	while (rawlen > 0 && (raw[rawlen - 1] == '\r' || raw[rawlen - 1] == '\n')) rawlen--;
	put(o, raw, rawlen);
	put(o, "\r\n", 2);
}

static void type_param(int t, int v4, int kind, char *out, int max)
{
	static const struct { int bit; const char *w; } k[] = {
		{ T_HOME, "home" }, { T_WORK, "work" }, { T_CELL, "cell" }, { T_FAX, "fax" }, { T_PAGER, "pager" }, { T_VOICE, "voice" } };
	int i, first = 1;
	out[0] = 0;
	for (i = 0; i < 6; i++) {
		char w[8];
		int j;
		if (!(t & k[i].bit) || (kind != 2 && k[i].bit > T_WORK)) continue;
		pm_copy(w, k[i].w, sizeof(w));
		if (!v4) for (j = 0; w[j]; j++) w[j] = (char)(w[j] - 32);
		strncat(out, first ? ";TYPE=" : ",", max - strlen(out) - 1);
		strncat(out, w, max - strlen(out) - 1);
		first = 0;
	}
	if (t & T_PREF) strncat(out, v4 ? ";PREF=1" : (first ? ";TYPE=PREF" : ",PREF"), max - strlen(out) - 1);
	if (kind == 3 && !v4) strncat(out, first && !(t & T_PREF) ? ";TYPE=INTERNET" : ",INTERNET", max - strlen(out) - 1);
}

static void write30(Buf *o, const PsProp *pp, int v4)
{
	char val[VLINE * 3], tp[80], l[VLINE * 3 + 120];
	int kind = pp->kind;
	if (kind == 9) {               /* BDAY: 19850412 -> 1985-04-12 (3.0) or 19850412 (4.0) */
		if (v4) snprintf(l, sizeof(l), "BDAY:%s", pp->value);
		else snprintf(l, sizeof(l), "BDAY:%.4s-%.2s-%.2s", pp->value, pp->value + 4, pp->value + 6);
		put_line(o, l);
		return;
	}
	text30(pp->value, kind == 0 || kind == 4 || kind == 5, val, sizeof(val));
	type_param(pp->types, v4, kind, tp, sizeof(tp));
	if (kind == 2 && v4) snprintf(l, sizeof(l), "TEL;VALUE=text%s:%s", tp, val);
	else snprintf(l, sizeof(l), "%s%s:%s", kKinds[kind], tp, val);
	put_line(o, l);
}

/* the server card's properties of one kind, read as the Psion's would be,
   to see whether the Psion changed that kind */
#define tmp  (g_vw->sk_tmp)
#define mine (g_vw->sk_mine)
static int same_kind(const char *orig, int olen, const PsProp *ps, int nps, int kind)
{
	int n = vc_to_psion(orig, olen, tmp, sizeof(tmp)), m, i, j, a = 0, b = 0;
	if (n < 0) return 0;
	m = read_psion(tmp, n, mine, MAXP);
	for (i = 0; i < m; i++) if (mine[i].kind == kind) a++;
	for (i = 0; i < nps; i++) if (ps[i].kind == kind) b++;
	if (a != b) return 0;
	for (i = 0; i < nps; i++) {
		int found = 0;
		if (ps[i].kind != kind) continue;
		for (j = 0; j < m && !found; j++)
			if (mine[j].kind == kind && mine[j].types == ps[i].types && !strcmp(mine[j].value, ps[i].value)) found = 1;
		if (!found) return 0;
	}
	return 1;
}
#undef tmp
#undef mine

/* FN from the Psion's N ("family;given;middle;prefix;suffix"): "given family" */
static int fn_from_n(const PsProp *ps, int nps, PsProp *fn)
{
	char fam[200], giv[200];
	const char *nv = 0;
	int i;
	for (i = 0; i < nps; i++) if (ps[i].kind == 0) nv = ps[i].value;
	if (!nv) return 0;
	nv = component(nv, fam, sizeof(fam), 1);
	if (nv) component(nv, giv, sizeof(giv), 1); else giv[0] = 0;
	fn->kind = 1;
	fn->types = 0;
	snprintf(fn->value, sizeof(fn->value), "%s%s%s", giv, giv[0] && fam[0] ? " " : "", fam);
	return fn->value[0] != 0;
}

#define ps   (g_vw->mg_ps)
#define line (g_vw->mg_line)
#define f    (g_vw->mg_f)
int vc_merge(const char *orig, int olen, const char *psion, int plen, const char *uid, char *out, int max)
{
	char keep[NKINDS], groups[16][24];
	int nps, i, n, pos = 0, v4 = 0, ng = 0, has_fn = 0;
	Buf o;
	Prop p;
	o.b = out; o.n = 0; o.max = max; o.bad = 0;
	if (max < 2) return -1;
	out[0] = 0;
	if (vc_work(1) != 0) return -1;
	nps = read_psion(psion, plen, ps, MAXP);
	if (!orig || olen <= 0 || !orig[0]) {
		/* a new card, 3.0 */
		char l[200];
		put(&o, "BEGIN:VCARD\r\nVERSION:3.0\r\nPRODID:-//PsiSync//Psion//EN\r\n", -1);
		snprintf(l, sizeof(l), "UID:%s", uid ? uid : "");
		put_line(&o, l);
		for (i = 0; i < nps; i++) { write30(&o, &ps[i], 0); if (ps[i].kind == 1) has_fn = 1; }
		if (!has_fn) {
			/* FN is required: from N, else a placeholder */
			if (!fn_from_n(ps, nps, &f)) { f.kind = 1; f.types = 0; pm_copy(f.value, "(no name)", sizeof(f.value)); }
			write30(&o, &f, 0);
		}
		put(&o, "END:VCARD\r\n", -1);
		return o.bad ? -1 : o.n;
	}
	/* which kinds the Psion changed */
	{
		int pos2 = 0;
		while ((n = ics_line(orig, olen, &pos2, line, sizeof(line))) >= 0) {
			parse(line, &p);
			if (!strcmp(p.name, "VERSION")) v4 = p.value[0] == '4';
		}
	}
	for (i = 0; i < NKINDS; i++) keep[i] = (char)same_kind(orig, olen, ps, nps, i);
	/* Contacts' cards may have no FN: then FN stays, unless the name
	   changed, when it is made again from N (FN is required) */
	for (i = 0; i < nps; i++) if (ps[i].kind == 1) has_fn = 1;
	if (!has_fn) {
		keep[1] = 1;
		if (!keep[0] && nps < MAXP && fn_from_n(ps, nps, &ps[nps])) { nps++; keep[1] = 0; }
	}
	/* groups whose property is being replaced: their labels go with them */
	pos = 0;
	while ((n = ics_line(orig, olen, &pos, line, sizeof(line))) >= 0) {
		int k;
		parse(line, &p);
		k = kind_of(p.name);
		if (k >= 0 && !keep[k] && p.group[0] && ng < 16) pm_copy(groups[ng++], p.group, sizeof(groups[0]));
	}
	pos = 0;
	for (;;) {
		int k, g, drop = 0, from = pos;
		if ((n = ics_line(orig, olen, &pos, line, sizeof(line))) < 0) break;
		if (!n) continue;
		parse(line, &p);
		if (!strcmp(p.name, "END") && !pm_strcasecmp(p.value, "VCARD")) {
			for (i = 0; i < nps; i++) if (!keep[ps[i].kind]) write30(&o, &ps[i], v4);
			put_line(&o, line);
			break;
		}
		k = kind_of(p.name);
		if (k >= 0 && !keep[k]) drop = 1;
		for (g = 0; g < ng && !drop; g++) if (p.group[0] && !strcmp(p.group, groups[g])) drop = 1;
		if (!drop) keep_line(&o, line, n, orig + from, pos - from);
	}
	return o.bad ? -1 : o.n;
}
#undef ps
#undef line
#undef f
