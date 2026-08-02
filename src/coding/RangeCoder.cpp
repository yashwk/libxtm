#include "xtm/coding/RangeCoder.hpp"

namespace xtm::coding {

FrequencyTable::FrequencyTable(uint32_t num_symbols) {
    freqs_.resize(num_symbols, 1);
    cum_freq_.resize(num_symbols + 1, 0);
    total_ = num_symbols;
    for (uint32_t i = 0; i < num_symbols; ++i) {
        cum_freq_[i + 1] = cum_freq_[i] + freqs_[i];
    }
}

void FrequencyTable::increment(uint32_t symbol) {
    // Halve frequencies if total gets too large (e.g. 16384) to prevent overflow in math
    if (total_ >= 16384) {
        total_ = 0;
        for (uint32_t i = 0; i < freqs_.size(); ++i) {
            freqs_[i] = (freqs_[i] >> 1);
            if (freqs_[i] == 0) freqs_[i] = 1;
            cum_freq_[i + 1] = cum_freq_[i] + freqs_[i];
            total_ += freqs_[i];
        }
    }
    
    freqs_[symbol]++;
    total_++;
    for (uint32_t i = symbol + 1; i <= freqs_.size(); ++i) {
        cum_freq_[i]++;
    }
}

void ArithmeticEncoder::encode(FrequencyTable& freqs, uint32_t symbol) {
    uint64_t range = static_cast<uint64_t>(high_) - low_ + 1;
    uint32_t total = freqs.get_total();
    
    uint32_t sym_low = freqs.get_low(symbol);
    uint32_t sym_high = freqs.get_high(symbol);
    
    uint32_t new_high = low_ + (range * sym_high) / total - 1;
    uint32_t new_low = low_ + (range * sym_low) / total;
    
    high_ = new_high;
    low_ = new_low;
    
    while (true) {
        if ((high_ & 0x80000000) == (low_ & 0x80000000)) {
            uint8_t bit = (high_ >> 31) & 1;
            bit_writer_.write_bit(bit);
            while (pending_bits_ > 0) {
                bit_writer_.write_bit(bit ^ 1);
                pending_bits_--;
            }
            low_ = (low_ << 1) & 0xFFFFFFFF;
            high_ = ((high_ << 1) | 1) & 0xFFFFFFFF;
        } else if ((low_ & 0x40000000) && !(high_ & 0x40000000)) {
            pending_bits_++;
            low_ = (low_ << 1) ^ 0x80000000;
            high_ = ((high_ << 1) | 1) ^ 0x80000000;
        } else {
            break;
        }
    }
}

void ArithmeticEncoder::flush() {
    pending_bits_++;
    uint8_t bit = (low_ >> 30) & 1; // write the second top bit, since top bits are different
    bit_writer_.write_bit(bit);
    while (pending_bits_ > 0) {
        bit_writer_.write_bit(bit ^ 1);
        pending_bits_--;
    }
    bit_writer_.flush();
}

ArithmeticDecoder::ArithmeticDecoder(BitReader& bit_reader) : bit_reader_(bit_reader) {
    for (int i = 0; i < 32; ++i) {
        code_ = (code_ << 1) | bit_reader_.read_bit();
    }
}

uint32_t ArithmeticDecoder::decode(FrequencyTable& freqs) {
    uint64_t range = static_cast<uint64_t>(high_) - low_ + 1;
    uint32_t total = freqs.get_total();
    
    uint64_t scaled_value = ((static_cast<uint64_t>(code_) - low_ + 1) * total - 1) / range;
    
    uint32_t symbol = 0;
    for (uint32_t i = 0; i < freqs.get_num_symbols(); ++i) {
        if (scaled_value >= freqs.get_low(i) && scaled_value < freqs.get_high(i)) {
            symbol = i;
            break;
        }
    }
    
    uint32_t sym_low = freqs.get_low(symbol);
    uint32_t sym_high = freqs.get_high(symbol);
    
    high_ = low_ + (range * sym_high) / total - 1;
    low_ = low_ + (range * sym_low) / total;
    
    while (true) {
        if ((high_ & 0x80000000) == (low_ & 0x80000000)) {
            low_ = (low_ << 1) & 0xFFFFFFFF;
            high_ = ((high_ << 1) | 1) & 0xFFFFFFFF;
            code_ = ((code_ << 1) | bit_reader_.read_bit()) & 0xFFFFFFFF;
        } else if ((low_ & 0x40000000) && !(high_ & 0x40000000)) {
            low_ = (low_ << 1) ^ 0x80000000;
            high_ = ((high_ << 1) | 1) ^ 0x80000000;
            code_ = ((code_ << 1) | bit_reader_.read_bit()) ^ 0x80000000;
        } else {
            break;
        }
    }
    
    return symbol;
}

// Magnitude class helper: Number of bits needed to represent val.
static uint32_t get_magnitude_class(uint32_t val) {
    if (val == 0) return 0;
    return 32 - __builtin_clz(val);
}

void encode_value(ArithmeticEncoder& ac, FrequencyTable& freqs, uint32_t val) {
    uint32_t mag = get_magnitude_class(val);
    ac.encode(freqs, mag);
    freqs.increment(mag);
    
    if (mag > 1) {
        static FrequencyTable uniform_bit(2); // 0 and 1 equally probable
        uint32_t remainder = val & ((1u << (mag - 1)) - 1);
        for (int i = mag - 2; i >= 0; --i) {
            uint32_t bit = (remainder >> i) & 1;
            ac.encode(uniform_bit, bit);
        }
    }
}

uint32_t decode_value(ArithmeticDecoder& ad, FrequencyTable& freqs) {
    uint32_t mag = ad.decode(freqs);
    freqs.increment(mag);
    
    if (mag == 0) return 0;
    if (mag == 1) return 1;
    
    static FrequencyTable uniform_bit(2);
    uint32_t remainder = 0;
    for (int i = mag - 2; i >= 0; --i) {
        uint32_t bit = ad.decode(uniform_bit);
        remainder = (remainder << 1) | bit;
    }
    
    uint32_t val = (1u << (mag - 1)) | remainder;
    return val;
}

} // namespace xtm::coding
