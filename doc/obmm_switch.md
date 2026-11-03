<!-- Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved. -->

# obmm_switch: 在线切换内存设备后端

## 名称 NAME

`obmm_switch_begin_import`, `obmm_switch_begin_export`, `obmm_switch_end` - 在不销毁内存设备的前提下,整体切换其承载内存的后端

## 库 LIBRARY

OBMM用户态库 (libobmm)

## 摘要 SYNOPSIS

```c
#include <libobmm.h>

struct obmm_switch_import_target {
    uint64_t pa;
    uint32_t scna;
    uint32_t dcna;
    uint8_t seid[16];
    uint8_t deid[16];
    int base_dist;
};

struct obmm_switch_export_target {
    size_t size[OBMM_MAX_LOCAL_NUMA_NODES];
    int node_count;
    unsigned long export_flags;
};

int obmm_switch_begin_import(mem_id id, unsigned long flags,
                             const struct obmm_switch_import_target *target,
                             const void *priv, uint16_t priv_len);
int obmm_switch_begin_export(mem_id id, unsigned long flags,
                             const struct obmm_switch_export_target *target,
                             struct obmm_mem_desc *desc,
                             const void *priv, uint16_t priv_len);
int obmm_switch_end(mem_id id, int success);
```

## 描述 DESCRIPTION

对已创建的 obmm_shmdev(4) 内存设备,在不销毁、不更换 `mem_id` 的前提下,将其背后承载内存的"后端"整体切换。**源设备必须是 import region**:它可以切到新的远端内存,也可以切成本地 export pool;反向(export 作为源)不在本版本支持范围内,内核返回 `EOPNOTSUPP`。切换过程中,设备节点 `/dev/obmm_shmdev${id}`、已打开的文件描述符、已建立的映射全部保持有效。配合切换间隙(gap)提供的双后端映射能力,用户可以实现内存数据的在线迁移,例如:

* 远端内存提供方变更:旧远端内存 → 新远端内存(import → import)
* 远端数据落地本地:远端内存 → 本地导出内存(import → export pool)

**两阶段协议**

一次切换由 `obmm_switch_begin_*()` 与 `obmm_switch_end()` 组成:

1. **begin**:内核在锁外构造目标后端,完成校验后,将设备切入切换间隙。begin 成功返回后,设备处于 `AWAIT_READY` 状态:新后端已挂接、数据通路已就绪,旧后端在间隙内由内核继续持有(提交成功后销毁)。设置 *OBMM_SWITCH_FLAG_KEEP_OLD_ALIVE* 才能用 `OBMM_MMAP_FLAG_SWITCH_OLD` 映射旧后端,并使 abort 能够恢复它。
2. **(gap,用户态控制)**:用户在间隙内完成数据迁移。可以直接对新后端映射访存,也可以通过 `OBMM_MMAP_FLAG_SWITCH_OLD` / `OBMM_MMAP_FLAG_SWITCH_NEW` 映射(见 obmm_shmdev(4))同时访问新旧两块内存做拷贝。
3. **end**:以 `success = 1` 提交切换,内核完成收尾(脏数据回刷、旧后端销毁、设备回到 `IDLE`);以 `success = 0` 放弃切换,回滚到旧后端。

设备切换状态可通过 sysfs 的 `switch_state` 属性观察(见 obmm_shmdev_sysfs(5)):

| 值 | 状态 | 含义 |
| --- | --- | --- |
| 0 | IDLE | 空闲,无切换进行 |
| 1 | SWAPPING | begin 执行中(换出旧后端、挂接新后端) |
| 2 | AWAIT_READY | 切换间隙:等待 switch_end |
| 3 | FINISHING | end 已确认,收尾中 |
| 4 | STRANDED | 终态:后端已丢失(见下文) |

**间隙的生命周期与 STRANDED**

间隙不会自行结束:内核不按定时器替用户态决定"提交还是放弃",只有 `obmm_switch_end()` 能关闭它(与 userfaultfd 的模型一致)。控制器崩溃或遗漏 end 时,设备会一直停在 `AWAIT_READY`:普通映射的缺页会阻塞(可被信号打断),其余接口继续返回 `-EAGAIN` / `-EBUSY`,`switch_state` 仍可通过 sysfs 观察,重启后的控制器可以先看状态再决定。

任何具备 CAP_SYS_ADMIN 的调用方都可以用 `obmm_switch_end(id, 0)` 关闭被遗弃的间隙:若 begin 时设置了 *OBMM_SWITCH_FLAG_KEEP_OLD_ALIVE*,旧后端被恢复;否则(或恢复失败)设备进入 `STRANDED` 终态——此时设备无后端,映射访存收到 SIGBUS;用户应通过 obmm_unimport(3) / obmm_unexport(3) 销毁设备后重建。

#### Input Parameters(obmm_switch_begin_import)

**id**:待切换的内存设备编号。

**flags**:切换属性。当前仅支持 *OBMM_SWITCH_FLAG_KEEP_OLD_ALIVE*:允许在间隙内用 `OBMM_MMAP_FLAG_SWITCH_OLD` 映射旧后端,并使 abort 能够恢复旧后端。不设置时旧后端在间隙内仍由内核持有,但 abort 会让设备进入 `STRANDED`。

**target**:新远端内存的描述,各字段含义同 obmm_import(3):

| 字段 | 描述 |
| --------- | --------------------------------------------- |
| pa        | 新远端内存起始物理地址,需按 2MB(OBMM_BASIC_GRANU)对齐 |
| scna      | 使用方引入内存的 UB controller 的 CNA 地址        |
| dcna      | 提供方提供内存的 UB controller 的 CNA 地址,当前仅用于记账 |
| seid      | 使用方引入内存的 UB controller 的 EID             |
| deid      | 提供方提供内存的 UB controller 的 EID,当前仅用于记账 |
| base_dist | 含义同 obmm_import(3)                            |

源设备必须是普通可 mmap 的 import region:NUMA_REMOTE、PREIMPORT 等特殊引入类别,以及创建时不允许 mmap 的 region,均返回 `EOPNOTSUPP`。目标内存的长度必须与设备 `size` 一致(由内核校验)。

**priv / priv_len**:新后端的私有数据。在 begin 时暂存,仅当切换最终以 `success = 1` 提交后才生效(sysfs `priv`/`priv_len` 亦如此);abort 后仍保持旧值。

#### Input Parameters(obmm_switch_begin_export)

**id / flags / priv / priv_len**:同 obmm_switch_begin_import()。

**target**:新的本地 export pool 内存描述:

| 字段 | 描述 |
| --------- | --------------------------------------------- |
| size      | 各本地 NUMA 节点的内存段大小;仅前 node_count 项有效,尾随项必须为 0;每项须为运行时 OBMM_MEMSEG_SIZE 的整数倍 |
| node_count | 有效表项数,范围 [1, 16]                        |
| export_flags | 仅允许 OBMM_EXPORT_FLAG_FAST 子集;ALLOW_MMAP 是设备级属性,由创建时的配置继承,此处不适用 |

pxm(本地 NUMA 节点)不在上表中:它由 `desc` 经 vendor 适配得到,与 obmm_export(3) 的分发方式一致,调用方不直接指定。

**desc**:兼具输入与输出。输入侧提供 vendor 信息与 pxm;`seid`/`deid` 会被接受但内核不使用(与 obmm_cmd_export 相同),远端授权以 UMMU token 为准。输出侧,begin 成功后 `desc->tokenid` 与 `desc->addr`(uba)携带新后端的 UMMU 凭证。desc 的 `priv`/`priv_len` 在本接口中被忽略,新后端的私有数据仅来自 priv/priv_len 参数。

**tokenid/uba 分发时序约束**:exp 目标的凭证在 begin 成功时即已产生,但若后续 abort,该后端(连同 token)会被销毁。运行时**不得在 end(success = 1)之前**将 tokenid/uba 分发给远端使用方。另外,begin 把输出结构拷回用户态失败时仍然返回成功,因此调用前应先清零 `desc`,并在使用前校验 `tokenid`/`uba` 非零。

#### Input Parameters(obmm_switch_end)

**id**:待收尾的内存设备编号。

**success**:`1` 提交——内核回刷两侧脏数据、销毁旧后端(若保活)、提交新私有数据,设备回到 `IDLE`;`0` 放弃——若 begin 设置了 *KEEP_OLD_ALIVE*,恢复旧后端(包括 abort 期间被映射过的旧内存的回刷),否则设备进入 `STRANDED`。

#### 切换间隙内的其它接口行为

| 接口 | 间隙内(SWAPPING/AWAIT_READY/FINISHING) | STRANDED |
| --- | --- | --- |
| mmap(2)(普通,不带 SWITCH 标志) | 允许,装页推迟到间隙结束 | 允许,首次访存 SIGBUS |
| obmm_set_ownership(3) | -EAGAIN | -EIO |
| obmm_query_memid_by_pa(3) / obmm_query_pa_by_memid(3) | -EAGAIN | 正常(无后端,按无覆盖/越界处理) |
| sysfs 身份类属性(type、backend_type、import_info/、export_info/) | 读取 -EAGAIN | 可读(值为 "-" 或创建身份) |
| 其余 sysfs 属性(switch_state、switch_flush_result、size、priv 等) | 可正常读取 | 可正常读取 |
| obmm_unimport(3) / obmm_unexport(3) | -EBUSY | 可执行(用于清理 STRANDED 设备) |
| 进程间访问(/proc/pid/mem 等) | -EAGAIN | -EIO |

#### 间隙数据迁移映射

mmap(2) 的 `offset` 参数高位携带标志位。切换新增 `OBMM_MMAP_FLAG_SWITCH_OLD` / `OBMM_MMAP_FLAG_SWITCH_NEW` 两个**仅在间隙(AWAIT_READY)内合法**的标志——标志位定义、完整使用约束(互斥、KEEP_OLD_ALIVE 前提、与 HUGETLB_PMD 组合、madvise 降级、THP 依赖)见 obmm_shmdev(4)。各状态下标志的合法性矩阵:

| offset 标志组合 | IDLE | AWAIT_READY | 其它切换状态 |
| --- | --- | --- | --- |
| 无(普通映射) | 首访缺页时装页 | 推迟到间隙结束(fault 等待) | 推迟到间隙结束 |
| SWITCH_NEW | -EINVAL | 立即安装到新后端 | -EINVAL |
| SWITCH_OLD | -EINVAL | 立即安装到旧后端(要求 begin 设置了 KEEP_OLD_ALIVE) | -EINVAL |
| SWITCH_OLD \| SWITCH_NEW | -EINVAL(互斥) | -EINVAL | -EINVAL |
| 任一 SWITCH 标志 \| HUGETLB_PMD | — | 可组合,按 PMD 粒度安装 | — |

## 返回值 RETURN VALUE

三个函数成功时返回 `0`。

失败时返回 `-1`,详细的错误类型存储在`errno`中。`obmm_switch_end()` 与内核收尾 worker 竞争时返回 `-1` 且 errno 为 `EAGAIN`,该重试由调用方负责,收敛到成功或 `ESTALE`(见"错误")。`obmm_switch_begin_export()` 成功时额外通过 `desc->tokenid`、`desc->addr` 输出新后端的 UMMU 凭证。

## 错误 ERRORS

故障码对应的部分情形如下。

obmm_switch_begin_import() / obmm_switch_begin_export():

* `EPERM`:调用方无 CAP_SYS_ADMIN 权限。
* `EFAULT`:参数拷贝失败。
* `EINVAL`:参数形状错误——flags 含无效标志位、保留字段非零、target_type 非法、import 目标地址未按基础粒度对齐、export 目标 size 表非 OBMM_MEMSEG_SIZE 整数倍或总和与设备 size 不符、node_count 越界、priv_len 超出 `OBMM_MAX_PRIV_LEN` 等。
* `ENOENT`:`id` 对应的内存设备不存在。
* `ENODEV`:export 目标的 pxm 节点不在线或非本地节点;import 目标的 scna 不对应任何 UB controller,或与 seid 不匹配。
* `ENOMEM`:后端构造内存不足。
* `EEXIST` / `EOPNOTSUPP` / `EIO` / `EBUSY`:设备状态不允许——间隙已开启、存在不兼容的映射等。

obmm_switch_end():

* `EPERM`:调用方无 CAP_SYS_ADMIN 权限。
* `EFAULT`:参数拷贝失败。
* `EINVAL`:success/reserved 非法。
* `ENOENT`:`id` 对应的内存设备不存在。
* `EAGAIN`:与内核收尾 worker 竞争(收尾临界区可达数百毫秒),调用方重试即可,最终收敛。
* `ESTALE`:间隙已结束——重复 end,或该间隙已被其他调用者收尾。

## 约束 CONSTRAINTS

**权限**

使用切换接口需要 `CAP_SYS_ADMIN` 权限。

**串行化**

同一内存设备的 begin/end 序列必须由**单一用户态控制器串行**驱动。切换 ABI 出于体积考虑不携带代数(generation)编号,内核无法识别"迟到的旧 end"——若两个控制器交叉发起切换,旧控制器的 end 可能错误地终结新控制器的间隙。

**凭证分发时序**

export 目标的 tokenid/uba 不得在 end(success = 1)之前分发给远端使用方(见"描述")。

**内核要求**

在线切换要求内核包含统一后端切换能力的 obmm.ko。

## 附注 NOTES

切换能力也可以不经过 libobmm,直接对 `/dev/obmm` 设备使用 `OBMM_CMD_SWITCH_BEGIN`(_IOWR('x', 8)) / `OBMM_CMD_SWITCH_END`(_IOW('x', 9)) ioctl(2) 驱动,参数结构与上述 libobmm 接口一一对应(联合体 target_type 选择 import/export pool 分支;所有保留字段、联合体内 pad 及 exp.size[] 未使用的尾随表项必须为 0)。

切换相关 sysfs 属性(`switch_state`、`switch_flush_result`、`backend_type`)与目录身份子目录跟随切换的行为(提交后互换、过渡期身份类属性返回 `-EAGAIN`),统一见 obmm_shmdev_sysfs(5) 的"目录身份与在线切换"一节;`priv` 在切换期间保持旧值,提交后才生效新值。

## 样例 EXAMPLES

以下样例将一段 import 内存在线切换为本地 export pool 内存,并在间隙内完成数据迁移。错误处理仅展示 abort 路径,其余从简。

```c
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <libobmm.h>

static int migrate_import_to_pool(mem_id id, size_t size)
{
	struct obmm_switch_export_target target = {0};
	struct obmm_mem_desc desc = {0};
	char path[128];
	void *src, *dst;
	int fd;

	target.size[0] = size;	/* 单节点,和为设备 size */
	target.node_count = 1;
	/* desc 提供 vendor 信息与 pxm(同 obmm_export(3));seid/deid 接受但被内核忽略 */

	if (obmm_switch_begin_export(id, OBMM_SWITCH_FLAG_KEEP_OLD_ALIVE,
				     &target, &desc, NULL, 0)) {
		perror("obmm_switch_begin_export");
		return -1;
	}
	/* begin 成功:desc.tokenid / desc.addr(uba) 已填出,但必须等
	 * obmm_switch_end(id, 1) 之后才能分发给远端使用方。 */

	/* gap:双后端映射,迁移数据 */
	snprintf(path, sizeof(path), "/dev/obmm_shmdev%lu", id);
	fd = open(path, O_RDWR);
	if (fd < 0)
		goto abort;

	src = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		   OBMM_MMAP_FLAG_SWITCH_OLD);
	dst = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		   OBMM_MMAP_FLAG_SWITCH_NEW);
	if (src == MAP_FAILED || dst == MAP_FAILED)
		goto abort;

	memcpy(dst, src, size);	/* 用户自定义迁移(可分块、可带校验) */

	munmap(src, size);
	munmap(dst, size);
	close(fd);

	/* 提交切换;与内核收尾 worker 竞争得到的 -EAGAIN 由调用方重试 */
	while (obmm_switch_end(id, 1) == -1 && errno == EAGAIN)
		usleep(1000);
	return 0;

abort:
	obmm_switch_end(id, 0);	/* 放弃切换:KEEP_OLD_ALIVE 保证旧后端恢复 */
	return -1;
}
```

切换到新的远端内存(import 目标)时,使用 `obmm_switch_begin_import()` 与 `struct obmm_switch_import_target`(填新内存的 pa/scna/dcna/seid/deid/base_dist),其余流程相同。
