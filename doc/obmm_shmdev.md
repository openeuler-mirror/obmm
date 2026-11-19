<!-- Copyright (c) 2025-2026 Huawei Technologies Co., Ltd. All rights reserved. -->

# obmm_shmdev: OBMM 内存设备

obmm_shmdev 是 OBMM 为管理远端内存创建的字符设备。若一段 OBMM 内存的 memid 为 `${id}`，其设备所在的路径为`/dev/obmm_shmdev${id}` 。

obmm_shmdev 设备为用户态库（libobmm）提供了配置内存设备的 ioctl 接口，为一般用户态应用提供了映射该 OBMM 内存的接口。

obmm_shmdev 可以为 export 设备或 import 设备。其详细属性可以通过 obmm_shmdev_sysfs(5) 查看。

## 创建与销毁

应用应通过 obmm_export(3) 或 obmm_import(3) 创建 OBMM 内存设备。通过 obmm_unexport(3) 或 obmm_unimport(3) 销毁内存设备。

## 映射访问

obmm_shmdev 仅支持使用 POSIX 接口进行映射。一次标准的映射使用过程应包括对该设备的 open(2)、mmap(2)、load/store访问、munmap(2)、close(2)。

### open

```C
int open(const char *pathname, int flags, ...
				  /* mode_t mode */ );
```

在操作 obmm_shmdev 时，flags 可以影响 shmdev 的下列行为：

* 操作权限：O_RDONLY 代表请求只读权限，O_WRONLY 代表请求只写权限（当前无法基于此flag做映射），O_RDWR 代表请求读写权限，三者配置仅配置一项。
* 同步模式：在 flags 中对 O_SYNC 置位时，使用类似同步IO的语义，远端内存将被以 non-cacheable（NC）方式映射；O_SYNC 未置位时，远端内存将被 cacheable（CC）方式映射。cacheability 在 open(2) 阶段即按内存设备的后端能力（promised_cap）进行校验：请求 CC 而设备未承诺 CACHEABLE 能力、或请求 NC 而设备未承诺 NONCACHEABLE 能力时，open(2) 直接返回 -EPERM（即使调用方只打算执行 ioctl、不进行 mmap 也会失败）。cacheability 是设备级属性，在设备仍有映射期间保持冻结，CC 与 NC 不能同时并存；最后一个映射解除后模式重置、可重新选择（仍受后端能力校验）。

### mmap

```C
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
```

在操作 obmm_shmdev 时，各参数还具有如下含义：

* addr：用户态应用期望的虚拟地址，默认为 hint（内核不保证能从分配到该地址）。一般可置为 NULL。
* length：映射内存的长度，此参数必须按页大小对齐。
* prot：映射内存的访问权限。PROT_NONE表示空权限（无映射、无缓存），PROT_READ表示只读权限，PROT_WRITE 和 PROT_READ | PROT_WRITE 表示读写权限。
* flags：用户需配置 MAP_SHARED flag，不应配置 MAP_ANONYMOUS flag。**不允许配置 MAP_PRIVATE**。OBMM 内存为共享语义，进程的写入对其它进程都是可见的，MAP_PRIVATE 的写时复制（copy-on-write）语义不适用。
* fd：需为 open(2) 打开 obmm_shmdev 设备所创建的文件描述符
* offset：映射域段的起始偏移量，此参数必须按页大小对齐。

此外，如果fd在打开时指定了O_SYNC的flag，那么该次mmap会以NC的方式进行映射。此时，prot为PROT_READ或PROT_WRITE（PROT_READ | PROT_WRITE）时最终会呈现为 NC + 对应访问权限；PROT_NONE 始终是无权限（PAGE_NONE），不携带 NC 属性。NC 映射的权限必须在 mmap 时通过 prot 一次性指定，之后不允许用户进行一致性或权限变更；且不允许与CC映射的方式混用（混用时 mmap 返回 -EPERM）。

mmap 失败时，返回 MAP_FAILED（-1）；成功时，会返回一个虚拟地址。应用可基于该虚拟地址进行 load, store 访问。用户所做的访问必须与当前该段内存的权限一致。如果不一致，可能产生 bus error。

OBMM支持部分映射，不同进程可以使用不同的页范围进行映射。关于VMA拆分、部分解除映射等行为规则，详见下方"VMA 管理"章节。

在访问过程中，用户可通过 obmm_set_ownership(3) 切换权限，实现细粒度的数据跨机共享。注意，NC映射不能使用obmm_set_ownership(3)切换权限。

映射时，应用还需要满足以下限制：

* obmm_shmdev 对应的内存具备 allow_mmap 属性：allow_mmap 属性由创建 OBMM 内存设备的 flags（ *OBMM_EXPORT_FLAG_ALLOW_MMAP* 或 *OBMM_IMPORT_FLAG_ALLOW_MMAP*） 配置。
* 每个 cacheable 的 ownership 条目（PAGE 粒度映射为页，PMD 粒度映射为 2MB）最多允许 2^16-1 个访问者进行映射，空权限映射不受此限制。写权限访问者会同时占用一个写名额和一个读名额，因此写者达到上限时读者名额也已耗尽，二者不是相互独立的配额；没有 ownership 概念的 non-cacheable 页不受前述限制。
* 进程实际的映射数上限还受内核文件描述符上限、进程映射数量上限等配置的制约，不仅由 OBMM 的状态上限决定。

当 obmm_shmdev 设备被打开或 mmap 时，内存设备均无法销毁。销毁内存设备时，用户应先解除映射，关闭文件描述符。

#### PMD映射
obmm_shmdev字符设备mmap提供PMD映射方式，通过增加映射粒度来降低Page Table Walk耗时，降低TLB Miss率，从而降低平均访存时延。

##### 使用方式
`#define OBMM_MMAP_FLAG_HUGETLB_PMD (1UL << 63)`
在mmap时，通过指定偏移为(`OBMM_MMAP_FLAG_HUGETLB_PMD` | offset)实现指定PMD映射的效果。

mmap其余参数描述有所变化：
* addr：建议置为 NULL，由内核分配 PMD_SIZE 对齐的虚拟地址；若传入非 NULL 的 hint，调用方必须自行保证该地址为 PMD_SIZE 对齐，否则 mmap 返回 -EINVAL（内核仅在自行选址时做 PMD 对齐，hint 被直接采用时不会调整）。
* length：映射内存的长度，此参数必须按PMD_SIZE大小对齐。
* offset：去掉标志位后的数据偏移必须按 PMD_SIZE 对齐，否则 mmap 返回 -EINVAL。

##### 使用约束
1. 一个obmm_shmdev不允许混合不同粒度映射。设备首次映射时会记录映射粒度。
2. 通过PMD方式映射的虚拟地址支持使用`obmm_set_ownership`接口维护一致性。此时每次调用的 start/end 也必须按 PMD_SIZE 对齐，否则返回 -EINVAL。
3. obmm_shmdev根据VMA的保护属性（prot）判断vma access的访问权限。PROT_WRITE-only 映射同样支持 ptrace/GDB 访问：读访问要求 vm_flags 中 VM_READ 或 VM_WRITE 置位，写访问要求 VM_WRITE 置位。
4. 缺页重建与透明大页（THP）的运行时配置无关。映射一律在缺页时安装：驱动按 mmap 粒度（mmap_granu）、VMA 的 PMD 对齐性以及后端物理地址连续性决定安装 PMD 叶子还是 PTE，不读取 `/sys/kernel/mm/transparent_hugepage/enabled` 的状态，也不设置或依赖 VM_HUGEPAGE；`never` 或应用显式 `MADV_NOHUGEPAGE` 都不会使重建退化为 4K 页。透明大页仅是内核编译期依赖（CONFIG_TRANSPARENT_HUGEPAGE）。

#### 切换间隙映射（SWITCH_OLD / SWITCH_NEW）

内存设备在线后端切换（见obmm_switch(3)）的间隙期间，mmap(2) 可通过offset高位标志同时映射新旧两个后端，用于数据迁移：

```c
#define OBMM_MMAP_FLAG_SWITCH_OLD  (1UL << 61)   /* 映射旧后端 */
#define OBMM_MMAP_FLAG_SWITCH_NEW  (1UL << 60)   /* 映射新（当前）后端 */
```

使用约束：
1. 两个标志仅在切换间隙（switch_state 为 AWAIT_READY）内合法，其余状态返回 -EINVAL；二者互斥。
2. SWITCH_OLD 要求切换以 OBMM_SWITCH_FLAG_KEEP_OLD_ALIVE 发起。
3. 两个标志可与 OBMM_MMAP_FLAG_HUGETLB_PMD 组合使用。
4. 间隙内的普通映射（不带上述标志）允许创建，其页表安装会延迟到切换完成之后。
5. 对 OBMM 映射（包括切换间隙映射）执行 MADV_DONTNEED 等清页操作会返回 -EINVAL（VMA 带 VM_PFNMAP/VM_LOCKED），不会清页，也不会改变映射语义。SWITCH_OLD/SWITCH_NEW 的作用域仅持续到当前切换间隙结束：switch_end 之后重新缺页将落到存活的后端，而不是当初映射的那个后端。

切换期间 obmm_set_ownership(3) 返回 -EAGAIN；销毁接口返回 -EBUSY。完整的切换协议、errno 与数据迁移样例见 obmm_switch(3)。

### VMA 管理

OBMM 映射的 VMA 具有以下行为规则：

* **VMA 拆分（split）**：允许对 OBMM 映射的 VMA 进行拆分操作（如通过 munmap 在映射中间挖洞），拆分后各子 VMA 独立管理。拆分地址必须按映射粒度对齐：PAGE 粒度映射按 PAGE_SIZE 对齐，PMD 粒度映射按 PMD_SIZE 对齐；未对齐的拆分（以及对应的 munmap）返回 -EINVAL。
* **部分解除映射（partial unmap）**：允许对 OBMM 映射的 VMA 进行部分 munmap，解除映射后剩余区域仍可正常访问。与拆分相同，解除边界必须按映射粒度对齐（PAGE 粒度按 PAGE_SIZE，PMD 粒度按 PMD_SIZE），否则 munmap 返回 -EINVAL。
* **VMA 合并（merge）**：不允许 OBMM 映射的 VMA 与其他 VMA（包括其他 OBMM 映射的 VMA）进行合并。
* **mprotect**：不允许对 OBMM 映射的 VMA 调用 mprotect(2) 修改保护属性。
* **mremap**：不允许对 OBMM 映射的 VMA 调用 mremap(2) 进行重映射。
* **VMA 属性同步**：VMA 的保护属性（prot）与实际的读写权限保持一致。当通过 obmm_set_ownership(3) 将权限设为 PROT_READ 时，对应 VMA 的 prot 属性也会同步更新为 PROT_READ；设为 PROT_WRITE（或 PROT_READ | PROT_WRITE）时同步为可读写；设为 PROT_NONE 时同步为无权限。

## 样例

以下函数展示了应用通过 POSIX 标准接口访问 OBMM 内存设备的过程。函数使用 open(2), mmap(2) 映射内存设备。使用 cacheable 内存时，需要使用 obmm_set_ownership(3) 接口维护 libobmm(3) 中描述的一致性模型。访问结束后，函数使用 munmap(2) 和 close(2) 解除了对设备的映射和占用。

```c
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <libobmm.h>

#define SZ_2M	(1UL << 21)

#define MAX_OBMM_MEMDEV_PATH	128
int memdev_demo(mem_id id, size_t size)
{
	int ret, fd, *value;
	void *ptr;
	char memdev_path[128];

	ret = snprintf(memdev_path, sizeof(memdev_path), "/dev/obmm_shmdev%lu", id);
	if (ret < 0 || ret >= (int)sizeof(memdev_path)) {
		fprintf(stderr, "Failed to construct OBMM memdev path.\n");
		exit(EXIT_FAILURE);
	}

	fd = open(memdev_path, O_RDWR);
	if (fd == -1) {
		perror("open() failed on OBMM memdev.\n");
		exit(EXIT_FAILURE);
	}

	/* Map the memory device with NONE access right. */
	ptr = mmap(NULL, size, PROT_NONE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		perror("mmap() failed on OBMM memdev.\n");
		exit(EXIT_FAILURE);
	}
	/* Map char device succeeded. */

	/* Do your work here. Here is a simple non-atomic increment example. */
	ret = obmm_set_ownership(fd, ptr, (void*)((uintptr_t)ptr + SZ_2M), PROT_WRITE);
	if (ret == -1) {
		perror("obmm_set_ownership() failed.\n");
		exit(EXIT_FAILURE);
	}
	value = (int*)ptr;
	*value = *value + 1;
	ret = obmm_set_ownership(fd, ptr, (void*)((uintptr_t)ptr + SZ_2M), PROT_NONE);
	if (ret == -1) {
		perror("obmm_set_ownership() failed.\n");
		exit(EXIT_FAILURE);
	}

	/* Cleanup.*/
	ret = munmap(ptr, size);
	if (ret == -1) {
		perror("munmap() failed on OBMM memdev pointer.\n");
		exit(EXIT_FAILURE);
	}

	ret = close(fd);
	if (ret == -1) {
		perror("close() failed on OBMM memdev.\n");
		exit(EXIT_FAILURE);
	}

	return 0;
}
```

以下函数展示了应用通过 POSIX 标准接口访问 OBMM 内存设备的过程。函数使用 open(2), mmap(2) 以PMD方式映射non_cachable属性内存设备，访问结束后，函数使用 munmap(2) 和 close(2) 解除了对设备的映射和占用。

```c
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <libobmm.h>

#define SZ_2M	(1UL << 21)

#define MAX_OBMM_MEMDEV_PATH	128
int memdev_demo(mem_id id, size_t size)
{
	int ret, fd, *value;
	void *ptr;
	char memdev_path[128];

	ret = snprintf(memdev_path, sizeof(memdev_path), "/dev/obmm_shmdev%lu", id);
	if (ret < 0 || ret >= (int)sizeof(memdev_path)) {
		fprintf(stderr, "Failed to construct OBMM memdev path.\n");
		exit(EXIT_FAILURE);
	}

	fd = open(memdev_path, O_RDWR|O_SYNC);
	if (fd == -1) {
		perror("open() failed on OBMM memdev.\n");
		exit(EXIT_FAILURE);
	}

	/* NC mappings must request their access rights at mmap time; the prot
	 * cannot be changed later by obmm_set_ownership().
	 */
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		   OBMM_MMAP_FLAG_HUGETLB_PMD);
	if (ptr == MAP_FAILED) {
		perror("mmap() failed on OBMM memdev.\n");
		exit(EXIT_FAILURE);
	}
	/* Map char device succeeded. */
	value = (int*)ptr;
	*value = *value + 1;
	/* Cleanup.*/
	ret = munmap(ptr, size);
	if (ret == -1) {
		perror("munmap() failed on OBMM memdev pointer.\n");
		exit(EXIT_FAILURE);
	}

	ret = close(fd);
	if (ret == -1) {
		perror("close() failed on OBMM memdev.\n");
		exit(EXIT_FAILURE);
	}

	return 0;
}
```