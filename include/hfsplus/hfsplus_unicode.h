#ifndef HFSPLUS_UNICODE_H
#define HFSPLUS_UNICODE_H

#include "hfsplus/hfsplus_defs.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Convert HFSUniStr255 (UTF-16BE on disk) to null-terminated UTF-8 string */
int hfs_unistr_to_utf8(const HFSUniStr255* src, char* dst, size_t dst_size);

/* Convert null-terminated UTF-8 string to HFSUniStr255 (UTF-16BE) */
int hfs_utf8_to_unistr(const char* src, HFSUniStr255* dst);

/* Convert HFSUniStr255 (UTF-16BE) to Windows UTF-16LE (WCHAR) string */
int hfs_unistr_to_utf16le(const HFSUniStr255* src, uint16_t* dst, size_t dst_chars);

/* Compare two HFSUniStr255 strings according to HFS+ collation (case-insensitive or binary) */
int hfs_compare_unistr(const HFSUniStr255* s1, const HFSUniStr255* s2, bool case_sensitive);

/* Compare two HFSPlusCatalogKeys (parentID first, then nodeName) */
int hfs_catalog_key_compare(const void* k1, const void* k2, bool case_sensitive);

/* Compare two HFSPlusExtentKeys (fileID first, then forkType, then startBlock) */
int hfs_extent_key_compare(const void* k1, const void* k2, bool case_sensitive);

#ifdef __cplusplus
}
#endif

#endif /* HFSPLUS_UNICODE_H */
