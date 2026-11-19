# OBMM: Ownership-Based Memory Management Component

[![License](https://img.shields.io/badge/License-GPL--2.0-blue.svg)](https://opensource.org/licenses/GPL-2.0)
[![Kernel Version](https://img.shields.io/badge/Kernel-6.x%2B-green.svg)](https://www.kernel.org/)
[![Unified Bus](https://img.shields.io/badge/SIG--Long-%E7%81%B5%E8%A1%A2(UnifiedBus)-green.svg)](https://www.kernel.org/)
[![CXL](https://img.shields.io/badge/SIG--Long-CXL-red.svg)](https://www.kernel.org/)

OBMM (Ownership Based Memory Management) is a kernel memory management system designed for supernode environments, supporting physical memory sharing across nodes. The system provides efficient remote memory access capabilities through a kernel module (`obmm.ko`) and userspace library (`libobmm.so`).

---

**[📖 中文版](README.md)** | **English Version**

---

**Important Background**: Many bus technologies in sig-Long (such as Unified Bus, CXL) have significant limitations in cross-node data consistency support, leading to data race and inconsistency issues when multiple nodes access the same memory region simultaneously. OBMM's **ownership mechanism** is specifically designed to address this critical challenge by ensuring that only one node has write permission to memory at any given time, enabling safe and reliable cross-node memory sharing in environments lacking hardware consistency guarantees.

## 🚀 Core Features

- **Ownership Management**: Ownership-based memory access control, ensuring data consistency in multi-node environments lacking hardware consistency guarantees
- **Cross-Node Memory Sharing**: Manage remote memory within a single node, supporting memory export and import
- **Transparent Access**: Applications can use regular `load`, `store` instructions to access remote memory
- **NUMA Support**: Bring remote memory online as remote NUMA nodes
- **High Performance**: Optimize hot path latency through preimport
- **Device Interface**: Provide character device interface (`/dev/obmm_shmdev{mem_id}`) for memory mapping

## 🛠️ Quick Start

### Requirements

- **Hardware**: ARM64 server with Unified Bus support; cross-node deployments require UB links and decoder configuration for the actual topology.
- **Version**: the current repository version is 1.2.0 (see [VERSION](VERSION)); the userspace library and kernel must be used as a matched pair.
- **Kernel**: openEuler OLK-6.6 branch (validated against commit `eb444c5`) with the OBMM driver applied ([openEuler Kernel PR #19100](https://gitee.com/openeuler/kernel/pulls/19100)). The userspace library and kernel must use consistent core build options, especially the page size.
- **Kernel command line**: configure `numa_remote` (use `numa_remote=preonline` for preimport) and `pmd_mapping` as required; use `rodata=full` when `PAGE_SIZE=64K`. See the deployment section of the [OBMM Kernel Module](doc/obmm.md) documentation.
- **Kernel configuration**: `UB_UMMU_CORE`, `UB_UBUS`, `HISI_SOC_CACHE` and `TRANSPARENT_HUGEPAGE` must be enabled as built-in kernel configuration options and cannot be loaded with insmod/modprobe; UMMU core is built in as `ummu_core_builtin.o`, and there is no loadable module named `hisi_ummu_core`. Only `hisi_soc_hha` must be loaded additionally; when it is missing, cache flush returns `-EOPNOTSUPP` and export/import fail.
- **Permissions**: sysfs monitoring attributes are `root:root 0400`; `/dev/obmm` and `/dev/obmm_shmdev*` default to `root:root 0600`. Export, import and mapping operations must run as root or a user with the corresponding permissions.
- **Alignment**: the OBMM basic granularity is fixed at 2 MB; export/import addresses and lengths must be 2 MB aligned. `obmm_export_useraddr` is the exception and still requires `PMD_SIZE` alignment.

### Build and Install libobmm

The repository has no top-level build script; build each component separately. `libobmm.h` includes the kernel UAPI header `<ub/obmm.h>`; install it from a kernel source tree matching the running kernel with `make headers_install INSTALL_HDR_PATH=<dir>`, and pass `-I<dir>/include` when compiling both libobmm and applications.

```sh
cmake -S src/libobmm -B build/libobmm -DCMAKE_C_FLAGS="-I<dir>/include"
cmake --build build/libobmm
sudo cmake --install build/libobmm
```

The default install prefix is `/usr/local`; the artifacts are `lib64/libobmm.so` and `include/libobmm.h`. Use `-DCMAKE_INSTALL_PREFIX=<prefix>` for another prefix. With a non-default prefix, compile applications with both `-I<prefix>/include` and `-L<prefix>/lib64`, and set `LD_LIBRARY_PATH` or rpath at runtime, for example:

```sh
cc -I<prefix>/include -o demo demo.c -L<prefix>/lib64 -lobmm
```

### Minimal Example

The example below exports 2 MB of memory on the provider and maps it for access. On the consumer side, fill `obmm_mem_desc` as required by `obmm_import()` (`addr` is the remote physical address supplied by the component that configures the ubmem decoder, and link information such as `seid`/`scna` comes from the deployment), then map the device as described in [Shared Memory Device](doc/obmm_shmdev.md); the mapping must be released with `munmap()` and the fd closed with `close()` before the memory device is destroyed. See [Memory Export](doc/obmm_export.md), [Memory Import](doc/obmm_import.md) and [Shared Memory Device](doc/obmm_shmdev.md) for complete examples.

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

/* deid: EID (16 bytes) of the local UB controller, supplied by the deployment. */
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

	/* Map with no access first, then switch to read/write via obmm_set_ownership(). */
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

	/* The mapping and fd must be released before the memory device is destroyed. */
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
	/* Replace with the actual EID of the local UB controller. */
	uint8_t deid[16] = { 0 };

	return export_map_demo(deid) ? 1 : 0;
}
```

Compile command (`<dir>` is the kernel UAPI header install directory):

```sh
cc -I<dir>/include -o demo demo.c -L/usr/local/lib64 -lobmm
```

## 🎯 Main Application Scenarios

### Scenario 1: Single-Node Memory Extension

Extend available memory capacity in single-node environments, breaking through local physical memory limitations.

**Specific Application Examples**:

- **Large Memory Databases**: Database servers need to process datasets exceeding local physical memory capacity, importing remote memory as NUMA nodes through OBMM for transparent memory capacity extension
- **Memory-Intensive Computing**: Scientific computing, machine learning training scenarios requiring large amounts of memory, dynamically extending memory resources through OBMM
- **Memory Tiering Optimization**: Migrating cold data to remote memory while keeping hot data in local memory for intelligent memory tiering management
- **Container Memory Extension**: Providing additional memory resources for containers, breaking through single-node memory quota limitations

### Scenario 2: Inter-Node Memory Sharing

Achieve efficient memory data sharing and collaboration in multi-node environments.

**Specific Application Examples**:

- **Distributed Cache**: Multiple application nodes share the same cache data, avoiding duplicate storage and improving cache hit rates
- **Real-Time Data Sharing**: Multiple trading nodes in financial trading systems share real-time market data, ensuring data consistency
- **Collaborative Processing Pipelines**: Multiple processing nodes share input data and intermediate results, reducing data transmission overhead
- **High-Availability Clusters**: Primary and backup nodes share state data for rapid failover and state synchronization

## 🚀 Future Application Possibilities

OBMM's ownership mechanism and cross-node memory sharing capabilities provide the technical foundation for more innovative application scenarios in the future:

### Edge Computing and Cloud-Edge Collaboration
- **Edge Node Caching**: Edge devices share cached data, reducing cloud access latency
- **Distributed Inference**: AI inference tasks share models and data among edge nodes
- **Content Delivery Networks**: CDN nodes share popular content, improving access efficiency

### Heterogeneous Computing Acceleration
- **GPU/FPGA Memory Sharing**: CPU and accelerators share large-capacity memory pools
- **Heterogeneous Memory Management**: Unified management of different types of memory media (DDR, HBM, persistent memory)
- **Compute-Storage Fusion**: Storage nodes directly participate in computation, reducing data movement

### New Computing Paradigms
- **Memory-Centric Computing**: Memory-centric computing architecture breaking through traditional CPU-centric limitations
- **Near-Data Computing**: Execute computations near data storage locations, minimizing data movement
- **Distributed Memory Computing**: Distribute computing tasks to nodes where memory is located

### System Software Innovation
- **New Virtualization**: Lightweight virtualization solutions based on memory sharing
- **Distributed Operating Systems**: Cross-node unified memory view and process management
- **Intelligent Memory Scheduling**: AI-driven intelligent memory allocation and migration strategies

> **Technical Vision**: OBMM not only solves current consistency issues but also provides foundational support for future computing architecture innovations. As hardware technology develops, OBMM will continue to evolve, supporting richer application scenarios and higher performance requirements.

## 📚 Documentation

### API Documentation
- **[Userspace Library Overview](doc/libobmm.md)** - libobmm interface overview, core data structures, usage models
- **[Logging](doc/obmm_log.md)** - OBMM log viewing, configuration and troubleshooting
- **[Memory Export](doc/obmm_export.md)** - `obmm_export()` interface documentation
- **[Memory Unexport](doc/obmm_unexport.md)** - `obmm_unexport()` interface documentation
- **[Memory Import](doc/obmm_import.md)** - `obmm_import()` interface documentation
- **[Memory Unimport](doc/obmm_unimport.md)** - `obmm_unimport()` interface documentation
- **[Memory Preimport](doc/obmm_preimport.md)** - `obmm_preimport()` interface documentation
- **[Memory Unpreimport](doc/obmm_unpreimport.md)** - `obmm_unpreimport()` interface documentation
- **[Ownership Setting](doc/obmm_set_ownership.md)** - `obmm_set_ownership()` interface documentation
- **[Memory Query](doc/obmm_query.md)** - `obmm_query_memid_by_pa()`, `obmm_query_pa_by_memid()` interface documentation
- **[Online Backend Switch](doc/obmm_switch.md)** - `obmm_switch_begin_import()`, `obmm_switch_begin_export()`, `obmm_switch_end()` interface documentation
- **[NC-Aware Copy Library](doc/libobmm-accel.md)** - libobmm-accel interface overview and `memcpy_nc_mt()` documentation

### Device Documentation
- **[OBMM Kernel Module](doc/obmm.md)** - obmm.ko parameter description and /dev/obmm device documentation
- **[Shared Memory Device](doc/obmm_shmdev.md)** - obmm_shmdev\${mem_id} device usage documentation

### System Interface Documentation
- **[Shared Memory Device Sysfs](doc/obmm_shmdev_sysfs.md)** - /sys/devices/obmm/obmm_shmdev\${mem_id}/ monitoring directory content description
- **[Preimport Info (procfs)](doc/obmm_preimport_sysfs.md)** - /proc/obmm/preimport_info monitoring file format description
- **[Memory Pool Sysfs](doc/obmm_mempool_sysfs.md)** - /sys/kernel/obmm_mempool/ memory pool status description

### Other Documentation
- **[Documentation Directory](doc/README.md)** - Documentation organization structure description
- **[Release Notes](RELEASE-NOTES.md)** - Version release information

## 🤝 Community Support

- Issue Reporting: [Gitee Issues](https://gitee.com/openeuler/obmm/issues)
- Documentation Wiki: [Project Wiki](https://gitee.com/openeuler/obmm/wikis)

## 📄 License

The kernel module obmm is licensed under the [GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) License.

The userspace libobmm is licensed under the Mulan Permissive Software License v2 (Mulan PSL v2); see [License/LICENSE](License/LICENSE) for the full text.

## 🏷️ Project Timeline

|Date	| Event	| Milestone |
|-------|-------|-----------|
| 2025.09.18 | Huawei Connect 2025 | UB Protocol Released |
| 2025.11.14 | openEuler Summit 2025 | OBMM Technical Session Held |
| 2025.11.18 | Opened Source on Gitee | First Community Pull Request Submitted |
| 2025.12.30 | openEuler 24.03 SP3 Released | Integrated as a Core Component |

## 🔗 Related Links

- [Linux Kernel Official Website](https://www.kernel.org/)
- [openEuler Operating System](https://www.openeuler.org/)
- [Lingqu Interconnect Bus](https://unifiedbus.com/)
- [openEuler Kernel Development Documentation](https://gitee.com/openeuler/kernel/wikis/Home)

---

**Note**: This project requires corresponding hardware platform support and kernel modifications (related code has been submitted to [openEuler Kernel PR #19100](https://gitee.com/openeuler/kernel/pulls/19100)). Please ensure your environment meets all dependency requirements before deployment.
