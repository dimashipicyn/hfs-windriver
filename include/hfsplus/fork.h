#ifndef HFSPLUS_FORK_H
#define HFSPLUS_FORK_H

#include "hfsplus/volume.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read 'count' bytes from 'fork' starting at 'offset' into 'buffer'.
 * Resolves logical file offset into volume allocation blocks using initial 8 extents.
 * Returns 0 on success, negative error code on failure. */
int hfs_fork_read(hfs_volume_t* vol, const hfs_fork_t* fork, uint64_t offset, void* buffer, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* HFSPLUS_FORK_H */
