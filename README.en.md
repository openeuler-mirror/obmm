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

## 🤝 Community Support

- Issue Reporting: [Gitee Issues](https://gitee.com/openeuler/obmm/issues)
- Documentation Wiki: [Project Wiki](https://gitee.com/openeuler/obmm/wikis)

## 📄 License

This project is licensed under the [GPL-2.0](LICENSE/GPL-2.0) License.

## 🔗 Related Links

- [Linux Kernel Official Website](https://www.kernel.org/)
- [openEuler Operating System](https://www.openeuler.org/)
- [Lingqu Interconnect Bus](https://unifiedbus.com/)
- [openEuler Kernel Development Documentation](https://gitee.com/openeuler/kernel/wikis/Home)

---

**Note**: This project requires corresponding hardware platform support. Please ensure your environment meets all dependency requirements before deployment.