#pragma once
#include "xtm/coding/BitStream.hpp"
#include <vector>

namespace xtm::coding {

class FrequencyTable {
public:
    explicit FrequencyTable(uint32_t num_symbols);
    
    uint32_t get_total() const { return total_; }
    uint32_t get_low(uint32_t symbol) const { return cum_freq_[symbol]; }
    uint32_t get_high(uint32_t symbol) const { return cum_freq_[symbol + 1]; }
    uint32_t get_num_symbols() const { return static_cast<uint32_t>(freqs_.size()); }
    
    void increment(uint32_t symbol);
    
private:
    std::vector<uint32_t> freqs_;
    std::vector<uint32_t> cum_freq_;
    uint32_t total_;
};

class ArithmeticEncoder {
public:
    explicit ArithmeticEncoder(BitWriter& bit_writer) : bit_writer_(bit_writer) {}
    
    void encode(FrequencyTable& freqs, uint32_t symbol);
    void flush();
    
private:
    BitWriter& bit_writer_;
    uint32_t low_ = 0;
    uint32_t high_ = 0xFFFFFFFF;
    uint64_t pending_bits_ = 0;
};

class ArithmeticDecoder {
public:
    explicit ArithmeticDecoder(BitReader& bit_reader);
    
    uint32_t decode(FrequencyTable& freqs);
    
private:
    BitReader& bit_reader_;
    uint32_t low_ = 0;
    uint32_t high_ = 0xFFFFFFFF;
    uint32_t code_ = 0;
};

// Encodes a full 32-bit unsigned value using Magnitude Class + Raw Bits.
// Not thread-safe against concurrent use of the same table; tables are per-stream.
void encode_value(ArithmeticEncoder& ac, FrequencyTable& freqs, uint32_t val);
uint32_t decode_value(ArithmeticDecoder& ad, FrequencyTable& freqs);

} // namespace xtm::coding
