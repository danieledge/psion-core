/* aesgcm.c - AES-128-GCM (aesgcm.h). AES as FIPS-197 sets it out, a byte
 * at a time; GHASH by Shoup's 4-bit table, as mbed TLS does it. */
#include <string.h>
#include "aesgcm.h"

static const unsigned char kSbox[256] = {
	0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
	0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
	0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
	0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
	0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
	0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
	0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
	0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
	0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
	0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
	0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
	0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
	0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
	0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
	0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
	0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

static unsigned char xtime(unsigned char x) { return (unsigned char)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

static void expand(unsigned char rk[176], const unsigned char key[16])
{
	unsigned char rcon = 1;
	int i;
	memcpy(rk, key, 16);
	for (i = 16; i < 176; i += 4) {
		unsigned char t[4];
		memcpy(t, rk + i - 4, 4);
		if (i % 16 == 0) {
			unsigned char u = t[0];
			t[0] = (unsigned char)(kSbox[t[1]] ^ rcon);
			t[1] = kSbox[t[2]];
			t[2] = kSbox[t[3]];
			t[3] = kSbox[u];
			rcon = xtime(rcon);
		}
		rk[i] = rk[i - 16] ^ t[0];
		rk[i + 1] = rk[i - 15] ^ t[1];
		rk[i + 2] = rk[i - 14] ^ t[2];
		rk[i + 3] = rk[i - 13] ^ t[3];
	}
}

void aes128_encrypt(const unsigned char rk[176], const unsigned char in[16], unsigned char out[16])
{
	unsigned char s[16], t[16];
	int r, c, i;
	for (i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
	for (r = 1; r <= 10; r++) {
		/* SubBytes and ShiftRows together: column c, row i takes column c+i */
		for (c = 0; c < 4; c++)
			for (i = 0; i < 4; i++)
				t[c * 4 + i] = kSbox[s[((c + i) & 3) * 4 + i]];
		if (r < 10) {
			for (c = 0; c < 4; c++) {
				unsigned char *a = t + c * 4;
				unsigned char a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
				unsigned char all = a0 ^ a1 ^ a2 ^ a3;
				a[0] = a0 ^ all ^ xtime(a0 ^ a1);
				a[1] = a1 ^ all ^ xtime(a1 ^ a2);
				a[2] = a2 ^ all ^ xtime(a2 ^ a3);
				a[3] = a3 ^ all ^ xtime(a3 ^ a0);
			}
		}
		for (i = 0; i < 16; i++) s[i] = t[i] ^ rk[r * 16 + i];
	}
	memcpy(out, s, 16);
}

static unsigned long long be64(const unsigned char *p)
{
	unsigned long long v = 0;
	int i;
	for (i = 0; i < 8; i++) v = (v << 8) | p[i];
	return v;
}

static void put64(unsigned char *p, unsigned long long v)
{
	int i;
	for (i = 7; i >= 0; i--) { p[i] = (unsigned char)v; v >>= 8; }
}

void aesgcm_setkey(aesgcm_key *k, const unsigned char key[16])
{
	unsigned char h[16];
	unsigned long long vh, vl;
	int i, j;
	expand(k->rk, key);
	memset(h, 0, 16);
	aes128_encrypt(k->rk, h, h);
	vh = be64(h);
	vl = be64(h + 8);
	k->hl[8] = vl; k->hh[8] = vh;
	k->hl[0] = 0; k->hh[0] = 0;
	for (i = 4; i > 0; i >>= 1) {
		unsigned long t = (unsigned long)(vl & 1) * 0xe1000000UL;
		vl = (vh << 63) | (vl >> 1);
		vh = (vh >> 1) ^ ((unsigned long long)t << 32);
		k->hl[i] = vl; k->hh[i] = vh;
	}
	for (i = 2; i <= 8; i *= 2) {
		vh = k->hh[i]; vl = k->hl[i];
		for (j = 1; j < i; j++) { k->hh[i + j] = vh ^ k->hh[j]; k->hl[i + j] = vl ^ k->hl[j]; }
	}
}

static const unsigned long long kLast4[16] = {
	0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
	0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0 };

/* x = x * H */
static void gmult(const aesgcm_key *k, unsigned char x[16])
{
	unsigned long long zh, zl;
	int i, lo = x[15] & 0xf, hi, rem;
	zh = k->hh[lo]; zl = k->hl[lo];
	for (i = 15; i >= 0; i--) {
		lo = x[i] & 0xf;
		hi = (x[i] >> 4) & 0xf;
		if (i != 15) {
			rem = (int)(zl & 0xf);
			zl = (zh << 60) | (zl >> 4);
			zh >>= 4;
			zh ^= kLast4[rem] << 48;
			zh ^= k->hh[lo]; zl ^= k->hl[lo];
		}
		rem = (int)(zl & 0xf);
		zl = (zh << 60) | (zl >> 4);
		zh >>= 4;
		zh ^= kLast4[rem] << 48;
		zh ^= k->hh[hi]; zl ^= k->hl[hi];
	}
	put64(x, zh);
	put64(x + 8, zl);
}

static void ghash(const aesgcm_key *k, unsigned char y[16], const unsigned char *d, int n)
{
	while (n > 0) {
		int i, m = n < 16 ? n : 16;
		for (i = 0; i < m; i++) y[i] ^= d[i];
		gmult(k, y);
		d += m;
		n -= m;
	}
}

static void tag_of(const aesgcm_key *k, const unsigned char iv[12], const unsigned char *aad, int aadlen,
                   const unsigned char *ct, int len, unsigned char tag[16])
{
	unsigned char y[16], lens[16], j0[16];
	int i;
	memset(y, 0, 16);
	ghash(k, y, aad, aadlen);
	ghash(k, y, ct, len);
	put64(lens, (unsigned long long)aadlen * 8);
	put64(lens + 8, (unsigned long long)len * 8);
	ghash(k, y, lens, 16);
	memcpy(j0, iv, 12);
	j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
	aes128_encrypt(k->rk, j0, j0);
	for (i = 0; i < 16; i++) tag[i] = (unsigned char)(y[i] ^ j0[i]);
}

/* counter mode from counter 2 (counter 1 is the tag's) */
static void ctr(const aesgcm_key *k, const unsigned char iv[12], unsigned char *buf, int len)
{
	unsigned char cb[16], ks[16];
	unsigned long n = 2;
	int i;
	memcpy(cb, iv, 12);
	while (len > 0) {
		int m = len < 16 ? len : 16;
		cb[12] = (unsigned char)(n >> 24); cb[13] = (unsigned char)(n >> 16);
		cb[14] = (unsigned char)(n >> 8); cb[15] = (unsigned char)n;
		aes128_encrypt(k->rk, cb, ks);
		for (i = 0; i < m; i++) buf[i] ^= ks[i];
		buf += m;
		len -= m;
		n++;
	}
}

void aesgcm_seal(const aesgcm_key *k, const unsigned char iv[12],
                 const unsigned char *aad, int aadlen, unsigned char *buf, int len)
{
	ctr(k, iv, buf, len);
	tag_of(k, iv, aad, aadlen, buf, len, buf + len);
}

int aesgcm_open(const aesgcm_key *k, const unsigned char iv[12],
                const unsigned char *aad, int aadlen, unsigned char *buf, int len)
{
	unsigned char tag[16];
	int i, diff = 0;
	if (len < 16) return -1;
	len -= 16;
	tag_of(k, iv, aad, aadlen, buf, len, tag);
	for (i = 0; i < 16; i++) diff |= tag[i] ^ buf[len + i];
	if (diff) return -1;
	ctr(k, iv, buf, len);
	return 0;
}
