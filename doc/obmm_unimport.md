# obmm_unimport: 取消远端内存引入

## 名称 NAME

`obmm_unimport` - 取消远端内存引入

## 库 LIBRARY

OBMM用户态库 (libobmm)

## 摘要 SYNOPSIS

```c
#include <libobmm.h>
int obmm_unimport(mem_id id, unsigned long flags);
```

## 描述 DESCRIPTION

内存使用方根据内存编号释放引入内存。

### Input Parameters

**id**：要回收的内存的编号

**flags**：选项（预留，当前没有使用，必须配置为0）

## 返回值 RETURN VALUE

成功时返回0。

失败时，返回-1，详细的错误类型存储在`errno`中。注意：若失败时内核已注销并销毁了该内存设备（例如后端释放或远端 NUMA 拆除失败后，region 仍会被注销并释放），此后重试 unimport 只会得到 `ENOENT`，此时不要重试，必要时应结合内核日志处理，或重新加载模块恢复；若因 `EBUSY` 失败，则 region 尚未销毁，应在 munmap/close 或等待在线切换结束后重试 unimport。

调用 unimport 前，应用必须先 munmap 全部映射并 close 全部 obmm_shmdev 文件描述符，否则会因 region 仍被引用而失败。

## 错误 ERRORS

故障码对应的部分情形如下：

* `ENOENT`：传入的 memid 没有对应的 OBMM 内存。
* `EINVAL`：传入了未定义的 flags，传入的 memid 为 OBMM_INVALID_MEMID (0) 或对应导出内存。
* `EBUSY` : region 仍被使用（存在活跃映射、未关闭的 obmm_shmdev 文件描述符或在线切换间隙），或正处于创建/销毁过程中。

注意：在线切换会改变 region 当前挂接的后端。销毁接口接受“创建身份或当前后端身份”之一：import 设备被切换为 export pool 后，`obmm_unimport` 与 `obmm_unexport` 均可销毁该设备。仅当设备处于 `STRANDED`（无后端）时回退到创建身份，此时 import 创建的设备只能用 `obmm_unimport` 清理。详见 obmm_switch(3)。

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

见 obmm_import(3) 。
