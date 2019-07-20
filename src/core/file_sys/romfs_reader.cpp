#include <algorithm>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include "core/file_sys/romfs_reader.h"

namespace FileSys {

RomFSReader::~RomFSReader() {
    if (l3data != nullptr)
        delete[] l3data;
    if (l3offsetMap != nullptr)
        delete l3offsetMap;
}

std::size_t RomFSReader::ReadFile(std::size_t offset, std::size_t length, u8* buffer) {

    if (!buffer)
        return 0;

    // if reading from loose files
    if (l3data) {
        if (offset + length > data_size)
            return 0;

        // if reading L3 header data
        if (offset < file_offset) {
            if (offset + length > file_offset)
                return 0;

            memcpy(buffer, l3data + offset, length);
            return length;
        } else {
            std::string temp;
            std::size_t fileOfs = 0;

            if (l3offsetMap->count(offset)) { // try {
                temp = l3offsetMap->at(offset);
            } else { // catch (...) {
                auto it = l3offsetMap->upper_bound(offset);
                it--;
                temp = it->second;
                fileOfs = offset - it->first;
            }

            file.Open(temp, "rb"); // TODO (EddyK28): check if file is already open
            if (!file.IsGood())
                return 0;
            if (fileOfs)
                file.Seek(fileOfs, SEEK_SET); // TODO (EddyK28): ensure no reading beyond file
            std::size_t read_length = file.ReadBytes(buffer, length);
            // TODO (EddyK28): get length from file itself? (aka max out at file size)
            file.Close();

            return read_length;
        }
    }

    if (length == 0)
        return 0; // Crypto++ does not like zero size buffer
    file.Seek(file_offset + offset, SEEK_SET);
    std::size_t read_length = std::min(length, data_size - offset);
    read_length = file.ReadBytes(buffer, read_length);
    if (is_encrypted) {
        CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption d(key.data(), key.size(), ctr.data());
        d.Seek(crypto_offset + offset);
        d.ProcessData(buffer, buffer, read_length);
    }
    return read_length;
}

} // namespace FileSys
