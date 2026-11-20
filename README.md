# OBMM: 基于所有权的内存管理组件

[![License](https://img.shields.io/badge/License-GPL--2.0-blue.svg)](https://opensource.org/licenses/GPL-2.0)
[![Kernel Version](https://img.shields.io/badge/Kernel-6.x%2B-green.svg)](https://www.kernel.org/)
[![Unified Bus](https://img.shields.io/badge/SIG--Long-%E7%81%B5%E8%A1%A2(UnifiedBus)-green.svg)](https://www.kernel.org/)
[![CXL](https://img.shields.io/badge/SIG--Long-CXL-red.svg)](https://www.kernel.org/)

---

**中文版** | **[🌐 English Version](README.en.md)**

---

OBMM (Ownership Based Memory Management) 是面向超节点环境的内核内存管理系统，支持跨节点的物理内存共享。该系统通过内核模块 (`obmm.ko`) 和用户态库 (`libobmm.so`) 提供高效的远程内存访问能力。

**重要背景**: 当前很多 sig-Long 总线技术（如 Unified Bus、CXL）中，跨节点数据一致性支持存在重大限制，导致多节点同时访问同一内存区域时可能出现数据竞争和不一致问题。OBMM 的**所有权机制**正是为了解决这一关键挑战而设计，通过确保任意时刻只有一个节点拥有内存的写入权限，从而在缺乏硬件一致性保证的环境中实现安全、可靠的跨节点内存共享。

## 🚀 核心特性

- **所有权管理**: 基于所有权的内存访问控制，确保在缺乏硬件一致性保证的多节点环境中实现数据一致性
- **跨节点内存共享**: 在单机内管理远端内存，支持内存导出 (export) 和导入 (import)
- **透明访问**: 应用可使用普通的 `load`、`store` 指令访问远端内存
- **NUMA 支持**: 将远端内存作为远程 NUMA 节点上线
- **高性能**: 通过预导入 (preimport) 优化降低热路径时延
- **设备接口**: 提供字符设备接口 (`/dev/obmm_shmdev{mem_id}`) 用于内存映射

## 🛠️ 快速开始

### 环境要求

- **硬件平台**：支持 Unified Bus 的 ARM64 服务器；跨节点场景需按实际拓扑完成 UB 链路与 decoder 配置。
- **版本**：仓库当前版本为 1.2.0（见 [VERSION](VERSION)），用户态库与内核需配套使用。
- **内核**：openEuler OLK-6.6 内核分支（本文档基于提交 `eb444c5` 验证），并合入 OBMM 驱动（[openEuler Kernel PR #19100](https://gitee.com/openeuler/kernel/pulls/19100)）。用户态库与内核的核心编译选项（尤其是页大小）需保持一致。
- **内核启动参数**：按部署场景配置 `numa_remote`（预上线功能需 `numa_remote=preonline`）、`pmd_mapping`；`PAGE_SIZE=64K` 时改用 `rodata=full`。详见 [OBMM 内核模块](doc/obmm.md) 的“OBMM 部署”章节。
- **内核配置项**：`UB_UMMU_CORE`、`UB_UBUS`、`HISI_SOC_CACHE`、`TRANSPARENT_HUGEPAGE` 必须作为内核内建配置项使能，不能通过 insmod/modprobe 加载；其中 UMMU core 以 `ummu_core_builtin.o` 内建，不存在名为 `hisi_ummu_core` 的可加载模块。真正需要额外加载的模块只有 `hisi_soc_hha`，缺失时刷 cache 返回 `-EOPNOTSUPP`，导出/导入会失败。
- **权限**：sysfs 维测属性为 `root:root 0400`；`/dev/obmm` 与 `/dev/obmm_shmdev*` 默认为 `root:root 0600`，导出、导入和映射操作需以 root 或具备相应权限的用户执行。
- **对齐要求**：OBMM 基础粒度固定为 2MB，export/import 的地址与长度均需按 2MB 对齐；`obmm_export_useraddr` 例外，仍要求按 `PMD_SIZE` 对齐。

### 编译与安装 libobmm

仓库未提供根目录构建脚本，需按组件分别编译。`libobmm.h` 依赖内核 UAPI 头文件 `<ub/obmm.h>`，请从与运行内核一致的内核源码树执行 `make headers_install INSTALL_HDR_PATH=<dir>` 安装，并在编译 libobmm 与应用时用 `-I<dir>/include` 指定。

```sh
cmake -S src/libobmm -B build/libobmm -DCMAKE_C_FLAGS="-I<dir>/include"
cmake --build build/libobmm
sudo cmake --install build/libobmm
```

默认安装前缀为 `/usr/local`，产物为 `lib64/libobmm.so` 和 `include/libobmm.h`；可通过 `-DCMAKE_INSTALL_PREFIX=<prefix>` 指定其他前缀。使用非默认前缀时，编译应用需同时指定 `-I<prefix>/include` 与 `-L<prefix>/lib64`，运行时设置 `LD_LIBRARY_PATH` 或 rpath，例如：

```sh
cc -I<prefix>/include -o demo demo.c -L<prefix>/lib64 -lobmm
```

### 最小示例

下面示例在提供方导出 2MB 内存并映射访问。使用方按 `obmm_import()` 的要求填写 `obmm_mem_desc`（其中 `addr` 为配置 ubmem decoder 的组件提供的远端物理地址，`seid`/`scna` 等链路信息由部署环境提供），再按 [共享内存设备](doc/obmm_shmdev.md) 进行映射；销毁内存设备前必须先 `munmap()` 并 `close()`。完整的导出、导入样例见 [内存导出](doc/obmm_export.md)、[内存导入](doc/obmm_import.md) 和 [共享内存设备](doc/obmm_shmdev.md)。

```c
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <libobmm.h>

#define SZ_2M (1UL << 21)

/* deid：本节点 UB controller 的 EID（16 字节），由部署环境提供。 */
int export_map_demo(const uint8_t deid[16])
{
	size_t length[OBMM_MAX_LOCAL_NUMA_NODES] = { SZ_2M };
	unsigned long flags = OBMM_EXPORT_FLAG_ALLOW_MMAP;
	struct obmm_mem_desc desc = { .priv_len = 0 };
	char path[64];
	void *ptr = MAP_FAILED;
	mem_id id;
	int fd = -1;
	int errsv = 0;
	int ret = 0;

	memcpy(desc.deid, deid, sizeof(desc.deid));
	id = obmm_export(length, flags, &desc);
	if (id == OBMM_INVALID_MEMID) {
		perror("obmm_export");
		return -1;
	}

	snprintf(path, sizeof(path), "/dev/obmm_shmdev%" PRIu64, id);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		errsv = errno;
		ret = -1;
		perror("open");
		goto out_unexport;
	}

	/* 先以空权限映射，再通过 obmm_set_ownership() 切换为读写权限 */
	ptr = mmap(NULL, desc.length, PROT_NONE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		errsv = errno;
		ret = -1;
		perror("mmap");
		goto out_close;
	}

	ret = obmm_set_ownership(fd, ptr, (char *)ptr + desc.length, PROT_WRITE);
	if (ret) {
		errsv = errno;
		perror("obmm_set_ownership");
		goto out_munmap;
	}
	*(volatile uint64_t *)ptr = 0x1234;

	/* 销毁内存设备前必须先 munmap 并 close */
	if (munmap(ptr, desc.length)) {
		errsv = errno;
		ret = -1;
		perror("munmap");
	}
	ptr = MAP_FAILED;

out_close:
	if (close(fd)) {
		if (!ret) {
			errsv = errno;
			ret = -1;
		}
		perror("close");
	}
out_unexport:
	if (obmm_unexport(id, 0)) {
		perror("obmm_unexport");
		return -1;
	}
	if (ret)
		errno = errsv;
	return ret;

out_munmap:
	munmap(ptr, desc.length);
	close(fd);
	goto out_unexport;
}

int main(void)
{
	/* 替换为本节点 UB controller 的实际 EID */
	uint8_t deid[16] = { 0 };

	return export_map_demo(deid) ? 1 : 0;
}
```

编译命令（`<dir>` 为内核 UAPI 头文件安装目录）：

```sh
cc -I<dir>/include -o demo demo.c -L/usr/local/lib64 -lobmm
```

## 🎯 主要应用场景

### 场景一：单节点内存扩展

在单节点环境中扩展可用内存容量，突破本地物理内存限制。

**具体应用实例**：

- **大内存数据库**: 数据库服务器需要处理超过本地物理内存容量的数据集，通过 OBMM 导入远程内存作为 NUMA 节点，透明扩展内存容量
- **内存密集型计算**: 科学计算、机器学习训练等场景需要大量内存，通过 OBMM 动态扩展内存资源
- **内存层次优化**: 将冷数据迁移到远程内存，热数据保留在本地内存，实现智能的内存分层管理
- **容器内存扩展**: 为容器提供额外的内存资源，突破单机内存配额限制

### 场景二：节点间共享内存

在多节点环境中实现高效的内存数据共享和协作。

**具体应用实例**：

- **分布式缓存**: 多个应用节点共享同一份缓存数据，避免重复存储，提高缓存命中率
- **实时数据共享**: 金融交易系统中，多个交易节点共享实时行情数据，确保数据一致性
- **协同处理流水线**: 多个处理节点共享输入数据和中间结果，减少数据传输开销
- **高可用性集群**: 主备节点共享状态数据，实现快速故障切换和状态同步

## 🚀 未来应用可能性

OBMM 的所有权机制和跨节点内存共享能力为未来更多创新应用场景提供了技术基础：

### 边缘计算与云边协同
- **边缘节点缓存**: 边缘设备共享缓存数据，减少云端访问延迟
- **分布式推理**: AI 推理任务在边缘节点间共享模型和数据
- **内容分发网络**: CDN 节点间共享热门内容，提高访问效率

### 异构计算加速
- **GPU/FPGA 内存共享**: CPU 与加速器间共享大容量内存池
- **异构内存管理**: 统一管理不同类型的内存介质（DDR、HBM、持久内存）
- **计算-存储融合**: 存储节点直接参与计算，减少数据搬移

### 新型计算范式
- **内存中心计算**: 以内存为中心的计算架构，突破传统 CPU 中心限制
- **近数据计算**: 在数据存储位置附近执行计算，最小化数据移动
- **分布式内存计算**: 将计算任务分布到内存所在的各个节点

### 系统软件创新
- **新型虚拟化**: 基于内存共享的轻量级虚拟化方案
- **分布式操作系统**: 跨节点的统一内存视图和进程管理
- **智能内存调度**: AI 驱动的智能内存分配和迁移策略

> **技术愿景**: OBMM 不仅解决当前的一致性问题，更为未来的计算架构创新提供基础支撑。随着硬件技术的发展，OBMM 将持续演进，支持更丰富的应用场景和更高的性能要求。

## 📚 文档

### API 文档
- **[用户态库总览](doc/libobmm.md)** - libobmm 接口总览、核心数据结构说明、使用模型说明
- **[日志系统](doc/obmm_log.md)** - OBMM 日志查看、配置与问题诊断说明
- **[内存导出](doc/obmm_export.md)** - `obmm_export()` 接口说明
- **[内存取消导出](doc/obmm_unexport.md)** - `obmm_unexport()` 接口说明
- **[内存导入](doc/obmm_import.md)** - `obmm_import()` 接口说明
- **[内存取消导入](doc/obmm_unimport.md)** - `obmm_unimport()` 接口说明
- **[内存预导入](doc/obmm_preimport.md)** - `obmm_preimport()` 接口说明
- **[取消预导入](doc/obmm_unpreimport.md)** - `obmm_unpreimport()` 接口说明
- **[所有权设置](doc/obmm_set_ownership.md)** - `obmm_set_ownership()` 接口说明
- **[内存查询](doc/obmm_query.md)** - `obmm_query_memid_by_pa()`, `obmm_query_pa_by_memid()` 接口说明
- **[在线后端切换](doc/obmm_switch.md)** - `obmm_switch_begin_import()`, `obmm_switch_begin_export()`, `obmm_switch_end()` 接口说明
- **[NC 加速拷贝库](doc/libobmm-accel.md)** - libobmm-accel 接口总览与 `memcpy_nc_mt()` 说明

### 设备文档
- **[OBMM 内核模块](doc/obmm.md)** - obmm.ko 参数说明与 /dev/obmm 设备说明
- **[共享内存设备](doc/obmm_shmdev.md)** - obmm_shmdev\${mem_id} 设备使用说明

### 系统接口文档
- **[共享内存设备 Sysfs](doc/obmm_shmdev_sysfs.md)** - /sys/devices/obmm/obmm_shmdev\${mem_id}/ 维测目录内容说明
- **[预导入信息（procfs）](doc/obmm_preimport_sysfs.md)** - /proc/obmm/preimport_info 维测文件格式说明
- **[内存池 Sysfs](doc/obmm_mempool_sysfs.md)** - /sys/kernel/obmm_mempool/ 内存池状态说明

### 其他文档
- **[文档目录说明](doc/README.md)** - 文档组织结构说明
- **[发布说明](RELEASE-NOTES.md)** - 版本发布信息

## 🤝 社区支持

- 问题报告: [Gitee Issues](https://gitee.com/openeuler/obmm/issues)
- 文档 Wiki: [项目 Wiki](https://gitee.com/openeuler/obmm/wikis)

## 📄 许可证

本项目内核态驱动OBMM采用 [GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) 许可证。

本项目用户态库libobmm采用木兰宽松许可证第 2 版（Mulan PSL v2），许可证全文见 [License/LICENSE](License/LICENSE)。

## 🏷️ 开源
| 时间 | 事件 | 里程碑 | 
|------|------|------------|
| 2025.09.18 | 华为全联接大会| UB协议发布 |
| 2025.11.14 | openEuler Summit 2025 | OBMM专题讨论 |
| 2025.11.18 | Gitee 开源 | OBMM提交首个社区 Pull Request |
| 2025.12.30 | openEuler 24.03 SP3发布| 作为核心组件集成 |

## 🔗 相关链接

- [Linux 内核官网](https://www.kernel.org/)
- [openEuler 操作系统](https://www.openeuler.org/)
- [灵衢互连总线](https://unifiedbus.com/)
- [openEuler 内核开发文档](https://gitee.com/openeuler/kernel/wikis/Home)

---

**注意**: 本项目需要相应的硬件平台支持和内核态修改支持（相关代码已提交至 [openEuler Kernel PR #19100](https://gitee.com/openeuler/kernel/pulls/19100)）。在部署前请确认您的环境满足所有依赖要求。