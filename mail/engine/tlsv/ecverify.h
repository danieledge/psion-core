/* ecverify.h - ECDSA signature checks on P-256 and P-384, for certcheck.c
 * (which includes this file once: one object, so the programs that build
 * certcheck.c need no new file in their builds).
 *
 * Verification only: every number here is public (the key, the signature,
 * the hash), so nothing needs to run in constant time; it needs to be
 * right. Checked: r and s in 1..n-1, the key a point on the curve (x and y
 * below p, y^2 = x^3 - 3x + b), the hash cut to the curve's size, R = u1 G +
 * u2 Q not the point at infinity, and R's x mod n equal to r.
 *
 * Arithmetic: 32-bit words, Montgomery multiplication (CIOS) modulo p and
 * modulo n, Jacobian coordinates with a = -3 (both curves), u1 G + u2 Q by
 * Shamir's trick (G + Q worked out once), inverses by Fermat (x^(m-2)).
 * The 32 x 32 -> 64 bit products want the ARM's UMULL: a program builds
 * certcheck.c with -march=armv4 for that (PsiWeb does; the ARM710T of the
 * 5mx and the netBook's SA-1100 both have it). Without it gcc calls
 * __muldi3: the same results, about three times slower.
 *
 * Curve constants from OpenSSL (ecparam -param_enc explicit), checked: G
 * on the curve and equal to 1 x G as python's cryptography makes it.
 */
#ifndef ECVERIFY_H
#define ECVERIFY_H

typedef unsigned int ecw;                 /* one 32-bit word */
typedef unsigned long long ecdw;          /* two */
#define EC_W 12                           /* words in the largest number (384 bits) */

typedef struct
	{
	int w;                                /* words: 8 or 12 */
	ecw m[EC_W];                          /* the modulus */
	ecw ninv;                             /* -m^-1 mod 2^32 */
	ecw rr[EC_W];                         /* R^2 mod m, R = 2^(32w) */
	ecw one[EC_W];                        /* R mod m: 1 in Montgomery form */
	} EcMod;

typedef struct
	{
	int bits, nb;                         /* 256, 32 / 384, 48 */
	int ready;
	EcMod p, n;
	ecw b[EC_W], gx[EC_W], gy[EC_W];      /* Montgomery form, modulo p */
	} EcCurve;

typedef struct { ecw x[EC_W], y[EC_W], z[EC_W]; } EcPoint;   /* Jacobian; z = 0: infinity */

static const unsigned char k_p256_p[32] = {
	0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
};
static const unsigned char k_p256_n[32] = {
	0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51,
};
static const unsigned char k_p256_b[32] = {
	0x5a,0xc6,0x35,0xd8,0xaa,0x3a,0x93,0xe7,0xb3,0xeb,0xbd,0x55,0x76,0x98,0x86,0xbc,
	0x65,0x1d,0x06,0xb0,0xcc,0x53,0xb0,0xf6,0x3b,0xce,0x3c,0x3e,0x27,0xd2,0x60,0x4b,
};
static const unsigned char k_p256_gx[32] = {
	0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
	0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96,
};
static const unsigned char k_p256_gy[32] = {
	0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
	0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5,
};
static const unsigned char k_p384_p[48] = {
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,
	0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,
};
static const unsigned char k_p384_n[48] = {
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xc7,0x63,0x4d,0x81,0xf4,0x37,0x2d,0xdf,
	0x58,0x1a,0x0d,0xb2,0x48,0xb0,0xa7,0x7a,0xec,0xec,0x19,0x6a,0xcc,0xc5,0x29,0x73,
};
static const unsigned char k_p384_b[48] = {
	0xb3,0x31,0x2f,0xa7,0xe2,0x3e,0xe7,0xe4,0x98,0x8e,0x05,0x6b,0xe3,0xf8,0x2d,0x19,
	0x18,0x1d,0x9c,0x6e,0xfe,0x81,0x41,0x12,0x03,0x14,0x08,0x8f,0x50,0x13,0x87,0x5a,
	0xc6,0x56,0x39,0x8d,0x8a,0x2e,0xd1,0x9d,0x2a,0x85,0xc8,0xed,0xd3,0xec,0x2a,0xef,
};
static const unsigned char k_p384_gx[48] = {
	0xaa,0x87,0xca,0x22,0xbe,0x8b,0x05,0x37,0x8e,0xb1,0xc7,0x1e,0xf3,0x20,0xad,0x74,
	0x6e,0x1d,0x3b,0x62,0x8b,0xa7,0x9b,0x98,0x59,0xf7,0x41,0xe0,0x82,0x54,0x2a,0x38,
	0x55,0x02,0xf2,0x5d,0xbf,0x55,0x29,0x6c,0x3a,0x54,0x5e,0x38,0x72,0x76,0x0a,0xb7,
};
static const unsigned char k_p384_gy[48] = {
	0x36,0x17,0xde,0x4a,0x96,0x26,0x2c,0x6f,0x5d,0x9e,0x98,0xbf,0x92,0x92,0xdc,0x29,
	0xf8,0xf4,0x1d,0xbd,0x28,0x9a,0x14,0x7c,0xe9,0xda,0x31,0x13,0xb5,0xf0,0xb8,0xc0,
	0x0a,0x60,0xb1,0xce,0x1d,0x7e,0x81,0x9d,0x7a,0x43,0x1d,0x7c,0x90,0xea,0x0e,0x5f,
};

static EcCurve g_p256 = { 256, 32 }, g_p384 = { 384, 48 };

/* ---- numbers of w words, least significant first */

static void ec_from_bytes(ecw *r, int w, const unsigned char *b, int nb)
{
	int i;
	memset(r, 0, sizeof(ecw) * w);
	for (i = 0; i < nb && i < 4 * w; i++)
		r[i / 4] |= (ecw)b[nb - 1 - i] << (8 * (i % 4));
}

static int ec_cmp(const ecw *a, const ecw *b, int w)
{
	while (w--) if (a[w] != b[w]) return a[w] < b[w] ? -1 : 1;
	return 0;
}

static int ec_zero(const ecw *a, int w)
{
	while (w--) if (a[w]) return 0;
	return 1;
}

/* r = a - b, the borrow out */
static ecw ec_sub(ecw *r, const ecw *a, const ecw *b, int w)
{
	ecw br = 0;
	int i;
	for (i = 0; i < w; i++) {
		ecdw d = (ecdw)a[i] - b[i] - br;
		r[i] = (ecw)d;
		br = (ecw)(d >> 32) & 1;
	}
	return br;
}

/* r = a + b, the carry out */
static ecw ec_add(ecw *r, const ecw *a, const ecw *b, int w)
{
	ecw c = 0;
	int i;
	for (i = 0; i < w; i++) {
		ecdw s = (ecdw)a[i] + b[i] + c;
		r[i] = (ecw)s;
		c = (ecw)(s >> 32);
	}
	return c;
}

/* ---- modulo M (a, b below M) */

static void ec_modadd(ecw *r, const ecw *a, const ecw *b, const EcMod *M)
{
	ecw c = ec_add(r, a, b, M->w);
	if (c || ec_cmp(r, M->m, M->w) >= 0) ec_sub(r, r, M->m, M->w);
}

static void ec_modsub(ecw *r, const ecw *a, const ecw *b, const EcMod *M)
{
	if (ec_sub(r, a, b, M->w)) ec_add(r, r, M->m, M->w);
}

/* r = a b R^-1 mod M (r may be a or b) */
static void ec_mul(ecw *r, const ecw *a, const ecw *b, const EcMod *M)
{
	ecw t[EC_W + 2];
	int i, j, w = M->w;
	memset(t, 0, sizeof(ecw) * (w + 2));
	for (i = 0; i < w; i++) {
		ecdw c = 0;
		ecw bi = b[i], u;
		for (j = 0; j < w; j++) {
			c = (ecdw)a[j] * bi + t[j] + (c >> 32);
			t[j] = (ecw)c;
		}
		c = (ecdw)t[w] + (c >> 32);
		t[w] = (ecw)c;
		t[w + 1] = (ecw)(c >> 32);
		u = t[0] * M->ninv;
		c = (ecdw)u * M->m[0] + t[0];
		for (j = 1; j < w; j++) {
			c = (ecdw)u * M->m[j] + t[j] + (c >> 32);
			t[j - 1] = (ecw)c;
		}
		c = (ecdw)t[w] + (c >> 32);
		t[w - 1] = (ecw)c;
		t[w] = t[w + 1] + (ecw)(c >> 32);
	}
	if (t[w] || ec_cmp(t, M->m, w) >= 0) ec_sub(t, t, M->m, w);
	memcpy(r, t, sizeof(ecw) * w);
}

static void ec_mod_init(EcMod *M, const unsigned char *m, int nb)
{
	ecw x;
	int i;
	M->w = nb / 4;
	ec_from_bytes(M->m, M->w, m, nb);
	x = 1;                                  /* m[0]^-1 mod 2^32, by Newton */
	for (i = 0; i < 5; i++) x *= 2 - M->m[0] * x;
	M->ninv = (ecw)0 - x;
	/* R mod m and R^2 mod m by doubling 1 (m is above 2^(32w - 1)) */
	memset(M->one, 0, sizeof(M->one));
	M->one[0] = 1;
	for (i = 0; i < 32 * M->w; i++) ec_modadd(M->one, M->one, M->one, M);
	memcpy(M->rr, M->one, sizeof(M->rr));
	for (i = 0; i < 32 * M->w; i++) ec_modadd(M->rr, M->rr, M->rr, M);
}

/* to and from Montgomery form */
static void ec_to(ecw *r, const ecw *a, const EcMod *M) { ec_mul(r, a, M->rr, M); }
static void ec_from(ecw *r, const ecw *a, const EcMod *M)
{
	ecw one[EC_W];
	memset(one, 0, sizeof(one));
	one[0] = 1;
	ec_mul(r, a, one, M);
}

/* r = a^(M - 2) = a^-1, all in Montgomery form (M prime) */
static void ec_inv(ecw *r, const ecw *a, const EcMod *M)
{
	ecw e[EC_W], two[EC_W], x[EC_W];
	int i;
	memset(two, 0, sizeof(two));
	two[0] = 2;
	ec_sub(e, M->m, two, M->w);
	memcpy(x, M->one, sizeof(x));
	for (i = 32 * M->w - 1; i >= 0; i--) {
		ec_mul(x, x, x, M);
		if ((e[i / 32] >> (i % 32)) & 1) ec_mul(x, x, a, M);
	}
	memcpy(r, x, sizeof(x));
}

static EcCurve *ec_curve(int bits)
{
	EcCurve *C = bits == 256 ? &g_p256 : bits == 384 ? &g_p384 : 0;
	ecw t[EC_W];
	if (!C || C->ready) return C;
	if (bits == 256) {
		ec_mod_init(&C->p, k_p256_p, 32);
		ec_mod_init(&C->n, k_p256_n, 32);
		ec_from_bytes(t, 8, k_p256_b, 32); ec_to(C->b, t, &C->p);
		ec_from_bytes(t, 8, k_p256_gx, 32); ec_to(C->gx, t, &C->p);
		ec_from_bytes(t, 8, k_p256_gy, 32); ec_to(C->gy, t, &C->p);
	} else {
		ec_mod_init(&C->p, k_p384_p, 48);
		ec_mod_init(&C->n, k_p384_n, 48);
		ec_from_bytes(t, 12, k_p384_b, 48); ec_to(C->b, t, &C->p);
		ec_from_bytes(t, 12, k_p384_gx, 48); ec_to(C->gx, t, &C->p);
		ec_from_bytes(t, 12, k_p384_gy, 48); ec_to(C->gy, t, &C->p);
	}
	C->ready = 1;
	return C;
}

/* ---- points (Montgomery form modulo p) */

/* r = 2 a (dbl-2001-b, a = -3); r may be a */
static void ec_double(EcPoint *r, const EcPoint *a, const EcCurve *C)
{
	const EcMod *P = &C->p;
	ecw delta[EC_W], gamma[EC_W], beta[EC_W], alpha[EC_W], t1[EC_W], t2[EC_W], x3[EC_W];
	if (ec_zero(a->z, P->w)) { *r = *a; return; }
	ec_mul(delta, a->z, a->z, P);
	ec_mul(gamma, a->y, a->y, P);
	ec_mul(beta, a->x, gamma, P);
	ec_modsub(t1, a->x, delta, P);
	ec_modadd(t2, a->x, delta, P);
	ec_mul(alpha, t1, t2, P);
	ec_modadd(t1, alpha, alpha, P);
	ec_modadd(alpha, t1, alpha, P);                   /* 3 (x - delta)(x + delta) */
	ec_mul(x3, alpha, alpha, P);
	ec_modadd(t1, beta, beta, P);                     /* 2 beta */
	ec_modadd(t1, t1, t1, P);                         /* 4 beta */
	ec_modadd(t2, t1, t1, P);                         /* 8 beta */
	ec_modsub(x3, x3, t2, P);
	ec_modadd(t2, a->y, a->z, P);                     /* Z3 = (y + z)^2 - gamma - delta */
	ec_mul(t2, t2, t2, P);
	ec_modsub(t2, t2, gamma, P);
	ec_modsub(r->z, t2, delta, P);
	ec_modsub(t1, t1, x3, P);                         /* Y3 = alpha (4 beta - X3) - 8 gamma^2 */
	ec_mul(t1, alpha, t1, P);
	ec_mul(t2, gamma, gamma, P);
	ec_modadd(t2, t2, t2, P);
	ec_modadd(t2, t2, t2, P);
	ec_modadd(t2, t2, t2, P);
	ec_modsub(r->y, t1, t2, P);
	memcpy(r->x, x3, sizeof(x3));
}

/* r = a + b (add-2007-bl); r may be a or b */
static void ec_addp(EcPoint *r, const EcPoint *a, const EcPoint *b, const EcCurve *C)
{
	const EcMod *P = &C->p;
	ecw z1z1[EC_W], z2z2[EC_W], u1[EC_W], u2[EC_W], s1[EC_W], s2[EC_W], h[EC_W], i[EC_W], j[EC_W], rr[EC_W], v[EC_W];
	ecw x3[EC_W], y3[EC_W], z3[EC_W];
	if (ec_zero(a->z, P->w)) { *r = *b; return; }
	if (ec_zero(b->z, P->w)) { *r = *a; return; }
	ec_mul(z1z1, a->z, a->z, P);
	ec_mul(z2z2, b->z, b->z, P);
	ec_mul(u1, a->x, z2z2, P);
	ec_mul(u2, b->x, z1z1, P);
	ec_mul(s1, a->y, b->z, P); ec_mul(s1, s1, z2z2, P);
	ec_mul(s2, b->y, a->z, P); ec_mul(s2, s2, z1z1, P);
	ec_modsub(h, u2, u1, P);
	ec_modsub(rr, s2, s1, P);
	if (ec_zero(h, P->w)) {
		if (ec_zero(rr, P->w)) { ec_double(r, a, C); return; }
		memset(r, 0, sizeof(*r));                     /* a = -b: infinity */
		return;
	}
	ec_modadd(i, h, h, P); ec_mul(i, i, i, P);        /* I = (2H)^2 */
	ec_mul(j, h, i, P);
	ec_modadd(rr, rr, rr, P);                         /* r = 2 (S2 - S1) */
	ec_mul(v, u1, i, P);
	ec_mul(x3, rr, rr, P);                            /* X3 = r^2 - J - 2V */
	ec_modsub(x3, x3, j, P);
	ec_modsub(x3, x3, v, P);
	ec_modsub(x3, x3, v, P);
	ec_modsub(y3, v, x3, P);                          /* Y3 = r (V - X3) - 2 S1 J */
	ec_mul(y3, rr, y3, P);
	ec_mul(s1, s1, j, P);
	ec_modadd(s1, s1, s1, P);
	ec_modsub(y3, y3, s1, P);
	ec_modadd(z3, a->z, b->z, P);                     /* Z3 = ((Z1 + Z2)^2 - Z1Z1 - Z2Z2) H */
	ec_mul(z3, z3, z3, P);
	ec_modsub(z3, z3, z1z1, P);
	ec_modsub(z3, z3, z2z2, P);
	ec_mul(r->z, z3, h, P);
	memcpy(r->x, x3, sizeof(x3));
	memcpy(r->y, y3, sizeof(y3));
}

/* ---- ECDSA */

/* the INTEGER at *p (DER) into nb bytes, big-endian; 0 ok */
static int ec_der_int(const unsigned char **p, const unsigned char *end, unsigned char *out, int nb)
{
	const unsigned char *q = *p;
	int l;
	if (end - q < 2 || q[0] != 0x02) return -1;
	l = q[1];
	q += 2;
	if (l < 1 || l > 0x7f || l > end - q) return -1;
	if (q[0] & 0x80) return -1;                       /* negative */
	*p = q + l;
	while (l > 0 && *q == 0) { q++; l--; }
	if (l > nb) return -1;
	memset(out, 0, nb);
	memcpy(out + nb - l, q, l);
	return 0;
}

/* 0 if sig (DER: SEQUENCE { r, s }) is pub's (X || Y, 2 x nb bytes)
   signature of hash, on curve bits (256 or 384) */
int tlsv_ecdsa_verify(int bits, const unsigned char *pub, int publen, const unsigned char *hash, int hlen,
                      const unsigned char *sig, int siglen)
{
	EcCurve *C = ec_curve(bits);
	const EcMod *P, *N;
	const unsigned char *p, *end;
	unsigned char rb[48], sb[48];
	ecw r[EC_W], s[EC_W], e[EC_W], w[EC_W], u1[EC_W], u2[EC_W], qx[EC_W], qy[EC_W], t[EC_W], t2[EC_W];
	EcPoint G, Q, GQ, R;
	const EcPoint *tab[4];
	int nb, k, i, l;
	if (!C) return -1;
	P = &C->p; N = &C->n; nb = C->nb;
	/* the signature */
	if (siglen < 8 || sig[0] != 0x30) return -1;
	l = sig[1];
	if (l > 0x7f || l != siglen - 2) return -1;
	p = sig + 2; end = sig + siglen;
	if (ec_der_int(&p, end, rb, nb) || ec_der_int(&p, end, sb, nb) || p != end) return -1;
	ec_from_bytes(r, P->w, rb, nb);
	ec_from_bytes(s, P->w, sb, nb);
	if (ec_zero(r, N->w) || ec_zero(s, N->w) || ec_cmp(r, N->m, N->w) >= 0 || ec_cmp(s, N->m, N->w) >= 0) return -1;
	/* the key: on the curve */
	if (publen != 2 * nb) return -1;
	ec_from_bytes(qx, P->w, pub, nb);
	ec_from_bytes(qy, P->w, pub + nb, nb);
	if (ec_cmp(qx, P->m, P->w) >= 0 || ec_cmp(qy, P->m, P->w) >= 0) return -1;
	ec_to(qx, qx, P);
	ec_to(qy, qy, P);
	ec_mul(t, qy, qy, P);                             /* y^2 */
	ec_mul(t2, qx, qx, P);
	ec_mul(t2, t2, qx, P);                            /* x^3 */
	ec_modsub(t2, t2, qx, P);
	ec_modsub(t2, t2, qx, P);
	ec_modsub(t2, t2, qx, P);                         /* x^3 - 3x */
	ec_modadd(t2, t2, C->b, P);
	if (ec_cmp(t, t2, P->w)) return -1;
	/* e: the hash's leftmost nb bytes, mod n */
	ec_from_bytes(e, N->w, hash, hlen < nb ? hlen : nb);
	if (ec_cmp(e, N->m, N->w) >= 0) ec_sub(e, e, N->m, N->w);
	/* w = s^-1, u1 = e w, u2 = r w (mod n) */
	ec_to(w, s, N);
	ec_inv(w, w, N);
	ec_mul(u1, e, w, N);
	ec_mul(u2, r, w, N);
	/* R = u1 G + u2 Q */
	memcpy(G.x, C->gx, sizeof(G.x)); memcpy(G.y, C->gy, sizeof(G.y)); memcpy(G.z, P->one, sizeof(G.z));
	memcpy(Q.x, qx, sizeof(Q.x)); memcpy(Q.y, qy, sizeof(Q.y)); memcpy(Q.z, P->one, sizeof(Q.z));
	ec_addp(&GQ, &G, &Q, C);
	tab[1] = &G; tab[2] = &Q; tab[3] = &GQ;
	memset(&R, 0, sizeof(R));
	for (i = 32 * N->w - 1; i >= 0; i--) {
		ec_double(&R, &R, C);
		k = ((u1[i / 32] >> (i % 32)) & 1) | (((u2[i / 32] >> (i % 32)) & 1) << 1);
		if (k) ec_addp(&R, &R, tab[k], C);
	}
	if (ec_zero(R.z, P->w)) return -1;
	/* x = X / Z^2, back from Montgomery form, mod n */
	ec_inv(t, R.z, P);
	ec_mul(t, t, t, P);
	ec_mul(t, R.x, t, P);
	ec_from(t, t, P);
	if (ec_cmp(t, N->m, N->w) >= 0) ec_sub(t, t, N->m, N->w);
	return ec_cmp(t, r, N->w) ? -1 : 0;
}

#endif
