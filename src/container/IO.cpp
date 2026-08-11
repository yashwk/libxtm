#include "xtm/container/IO.hpp"
#include <stdexcept>
#include <mutex>
#include <algorithm>

namespace xtm::container {

namespace {
// CRC32 table, built once via thread-safe static initialization (no data
// race on first use from concurrent decoder threads).
uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    static const auto table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                if (c & 1) c = 0xedb88320 ^ (c >> 1);
                else c >>= 1;
            }
            t[i] = c;
        }
        return t;
    }();
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

void XtmWriter::write_superblock(uint32_t s_idx, std::vector<XtmWriter::PendingBlock> blocks) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (finalized_) throw std::runtime_error("Cannot write to finalized XTM file");
    
    buffered_superblocks_[s_idx] = std::move(blocks);
    
    while (buffered_superblocks_.count(expected_s_idx_)) {
        auto& sblocks = buffered_superblocks_[expected_s_idx_];
        
        for (const auto& block : sblocks) {
            uint64_t offset = stream_.tellp();
            stream_.write(reinterpret_cast<const char*>(block.bitstream.data()), block.bitstream.size());

            BlockIndexEntry entry;
            entry.block_x = block.x;
            entry.block_y = block.y;
            entry.block_width = block.width;
            entry.block_height = block.height;
            entry.byte_offset = offset;
            entry.byte_length = block.bitstream.size();
            entry.checksum = calculate_crc32(block.bitstream.data(), block.bitstream.size());
            final_index_.push_back(entry);
        }
        
        buffered_superblocks_.erase(expected_s_idx_);
        expected_s_idx_++;
    }
}

void XtmWriter::finalize() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (finalized_) return;

    for (auto& kv : buffered_superblocks_) {
        auto& sblocks = kv.second;
        for (const auto& block : sblocks) {
            uint64_t offset = stream_.tellp();
            stream_.write(reinterpret_cast<const char*>(block.bitstream.data()), block.bitstream.size());

            BlockIndexEntry entry;
            entry.block_x = block.x;
            entry.block_y = block.y;
            entry.block_width = block.width;
            entry.block_height = block.height;
            entry.byte_offset = offset;
            entry.byte_length = block.bitstream.size();
            entry.checksum = calculate_crc32(block.bitstream.data(), block.bitstream.size());
            final_index_.push_back(entry);
        }
    }
    buffered_superblocks_.clear();

    // Record where the index starts
    uint64_t index_offset = stream_.tellp();
    header_.index_offset = index_offset;
    
    // Write number of entries
    uint32_t num_entries = final_index_.size();
    stream_.write(reinterpret_cast<const char*>(&num_entries), sizeof(num_entries));
    
    // Write all entries
    for (const auto& entry : final_index_) {
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
    if (file_size < 0) {
        throw std::runtime_error("Corrupt XTM: cannot determine file size");
    }
    file_size_ = static_cast<std::uint64_t>(file_size);
    if (file_size_ <= header_.index_offset) {
        throw std::runtime_error("Corrupt XTM: index offset lies outside the file");
    }
    
    // Seek to index
    stream_.seekg(header_.index_offset);
    uint32_t num_entries = 0;
    stream_.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
    if (!stream_) {
        throw std::runtime_error("Corrupt XTM: failed to read index entry count");
    }
    
    // Each entry is 36 bytes (4 x uint32 + 2 x uint64 + checksum u32)
    std::uint64_t entry_size = 4 * sizeof(uint32_t) + 2 * sizeof(uint64_t) + sizeof(uint32_t);
    if (num_entries > 0 &&
        header_.index_offset + sizeof(num_entries) + static_cast<std::uint64_t>(num_entries) * entry_size >
            file_size_) {
        throw std::runtime_error("Corrupt XTM: index exceeds file size");
    }
    
    index_.resize(num_entries);
    for (uint32_t i = 0; i < num_entries; ++i) {
        index_[i].read(stream_);
    }
}

std::vector<uint8_t> XtmReader::read_block(const BlockIndexEntry& entry) {
    std::vector<uint8_t> data;
    read_block(entry, data);
    return data;
}

void XtmReader::read_block(const BlockIndexEntry& entry, std::vector<uint8_t>& out_buffer) {
    std::lock_guard<std::mutex> lock(read_mutex_);
    
    // Sanity-check the block region against the file size (cached at open;
    // avoids a seekg(0, end) + tellg() pair per block).
    if (entry.byte_offset + entry.byte_length > file_size_) {
        throw std::runtime_error("Corrupt XTM: block region lies outside the file");
    }
    
    out_buffer.resize(entry.byte_length);
    stream_.seekg(entry.byte_offset);
    stream_.read(reinterpret_cast<char*>(out_buffer.data()), entry.byte_length);
    if (!stream_) {
        throw std::runtime_error("Corrupt XTM: failed to read block bitstream");
    }
    
    uint32_t computed = calculate_crc32(out_buffer.data(), out_buffer.size());
    if (computed != entry.checksum) {
        throw std::runtime_error("Corrupt XTM: block CRC32 mismatch");
    }
}

} // namespace xtm::container
