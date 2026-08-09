#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace xtm::coding {

class BitWriter {
public:
    void write_bit(uint8_t bit) {
        write_bits(bit, 1);
    }
    
    void write_bits(uint32_t val, uint8_t num_bits) {
        bit_buffer_ = (bit_buffer_ << num_bits) | (val & ((1ULL << num_bits) - 1));
        bits_in_buffer_ += num_bits;
        
        while (bits_in_buffer_ >= 8) {
            uint8_t b = static_cast<uint8_t>(bit_buffer_ >> (bits_in_buffer_ - 8));
            buffer_.push_back(b);
            bits_in_buffer_ -= 8;
        }
    }
    
    void flush() {
        if (bits_in_buffer_ > 0) {
            uint8_t b = static_cast<uint8_t>(bit_buffer_ << (8 - bits_in_buffer_));
            buffer_.push_back(b);
            bits_in_buffer_ = 0;
            bit_buffer_ = 0;
        }
    }
    
    void reset() {
        buffer_.clear(); // Keeps capacity
        bit_buffer_ = 0;
        bits_in_buffer_ = 0;
    }
    
    const std::vector<uint8_t>& get_buffer() const { return buffer_; }
    
private:
    std::vector<uint8_t> buffer_;
    uint64_t bit_buffer_ = 0;
    uint32_t bits_in_buffer_ = 0;
};

class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t>& buffer) : buffer_(buffer) {}
    
    uint8_t read_bit() {
        return static_cast<uint8_t>(read_bits(1));
    }
    
    uint32_t read_bits(uint8_t num_bits) {
        while (bits_in_buffer_ < num_bits) {
            if (byte_pos_ >= buffer_.size()) {
                underflow_ = true;
                excess_ += (num_bits - bits_in_buffer_);
                // shift remaining bits and return, pad with 0
                uint32_t val = (bit_buffer_ << (num_bits - bits_in_buffer_)) & ((1ULL << num_bits) - 1);
                bits_in_buffer_ = 0;
                bit_buffer_ = 0;
                return val;
            }
            bit_buffer_ = (bit_buffer_ << 8) | buffer_[byte_pos_++];
            bits_in_buffer_ += 8;
        }
        
        uint32_t val = (bit_buffer_ >> (bits_in_buffer_ - num_bits)) & ((1ULL << num_bits) - 1);
        bits_in_buffer_ -= num_bits;
        return val;
    }

    // True once any read has fallen past the end of the buffer. Corrupt or
    // truncated streams can be detected by checking this after decoding.
    bool underflowed() const { return underflow_; }

    // Number of bits read past the end of the buffer (0 if none).
    size_t excess_bits() const { return excess_; }

private:
    const std::vector<uint8_t>& buffer_;
    size_t byte_pos_ = 0;
    uint64_t bit_buffer_ = 0;
    uint32_t bits_in_buffer_ = 0;
    bool underflow_ = false;
    size_t excess_ = 0;
};

} // namespace xtm::coding
