/* aesgcmtest.c - aesgcm.c against FIPS-197 (AES-128) and the GCM
 * specification's test cases 1 to 4 (128-bit key), round trips of every
 * length up to 300 bytes, and a damaged tag, byte or AAD refused.
 *   in ssh/: make -f test/Makefile.host aesgcmtest && ./aesgcmtest */
#include <stdio.h>
#include <string.h>
#include "aesgcm.h"

static int gFail, gChecks;
#define CHECK(c) do { gChecks++; if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); gFail++; } } while (0)

static int unhex(const char *h, unsigned char *out)
{
	int n = 0;
	while (h[0] && h[1]) {
		unsigned v;
		sscanf(h, "%2x", &v);
		out[n++] = (unsigned char)v;
		h += 2;
	}
	return n;
}

static void gcm_case(const char *key, const char *iv, const char *pt, const char *aad, const char *ct, const char *tag)
{
	unsigned char k[16], v[12], p[128], a[64], c[128], t[16], buf[160];
	int pn, an, cn;
	aesgcm_key gk;
	unhex(key, k); unhex(iv, v);
	pn = unhex(pt, p); an = unhex(aad, a); cn = unhex(ct, c); unhex(tag, t);
	aesgcm_setkey(&gk, k);
	memcpy(buf, p, pn);
	aesgcm_seal(&gk, v, a, an, buf, pn);
	CHECK(cn == pn && !memcmp(buf, c, cn) && !memcmp(buf + pn, t, 16));
	CHECK(aesgcm_open(&gk, v, a, an, buf, pn + 16) == 0 && !memcmp(buf, p, pn));
}

int main(void)
{
	unsigned char k[16], in[16], out[16], want[16];
	unhex("000102030405060708090a0b0c0d0e0f", k);
	unhex("00112233445566778899aabbccddeeff", in);
	unhex("69c4e0d86a7b0430d8cdb78070b4c55a", want);
	{
		aesgcm_key gk;
		aesgcm_setkey(&gk, k);
		aes128_encrypt(gk.rk, in, out);
		CHECK(!memcmp(out, want, 16));
	}
	gcm_case("00000000000000000000000000000000", "000000000000000000000000", "", "", "", "58e2fccefa7e3061367f1d57a4e7455a");
	gcm_case("00000000000000000000000000000000", "000000000000000000000000",
		"00000000000000000000000000000000", "", "0388dace60b6a392f328c2b971b2fe78", "ab6e47d42cec13bdf53a67b21257bddf");
	gcm_case("feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888",
		"d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
		"", "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985",
		"4d5c2af327cd64a62cf35abd2ba6fab4");
	gcm_case("feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888",
		"d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeefabaddad2",
		"42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091",
		"5bc94fbc3221a5db94fae95ae7121a47");
	{
		aesgcm_key gk;
		unsigned char iv[12], aad[13], buf[320], orig[300];
		int n, i;
		for (i = 0; i < 16; i++) k[i] = (unsigned char)(i * 7 + 1);
		for (i = 0; i < 12; i++) iv[i] = (unsigned char)(i * 3);
		for (i = 0; i < 13; i++) aad[i] = (unsigned char)(i + 100);
		for (i = 0; i < 300; i++) orig[i] = (unsigned char)(i * 31 + 5);
		aesgcm_setkey(&gk, k);
		for (n = 0; n <= 300; n++) {
			memcpy(buf, orig, n);
			aesgcm_seal(&gk, iv, aad, 13, buf, n);
			if (n >= 1) {
				buf[n / 2] ^= 1;                       /* a damaged byte */
				CHECK(aesgcm_open(&gk, iv, aad, 13, buf, n + 16) != 0);
				buf[n / 2] ^= 1;
			}
			buf[n + 15] ^= 0x80;                       /* a damaged tag */
			CHECK(aesgcm_open(&gk, iv, aad, 13, buf, n + 16) != 0);
			buf[n + 15] ^= 0x80;
			aad[0] ^= 1;                               /* other AAD */
			CHECK(aesgcm_open(&gk, iv, aad, 13, buf, n + 16) != 0);
			aad[0] ^= 1;
			CHECK(aesgcm_open(&gk, iv, aad, 13, buf, n + 16) == 0 && !memcmp(buf, orig, n));
		}
		CHECK(aesgcm_open(&gk, iv, aad, 13, buf, 15) != 0);
	}
	printf("aesgcmtest: %d checks, %d failed\n", gChecks, gFail);
	return gFail != 0;
}
