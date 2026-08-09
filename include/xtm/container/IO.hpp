#pragma once
#include "xtm/container/Header.hpp"
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace xtm::container {

class XtmWriter {
public:
    explicit XtmWriter(const std::string& filepath, const XtmHeader& header);
    ~XtmWriter();

    // Buffers a block's compressed bitstream. Payloads are written to the
    // file only at finalize(), in deterministic (y, x) order, so the output
    // bytes no longer depend on worker thread completion order.
    void write_block(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const std::vector<uint8_t>& bitstream, uint64_t sequence_id = 0);

    // Flushes the buffered block payloads (sorted by sequence_id) then the Block
    // Index Table to the end of the file, and updates the header.
    void finalize();

private:
    struct PendingBlock {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> bitstream;
        uint64_t sequence_id = 0;
    };

    std::string filepath_;
    std::fstream stream_;
    XtmHeader header_;
    std::vector<PendingBlock> pending_;
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
};

} // namespace xtm::container
