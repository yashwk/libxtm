#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace xtm::coding {

class BitWriter {
public:
    void write_bit(uint8_t bit) {
        current_byte_ = (current_byte_ << 1) | (bit & 1);
        bits_in_byte_++;
        if (bits_in_byte_ == 8) {
            buffer_.push_back(current_byte_);
            current_byte_ = 0;
            bits_in_byte_ = 0;
        }
    }
    
    void write_bits(uint32_t val, uint8_t num_bits) {
        for (int i = num_bits - 1; i >= 0; --i) {
            write_bit((val >> i) & 1);
        }
    }
    
    void flush() {
        if (bits_in_byte_ > 0) {
            buffer_.push_back(current_byte_ << (8 - bits_in_byte_));
            bits_in_byte_ = 0;
            current_byte_ = 0;
        }
    }
    
    const std::vector<uint8_t>& get_buffer() const { return buffer_; }
    
private:
    std::vector<uint8_t> buffer_;
    uint8_t current_byte_ = 0;
    uint8_t bits_in_byte_ = 0;
};

class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t>& buffer) : buffer_(buffer) {}
    
    uint8_t read_bit() {
        if (bits_left_ == 0) {
            if (byte_pos_ >= buffer_.size()) {
                // Return 0 if we read past EOF (happens during Arithmetic decoder flushing)
                return 0;
            }
            current_byte_ = buffer_[byte_pos_++];
            bits_left_ = 8;
        }
        uint8_t bit = (current_byte_ >> (bits_left_ - 1)) & 1;
        bits_left_--;
        return bit;
    }
    
    uint32_t read_bits(uint8_t num_bits) {
        uint32_t val = 0;
        for (int i = 0; i < num_bits; ++i) {
            val = (val << 1) | read_bit();
        }
        return val;
    }
    
private:
    const std::vector<uint8_t>& buffer_;
    size_t byte_pos_ = 0;
    uint8_t current_byte_ = 0;
    uint8_t bits_left_ = 0;
};

} // namespace xtm::coding
