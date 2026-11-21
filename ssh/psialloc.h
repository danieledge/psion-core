/* psialloc.h - a malloc for the C engines (Part 2, P2-05), off unless a
 * program's build turns it on with -DPSI_ALLOC=1.
 *
 * ESTLIB's malloc is the thread's heap (User::Alloc), and EPOC's heap only
 * gives back the free cell at its top: a big block freed below a small one
 * that lives on stays committed (PsiWeb's own log lines showed a third of
 * its heap stranded so). With psialloc a block of PSI_ALLOC_BIG (32 KB) or
 * more is a chunk of its own: freed, its pages go back to the Psion at
 * once; grown with realloc, it grows in place (each chunk sets aside 1 MB
 * of address room, no RAM), so there is no copy and no hole. Everything
 * smaller stays on the heap, as before.
 *
 *   #include "psialloc.h"   after the C library's headers, in each file of
 *                           the engine (or with -include), and psialloc.c
 *                           in the build. Its malloc, calloc, realloc and
 *                           free are then psi_malloc and the rest.
 *
 * A block from ESTLIB itself (strdup, the stdio buffers) is a heap cell and
 * goes back to the heap here as it would there. A big block must not be
 * given to ESTLIB to free or realloc: none of the engines does (they free
 * what they malloc). At most PSI_ALLOC_SLOTS (24) big blocks at a time (each
 * reserves 1 MB of the process's address room); past that a big block is a
 * heap cell, as before.
 *
 * psi_alloc_stats(): blocks and bytes in chunks now, and the most there were.
 *
 * Off switch: PSI_ALLOC=0 (the default): this header defines nothing and
 * the engine is as it was. Not on in any program until measured (MEM-STATUS-M2).
 */
#ifndef PSI_ALLOC_H
#define PSI_ALLOC_H
#ifndef PSI_ALLOC
#define PSI_ALLOC 0
#endif
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PSI_ALLOC_BIG   (32 * 1024)
#define PSI_ALLOC_SLOTS 24
void *psi_malloc(size_t n);
void *psi_calloc(size_t n, size_t m);
void *psi_realloc(void *p, size_t n);
void psi_free(void *p);
typedef struct { int blocks, peak_blocks; long bytes, peak_bytes; long grown_in_place; } PsiAllocStats;
void psi_alloc_stats(PsiAllocStats *s);
/* the chunk underneath (psiglue.cpp on the Psion; the host test's own on a
   PC): a block that reserves max bytes and commits n of them; 0 if none */
void *pg_chunk_new(unsigned long n, unsigned long max);
int pg_chunk_adjust(void *base, unsigned long n);   /* 0 ok, -1 no memory */
void pg_chunk_close(void *base);
#ifdef __cplusplus
}
#endif
#if PSI_ALLOC && !defined(PSI_ALLOC_SELF)
#define malloc(n)     psi_malloc(n)
#define calloc(n, m)  psi_calloc((n), (m))
#define realloc(p, n) psi_realloc((p), (n))
#define free(p)       psi_free(p)
#endif
#endif
