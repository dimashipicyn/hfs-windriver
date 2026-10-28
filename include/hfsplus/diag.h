#ifndef HFSPLUS_DIAG_H
#define HFSPLUS_DIAG_H

#include "hfsplus/block_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run comprehensive diagnostics on a physical drive or image file.
 * Analyzes MBR, GPT, APFS/HFS+ partition types, Volume Headers,
 * Catalog B-Tree nodes, and lists all found entries.
 */
int hfs_run_diagnostics(hfs_block_device_t* dev);

#ifdef __cplusplus
}
#endif

#endif /* HFSPLUS_DIAG_H */
