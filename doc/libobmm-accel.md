<!-- Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved. -->

# libobmm-accel: NC 内存加速拷贝库

libobmm-accel 面向 aarch64 OBMM 设备，提供感知 NC（Non-Cacheable）内存属性的多线程拷贝接口。当目的端为 NC 映射时，自动选择针对互连链路优化的拷贝内核；其余情形直接使用 libc memcpy。

## libobmm-accel API 总览

| API          | 功能                   |
| ------------ | ---------------------- |
| memcpy_nc_mt | 多线程 NC 感知内存拷贝 |

`flags` 是调用方对内存属性的**自行声明**，库不会核实声明与真实映射是否一致：`OBMM_ACCEL_NC_SRC` 声明 `src` 为 NC 映射，`OBMM_ACCEL_NC_DST` 声明 `dst` 为 NC 映射，两端均可缓存时传 0。声明与实际映射不符会选错拷贝内核（见下文“内核选择”）：目的端实为 NC 却未声明时退化为 libc 写路径，性能显著下降；目的端并非 NC 却声明为 NC 时，即使实际拷贝可行，也会因要求 FEAT_LS64 而在不支持的硬件上返回 `EOPNOTSUPP`。因此调用方必须保证 `flags` 与 `mmap` 的实际属性一致。

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
- 当 `n < nthreads * 64` 时，每个线程分不到 64 字节的整块（分块长度为 0），拷贝自动退化为由调用线程串行完成；此尺寸下提高 `nthreads` 不会带来收益。

## 返回值 RETURN VALUE

成功时返回 `dst`；失败时返回 NULL，并通过 `errno` 指示错误。

## 错误 ERRORS

- `EINVAL`：`nthreads` 不在 1..128 范围内。
- `EOPNOTSUPP`：dst 声明为 NC，但硬件不支持 FEAT_LS64。

## 编译与链接

头文件为 `libobmm-accel.h`，构建产物为 `libobmm-accel.so`，链接时使用 `-lobmm-accel`：

```sh
cc -o demo demo.c -lobmm-accel
```

安装后头文件位于 `<prefix>/include`，库位于 `<prefix>/lib64`；非标准安装路径需通过 `-I`、`-L` 指定，并在运行时设置 `LD_LIBRARY_PATH` 或 rpath。库自身已链接 pthread，调用方无需额外链接线程库。

## 示例

以下为代码片段（伪代码）：`dst_fd`、`src_fd` 需调用方自行打开并声明，分别对应 `/dev/obmm_shmdev${mem_id}`。示例假设两段内存均以 NC 属性映射，即设备 fd 需以 `O_SYNC` 打开且设备承诺 NONCACHEABLE 能力；设备节点的打开、映射、销毁（先 `munmap` 再 `close`）流程参见 obmm_shmdev(4)。

```c
#include <stddef.h>
#include <stdio.h>
#include <sys/mman.h>
#include <libobmm-accel.h>

/* dst_fd、src_fd 由调用方打开，此处作为入参传入 */
int copy_nc(int dst_fd, int src_fd, size_t n)
{
	void *dst = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, dst_fd, 0);
	if (dst == MAP_FAILED) {
		perror("mmap dst");
		return -1;
	}

	void *src = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, src_fd, 0);
	if (src == MAP_FAILED) {
		perror("mmap src");
		munmap(dst, n);
		return -1;
	}

	/* 4 线程把远端 NC 内存拷贝到本地 NC 设备 */
	if (memcpy_nc_mt(dst, src, n, 4,
	                 OBMM_ACCEL_NC_SRC | OBMM_ACCEL_NC_DST) != dst) {
		perror("memcpy_nc_mt");
		munmap(src, n);
		munmap(dst, n);
		return -1;
	}

	/* 使用完毕后先解除映射，再由调用方关闭设备 fd */
	munmap(src, n);
	munmap(dst, n);
	return 0;
}
```

## 参见 SEE ALSO

libobmm(3), obmm(4), obmm_shmdev(4)
