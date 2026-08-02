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

    // Appends a block's compressed bitstream to the file and records it in the index
    void write_block(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const std::vector<uint8_t>& bitstream);

    // Flushes the Block Index Table to the end of the file and updates the header
    void finalize();

private:
    std::string filepath_;
    std::fstream stream_;
    XtmHeader header_;
    std::vector<BlockIndexEntry> index_;
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

private:
    std::fstream stream_;
    XtmHeader header_;
    std::vector<BlockIndexEntry> index_;
    std::mutex read_mutex_;
};

} // namespace xtm::container
