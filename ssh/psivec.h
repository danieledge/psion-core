/* psivec.h - a growing array in a block of its own (Part 2, P2-06).
 *
 * A list that grows by doubling realloc on the heap (PsiMail's folder
 * index, MEM-57) copies itself at each step and leaves the old block as a
 * hole below the new one: a 2,000-message folder peaks at about 560 KB for
 * 280 KB of records. A PsiVec sets aside room for its largest size (address
 * room, no RAM) and takes RAM as it grows, in place (psiwork.h's
 * psi_work_alloc_max and psi_work_grow): no copy, no hole, and all of it
 * goes back at once when freed. Elements never move, so pointers into the
 * array stay good while it grows.
 *
 *   PsiVec v; psi_vec_init(&v, sizeof(Msg), 4000);
 *   Msg *m = (Msg *)psi_vec_push(&v);        0: no memory, or full
 *   ((Msg *)v.data)[i] ... v.n ...
 *   psi_vec_free(&v);
 *
 * It is one of the twelve work blocks (psiwork.h). Off switch: PSI_VEC=0
 * at build time gives the same calls on malloc and realloc, as before (the
 * array may then move as it grows). Default 1: it changes nothing until a
 * program uses it.
 */
#ifndef PSI_VEC_H
#define PSI_VEC_H
#include <string.h>
#include <stdlib.h>
#include "psiwork.h"
#ifndef PSI_VEC
#define PSI_VEC 1
#endif
typedef struct
	{
	char *data;
	int n;              /* elements used */
	int cap;            /* elements there now */
	int max;            /* most it can hold */
	int size;           /* bytes an element */
	} PsiVec;

static __inline__ void psi_vec_init(PsiVec *v, int size, int max)
{
	memset(v, 0, sizeof(*v));
	v->size = size > 0 ? size : 1;
	v->max = max > 0 ? max : 1;
}

/* room for at least n elements: 0, or -1 (no memory, or past max) */
static __inline__ int psi_vec_reserve(PsiVec *v, int n)
{
	int want;
	if (n <= v->cap) return 0;
	if (n > v->max) return -1;
	want = v->cap ? v->cap * 2 : 64;
	if (want < n) want = n;
	if (want > v->max) want = v->max;
#if PSI_VEC
	if (!v->data) {
		if (!(v->data = (char *)psi_work_alloc_max((unsigned long)want * v->size, (unsigned long)v->max * v->size))) return -1;
	} else if (psi_work_grow(v->data, (unsigned long)want * v->size) != 0) return -1;
#else
	{
		char *d = (char *)realloc(v->data, (size_t)want * v->size);
		if (!d) return -1;
		memset(d + (size_t)v->cap * v->size, 0, (size_t)(want - v->cap) * v->size);
		v->data = d;
	}
#endif
	v->cap = want;
	return 0;
}

/* a new element at the end, zeroed; 0 if there is no room */
static __inline__ void *psi_vec_push(PsiVec *v)
{
	char *e;
	if (psi_vec_reserve(v, v->n + 1) != 0) return 0;
	e = v->data + (size_t)v->n * v->size;
	memset(e, 0, v->size);
	v->n++;
	return e;
}

static __inline__ void psi_vec_free(PsiVec *v)
{
#if PSI_VEC
	if (v->data) psi_work_free(v->data);
#else
	free(v->data);
#endif
	v->data = 0; v->n = v->cap = 0;
}
#endif
