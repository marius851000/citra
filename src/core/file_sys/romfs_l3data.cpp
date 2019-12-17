// Code adapted in part from 3dstool by Daowen Sun
// MIT License
// Copyright(c) 2014-2018 Daowen Sun

#include <codecvt>
#include "common/file_util.h"
#include "romfs_l3data.h"

#include <locale>
#include <cstring>

namespace FileSys {

s64 Align(s64 a_nData, s64 a_nAlignment) {
    return (a_nData + a_nAlignment - 1) / a_nAlignment * a_nAlignment;
}

std::u16string ToU16(const std::string& s) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
    return conv.from_bytes(s);
}

const int RomFSL3::cBlockSizePower = 0xC;
const int RomFSL3::cBlockSize = 1 << cBlockSizePower;
const s32 RomFSL3::cInvalidOffset = -1;
const int RomFSL3::cEntryNameAlignment = 4;
const s64 RomFSL3::cFileSizeAlignment = 0x10;

RomFSL3::RomFSL3(std::string dirPath) : l3size(0), romFsDirName(dirPath) {
    BuildL3Data();
}

bool RomFSL3::GetL3Data(u8*& l3data, std::size_t& file_offset, std::size_t& data_size) {
    if (l3data)
        return false;

    file_offset = header.DataOffset;
    // TODO (EddyK28): get final romfs file size (including L1 and L2)?
    data_size = Align(l3size + header.DataOffset, cBlockSize);
    l3data = new u8[header.DataOffset]{0};
    u8* L3WritePos = l3data;

    // write RomFS Meta(L3 Header) data to L3
    memcpy(L3WritePos, &header, sizeof(header));
    L3WritePos += sizeof(header);

    // write dir hash table to L3
    memcpy(L3WritePos, &*dirBucket.begin(), header.Section[kSectionTypeDirHash].Size);
    L3WritePos += header.Section[kSectionTypeDirHash].Size;

    // write dir entries to L3
    for (int i = 0; i < static_cast<int>(dirList.size()); i++) {
        l3Entry& currentEntry = dirList[i];
        memcpy(L3WritePos, &currentEntry.Entry.Dir, sizeof(currentEntry.Entry.Dir));
        L3WritePos += sizeof(currentEntry.Entry.Dir);
        memcpy(L3WritePos, currentEntry.EntryName.c_str(), currentEntry.EntryNameSize);
        L3WritePos += currentEntry.EntryNameSize;
    }

    // write file hash table to L3
    memcpy(L3WritePos, &*fileBucket.begin(), header.Section[kSectionTypeFileHash].Size);
    L3WritePos += header.Section[kSectionTypeFileHash].Size;

    // write file entries to L3
    for (int i = 0; i < static_cast<int>(fileList.size()); i++) {
        l3Entry& currentEntry = fileList[i];
        memcpy(L3WritePos, &currentEntry.Entry.File, sizeof(currentEntry.Entry.File));
        L3WritePos += sizeof(currentEntry.Entry.File);
        memcpy(L3WritePos, currentEntry.EntryName.c_str(), currentEntry.EntryNameSize);
        L3WritePos += currentEntry.EntryNameSize;
    }

    return true;
}

bool RomFSL3::GetL3Table(std::map<std::size_t, std::string>*& ofsTable) {
    if (ofsTable)
        return false;

    ofsTable = new std::map<std::size_t, std::string>();
    // TODO (EddyK28): pre-set capacity

    for (const l3Entry& entry : fileList) {
        if (entry.Entry.File.FileSize > 0)
            ofsTable->emplace(entry.Entry.File.FileOffset + header.DataOffset, entry.Path);
    }

    return true;
}

void RomFSL3::BuildL3Data() {
    bool bResult = true;
    initialize();        // set some basic L3 Header values (all from constants or struct size)
    pushDirEntry("", 0); // push root dir to dir list
    pushCreateStackElement(0);   // create "Create Stack" element for root dir?
    while (!createStack.empty()) // populate file/dir lists using the "Create Stack"
    {
        if (!createEntryList()) {
            bResult = false;
        }
    }
    removeEmptyDirEntry(); // remove any empty dirs from the dir list
    createHash();          // create the file/dir hash tables
    redirectOffset();      // convert hash tables from index to offset
    buildHeaderData();     // set L3 Header values based on found files/dirs
}

void RomFSL3::initialize() {
    header.Size = sizeof(header);
    header.Section[kSectionTypeDirHash].Offset =
        static_cast<u32>(Align(header.Size, cEntryNameAlignment));
    header.Section[kSectionTypeDirHash].Size = 0;
    for (int i = 1; i < 4; i++) {
        header.Section[i].Offset = 0;
        header.Section[i].Size = 0;
    }
    header.DataOffset = 0;
}

void RomFSL3::pushDirEntry(const std::string& entryName, s32 parentDirOffset) {
    dirList.push_back(l3Entry());           // add entry to list of Dir Entries
    l3Entry& currentEntry = dirList.back(); // get a ref to it

    // if it's the only one on the list (it's root), its path is the RomFS Dir
    if (dirList.size() == 1) {
        currentEntry.Path = romFsDirName;
    } else { // otherwise build its path based on its parent's path
        currentEntry.Path = dirList[parentDirOffset].Path + "/" + entryName;
    }

    // set entry name to given name
    currentEntry.EntryName = ToU16(entryName);

    // set entry parent offset to given offset and set all other offsets as invalid
    currentEntry.Entry.Dir.ParentDirOffset = parentDirOffset;
    currentEntry.Entry.Dir.SiblingDirOffset = cInvalidOffset;
    currentEntry.Entry.Dir.ChildDirOffset = cInvalidOffset;
    currentEntry.Entry.Dir.ChildFileOffset = cInvalidOffset;
    currentEntry.Entry.Dir.PrevDirOffset = cInvalidOffset;

    // get and store size of entry name
    currentEntry.Entry.Dir.NameSize = static_cast<s32>(currentEntry.EntryName.size() * 2);
    currentEntry.EntryNameSize =
        static_cast<int>(Align(currentEntry.Entry.Dir.NameSize, cEntryNameAlignment));

    //
    if (dirList[parentDirOffset].Entry.Dir.ChildDirOffset != cInvalidOffset &&
        dirList.size() - 1 != dirList[parentDirOffset].Entry.Dir.ChildDirOffset) {
        dirList[dirList.size() - 2].Entry.Dir.SiblingDirOffset =
            static_cast<s32>(dirList.size() - 1);
    }
}

bool RomFSL3::pushFileEntry(const std::string& entryName, s32 a_nParentDirOffset) {
    bool bResult = true;
    fileList.push_back(l3Entry());
    l3Entry& currentEntry = fileList.back();
    currentEntry.Path = dirList[a_nParentDirOffset].Path + "/" + entryName;
    currentEntry.EntryName = ToU16(entryName);
    currentEntry.EntryOffset =
        static_cast<s32>(Align(header.Section[kSectionTypeFile].Size, cEntryNameAlignment));
    currentEntry.Entry.File.ParentDirOffset = a_nParentDirOffset;
    currentEntry.Entry.File.SiblingFileOffset = cInvalidOffset;
    currentEntry.Entry.File.FileOffset = Align(l3size, cFileSizeAlignment);
    currentEntry.Entry.File.FileSize = FileUtil::GetSize(currentEntry.Path);
    currentEntry.Entry.File.PrevFileOffset = cInvalidOffset;
    currentEntry.Entry.File.NameSize = static_cast<s32>(currentEntry.EntryName.size() * 2);
    currentEntry.EntryNameSize =
        static_cast<int>(Align(currentEntry.Entry.File.NameSize, cEntryNameAlignment));
    if (dirList[a_nParentDirOffset].Entry.Dir.ChildFileOffset != cInvalidOffset &&
        fileList.size() - 1 != dirList[a_nParentDirOffset].Entry.Dir.ChildFileOffset) {
        fileList[fileList.size() - 2].Entry.File.SiblingFileOffset =
            static_cast<s32>(fileList.size() - 1);
    }
    header.Section[kSectionTypeFile].Size =
        currentEntry.EntryOffset + sizeof(currentEntry.Entry.File) + currentEntry.EntryNameSize;
    l3size = currentEntry.Entry.File.FileOffset + currentEntry.Entry.File.FileSize;
    return bResult;
}

void RomFSL3::pushCreateStackElement(int entryOffset) {
    createStack.push(l3CreationElement());
    l3CreationElement& current = createStack.top();
    current.EntryOffset = entryOffset;
    current.ChildIndex = -1;
}

bool RomFSL3::createEntryList() {
    bool bResult = true;
    // get ref to top "creation element" (each create element corresponds to a dir entry)
    l3CreationElement& current = createStack.top();

    if (current.ChildIndex == -1) // if it has no children
    {
        // get all dirs and files in this element's dir // TODO: what to do if nothing found?
        FileUtil::FSTEntry entries;
        FileUtil::ScanDirectoryTree(dirList[current.EntryOffset].Path, entries);

        // for each entry
        for (auto entry : entries.children) {
            // if file
            if (!entry.isDirectory) {
                // do file things
                if (dirList[current.EntryOffset].Entry.Dir.ChildFileOffset == cInvalidOffset) {
                    dirList[current.EntryOffset].Entry.Dir.ChildFileOffset =
                        static_cast<s32>(fileList.size());
                }
                // add an entry for this file
                if (!pushFileEntry(entry.virtualName, current.EntryOffset)) {
                    bResult = false; //  set result to false if this fails
                }
            } else // elseif valid dir
            {
                // do dir things
                if (dirList[current.EntryOffset].Entry.Dir.ChildDirOffset == cInvalidOffset) {
                    dirList[current.EntryOffset].Entry.Dir.ChildDirOffset =
                        static_cast<s32>(dirList.size());
                }
                current.ChildOffset.push_back(static_cast<int>(dirList.size()));
                pushDirEntry(entry.virtualName, current.EntryOffset); // add an entry for this dir
            }
        }
        current.ChildIndex = 0;
    } else if (current.ChildIndex != current.ChildOffset.size()) {
        pushCreateStackElement(current.ChildOffset[current.ChildIndex++]);
    } else {
        createStack.pop();
    }
    return bResult;
}

void RomFSL3::removeEmptyDirEntry() {
    int nEmptyDirIndex;
    do {
        nEmptyDirIndex = 0;
        for (int i = static_cast<int>(dirList.size()) - 1; i > 0; i--) {
            l3Entry& currentEntry = dirList[i];
            if (currentEntry.Entry.Dir.ChildDirOffset == cInvalidOffset &&
                currentEntry.Entry.Dir.ChildFileOffset == cInvalidOffset) {
                nEmptyDirIndex = i;
                break;
            }
        }
        if (nEmptyDirIndex > 0) {
            removeDirEntry(nEmptyDirIndex);
        }
    } while (nEmptyDirIndex > 0);
    for (int i = 0; i < static_cast<int>(dirList.size()); i++) {
        l3Entry& currentEntry = dirList[i];
        currentEntry.EntryOffset =
            static_cast<s32>(Align(header.Section[kSectionTypeDir].Size, cEntryNameAlignment));
        header.Section[kSectionTypeDir].Size =
            currentEntry.EntryOffset + sizeof(currentEntry.Entry.Dir) + currentEntry.EntryNameSize;
    }
}

void RomFSL3::removeDirEntry(int index) {
    l3Entry& removedEntry = dirList[index];
    l3Entry& siblingEntry = dirList[index - 1];
    l3Entry& parentEntry = dirList[removedEntry.Entry.Dir.ParentDirOffset];
    if (siblingEntry.Entry.Dir.SiblingDirOffset == index) {
        siblingEntry.Entry.Dir.SiblingDirOffset = removedEntry.Entry.Dir.SiblingDirOffset;
    } else if (parentEntry.Entry.Dir.ChildDirOffset == index) {
        parentEntry.Entry.Dir.ChildDirOffset = removedEntry.Entry.Dir.SiblingDirOffset;
    }
    for (int i = 0; i < static_cast<int>(dirList.size()); i++) {
        l3Entry& currentEntry = dirList[i];
        subDirOffset(currentEntry.Entry.Dir.ParentDirOffset, index);
        subDirOffset(currentEntry.Entry.Dir.SiblingDirOffset, index);
        subDirOffset(currentEntry.Entry.Dir.ChildDirOffset, index);
    }
    for (int i = 0; i < static_cast<int>(fileList.size()); i++) {
        l3Entry& currentEntry = fileList[i];
        subDirOffset(currentEntry.Entry.File.ParentDirOffset, index);
    }
    dirList.erase(dirList.begin() + index);
}

void RomFSL3::subDirOffset(s32& offset, int index) {
    if (offset > index) {
        offset--;
    }
}

void RomFSL3::createHash() {
    dirBucket.resize(computeBucketCount(static_cast<u32>(dirList.size())), cInvalidOffset);
    fileBucket.resize(computeBucketCount(static_cast<u32>(fileList.size())), cInvalidOffset);
    for (int i = 0; i < static_cast<int>(dirList.size()); i++) {
        l3Entry& currentEntry = dirList[i];
        currentEntry.BucketIndex = hash(dirList[currentEntry.Entry.Dir.ParentDirOffset].EntryOffset,
                                        currentEntry.EntryName) %
                                   dirBucket.size();
        if (dirBucket[currentEntry.BucketIndex] != cInvalidOffset) {
            currentEntry.Entry.Dir.PrevDirOffset = dirBucket[currentEntry.BucketIndex];
        }
        dirBucket[currentEntry.BucketIndex] = i;
    }
    for (int i = 0; i < static_cast<int>(fileList.size()); i++) {
        l3Entry& currentEntry = fileList[i];
        currentEntry.BucketIndex =
            hash(dirList[currentEntry.Entry.File.ParentDirOffset].EntryOffset,
                 currentEntry.EntryName) %
            fileBucket.size();
        if (fileBucket[currentEntry.BucketIndex] != cInvalidOffset) {
            currentEntry.Entry.File.PrevFileOffset = fileBucket[currentEntry.BucketIndex];
        }
        fileBucket[currentEntry.BucketIndex] = i;
    }
}

u32 RomFSL3::computeBucketCount(u32 entries) {
    u32 uBucket = entries;
    if (uBucket < 3) {
        uBucket = 3;
    } else if (uBucket <= 19) {
        uBucket |= 1;
    } else {
        while (uBucket % 2 == 0 || uBucket % 3 == 0 || uBucket % 5 == 0 || uBucket % 7 == 0 ||
               uBucket % 11 == 0 || uBucket % 13 == 0 || uBucket % 17 == 0) {
            uBucket += 1;
        }
    }
    return uBucket;
}

void RomFSL3::redirectOffset() {
    for (int i = 0; i < static_cast<int>(dirBucket.size()); i++) {
        redirectOffset(dirBucket[i], true);
    }
    for (int i = 0; i < static_cast<int>(fileBucket.size()); i++) {
        redirectOffset(fileBucket[i], false);
    }
    for (int i = 0; i < static_cast<int>(dirList.size()); i++) {
        l3Entry& currentEntry = dirList[i];
        redirectOffset(currentEntry.Entry.Dir.ParentDirOffset, true);
        redirectOffset(currentEntry.Entry.Dir.SiblingDirOffset, true);
        redirectOffset(currentEntry.Entry.Dir.ChildDirOffset, true);
        redirectOffset(currentEntry.Entry.Dir.ChildFileOffset, false);
        redirectOffset(currentEntry.Entry.Dir.PrevDirOffset, true);
    }
    for (int i = 0; i < static_cast<int>(fileList.size()); i++) {
        l3Entry& currentEntry = fileList[i];
        redirectOffset(currentEntry.Entry.File.ParentDirOffset, true);
        redirectOffset(currentEntry.Entry.File.SiblingFileOffset, false);
        redirectOffset(currentEntry.Entry.File.PrevFileOffset, false);
    }
}

void RomFSL3::redirectOffset(s32& offset, bool isDir) {
    if (offset != cInvalidOffset) {
        if (isDir) {
            offset = dirList[offset].EntryOffset;
        } else {
            offset = fileList[offset].EntryOffset;
        }
    }
}

void RomFSL3::buildHeaderData() {
    header.Section[kSectionTypeDirHash].Size = static_cast<u32>(dirBucket.size() * 4);
    header.Section[kSectionTypeDir].Offset = static_cast<u32>(
        Align(header.Section[kSectionTypeDirHash].Offset + header.Section[kSectionTypeDirHash].Size,
              cEntryNameAlignment));
    header.Section[kSectionTypeFileHash].Offset = static_cast<u32>(
        Align(header.Section[kSectionTypeDir].Offset + header.Section[kSectionTypeDir].Size,
              cEntryNameAlignment));
    header.Section[kSectionTypeFileHash].Size = static_cast<u32>(fileBucket.size() * 4);
    header.Section[kSectionTypeFile].Offset = static_cast<u32>(Align(
        header.Section[kSectionTypeFileHash].Offset + header.Section[kSectionTypeFileHash].Size,
        cEntryNameAlignment));
    header.DataOffset = static_cast<u32>(
        Align(header.Section[kSectionTypeFile].Offset + header.Section[kSectionTypeFile].Size,
              cFileSizeAlignment));
}

u32 RomFSL3::hash(s32 parentOffset, std::u16string& entryName) {
    u32 uHash = parentOffset ^ 123456789;
    for (int i = 0; i < static_cast<int>(entryName.size()); i++) {
        uHash = ((uHash >> 5) | (uHash << 27)) ^ entryName[i];
    }
    return uHash;
}

} // namespace FileSys