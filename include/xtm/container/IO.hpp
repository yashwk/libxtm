#pragma once
#include "xtm/container/Header.hpp"
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <map>
#include <array>

namespace xtm::container {

class XtmWriter {
public:
    explicit XtmWriter(const std::string& filepath, const XtmHeader& header);
    ~XtmWriter();

    struct PendingBlock {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> bitstream;
        uint64_t sequence_id = 0;
    };

    // Buffers a superblock's blocks. Payloads are written to the file as soon
    // as the sequentially expected superblock arrives.
    void write_superblock(uint32_t s_idx, std::vector<PendingBlock> blocks);

    // Flushes the Block Index Table to the end of the file and updates the header.
    void finalize();

private:

    std::string filepath_;
    std::fstream stream_;
    XtmHeader header_;
    std::map<uint32_t, std::vector<PendingBlock>> buffered_superblocks_;
    std::vector<BlockIndexEntry> final_index_;
    uint32_t expected_s_idx_ = 0;
    bool finalized_ = false;
    std::mutex write_mutex_;
};

class XtmReader {
public:
    explicit XtmReader(const std::string& filepath);
    
    const XtmHeader& get_header() const { return header_; }
    const std::vector<BlockIndexEntry>& get_index() const { return index_; }
    
    // Reads a specific block's bitstream
    std::vector<uint8_t> read_block(const BlockIndexEntry& entry);
    void read_block(const BlockIndexEntry& entry, std::vector<uint8_t>& out_buffer);

private:
    std::fstream stream_;
    XtmHeader header_;
    std::vector<BlockIndexEntry> index_;
    std::mutex read_mutex_;
    std::uint64_t file_size_ = 0; // cached at open for per-block bounds checks
};

} // namespace xtm::container
