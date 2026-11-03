/*
 * Copyright (c) 2025 Huawei Technologies Co., Ltd. All rights reserved.
 * libobmm is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 *
 * Description: libobmm main api
 * Author: Gao Chao
 * Create: 2025-10-28
 */

#ifndef _OBMM_API_H
#define _OBMM_API_H

#include <stddef.h>
#include <stdint.h>
#include <ub/obmm.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MAX_NUMA_NODES 16
#define OBMM_INVALID_MEMID 0

typedef uint64_t mem_id;

struct obmm_mem_desc {
    uint64_t addr;
    uint64_t length;
    /* 128bit eid, ordered by little-endian */
    uint8_t seid[16];
    uint8_t deid[16];
    uint32_t tokenid;
    uint32_t scna;
    uint32_t dcna;
    uint16_t priv_len;
    uint8_t  priv[];
};

struct obmm_preimport_info {
    uint64_t pa;
    uint64_t length;
    int base_dist;
    int numa_id;
    uint8_t seid[16];
    uint8_t deid[16];
    uint32_t scna;
    uint32_t dcna;
    /* mar_id, etc */
    uint16_t priv_len;
    uint8_t priv[];
};

mem_id obmm_export(const size_t length[OBMM_MAX_LOCAL_NUMA_NODES], unsigned long flags, struct obmm_mem_desc *desc);
int obmm_unexport(mem_id id, unsigned long flags);


int obmm_preimport(struct obmm_preimport_info *preimport_info, unsigned long flags);
int obmm_unpreimport(const struct obmm_preimport_info *preimport_info, unsigned long flags);

/* Export the specified va range of the process pid out of localhost.
 * Due to hardware limitations, during the export process, the corresponding
 * physical memory for the VA (virtual address) range will be allocated and
 * pinned, and the related pages will be checked to see if they are 2M pages.
 *
 * pid: the ID of the process in which va range are to exported. If pid is 0,
 * export va range of the calling process.
 **/
mem_id obmm_export_useraddr(int pid, void* va, size_t length, unsigned long flags, struct obmm_mem_desc *desc);

mem_id obmm_import(const struct obmm_mem_desc *desc, unsigned long flags, int base_dist, int *numa);
int obmm_unimport(mem_id id, unsigned long flags);

/*
 * Set the ownership (reader, writer, none) of a range of OBMM virtual address space.
 * @fd: The file descriptor of an OBMM memory device.
 * @start: The start virutal address.
 * @end: The end virtual addreses.
 * @prot: The ownership expressed as memory protection bits (PROT_NONE, PROT_READ, PROT_WRITE).
 *        NOTE: PROT_WRITE implies PROT_READ.
 */
int obmm_set_ownership(int fd, void *start, void *end, int prot);

/* debug interface */
int obmm_query_memid_by_pa(unsigned long pa, mem_id *id, unsigned long *offset);
int obmm_query_pa_by_memid(mem_id id, unsigned long offset, unsigned long *pa);

/*
 * Online backend switch (obmm_switch(4)): swap the memory backend of an
 * existing memory device without changing its mem_id, device node or the
 * mappings its users hold.
 */

struct obmm_switch_import_target {
    /* 2MB-aligned base PA of the new remote memory; its length must equal
     * the device size and it must be a plain direct-import class */
    uint64_t pa;
    uint32_t scna;
    uint32_t dcna;
    uint8_t seid[16];
    uint8_t deid[16];
    int base_dist;
};

struct obmm_switch_export_target {
    /* per local-node segment sizes; only the first node_count entries
     * are used, trailing entries are ignored */
    size_t size[OBMM_MAX_LOCAL_NUMA_NODES];
    int node_count;
    /* subset of OBMM_EXPORT_FLAG_FAST */
    unsigned long export_flags;
};

/*
 * Begin a switch to a new import target. On success (returns 0) the
 * device is in the switch gap; complete it with obmm_switch_end().
 * flags: OBMM_SWITCH_FLAG_KEEP_OLD_ALIVE keeps the old backend alive
 * through the gap (enables OBMM_MMAP_FLAG_SWITCH_OLD mappings and the
 * abort rollback).
 * priv: new private metadata, active only after the switch commits.
 */
int obmm_switch_begin_import(mem_id id, unsigned long flags,
                             const struct obmm_switch_import_target *target,
                             const void *priv, uint16_t priv_len);

/*
 * Begin a switch to a new local export-pool target. desc doubles as the
 * grantee identity input (deid/seid, resolved as in obmm_export()) and
 * the output holder: on success desc->tokenid and desc->addr (uba)
 * carry the new UMMU credentials. They must not be distributed to a
 * remote importer before the switch is committed by
 * obmm_switch_end(id, 1). desc->priv/priv_len are ignored here; the new
 * backend's private data comes solely from priv/priv_len.
 */
int obmm_switch_begin_export(mem_id id, unsigned long flags,
                             const struct obmm_switch_export_target *target,
                             struct obmm_mem_desc *desc,
                             const void *priv, uint16_t priv_len);

/*
 * Close the switch gap: success != 0 commits the swap and destroys the
 * old backend; success == 0 aborts, restoring the old backend when it
 * was kept alive. A transient -1/errno=EAGAIN (racing the finisher's
 * critical section) is retry policy owned by the CALLER: retry until it
 * converges to 0 or -1/errno=ESTALE (the gap already ended - duplicate
 * end or the timeout backstop fired).
 */
int obmm_switch_end(mem_id id, int success);

#if defined(__cplusplus)
}
#endif

#endif
