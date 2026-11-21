/* psiwork.h - work memory: the big buffers an engine needs only while it
 * is connected or syncing, taken when the work starts and given back to the
 * Psion when it ends. On the Psion each is a chunk of its own (psiglue.cpp,
 * pg_work_alloc), so its pages go back at once; a static buffer costs its
 * RAM for as long as the engine runs, and psisync.exe and psimail.exe run
 * all day. On a PC (the host tests) it is calloc and free.
 *
 *   static struct work { char line[1200]; ... } *g_w;
 *   if (!g_w && !(g_w = psi_work_alloc(sizeof(*g_w)))) -> "Not enough memory"
 *   ...
 *   psi_work_free(g_w); g_w = 0;
 *
 * The memory comes zeroed and is wiped when freed. At most 12 at a time.
 * psi_work_trim() gives back what the engine's heap no longer uses.
 *
 * (21 November 2026) A block that grows in place: psi_work_alloc_max(n,
 * max) sets aside room for max and takes n of it; psi_work_grow(p, n)
 * takes more (0, or -1 and the block as it was). The block never moves.
 * psi_mem_level(): how short of memory the Psion is (psimemlvl.h: 0 plenty,
 * 1 trim, 2 sleep, 3 very low), for an engine to look at where it idles.
 */
#ifndef PSI_WORK_H
#define PSI_WORK_H
#if defined(__EPOC32__) || defined(__SYMBIAN32__)
#ifdef __cplusplus
extern "C" {
#endif
void *pg_work_alloc(unsigned long bytes);
void pg_work_free(void *p);
void pg_work_trim(void);
void *pg_work_alloc_max(unsigned long commit, unsigned long max);
int pg_work_grow(void *p, unsigned long bytes);
int pg_mem_level(void);
#ifdef __cplusplus
}
#endif
#define psi_work_alloc(n) pg_work_alloc(n)
#define psi_work_free(p)  pg_work_free(p)
#define psi_work_trim()   pg_work_trim()
#define psi_work_alloc_max(n, m) pg_work_alloc_max((n), (m))
#define psi_work_grow(p, n) pg_work_grow((p), (n))
#define psi_mem_level()   pg_mem_level()
#else
#include <stdlib.h>
#define psi_work_alloc(n) calloc(1, (n))
#define psi_work_free(p)  free(p)
#define psi_work_trim()   ((void)0)
#define psi_work_alloc_max(n, m) calloc(1, (m))
#define psi_work_grow(p, n) ((p) ? 0 : -1)
#define psi_mem_level()   0
#endif
#endif
