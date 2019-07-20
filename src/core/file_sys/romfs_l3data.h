// Code adapted in part from 3dstool by Daowen Sun
// MIT License
// Copyright(c) 2014-2018 Daowen Sun

#pragma once

#include <map>
#include <stack>
#include <vector>
#include "common/common_types.h"

// set compiler specific struct packing
#if defined(_MSC_VER)
#define SDW_MSC_PUSH_PACKED <pshpack1.h>
#define SDW_MSC_POP_PACKED <poppack.h>
#define SDW_GNUC_PACKED
#else
#define SDW_MSC_PUSH_PACKED <cstdlib>
#define SDW_MSC_POP_PACKED <cstdlib>
#define SDW_GNUC_PACKED __attribute__((packed))
#endif

namespace FileSys {

typedef std::basic_string<std::uint16_t> u16str;

#include SDW_MSC_PUSH_PACKED
struct l3HeaderSection {
    u32 Offset;
    u32 Size;
} SDW_GNUC_PACKED;

struct l3Header {
    u32 Size;                   // size of the meta (L3 Header)
    l3HeaderSection Section[4]; // offset & length of Directory Hash, Directory Metadata, File
                                // Hash Table & File Metadata (in order)
    u32 DataOffset;             // File Data Offset
} SDW_GNUC_PACKED;
#include SDW_MSC_POP_PACKED

enum l3HeaderSectionType {
    kSectionTypeDirHash,
    kSectionTypeDir,
    kSectionTypeFileHash,
    kSectionTypeFile
};

// TODO (EddyK28): Add more error checking
//    -crash on load if a file is locked
//    -???

class RomFSL3 {
    struct l3DirEntry // Directory Metadata Structure (minus name string)
    {
        s32 ParentDirOffset;
        s32 SiblingDirOffset;
        s32 ChildDirOffset;
        s32 ChildFileOffset;
        s32 PrevDirOffset;
        s32 NameSize;
    };
    struct l3FileEntry // File Metadata Structure (minus name string)
    {
        s32 ParentDirOffset;
        s32 SiblingFileOffset;
        union {
            s64 FileOffset;
            u64 RemapIgnoreLevel;
        };
        s64 FileSize;
        s32 PrevFileOffset;
        s32 NameSize;
    };
    union l3CommonEntry // Metadata Structure that can be read as either Directory Meta or File Meta
    {
        l3DirEntry Dir;
        l3FileEntry File;
    };
    struct l3Entry // File/Dir entry created when packing
    {
        std::string Path;    //  absolote local file/dir path (on your computer)
        u16str EntryName;    //  name of the file/dir
        int EntryNameSize;   //  length of above name
        s32 EntryOffset;     //
        s32 BucketIndex;     //
        l3CommonEntry Entry; //  the L3 Metadata Structure for this file/dir
    };
    struct l3CreationElement {
        int EntryOffset;
        std::vector<int> ChildOffset;
        int ChildIndex;
    };

public:
    RomFSL3(std::string dirPath);

    bool GetL3Data(u8*& l3data, std::size_t& file_offset, std::size_t& data_size);
    bool GetL3Table(std::map<std::size_t, std::string>*& ofsTable);

    static const int cBlockSizePower;
    static const int cBlockSize;
    static const s32 cInvalidOffset;
    static const int cEntryNameAlignment;
    static const s64 cFileSizeAlignment;

private:
    void BuildL3Data();

    void initialize();

    void pushDirEntry(const std::string& a_sEntryName, s32 a_nParentDirOffset);
    bool pushFileEntry(const std::string& a_sEntryName, s32 a_nParentDirOffset);
    void pushCreateStackElement(int a_nEntryOffset);

    bool createEntryList();

    void removeEmptyDirEntry();
    void removeDirEntry(int a_nIndex);

    void subDirOffset(s32& a_nOffset, int a_nIndex);

    void createHash();

    u32 computeBucketCount(u32 a_uEntries);

    void redirectOffset();
    void redirectOffset(s32& a_nOffset, bool a_bIsDir);

    void buildHeaderData();

    u32 hash(s32 a_nParentOffset, u16str& a_sEntryName);

    std::string romFsDirName;

    l3Header header;
    std::vector<l3Entry> dirList;
    std::vector<l3Entry> fileList;
    std::stack<l3CreationElement> createStack;
    std::vector<s32> dirBucket;
    std::vector<s32> fileBucket;

    s64 l3size;
};

} // namespace FileSys