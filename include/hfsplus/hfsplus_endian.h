#ifndef HFSPLUS_ENDIAN_H
#define HFSPLUS_ENDIAN_H

#include <stdint.h>
#include <time.h>

#if defined(_MSC_VER)
#include <stdlib.h>
static inline uint16_t hfs_bswap16(uint16_t x) { return _byteswap_ushort(x); }
static inline uint32_t hfs_bswap32(uint32_t x) { return _byteswap_ulong(x); }
static inline uint64_t hfs_bswap64(uint64_t x) { return _byteswap_uint64(x); }
#elif defined(__GNUC__) || defined(__clang__)
static inline uint16_t hfs_bswap16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t hfs_bswap32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t hfs_bswap64(uint64_t x) { return __builtin_bswap64(x); }
#else
static inline uint16_t hfs_bswap16(uint16_t x) {
    return (uint16_t)(((x & 0x00FFU) << 8) | ((x & 0xFF00U) >> 8));
}
static inline uint32_t hfs_bswap32(uint32_t x) {
    return ((x & 0x000000FFU) << 24) |
           ((x & 0x0000FF00U) << 8)  |
           ((x & 0x00FF0000U) >> 8)  |
           ((x & 0xFF000000U) >> 24);
}
static inline uint64_t hfs_bswap64(uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8)  |
           ((x & 0x000000FF00000000ULL) >> 8)  |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
}
#endif

/* HFS+ is big-endian on disk. Most hosts (x86_64, aarch64) are little-endian. */
#define be16_to_cpu(x) hfs_bswap16(x)
#define be32_to_cpu(x) hfs_bswap32(x)
#define be64_to_cpu(x) hfs_bswap64(x)

#define cpu_to_be16(x) hfs_bswap16(x)
#define cpu_to_be32(x) hfs_bswap32(x)
#define cpu_to_be64(x) hfs_bswap64(x)

/* Seconds between Jan 1 1904 (HFS+ epoch) and Jan 1 1970 (Unix epoch) */
#define HFSPLUS_TO_UNIX_OFFSET 2082844800ULL

/* Seconds between Jan 1 1601 (Windows epoch) and Jan 1 1904 (HFS+ epoch) */
#define WINDOWS_TO_HFSPLUS_OFFSET 9561628800ULL

/* Convert HFS+ timestamp (seconds since 1904) to Unix time_t */
static inline time_t hfs_to_unix_time(uint32_t hfs_time) {
    if ((uint64_t)hfs_time < HFSPLUS_TO_UNIX_OFFSET) {
        return 0;
    }
    return (time_t)((uint64_t)hfs_time - HFSPLUS_TO_UNIX_OFFSET);
}

/* Convert Unix time_t to HFS+ timestamp */
static inline uint32_t unix_to_hfs_time(time_t unix_time) {
    if (unix_time < -(time_t)HFSPLUS_TO_UNIX_OFFSET) {
        return 0;
    }
    return (uint32_t)((int64_t)unix_time + (int64_t)HFSPLUS_TO_UNIX_OFFSET);
}

/* Convert HFS+ timestamp to Windows 64-bit FILETIME (100-nanosecond intervals since Jan 1 1601) */
static inline uint64_t hfs_to_windows_filetime(uint32_t hfs_time) {
    if (hfs_time == 0) {
        return 0;
    }
    uint64_t total_sec = (uint64_t)hfs_time + WINDOWS_TO_HFSPLUS_OFFSET;
    return total_sec * 10000000ULL;
}

#endif /* HFSPLUS_ENDIAN_H */
