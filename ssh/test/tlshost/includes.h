/* includes.h - tls13.c on a PC for test/tlstest.c: Dropbear's includes.h
 * stood in for (its guards), libtomcrypt's ChaCha20-Poly1305 and SHA-2,
 * Dropbear's X25519, session tickets on (TLS_RESUME) and AES-128-GCM
 * (TLS_AES_GCM), as PsiMail and PsiWeb build it. */
#ifndef DROPBEAR_INCLUDES_H_
#define DROPBEAR_INCLUDES_H_
#define DROPBEAR_RANDOM_H_
#include "options.h"
#define TLS_RESUME 1
#ifndef TLS_AES_GCM
#define TLS_AES_GCM 1
#endif
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "tomcrypt.h"
void genrandom(unsigned char *buf, unsigned int len);
#endif
