#include "xtm/container/IO.hpp"
#include <stdexcept>
#include <mutex>

namespace xtm::container {

namespace {
uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    static uint32_t table[256];
    static bool initialized = false;
    static std::mutex init_mutex;
    if (!initialized) {
        std::lock_guard<std::mutex> lock(init_mutex);
        if (!initialized) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int j = 0; j < 8; j++) {
                    if (c & 1) c = 0xedb88320 ^ (c >> 1);
                    else c >>= 1;
                }
                table[i] = c;
            }
            initialized = true;
        }
    }
    uint32_t c = 0xffffffff;
    for (size_t i = 0; i < length; i++) {
        c = table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffff;
}
} // namespace

XtmWriter::XtmWriter(const std::string& filepath, const XtmHeader& header) 
    : filepath_(filepath), header_(header) {
    stream_.open(filepath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream_) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }
    // Write placeholder header
    header_.write(stream_);
}

XtmWriter::~XtmWriter() {
    if (!finalized_ && stream_.is_open()) {
        try { finalize(); } catch (...) {}
    }
}

void XtmWriter::write_block(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const std::vector<uint8_t>& bitstream) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (finalized_) throw std::runtime_error("Cannot write to finalized XTM file");
    
    uint64_t offset = stream_.tellp();
    stream_.write(reinterpret_cast<const char*>(bitstream.data()), bitstream.size());
    
    BlockIndexEntry entry;
    entry.block_x = x;
    entry.block_y = y;
    entry.block_width = width;
    entry.block_height = height;
    entry.byte_offset = offset;
    entry.byte_length = bitstream.size();
    if (header_.version >= 3) {
        entry.checksum = calculate_crc32(bitstream.data(), bitstream.size());
    }
    
    index_.push_back(entry);
}

void XtmWriter::finalize() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (finalized_) return;
    
    // Record where the index starts
    uint64_t index_offset = stream_.tellp();
    header_.index_offset = index_offset;
    
    // Write number of entries
    uint32_t num_entries = index_.size();
    stream_.write(reinterpret_cast<const char*>(&num_entries), sizeof(num_entries));
    
    // Write all entries
    for (const auto& entry : index_) {
        entry.write(stream_);
    }
    
    // Seek back to beginning and rewrite header with the correct index offset
    stream_.seekp(0);
    header_.write(stream_);
    
    stream_.close();
    finalized_ = true;
}

XtmReader::XtmReader(const std::string& filepath) {
    stream_.open(filepath, std::ios::in | std::ios::binary);
    if (!stream_) {
        throw std::runtime_error("Failed to open file for reading: " + filepath);
    }
    
    header_.read(stream_);
    
    if (header_.index_offset == 0) {
        throw std::runtime_error("Invalid XTM file: No index offset");
    }
    
    // Sanity-check the index region against the actual file size
    stream_.seekg(0, std::ios::end);
    std::streamoff file_size = stream_.tellg();
    if (file_size < 0 || static_cast<std::uint64_t>(file_size) <= header_.index_offset) {
        throw std::runtime_error("Corrupt XTM: index offset lies outside the file");
    }
    
    // Seek to index
    stream_.seekg(header_.index_offset);
    uint32_t num_entries = 0;
    stream_.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
    if (!stream_) {
        throw std::runtime_error("Corrupt XTM: failed to read index entry count");
    }
    
    // Each entry is 32 bytes (4 x uint32 + 2 x uint64) in v2, 36 bytes in v3
    std::uint64_t entry_size = 4 * sizeof(uint32_t) + 2 * sizeof(uint64_t);
    if (header_.version >= 3) {
        entry_size += sizeof(uint32_t); // checksum
    }
    if (num_entries > 0 &&
        header_.index_offset + sizeof(num_entries) + static_cast<std::uint64_t>(num_entries) * entry_size >
            static_cast<std::uint64_t>(file_size)) {
        throw std::runtime_error("Corrupt XTM: index exceeds file size");
    }
    
    index_.resize(num_entries);
    for (uint32_t i = 0; i < num_entries; ++i) {
        index_[i].read(stream_, header_.version);
    }
}

std::vector<uint8_t> XtmReader::read_block(const BlockIndexEntry& entry) {
    std::lock_guard<std::mutex> lock(read_mutex_);
    
    // Sanity-check the block region against the file size
    stream_.seekg(0, std::ios::end);
    std::streamoff file_size = stream_.tellg();
    if (file_size >= 0 &&
        (entry.byte_offset + entry.byte_length > static_cast<std::uint64_t>(file_size))) {
        throw std::runtime_error("Corrupt XTM: block region lies outside the file");
    }
    
    std::vector<uint8_t> data(entry.byte_length);
    stream_.seekg(entry.byte_offset);
    stream_.read(reinterpret_cast<char*>(data.data()), entry.byte_length);
    if (!stream_) {
        throw std::runtime_error("Corrupt XTM: failed to read block bitstream");
    }
    
    if (header_.version >= 3) {
        uint32_t computed = calculate_crc32(data.data(), data.size());
        if (computed != entry.checksum) {
            throw std::runtime_error("Corrupt XTM: block CRC32 mismatch");
        }
    }
    
    return data;
}

} // namespace xtm::container
