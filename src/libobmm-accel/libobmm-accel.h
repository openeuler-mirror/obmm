/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 * libobmm-accel is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 *
 * Description: libobmm-accel public api
 * Author: Yuhao Zhang
 * Create: 2026-11-06
 */
#ifndef LIBOBMM_ACCEL_H
#define LIBOBMM_ACCEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NC memory flags for memcpy_nc_mt() */
#define OBMM_ACCEL_NC_SRC   (1u << 0)
#define OBMM_ACCEL_NC_DST   (1u << 1)

/*
 * Multi-threaded NC-aware memory copy for aarch64 OBMM devices.
 *
 * @nthreads: concurrent copy threads, 1..128, including the caller
 *            (1 = the calling thread copies alone). Any other value
 *            returns NULL with errno = EINVAL. Returns dst on success.
 *            Threads are created and joined per call: for frequent
 *            small or medium copies prefer a low count or 1.
 *
 * @flags: OBMM_ACCEL_NC_SRC if src is NC-mapped, OBMM_ACCEL_NC_DST if dst
 *         is NC-mapped. 0 for cacheable-to-cacheable copies. The caller
 *         declares memory attributes; the library does not verify them.
 *         Copies with NC dst require FEAT_LS64: on hardware without it
 *         they return NULL with errno = EOPNOTSUPP (probe at startup
 *         with a small NC-dst copy).
 */
void *memcpy_nc_mt(void *dst, const void *src, size_t n,
                   unsigned int nthreads, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif /* LIBOBMM_ACCEL_H */
