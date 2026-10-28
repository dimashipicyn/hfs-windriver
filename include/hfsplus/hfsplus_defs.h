#ifndef HFSPLUS_DEFS_H
#define HFSPLUS_DEFS_H

#include <stdint.h>
#include <stdbool.h>

#pragma pack(push, 2)

/* Volume signatures */
#define kHFSPlusSigWord         0x482B  /* 'H+' */
#define kHFSXSigWord            0x4858  /* 'HX' */
#define kHFSPlusVersion         0x0004

/* Standard CNIDs (Catalog Node IDs) */
#define kHFSRootParentID            1
#define kHFSRootFolderID            2
#define kHFSExtentsFileID           3
#define kHFSCatalogFileID           4
#define kHFSBadBlockFileID          5
#define kHFSAllocationFileID        6
#define kHFSStartupFileID           7
#define kHFSAttributesFileID        8
#define kHFSBogusExtentFileID       15
#define kHFSFirstUserCatalogNodeID  16

/* Extent descriptor */
typedef struct HFSPlusExtentDescriptor {
    uint32_t startBlock;
    uint32_t blockCount;
} HFSPlusExtentDescriptor;

typedef HFSPlusExtentDescriptor HFSPlusExtentRecord[8];

/* Fork Data */
typedef struct HFSPlusForkData {
    uint64_t logicalSize;
    uint32_t clumpSize;
    uint32_t totalBlocks;
    HFSPlusExtentRecord extents;
} HFSPlusForkData;

/* Volume Header (located at offset 1024 / sector 2) */
typedef struct HFSPlusVolumeHeader {
    uint16_t signature;
    uint16_t version;
    uint32_t attributes;
    uint32_t lastMountedVersion;
    uint32_t journalInfoBlock;
    uint32_t createDate;
    uint32_t modifyDate;
    uint32_t backupDate;
    uint32_t checkedDate;
    uint32_t fileCount;
    uint32_t folderCount;
    uint32_t blockSize;
    uint32_t totalBlocks;
    uint32_t freeBlocks;
    uint32_t nextAllocation;
    uint32_t rsrcClumpSize;
    uint32_t dataClumpSize;
    uint32_t nextCatalogID;
    uint32_t writeCount;
    uint64_t encodingsBitmap;
    uint8_t  finderInfo[32];
    HFSPlusForkData allocationFile;
    HFSPlusForkData extentsFile;
    HFSPlusForkData catalogFile;
    HFSPlusForkData attributesFile;
    HFSPlusForkData startupFile;
} HFSPlusVolumeHeader;

/* B-Tree Node Descriptor (at byte 0 of every B-tree node) */
typedef struct BTNodeDescriptor {
    uint32_t fLink;
    uint32_t bLink;
    int8_t   kind;
    uint8_t  height;
    uint16_t numRecords;
    uint16_t reserved;
} BTNodeDescriptor;

/* BTNodeDescriptor kind values */
#define kBTLeafNode       (-1)
#define kBTIndexNode      0
#define kBTHeaderNode     1
#define kBTMapNode        2

/* B-Tree Header Record (first record in Header Node 0) */
typedef struct BTHeaderRec {
    uint16_t treeDepth;
    uint32_t rootNode;
    uint32_t leafRecords;
    uint32_t firstLeafNode;
    uint32_t lastLeafNode;
    uint16_t nodeSize;
    uint16_t maxKeyLength;
    uint32_t totalNodes;
    uint32_t freeNodes;
    uint16_t reserved1;
    uint32_t clumpSize;
    uint8_t  btreeType;
    uint8_t  keyCompareType;
    uint32_t attributes;
    uint32_t reserved3[16];
} BTHeaderRec;

/* Key compare types */
#define kHFSCaseFolding         0xCF
#define kHFSBinaryCompare       0xBC

/* Unicode String representation */
typedef struct HFSUniStr255 {
    uint16_t length;
    uint16_t unicode[255];
} HFSUniStr255;

/* Catalog Record Types */
#define kHFSPlusFolderRecord        0x0001
#define kHFSPlusFileRecord          0x0002
#define kHFSPlusFolderThreadRecord  0x0003
#define kHFSPlusFileThreadRecord    0x0004

/* Catalog Key */
typedef struct HFSPlusCatalogKey {
    uint16_t      keyLength;
    uint32_t      parentID;
    HFSUniStr255  nodeName;
} HFSPlusCatalogKey;

/* POSIX / BSD Permissions Info */
typedef struct HFSPlusBSDInfo {
    uint32_t ownerID;
    uint32_t groupID;
    uint8_t  adminFlags;
    uint8_t  ownerFlags;
    uint16_t fileMode;
    union {
        uint32_t iNodeNum;
        uint32_t linkCount;
        uint32_t rawDevice;
    } special;
} HFSPlusBSDInfo;

/* Catalog Folder Record */
typedef struct HFSPlusCatalogFolder {
    uint16_t        recordType;
    uint16_t        flags;
    uint32_t        valence;
    uint32_t        folderID;
    uint32_t        createDate;
    uint32_t        contentModDate;
    uint32_t        attributeModDate;
    uint32_t        accessDate;
    uint32_t        backupDate;
    HFSPlusBSDInfo  bsdInfo;
    uint8_t         userInfo[16];
    uint8_t         finderInfo[16];
    uint32_t        textEncoding;
    uint32_t        reserved;
} HFSPlusCatalogFolder;

/* Catalog File Record */
typedef struct HFSPlusCatalogFile {
    uint16_t        recordType;
    uint16_t        flags;
    uint32_t        reserved1;
    uint32_t        fileID;
    uint32_t        createDate;
    uint32_t        contentModDate;
    uint32_t        attributeModDate;
    uint32_t        accessDate;
    uint32_t        backupDate;
    HFSPlusBSDInfo  bsdInfo;
    uint8_t         userInfo[16];
    uint8_t         finderInfo[16];
    uint32_t        textEncoding;
    uint32_t        reserved2;
    HFSPlusForkData dataFork;
    HFSPlusForkData resourceFork;
} HFSPlusCatalogFile;

/* Catalog Thread Record */
typedef struct HFSPlusCatalogThread {
    uint16_t      recordType;
    uint16_t      reserved;
    uint32_t      parentID;
    HFSUniStr255  nodeName;
} HFSPlusCatalogThread;

/* Extents Overflow Key */
#define kHFSPlusDataFork        0x00
#define kHFSPlusResourceFork    0xFF

typedef struct HFSPlusExtentKey {
    uint16_t keyLength;
    uint8_t  forkType;
    uint8_t  pad;
    uint32_t fileID;
    uint32_t startBlock;
} HFSPlusExtentKey;

#pragma pack(pop)

#endif /* HFSPLUS_DEFS_H */
