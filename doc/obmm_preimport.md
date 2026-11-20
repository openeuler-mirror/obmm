# obmm_preimport: 预引入远端内存

## 名称 NAME

`obmm_preimport` - 预引入远端内存

## 库 LIBRARY

OBMM用户态库 (libobmm)

## 摘要 SYNOPSIS

```c
#include <libobmm.h>
int obmm_preimport(struct obmm_preimport_info *preimport_info, unsigned long flags);
```

## 描述 DESCRIPTION

以NUMA方式引入远端内存时，创建 NUMA 节点会产生较长耗时。

预引入可以提前完成NUMA节点创建的工作，它不依赖内存导出时生成的完整信息，因此可以在内存导出之前完成。内存实际上线时，将该段内存”注入“NUMA即可，可以加速关键路径上的软件流程。

预引入依赖内核的 NUMA REMOTE 预上线能力：OBMM 内核模块依赖 `CONFIG_NUMA_REMOTE`（编译 OBMM 时会自动选中），并且需要配置内核启动参数 `numa_remote=preonline`。未满足该前提时，`obmm_preimport()` 内部的 `add_memory_remote()` 会失败，接口返回 `EPERM`。内核参数配置详见 obmm(4) 的“OBMM 部署”一节。

物理地址段\<pa, length\>为匹配索引。

实际引入内存时，obmm_import 需要配置 *OBMM_IMPORT_FLAG_PREIMPORT* flag ，同时其参数要命中索引，才能实现引入加速：

obmm_import 的物理地址段必须为预引入地址段的子集。除此之外，obmm_import 的 desc 还必须携带与预引入时完全相同的 scna、dcna、seid 和 deid，任一字段不一致都会导致 obmm_import 返回 `EINVAL`。

预引入参数需要满足以下要求：

**索引唯一**

每次预上线的物理地址段不能重叠，也不能与非预上线模式引入的内存物理地址重叠。

**NUMA有效**

传入的NUMA ID不为 -1时，NUMA ID必须合法（为非负数）且不能指向本地NUMA。

预引入内存需使用 obmm_unpreimport(3) 释放。

#### Input Parameters

**preimport_info**:

预上线地址段信息。包含组网和通路信息，是组网范围内、通过某一通路引入的内存的部分描述。

| 字段      | 描述                                             |
| --------- | ------------------------------------------------------------ |
| pa        | 预上线内存的物理地址基地址                                   |
| length    | 预上线 NUMA 节点所能容纳的最大内存                           |
| scna      | 指示本节点访问目标内存时，使用的IODie<br/>用于NUMA distance计算和记账 |
| dcna      | 指示访问目标内存时，经过的提供方IODie<br/>仅用于记账，不参与通路配置 |
| seid      | 指示本节点访问目标内存时，使用的IODie<br/>仅记录，不参与通路配置 |
| deid      | 指示访问目标内存时，经过的提供方IODie<br/>仅记录，不参与通路配置 |
| base_dist | 表示新上线NUMA到使用方IODie的基础距离。合法值为 0 或 11..255：0 表示不设置 NUMA 距离；1..10（保留值）或大于 255 返回 `EINVAL`。距离计算规则详见 obmm_import(3) |
| numa_id   | 指示预上线 NUMA 节点使用的 NUMA ID<br/>配置为-1时，用于接收自动分配的 NUMA ID |
| priv_len  | 忽略                                                         |
| priv      | 忽略                                                         |

**flags**：选项（预留，当前未使用，必须配置为0）。

## 返回值 RETURN VALUE

成功时，返回0。

失败时，返回-1，详细的错误类型存储在`errno`中。

## 错误 ERRORS

故障码对应的部分情形如下：

* `EPERM`：NUMA REMOTE 相关错误，即 `add_memory_remote()` 失败，例如：
  * 内核未启用 NUMA REMOTE（未配置 `numa_remote` 启动参数），或已启用但未配置 `numa_remote=preonline`；
  * 传入的 *numa_id* 指定了非法的 NUMA ID；
  * 无可用远端 NUMA 节点，或内存热插失败；
  * 模块正在卸载。
* `EINVAL`：传入参数不符合要求，例如：
  * preimport_info 为空；
  * pa 或 length 未按 `memory_block_size_bytes()` 对齐（4K/16K 页为 128MB，64K 页为 512MB），pa 为 0，或 pa + length 溢出；
  * length 为 0；
  * flags 中含有非法标志位；
  * base_dist 不在合法范围内（合法值为 0 或 11..255，见上表）。
* `ENODEV`: scna 无效、seid无效或 scna - seid 不匹配（不属于同一 UB entity）。
* `ENOMEM`: 系统内存不足导致的错误。
* `EEXIST`: 内存范围已被占用或冲突。
* `EBUSY`: 物理地址对应的 iomem 资源已被占用。

## 约束 CONSTRAINTS

**地址校验约束**

需要用户保证传入的物理地址范围正确落在UB远端内存的地址范围内。

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下函数预引入了一段物理基地址为 `pa` ，长度为 `size` 的内存。函数创建了一个新的 remote NUMA 节点，但节点起初并没有可用内存。
调用 obmm_import(3) 时，内存被注入了 NUMA 节点。最后，程序调用 obmm_unimport(3) 和 obmm_unpreimport(3) 解除了这段内存的上线和预上线。
现实场景中，预上线一般发生在内存导出之前，很少像本示例一样，在同一函数中顺次调用预上线和上线接口。

示例中的 scna、dcna、seid、deid 来自内存导出方提供的组网和通路信息；预引入与后续 obmm_import(3) 必须使用完全相同的一组值，否则 obmm_import(3) 会返回 `EINVAL`。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libobmm.h>

int preimport_demo_decoder(unsigned long pa, size_t size, unsigned int scna, unsigned int dcna,
			   uint8_t *seid, uint8_t *deid)
{
	int ret;
	mem_id id;
	unsigned long import_flags;
	/* Preimport [pa, pa+size) range and use a new NUMA node to hold the preimport info. */
	struct obmm_preimport_info info = {
		.pa = pa,
		.length = size,
		.scna = scna,
		.dcna = dcna,
		.base_dist = 0,
		.numa_id = -1,
		.priv_len = 0

	};
	memcpy(info.seid, seid, 16);
	memcpy(info.deid, deid, 16);
	struct obmm_mem_desc desc = {};

	ret = obmm_preimport(&info, 0);
	if (ret) {
		perror("obmm_preimport() failed.\n");
		exit(EXIT_FAILURE);
	}
	/* Preimport succeeded. The associated NUMA node id is stored in info.numa_id. */

	/* Import all memory prepared in the preimport phase. OBMM matches the import request
	 * with the preimport range using the PA range, and requires scna/dcna/seid/deid to be
	 * identical to those used at preimport time; otherwise obmm_import() fails with EINVAL. */
	desc.addr = pa;
	desc.length = size;
	desc.scna = scna;
	desc.dcna = dcna;
	memcpy(desc.seid, seid, 16);
	memcpy(desc.deid, deid, 16);
	desc.priv_len = 0;
	import_flags = OBMM_IMPORT_FLAG_NUMA_REMOTE | OBMM_IMPORT_FLAG_PREIMPORT;
	id = obmm_import(&desc, import_flags, 0, NULL);
	if (id == OBMM_INVALID_MEMID) {
		perror("obmm_import() failed.\n");
		exit(EXIT_FAILURE);
	}
	/* Import succeeded. */

	/* Do your work here... */

	/* Unimport memory. */
	ret = obmm_unimport(id, 0);
	if (ret) {
		perror("obmm_unimport failed.\n");
		exit(EXIT_FAILURE);
	}

	/* To unpreimport a range, info.pa and info.length must exactly
	 * match the preimport parameters. All other fields have no effect. */
	info.pa = pa;
	info.length = size;
	ret = obmm_unpreimport(&info, 0);
	if (ret) {
		perror("obmm_unpreimport() failed.\n");
		exit(EXIT_FAILURE);
	}

	return 0;
}
```
