/* psialloctest.c - psialloc.c (P2-05) and psivec.h (P2-06) on a PC. The
 * chunks are malloc'd blocks of their whole room here, so growing in place
 * can be seen (the address stays) and a failure can be made. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psialloc.h"
#include "psivec.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)

static int chunks, adjusts, refuse_new, refuse_adjust;
void *pg_chunk_new(unsigned long n, unsigned long max)
{
	void *p;
	if (refuse_new || n > max) return 0;
	if ((p = calloc(1, max))) chunks++;
	return p;
}
int pg_chunk_adjust(void *base, unsigned long n) { (void)base; (void)n; if (refuse_adjust) return -1; adjusts++; return 0; }
void pg_chunk_close(void *base) { chunks--; free(base); }

int main(void)
{
	PsiAllocStats st;
	char *a, *b, *c, *d;
	int i;
	/* small: the heap; big: a chunk */
	a = psi_malloc(100);
	b = psi_malloc(40000);
	CHECK(a && b && chunks == 1);
	psi_alloc_stats(&st);
	CHECK(st.blocks == 1 && st.bytes == 40960);
	memset(b, 7, 40000);
	/* grown within its room: the same address, nothing copied */
	c = psi_realloc(b, 700000);
	CHECK(c == b && c[39999] == 7 && adjusts == 1);
	psi_alloc_stats(&st);
	CHECK(st.bytes == 700416 && st.grown_in_place == 1);
	/* past its room (1 MB): a new chunk, the old one gone */
	c[0] = 9;
	d = psi_realloc(c, 1500000);
	CHECK(d && d != c && d[0] == 9 && d[39999] == 7 && chunks == 1);
	/* shrunk in place */
	CHECK(psi_realloc(d, 50000) == d);
	psi_free(d);
	psi_alloc_stats(&st);
	CHECK(chunks == 0 && st.blocks == 0 && st.bytes == 0 && st.peak_blocks == 2);   /* (both, for the move) */
	/* a heap cell grown big moves into a chunk */
	a = psi_realloc(a, 64 * 1024);
	CHECK(a && chunks == 1);
	psi_free(a);
	/* calloc: zero, and the overflow refused */
	a = psi_calloc(1000, 100);
	for (i = 0; i < 100000 && !a[i]; i++) ;
	CHECK(i == 100000 && chunks == 1);
	CHECK(psi_calloc((size_t)-1 / 2, 4) == 0);
	psi_free(a);
	/* no chunk to be had: the heap, as before */
	refuse_new = 1;
	a = psi_malloc(100000);
	CHECK(a && chunks == 0);
	a = psi_realloc(a, 200000);
	CHECK(a != 0);
	psi_free(a);
	refuse_new = 0;
	/* no room to grow in place: moved */
	a = psi_malloc(40000); a[5] = 3;
	refuse_adjust = 1;
	b = psi_realloc(a, 90000);
	CHECK(b && b != a && b[5] == 3 && chunks == 1);
	refuse_adjust = 0;
	psi_free(b);
	/* the slots full: the heap */
	{
		char *p[PSI_ALLOC_SLOTS + 1];
		for (i = 0; i <= PSI_ALLOC_SLOTS; i++) p[i] = psi_malloc(32768);
		CHECK(chunks == PSI_ALLOC_SLOTS && p[PSI_ALLOC_SLOTS] != 0);
		for (i = 0; i <= PSI_ALLOC_SLOTS; i++) psi_free(p[i]);
		CHECK(chunks == 0);
	}
	psi_free(0);
	/* psi_vec: grows in place to its most, then refuses */
	{
		PsiVec v;
		int *e = 0, *first;
		psi_vec_init(&v, sizeof(int), 1000);
		first = (int *)psi_vec_push(&v);
		*first = 42;
		for (i = 1; i < 1000; i++) { e = (int *)psi_vec_push(&v); if (!e) break; *e = i; }
		CHECK(i == 1000 && v.n == 1000 && ((int *)v.data)[0] == 42 && ((int *)v.data)[999] == 999);
		CHECK((int *)v.data == first);           /* (never moved) */
		CHECK(psi_vec_push(&v) == 0);
		psi_vec_free(&v);
		CHECK(v.data == 0 && v.n == 0);
	}
	printf("psialloctest: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
