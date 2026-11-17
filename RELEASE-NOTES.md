## Changelog

### Version 1.2.0
- Add libobmm-accel: multi-threaded NC-aware memory copy library (memcpy_nc_mt)

### Version 1.1.0
- Add online backend switch for obmm_shmdev devices: obmm_switch_begin_import, obmm_switch_begin_export, obmm_switch_end
- Support import -> import and import -> export pool migration without destroying the device
- Add OBMM_MMAP_FLAG_SWITCH_OLD/SWITCH_NEW mapping in the switch gap
- Add switch_state, switch_flush_result, backend_type, mmap_granu and mmap_mode sysfs attributes

### Version 1.0.1
- Add syslog logging for OBMM operations

### Version 1.0.0
- Initial release
- Core export/import APIs
- Ownership management
