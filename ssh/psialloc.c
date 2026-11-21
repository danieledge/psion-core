/* psialloc.c - see psialloc.h. Built only into a program that turns
 * PSI_ALLOC on; the host test (test/psialloctest.c) runs this file as it is.
 */
#define PSI_ALLOC_SELF            /* (our own malloc is the C library's) */
#include <stdlib.h>
#include <string.h>
#include "psialloc.h"

#define ROOM_MIN (1024UL * 1024)     /* a chunk reserves this much address room anyway */

typedef struct { char *base; unsigned long used, commit, max; } Big;
static Big g_big[PSI_ALLOC_SLOTS];
static PsiAllocStats g_st;

static unsigned long pages(unsigned long n) { return (n + 4095) & ~4095UL; }

static Big *find(const void *p)
{
	int i;
	if (!p) return 0;
	for (i = 0; i < PSI_ALLOC_SLOTS; i++)
		if (g_big[i].base == (const char *)p) return &g_big[i];
	return 0;
}

static void count(long d, int blocks)
{
	g_st.bytes += d;
	g_st.blocks += blocks;
	if (g_st.bytes > g_st.peak_bytes) g_st.peak_bytes = g_st.bytes;
	if (g_st.blocks > g_st.peak_blocks) g_st.peak_blocks = g_st.blocks;
}

/* a big block in a chunk of its own; 0: none to be had (the heap then) */
static void *big_new(size_t n)
{
	int i;
	unsigned long max = pages(n) > ROOM_MIN ? pages(n) : ROOM_MIN;
	for (i = 0; i < PSI_ALLOC_SLOTS; i++)
		if (!g_big[i].base) {
			char *b = (char *)pg_chunk_new(pages(n), max);
			if (!b) return 0;
			g_big[i].base = b;
			g_big[i].used = n;
			g_big[i].commit = pages(n);
			g_big[i].max = max;
			count((long)g_big[i].commit, 1);
			return b;
		}
	return 0;
}

void *psi_malloc(size_t n)
{
	if (n >= PSI_ALLOC_BIG) {
		void *p = big_new(n);
		if (p) return p;
	}
	return malloc(n);
}

void *psi_calloc(size_t n, size_t m)
{
	size_t t = n * m;
	void *p;
	if (m && t / m != n) return 0;
	if (t >= PSI_ALLOC_BIG && (p = big_new(t)) != 0) return p;   /* (a new chunk comes zeroed) */
	return calloc(n, m);
}

void psi_free(void *p)
{
	Big *b = find(p);
	if (!b) { free(p); return; }
	count(-(long)b->commit, -1);
	pg_chunk_close(b->base);
	memset(b, 0, sizeof(*b));
}

void *psi_realloc(void *p, size_t n)
{
	Big *b;
	void *q;
	if (!p) return psi_malloc(n);
	if (!n) { psi_free(p); return 0; }
	b = find(p);
	if (b) {
		unsigned long want = pages(n);
		if (want <= b->max) {
			/* in place: grown (or shrunk) where it is */
			if (want != b->commit && pg_chunk_adjust(b->base, want) == 0) {
				count((long)want - (long)b->commit, 0);
				if (want > b->commit) g_st.grown_in_place++;
				b->commit = want;
			}
			if (n <= b->commit) { b->used = n; return p; }
		}
		/* past its room (or no memory there): a new block and a copy */
		if (!(q = psi_malloc(n))) return 0;
		memcpy(q, p, b->used < n ? b->used : n);
		psi_free(p);
		return q;
	}
	if (n >= PSI_ALLOC_BIG) {
		/* a heap cell grown big: moved into a chunk of its own (its old
		   size is not known here: realloc on the heap first, as ESTLIB
		   would, then the move, which copies only what it must) */
		q = realloc(p, n);
		if (!q) return 0;
		{
			void *c = big_new(n);
			if (!c) return q;
			memcpy(c, q, n);
			free(q);
			return c;
		}
	}
	return realloc(p, n);
}

void psi_alloc_stats(PsiAllocStats *s) { *s = g_st; }
