#include "xtm/coding/RangeCoder.hpp"
#include <utility>
#include <algorithm>

namespace xtm::coding {

FrequencyTable::FrequencyTable(uint32_t num_symbols)
    : FrequencyTable(std::vector<uint32_t>(num_symbols, 1)) {}

FrequencyTable::FrequencyTable(std::vector<uint32_t> initial_freqs)
    : freqs_(std::move(initial_freqs)), initial_freqs_(freqs_) {
    cum_freq_.resize(freqs_.size() + 1, 0);
    total_ = 0;
    for (uint32_t i = 0; i < freqs_.size(); ++i) {
        cum_freq_[i + 1] = cum_freq_[i] + freqs_[i];
        total_ += freqs_[i];
    }
}

void FrequencyTable::reset() {
    freqs_ = initial_freqs_;
    std::fill(cum_freq_.begin(), cum_freq_.end(), 0);
    total_ = 0;
    for (uint32_t i = 0; i < freqs_.size(); ++i) {
        cum_freq_[i + 1] = cum_freq_[i] + freqs_[i];
        total_ += freqs_[i];
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
    
    // cum_freq_ is non-decreasing with cum[0]=0 and cum[N]=total, so the
    // containing symbol can be found by binary search: largest s with cum[s] <= scaled_value.
    uint32_t lo = 0, hi = freqs.get_num_symbols();
    while (lo + 1 < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (freqs.get_low(mid) <= scaled_value) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    uint32_t symbol = lo;
    
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

} // namespace xtm::coding
