# obmm_log: OBMM日志查看与使用

## 名称 NAME

`obmm_log` - OBMM日志系统使用说明

## 描述 DESCRIPTION

OBMM库内置了日志记录功能，会在部分接口被调用时自动记录详细信息，包括操作参数、执行结果、耗时等。当前实际会产生日志的接口包括：`obmm_export`、`obmm_export_useraddr`、`obmm_import`、`obmm_unexport`、`obmm_unimport`、`obmm_preimport`、`obmm_unpreimport`、`obmm_switch_begin_import`、`obmm_switch_begin_export`、`obmm_switch_end`。本文档说明如何查看和使用OBMM日志进行问题诊断和性能分析。

**注意**：`obmm_set_ownership`、`obmm_query_memid_by_pa`、`obmm_query_pa_by_memid` 当前**不会**产生任何日志，排查所有权变更或地址查询问题时不能依赖 syslog。

**注意**：日志功能由OBMM库内部自动调用，用户无需在代码中添加任何日志相关代码；库中也没有环境变量或配置项可以调整日志级别。

## 日志位置 LOG LOCATION

### 默认输出位置

OBMM日志通过系统syslog机制输出，默认位置为：

* **系统日志**：
  - **openEuler/RHEL/CentOS**: `/var/log/messages`
  - **Debian/Ubuntu**: `/var/log/syslog`
* **日志设施**：`LOG_LOCAL0`
* **日志标识**：包含 `[OBMM]` 前缀

### 查找OBMM日志

由于OBMM日志输出到系统syslog，可以通过以下方式查看：

```bash
# 查看系统日志中的OBMM日志（openEuler/RHEL/CentOS）
tail -f /var/log/messages | grep OBMM

# 查看系统日志中的OBMM日志（Debian/Ubuntu）
tail -f /var/log/syslog | grep OBMM
```
## 日志格式 LOG FORMAT

### 基本格式

每条OBMM日志遵循以下格式：

```
<时间戳> <主机名> <进程名>[<PID>]: [OBMM] <函数名>: <日志内容>
```

### 日志级别

OBMM当前实际只会输出以下两个syslog级别：

* **ERR (错误)**：操作失败（`FAILED` 日志），包含错误原因
* **INFO (信息)**：正常操作流程，包括操作开始和成功（`SUCCESS` 日志）

库中虽然定义了 WARNING/DEBUG 宏（`OBMM_LOGW`/`OBMM_LOGD`），但当前没有任何调用点，也没有环境变量或配置项可以开启，因此实际不会产生 WARNING/DEBUG 级别的日志。

### 日志字段说明

#### 操作开始日志

当OBMM操作开始时，记录操作参数。开始日志没有 `START` 字面量，只有 `[OBMM] <函数名>: ` 前缀加各接口自己的字段：

```
[OBMM] obmm_export: len(sizes)=16 sizes={ [0]:0x200000} flags=0x0 deid=0x00000000001234:0x00000000005678 priv_len=0
[OBMM] obmm_export_useraddr: pid=0 length=0x200000 priv_len=0 deid=0x00000000001234:0x00000000005678
[OBMM] obmm_import: scna=0x1 {pa=0x100000000 length=0x200000} flags=0x0 nid=0 base_dist=0 seid=0x0000000000abcd:0x0000000000ef01 priv_len=0
```

注意 `obmm_export` 与 `obmm_export_useraddr` 的开始日志字段不同：前者按 NUMA 节点列出 `sizes`，后者记录 `pid` 和 `length`。其余接口的具体字段参见各接口文档。

#### 操作成功日志

操作成功时，记录耗时和返回值。`elapsed` 字段位于最前面，之后才是各接口的返回值字段：

```
[OBMM] obmm_export: SUCCESS, elapsed=15ms, mem_id=1
[OBMM] obmm_import: SUCCESS, elapsed=8ms, mem_id=2
```

**字段说明**：
* **elapsed**：操作耗时（毫秒）

#### 操作失败日志

操作失败时，记录失败原因：

```
[OBMM] obmm_export: FAILED, length or desc is NULL
[OBMM] obmm_import: FAILED, failed to get device fd
[OBMM] obmm_export: FAILED, operation failed
```

## 问题诊断 TROUBLESHOOTING

### 常见问题

#### 1. 日志未输出

**现象**：在系统日志中找不到OBMM日志

**排查步骤**：

1. 确认应用正在使用OBMM库
2. 检查日志文件是否存在：
```bash
ls -l /var/log/messages  # openEuler/RHEL/CentOS
ls -l /var/log/syslog    # Debian/Ubuntu
```

3. 检查syslog服务状态：
```bash
sudo systemctl status rsyslog  # 大多数系统
sudo systemctl status syslog   # 某些系统
```

#### 2. 日志信息不全

**现象**：只看到部分操作的日志

**可能原因**：
* 应用程序未正常调用OBMM接口
* 日志被系统配置过滤
* 日志文件已被轮转清理
* syslog服务错误

**解决方法**：
1. 检查应用日志，确认OBMM接口调用情况
2. 查看较早的日志文件（如果存在）
3. 检查系统日志轮转配置，查看归档日志文件
4. 尝试重启syslog服务

#### 3. 日志时间戳不准确

**现象**：日志中的时间与系统时间不一致

**解决方法**：
```bash
# 检查系统时区
timedatectl

# 同步系统时间
sudo systemctl restart systemd-timesyncd
```


### 日志管理建议

虽然OBMM本身不提供日志轮转配置，但建议系统管理员：

1. 根据系统日志轮转策略管理OBMM日志
2. 监控日志文件大小，避免占用过多磁盘空间
3. 定期备份重要的日志数据用于分析
4. 考虑使用日志集中收集工具（如ELK、Splunk）

## 相关文档

obmm_export(3), obmm_import(3), obmm_unexport(3), obmm_unimport(3), syslog(3)
