/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 * libobmm-accel is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 *
 * Description: memcpy_nc_mt dispatch, LS64 probe and multi-thread chunking
 * Author: Yuhao Zhang
 * Create: 2026-11-06
 */
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <libobmm-accel.h>

#define MAX_THREADS 128

/*
 * ---- LS64 probe: MRS ID_AA64ISAR1_EL1 (S3_0_C0_C6_1), bits[63:60] ----
 *
 * The LS64 field lives in ISAR1, NOT ISAR0. ISAR0 bits[63:60] is RNDR
 * (FEAT_RNG). Reading the wrong register misidentifies RNG support as
 * LS64, causing SIGILL on real hardware when ST64B/LD64B are executed.
 *
 * The MRS instruction is wrapped in a SIGILL handler as insurance against
 * kernels that do not emulate EL0 ID-register reads.
 */
static sigjmp_buf g_probe_jmp;
static void probe_sigill(int sig) { (void)sig; siglongjmp(g_probe_jmp, 1); }

static int probe_ls64_once(void)
{
    unsigned long isar1 = 0;
    struct sigaction old_sa, sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = probe_sigill;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGILL, &sa, &old_sa) != 0) return 0;
    if (sigsetjmp(g_probe_jmp, 1) == 0) {
        __asm__ volatile("mrs %0, S3_0_C0_C6_1" : "=r"(isar1));
        sigaction(SIGILL, &old_sa, NULL);
        return (isar1 >> 60) != 0;              /* LS64 field != 0 */
    }
    sigaction(SIGILL, &old_sa, NULL);
    return 0;  /* MRS trapped: kernel does not emulate, treat as absent */
}

/* pthread_once serializes the probe: concurrent first calls would
 * interleave sigaction install/restore and could permanently leak
 * probe_sigill as the process-wide SIGILL handler. */
static int g_ls64_cached;
static void probe_init(void) { g_ls64_cached = probe_ls64_once(); }

static int have_ls64(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, probe_init);
    return g_ls64_cached;
}

static void copy_generic(void *dst, const void *src, size_t n)
{
    memcpy(dst, src, n);
}

/* Assembly kernel in memcpy_nc.S */
extern void nc_copy_st64b(void *dst, const void *src, size_t n);

typedef void (*copy_fn)(void *, const void *, size_t);

/*
 * ---- Staged dual-NC: CC staging buffer separates read/write phases ----
 *
 * Direct dual-NC copy is slow because NC transactions carry a W->R
 * ordering penalty: a write blocks subsequent reads even to different
 * addresses. Staging through CC memory separates the phases so the
 * penalty is paid only once per staging block:
 *
 *   Phase 1: read NC src -> CC staging (libc NEON loads, pipelined)
 *   Phase 2: read CC staging -> write NC dst (ST64B 64B-atomic stores)
 */
#define NC_STAGE_SIZE  (64 << 10)   /* 64KB CC staging block */
static void nc_copy_staged(void *dst, const void *src, size_t n)
{
    static __thread unsigned char staging[NC_STAGE_SIZE]
        __attribute__((aligned(64)));
    while (n > 0) {
        size_t chunk = n < NC_STAGE_SIZE ? n : (size_t)NC_STAGE_SIZE;
        memcpy(staging, src, chunk);
        nc_copy_st64b(dst, staging, chunk);
        src = (const char *)src + chunk;
        dst = (char *)dst + chunk;
        n -= chunk;
    }
}

/* ---- Path selection ----
 *   dst CC   -> libc memcpy (includes NC->CC: reads dominate)
 *   CC -> NC -> ST64B kernel (no NC reads in flight)
 *   NC -> NC -> staged (CC staging avoids the global W->R
 *              ordering penalty between NC reads and writes)
 *
 * dst-NC copies require FEAT_LS64; the entry point rejects them
 * with EOPNOTSUPP instead of silently degrading to slow libc writes.
 */
static copy_fn select_copy(unsigned flags)
{
    if (!(flags & OBMM_ACCEL_NC_DST)) return copy_generic;
    if (flags & OBMM_ACCEL_NC_SRC)    return nc_copy_staged;
    return nc_copy_st64b;
}

/*
 * ---- Multi-threaded chunking ----
 * Splits the copy into 64B-aligned chunks, one per thread. The main
 * thread handles chunk 0 while workers handle chunks 1..N-1. If thread
 * creation fails at worker i, the main thread serially copies chunks
 * i+1..N-1 after joining all successfully created workers.
 *
 * Workers are created unpinned: overriding the caller's scheduling
 * placement (cpusets, NUMA policy) is outside this library's contract,
 * and pinning prevents load balancing on shared machines.
 */
struct chunk {
    void *dst; const void *src; size_t len; copy_fn fn;
};

static void *chunk_worker(void *arg)
{
    struct chunk *c = arg;
    c->fn(c->dst, c->src, c->len);
    return NULL;
}

static void run_mt(void *dst, const void *src, size_t n,
                   unsigned nthreads, copy_fn fn)
{
    size_t chunk = (n / nthreads) & ~(size_t)63;
    if (chunk == 0) { fn(dst, src, n); return; }   /* serial fallback */

    struct chunk work[MAX_THREADS];
    pthread_t tid[MAX_THREADS];

    for (unsigned i = 0; i < nthreads; i++) {
        work[i].dst = (char *)dst + (size_t)i * chunk;
        work[i].src = (const char *)src + (size_t)i * chunk;
        work[i].len = chunk;
        work[i].fn  = fn;
    }
    work[nthreads - 1].len = n - (size_t)(nthreads - 1) * chunk;

    unsigned created = 0;
    for (unsigned i = 1; i < nthreads; i++) {
        if (pthread_create(&tid[i], NULL, chunk_worker, &work[i]) != 0)
            break;  /* creation failed: main thread will handle i..nthreads-1 */
        created = i;
    }
    work[0].fn(work[0].dst, work[0].src, work[0].len);  /* main does chunk 0 */
    /* glibc join of a valid joinable tid cannot realistically fail;
     * there is nothing better to do if it does. */
    for (unsigned i = 1; i <= created; i++)
        pthread_join(tid[i], NULL);
    for (unsigned i = created + 1; i < nthreads; i++)
        work[i].fn(work[i].dst, work[i].src, work[i].len);
}

__attribute__((visibility("default")))
void *memcpy_nc_mt(void *dst, const void *src, size_t n,
                   unsigned int nthreads, unsigned int flags)
{
    if (nthreads == 0 || nthreads > MAX_THREADS) {
        errno = EINVAL;
        return NULL;
    }
    if ((flags & OBMM_ACCEL_NC_DST) && !have_ls64()) {
        errno = EOPNOTSUPP;   /* no silent degradation */
        return NULL;
    }
    if (n == 0) return dst;

    copy_fn fn = select_copy(flags);
    if (nthreads <= 1)
        fn(dst, src, n);
    else
        run_mt(dst, src, n, nthreads, fn);
    return dst;
}
