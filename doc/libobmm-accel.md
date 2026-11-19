<!-- Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved. -->

# libobmm-accel: NC 内存加速拷贝库

libobmm-accel 面向 aarch64 OBMM 设备，提供感知 NC（Non-Cacheable）内存属性的多线程拷贝接口。当目的端为 NC 映射时，自动选择针对互连链路优化的拷贝内核；其余情形直接使用 libc memcpy。

## OBMM API 总览

| API          | 功能                   |
| ------------ | ---------------------- |
| memcpy_nc_mt | 多线程 NC 感知内存拷贝 |

## 内核选择

| 条件                          | 内核   |
| ----------------------------- | ------ |
| dst 为可缓存（含 NC -> CC）   | generic |
| CC -> NC                      | st64b  |
| NC -> NC                      | staged |

dst 为 NC 的拷贝要求硬件支持 FEAT_LS64，否则返回 NULL 并置 `errno = EOPNOTSUPP`。

## 线程模型

- `nthreads` 为并发拷贝线程数，1..128，**含调用线程**（1 表示调用线程独立完成拷贝）。
- 线程按次创建并回收：频繁的中小尺寸拷贝建议低线程数或单线程。

## 返回值 RETURN VALUE

成功时返回 `dst`；失败时返回 NULL，并通过 `errno` 指示错误。

## 错误 ERRORS

- `EINVAL`：`nthreads` 不在 1..128 范围内。
- `EOPNOTSUPP`：dst 声明为 NC，但硬件不支持 FEAT_LS64。

## 示例

```c
#include <libobmm-accel.h>

	/* 4 线程把远端 NC 内存拷贝到本地 NC 设备 */
	void *dst = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, dst_fd, 0);
	void *src = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, src_fd, 0);
	if (memcpy_nc_mt(dst, src, n, 4,
	                 OBMM_ACCEL_NC_SRC | OBMM_ACCEL_NC_DST) != dst) {
		perror("memcpy_nc_mt");
		return -1;
	}
```

## 参见 SEE ALSO

libobmm(3), obmm(4), obmm_shmdev(4)
