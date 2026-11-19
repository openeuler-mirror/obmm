# obmm_preimport_sysfs: OBMM 预引入地址段 procfs 维测文件

`obmm_preimport_sysfs` 描述的 `/proc/obmm/preimport_info` 是 procfs（不是 sysfs）文本文件，囊括系统内所有预引入地址段。

该文件的路径为 `/proc/obmm/preimport_info`，权限为 `0444`，所有用户只读、不可写。

对于每一个由 obmm_preimport(3) 创建的预引入地址段，在该文件中都有一行描述；即使系统中还没有任何预引入地址段，文件也至少会输出一行表头。

从左到右，各列依次为：

* 起始物理地址（pa_start）：十六进制数，预引入地址段的最小有效地址。
* 结束物理地址（pa_end）：十六进制数，预引入地址段的最大有效地址。
* dcna：十六进制数，内存提供方 bus controller 的 clan network address，含义详见 UB 协议。仅记录，不参与通路配置。
* scna：十六进制数，内存使用方 bus controller 的 clan network address，含义详见 UB 协议。
* deid：十六进制数，内存提供方 bus controller 的 entity id，含义详见 UB 协议。仅记录，不参与通路配置。以 u64 : u64 格式打印，每个 u64 为 `0x` 前缀加 16 位十六进制零填充。
* seid：十六进制数，内存使用方 bus controller 的 entity id，含义详见 UB 协议。仅记录，不参与通路配置。以 u64 : u64 格式打印，每个 u64 为 `0x` 前缀加 16 位十六进制零填充。
* numa_id：十进制数，预引入地址段所属的 NUMA 节点。

注：为和/proc/iomem, /proc/\$pid/maps 等标准维测文件保持一致，物理地址段没有附带0x前缀，但仍为16进制数表示。

示意图：
```
cat /proc/obmm/preimport_info
pa_start         - pa_end           : dcna     scna        deid                                  seid                                     nid
50000000000      - 50007ffffff      : 0        0x441       0x0000000000000012:0x0000000000000013 0x0000000000000010:0x0000000000000011    2
```
