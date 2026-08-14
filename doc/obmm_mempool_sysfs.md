# obmm_mempool_sysfs: OBMM 内存池 sysfs

OBMM 内存池 sysfs 用于展示系统内各 NUMA 节点的内存池状态。

## 路径

内存池 sysfs 目录位于 `/sys/kernel/obmm_mempool/`，每个本地 NUMA 节点对应一个子目录 `obmm-${nid}/`，其中 `${nid}` 为 NUMA 节点编号。

```
obmm_mempool/
├── obmm-0/
│   ├── total
│   ├── used
│   ├── available_cleared
│   ├── available_uncleared
│   └── max_total
├── obmm-1/
│   └── ...
└── ...
```

## 属性说明

**total**
- 类型：文本、十六进制数
- 描述：内存池中该 NUMA 节点的总内存大小（字节），包含已使用和可用的内存

**used**
- 类型：文本、十六进制数
- 描述：已分配给 OBMM 内存的内存大小（字节）

**available_cleared**
- 类型：文本、十六进制数
- 描述：已清零、可立即分配的内存大小（字节）。使用 obmm_export(3) 的 `OBMM_EXPORT_FLAG_FAST` 标志时仅从此处分配，可在调用前查询确认可用内存是否满足需求。

**available_uncleared**
- 类型：文本、十六进制数
- 描述：未清零、需要异步清零后才能分配的内存大小（字节）

**max_total**
- 类型：文本、十六进制数（读）/ 十进制或十六进制数值，可带 `K/M/G/T/P` 后缀（写）
- 权限：0600，仅 root 可读写
- 描述：该 NUMA 节点内存池总内存（含已借出与空闲部分）的上限（字节）。默认无上限（`0x7fffffffffffffff`，即 LLONG_MAX 哨兵值）。写入 `0` 表示清除上限、恢复无限制。上限按节点的内存池粒度自然对齐生效，写入值本身不要求对齐、不做取整。写入非法输入（空串、前导 `-`、尾部垃圾、超过 LLONG_MAX 的值）会被拒绝并返回 EINVAL
- 注意：max_total 只能通过 sysfs 配置，没有对应的内核模块参数

## 使用示例

```bash
# 查看节点 0 的内存池状态
cat /sys/kernel/obmm_mempool/obmm-0/total
cat /sys/kernel/obmm_mempool/obmm-0/used
cat /sys/kernel/obmm_mempool/obmm-0/available_cleared
cat /sys/kernel/obmm_mempool/obmm-0/available_uncleared

# 查看节点 0 的 max_total 上限
cat /sys/kernel/obmm_mempool/obmm-0/max_total

# 将节点 0 的内存池上限设为 4G
echo 4G > /sys/kernel/obmm_mempool/obmm-0/max_total

# 清除上限（恢复无限制）
echo 0 > /sys/kernel/obmm_mempool/obmm-0/max_total
```

## 注意事项

- `total`、`used`、`available_cleared`、`available_uncleared` 为只读，反映读取时刻的瞬时状态；`max_total` 可读写（0600）
- 数值以十六进制格式显示，如 `0x40000000` 表示 1GB
- 内存池大小通过内核模块参数 `mempool_size` 配置；内存池总量的上限通过各节点的 `max_total` sysfs 属性配置（无对应的内核模块参数）
- 内存池会在系统内存不足时自动收缩，在内存充足时自动扩充；收缩、扩充以及内存导出均会逐粒度检查 `max_total`。上限判定在并发场景下为尽力而为（无锁）：多个并发的分配方可能在检查与提交之间交错，使节点总量瞬时超过 `max_total`，超出量至多为并发分配方数量与内存池粒度的乘积，并随内存释放回落
