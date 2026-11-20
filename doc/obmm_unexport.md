# obmm_unexport: 取消本地内存导出

## 名称 NAME

`obmm_unexport` - 取消本地内存导出

## 库 LIBRARY

OBMM用户态库 (libobmm)

## 摘要 SYNOPSIS

```c
#include <libobmm.h>
int obmm_unexport(mem_id id, unsigned long flags);
```

## 描述 DESCRIPTION

内存提供方根据内存编号回收导出内存。

调用 `obmm_unexport` 前，应用必须先 `munmap` 掉所有通过 `/dev/obmm_shmdev${id}` 建立的映射，并 `close` 对应的设备文件描述符；`obmm_unexport` 不会替应用解除映射或关闭 fd。未关闭的 fd 会使 region 引用计数不为 1，从而导致 `EBUSY`；未解除的映射在回收后继续访问则属于未定义行为。

### Input Parameters

**id**：要回收的内存的编号。

**flags**：选项（预留，当前没有使用，必须配置为0）。

## 返回值 RETURN VALUE

成功时返回0。

失败时，返回-1，详细的错误类型存储在`errno`中。

## 错误 ERRORS

故障码对应的部分情形如下：

* `ENOENT`：传入的 memid 没有对应的 OBMM 内存。
* `EINVAL`：传入了未定义的 flags，传入的 memid 为 OBMM_INVALID_MEMID (0) 或对应引入内存。
* `EBUSY`：该 region 仍被本端占用——尚有进程打开着对应的 `/dev/obmm_shmdev${id}` 设备文件或未解除的 `mmap` 映射，或该 region 正处于 switch 的间隙（switch gap 持有引用）。请先关闭设备 fd、解除映射并等待切换结束。远端使用者不会被检测，也不会引发 `EBUSY`，详见 obmm_shmdev(4)。

注意：在线切换会改变 region 当前挂接的后端。销毁接口接受“创建身份或当前后端身份”之一：import 设备被切换为 export pool 后，`obmm_unexport` 与 `obmm_unimport` 均可销毁该设备。仅当设备处于 `STRANDED`（无后端）时回退到创建身份，此时 import 创建的设备只能用 `obmm_unimport` 清理。详见 obmm_switch(3)。

## 约束 CONSTRAINTS

作为单机组件，OBMM无法确认导出的内存是否还有远端使用者，提供方回收内存时，**用户需要保证该远端使用者已停止使用该内存**。否则，使用该内存的远端进程将处于不可预测的状态，可能遭遇进程崩溃、数据不一致、硬件故障上报、kernel panic等非预期后果。

## 附注 NOTES

暂无

## 样例 EXAMPLES

见 obmm_export(3) 。
