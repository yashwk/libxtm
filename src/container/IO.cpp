#include "xtm/container/IO.hpp"
#include <stdexcept>

namespace xtm::container {

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
    
    // Seek to index
    stream_.seekg(header_.index_offset);
    uint32_t num_entries = 0;
    stream_.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
    
    index_.resize(num_entries);
    for (uint32_t i = 0; i < num_entries; ++i) {
        index_[i].read(stream_);
    }
}

std::vector<uint8_t> XtmReader::read_block(const BlockIndexEntry& entry) {
    std::lock_guard<std::mutex> lock(read_mutex_);
    std::vector<uint8_t> data(entry.byte_length);
    stream_.seekg(entry.byte_offset);
    stream_.read(reinterpret_cast<char*>(data.data()), entry.byte_length);
    return data;
}

} // namespace xtm::container
