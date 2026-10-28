#include "hfsplus/hfsplus_unicode.h"
#include "hfsplus/hfsplus_endian.h"
#include <string.h>

static uint16_t fold_case(uint16_t c) {
    /* ASCII uppercase to lowercase */
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    /* Cyrillic А-Я to а-я */
    if (c >= 0x0410 && c <= 0x042F) {
        return c + 0x20;
    }
    /* Cyrillic Ё to ё */
    if (c == 0x0401) {
        return 0x0451;
    }
    return c;
}

int hfs_compare_unistr(const HFSUniStr255* s1, const HFSUniStr255* s2, bool case_sensitive) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    uint16_t len1 = be16_to_cpu(s1->length);
    uint16_t len2 = be16_to_cpu(s2->length);
    if (len1 > 255) len1 = 255;
    if (len2 > 255) len2 = 255;

    uint16_t min_len = (len1 < len2) ? len1 : len2;

    for (uint16_t i = 0; i < min_len; i++) {
        uint16_t c1 = be16_to_cpu(s1->unicode[i]);
        uint16_t c2 = be16_to_cpu(s2->unicode[i]);

        if (!case_sensitive) {
            c1 = fold_case(c1);
            c2 = fold_case(c2);
        }

        if (c1 != c2) {
            return (c1 < c2) ? -1 : 1;
        }
    }

    if (len1 < len2) return -1;
    if (len1 > len2) return 1;
    return 0;
}

int hfs_catalog_key_compare(const void* key1, const void* key2, bool case_sensitive) {
    const HFSPlusCatalogKey* k1 = (const HFSPlusCatalogKey*)key1;
    const HFSPlusCatalogKey* k2 = (const HFSPlusCatalogKey*)key2;
    uint32_t p1 = be32_to_cpu(k1->parentID);
    uint32_t p2 = be32_to_cpu(k2->parentID);

    if (p1 < p2) return -1;
    if (p1 > p2) return 1;

    return hfs_compare_unistr(&k1->nodeName, &k2->nodeName, case_sensitive);
}

int hfs_extent_key_compare(const void* key1, const void* key2, bool case_sensitive) {
    (void)case_sensitive;
    const HFSPlusExtentKey* k1 = (const HFSPlusExtentKey*)key1;
    const HFSPlusExtentKey* k2 = (const HFSPlusExtentKey*)key2;
    uint32_t f1 = be32_to_cpu(k1->fileID);
    uint32_t f2 = be32_to_cpu(k2->fileID);
    if (f1 < f2) return -1;
    if (f1 > f2) return 1;

    if (k1->forkType < k2->forkType) return -1;
    if (k1->forkType > k2->forkType) return 1;

    uint32_t s1 = be32_to_cpu(k1->startBlock);
    uint32_t s2 = be32_to_cpu(k2->startBlock);
    if (s1 < s2) return -1;
    if (s1 > s2) return 1;

    return 0;
}

int hfs_unistr_to_utf8(const HFSUniStr255* src, char* dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return -1;

    uint16_t len = be16_to_cpu(src->length);
    if (len > 255) len = 255;

    size_t out_idx = 0;

    for (uint16_t i = 0; i < len; i++) {
        uint32_t cp = be16_to_cpu(src->unicode[i]);

        /* Check for surrogate pair */
        if (cp >= 0xD800 && cp <= 0xDBFF && (i + 1) < len) {
            uint32_t next = be16_to_cpu(src->unicode[i + 1]);
            if (next >= 0xDC00 && next <= 0xDFFF) {
                cp = 0x10000 + (((cp - 0xD800) << 10) | (next - 0xDC00));
                i++;
            }
        }

        if (cp <= 0x7F) {
            if (out_idx + 1 >= dst_size) break;
            dst[out_idx++] = (char)cp;
        } else if (cp <= 0x7FF) {
            if (out_idx + 2 >= dst_size) break;
            dst[out_idx++] = (char)(0xC0 | (cp >> 6));
            dst[out_idx++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            if (out_idx + 3 >= dst_size) break;
            dst[out_idx++] = (char)(0xE0 | (cp >> 12));
            dst[out_idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[out_idx++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (out_idx + 4 >= dst_size) break;
            dst[out_idx++] = (char)(0xF0 | (cp >> 18));
            dst[out_idx++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[out_idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[out_idx++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    dst[out_idx] = '\0';
    return (int)out_idx;
}

int hfs_utf8_to_unistr(const char* src, HFSUniStr255* dst) {
    if (!src || !dst) return -1;

    size_t in_len = strlen(src);
    uint16_t out_count = 0;
    size_t i = 0;

    while (i < in_len && out_count < 255) {
        uint32_t cp = 0;
        uint8_t c = (uint8_t)src[i++];

        if (c <= 0x7F) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            if (i >= in_len) break;
            cp = ((c & 0x1F) << 6) | ((uint8_t)src[i++] & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 1 >= in_len) break;
            cp = ((c & 0x0F) << 12) | (((uint8_t)src[i] & 0x3F) << 6) | ((uint8_t)src[i+1] & 0x3F);
            i += 2;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 2 >= in_len) break;
            cp = ((c & 0x07) << 18) | (((uint8_t)src[i] & 0x3F) << 12) |
                 (((uint8_t)src[i+1] & 0x3F) << 6) | ((uint8_t)src[i+2] & 0x3F);
            i += 3;
        }

        if (cp <= 0xFFFF) {
            dst->unicode[out_count++] = cpu_to_be16((uint16_t)cp);
        } else if (cp <= 0x10FFFF) {
            if (out_count + 1 >= 255) break;
            cp -= 0x10000;
            dst->unicode[out_count++] = cpu_to_be16((uint16_t)(0xD800 + (cp >> 10)));
            dst->unicode[out_count++] = cpu_to_be16((uint16_t)(0xDC00 + (cp & 0x3FF)));
        }
    }

    dst->length = cpu_to_be16(out_count);
    return (int)out_count;
}

int hfs_unistr_to_utf16le(const HFSUniStr255* src, uint16_t* dst, size_t dst_chars) {
    if (!src || !dst || dst_chars == 0) return -1;

    uint16_t len = be16_to_cpu(src->length);
    if (len > 255) len = 255;

    size_t copy_len = (len < (dst_chars - 1)) ? len : (dst_chars - 1);
    for (size_t i = 0; i < copy_len; i++) {
        dst[i] = be16_to_cpu(src->unicode[i]);
    }
    dst[copy_len] = 0;
    return (int)copy_len;
}
