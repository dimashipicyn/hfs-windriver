#ifndef HFSPLUS_MEM_H
#define HFSPLUS_MEM_H

#include <stddef.h>

#if defined(_KERNEL_MODE) || defined(NTDDI_VERSION)
#include <ntifs.h>
#define HFS_POOL_TAG 'SFH+'

static inline void* hfs_alloc(size_t size) {
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, HFS_POOL_TAG);
}

static inline void* hfs_zalloc(size_t size) {
    return ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, size, HFS_POOL_TAG);
}

static inline void hfs_free(void* ptr) {
    if (ptr) {
        ExFreePoolWithTag(ptr, HFS_POOL_TAG);
    }
}
#else
#include <stdlib.h>
#include <string.h>

static inline void* hfs_alloc(size_t size) {
    return malloc(size);
}

static inline void* hfs_zalloc(size_t size) {
    return calloc(1, size);
}

static inline void hfs_free(void* ptr) {
    free(ptr);
}
#endif

#endif /* HFSPLUS_MEM_H */
