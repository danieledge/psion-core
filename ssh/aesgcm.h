/* aesgcm.h - AES-128-GCM for TLS 1.3's TLS_AES_128_GCM_SHA256 (tls13.c,
 * under TLS_AES_GCM: PsiWeb and PsiMail). Plain C, no tables kept beyond
 * a key's own (about 0.5 KB), no 64-bit multiplies. Tested on a PC against
 * FIPS-197 and the GCM specification's vectors (test/aesgcmtest.c).
 *
 * It is the second cipher, for servers that offer TLS 1.3 with AES only
 * (the one compulsory TLS 1.3 cipher); ChaCha20-Poly1305 stays the first
 * choice, being much the quicker on an ARM with no AES instructions. */
#ifndef AESGCM_H
#define AESGCM_H

typedef struct
	{
	unsigned char rk[176];         /* AES-128's 11 round keys */
	unsigned long long hl[16], hh[16];   /* GHASH's 4-bit table for H */
	} aesgcm_key;

void aesgcm_setkey(aesgcm_key *k, const unsigned char key[16]);
void aes128_encrypt(const unsigned char rk[176], const unsigned char in[16], unsigned char out[16]);

/* in place: buf holds len bytes of plaintext and 16 bytes of room after,
 * where the tag goes */
void aesgcm_seal(const aesgcm_key *k, const unsigned char iv[12],
                 const unsigned char *aad, int aadlen, unsigned char *buf, int len);
/* in place: buf holds len bytes, the last 16 the tag. 0 if the tag is
 * good (buf then holds len - 16 bytes of plaintext), -1 if not (buf is
 * left as it was) */
int aesgcm_open(const aesgcm_key *k, const unsigned char iv[12],
                const unsigned char *aad, int aadlen, unsigned char *buf, int len);
#endif
