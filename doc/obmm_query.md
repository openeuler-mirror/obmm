<!-- Copyright (c) 2025-2026 Huawei Technologies Co., Ltd. All rights reserved. -->

# obmm_query: 地址查询转换

## 名称 NAME

`obmm_query_memid_by_pa`, `obmm_query_pa_by_memid` - 实现两种地址描述（*物理地址*和*memid, offset*）之间的双向转换。

## 库 LIBRARY

OBMM用户态库 (libobmm)

## 摘要 SYNOPSIS

```c
#include <libobmm.h>
int obmm_query_memid_by_pa(unsigned long pa, mem_id *id, unsigned long *offset);
int obmm_query_pa_by_memid(mem_id id, unsigned long offset, unsigned long *pa);
```

其中 `mem_id` 定义于 `libobmm.h`，是 `uint64_t` 的 typedef，并非独立的结构体类型；请勿写成 `memid`。

## 描述 DESCRIPTION

`obmm_query_memid_by_pa` 可以根据一个本机的物理地址 `pa`，查询出该地址对应的OBMM内存ID `memid`，以及所指字节在该段内存中的偏移量 `offset`。如果物理地址和OBMM内存无关，函数将返回错误。

`obmm_query_pa_by_memid` 可以根据OBMM内存ID `memid`，以及该段内存上的偏移量 `offset`，查出该字节对应的物理地址`pa`。如果`memid`不存在或`offset`越界，函数将返回错误。

如果调用者只关注地址是否有效，不关注地址转换后的结果，可以将对应的出参指针配置为`NULL`。

**注意**：这里 `offset` 指的是 *UBA偏移量*。UBA 偏移量和虚拟地址偏移量、物理地址偏移量有如下关系

* PA offset 和 UBA offset
  * 在 export 方，PA一般不连续，二者无明确对应关系
  * 在 import 方，当前版本的硬件上，PA和UBA有线性对应关系，PA offset = UBA offset
* VA offset 和 UBA offset
  * 如果 VA 是通过OBMM设备mmap的，VA和UBA有线性对应关系，VA offset = UBA offset
  * 如果 VA 是由 NUMA 管理的，VA和UBA无明确对应关系

## 返回值 RETURN VALUE

如果入参描述的地址为有效的OBMM地址，函数会返回0。当出参指针不是`NULL`指针时，转换后的地址将被写入。

如果入参描述的值不是有效的OBMM地址，函数将返回 -1。详细的错误类型存储在`errno`中。

## 错误 ERRORS

故障码对应的部分情形如下：

* `ENOENT`：按物理地址查询（`obmm_query_memid_by_pa`）时，该 `pa` 未落在任何 OBMM 内存的覆盖范围内。内存设备处于 `STRANDED` 状态（已无后端）时同样按“无覆盖”处理，返回 `ENOENT`。按 memid 查询（`obmm_query_pa_by_memid`）时，`memid` 无效或其对应的 OBMM 内存不存在，也返回 `ENOENT`。
* `EINVAL`：按 memid 查询（`obmm_query_pa_by_memid`）时，`memid` 对应的 OBMM 内存存在，但是 `offset` 越界；或者该内存当前处于 `STRANDED` 状态、没有任何可用后端，此时也会返回 `EINVAL` 而并非 `offset` 越界，请结合内存设备的切换状态判断。各状态下接口的具体行为见 obmm_switch(3) 的状态表。
* `EAGAIN`：该内存正处于在线后端切换（见obmm_switch(3)）的过渡状态。切换间隙会保持到控制方调用 `switch_end` 为止，没有超时上限；调用方稍后重试即可，间隙被遗弃时可用 `switch_end(..., 0)` 关闭。

## 约束 CONSTRAINTS

本函数预期仅在调试、故障处理等使用场景使用，频繁调用可能导致关键控制面性能劣化，不应在性能敏感的业务面使用，不应高频使用。

## 附注 NOTES

本组函数在 export 方、import 方均可调用。

export 方的 PA 对应提供方本地的物理内存（DIMM）。

import 方的 PA 为芯片给 UB memory 保留的一段地址窗口，不严格对应物理意义上的内存。内存被 unimport 后，可能被后续新 import 的内存复用。