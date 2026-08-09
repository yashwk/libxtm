#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include <cmath>

namespace xtm::coding {

void encode_stream(const std::vector<int32_t>& data, uint32_t width, uint32_t height, ContextModel model, bool has_precision, ArithmeticEncoder& ac, EncodingContext& ctx_data) {
    
    auto process_stream = [&](uint32_t start_idx, uint32_t length, ContextStream stream) {
        uint32_t zero_run = 0;
        Context run_context;
        
        for (uint32_t i = 0; i < length; ++i) {
            int32_t val = data[start_idx + i];
            
            Context ctx;
            ctx.stream = static_cast<uint8_t>(stream);
            ctx.neighbour_activity = 0;
            
            if (model == ContextModel::Extended && i > 0) {
                if (std::abs(data[start_idx + i - 1]) > 2) {
                    ctx.neighbour_activity = 1;
                }
            }
            
            if (val == 0) {
                if (zero_run == 0) run_context = ctx;
                zero_run++;
                
                if (zero_run == 255 || i == length - 1) {
                    auto& table = ctx_data.tables[get_context_index(run_context)];
                    ac.encode(table, 0);
                    table.increment(0);
                    
                    ac.encode(ctx_data.run_table, zero_run - 1);
                    ctx_data.run_table.increment(zero_run - 1);
                    
                    zero_run = 0;
                }
            } else {
                if (zero_run > 0) {
                    auto& table = ctx_data.tables[get_context_index(run_context)];
                    ac.encode(table, 0);
                    table.increment(0);
                    
                    ac.encode(ctx_data.run_table, zero_run - 1);
                    ctx_data.run_table.increment(zero_run - 1);
                    
                    zero_run = 0;
                }
                
                uint32_t zz = zigzag_encode(val);
                uint32_t mag = get_magnitude_class(zz);
                uint32_t remainder = zz & ((1u << (mag - 1)) - 1);
                
                auto& table = ctx_data.tables[get_context_index(ctx)];
                ac.encode(table, mag);
                table.increment(mag);
                
                if (mag > 1) {
                    for (int j = mag - 2; j >= 0; --j) {
                        uint32_t bit = (remainder >> j) & 1;
                        ac.encode(ctx_data.uniform_bit, bit);
                    }
                }
            }
        }
    };
    
    uint32_t length = width * height;
    process_stream(0, length, ContextStream::Meter);
    
    if (has_precision) {
        process_stream(length, length, ContextStream::Precision);
    }
}

void decode_stream(std::vector<int32_t>& data, uint32_t width, uint32_t height, ContextModel model, bool has_precision, ArithmeticDecoder& ad, EncodingContext& ctx_data) {
    
    auto process_stream = [&](uint32_t start_idx, uint32_t length, ContextStream stream) {
        uint32_t decoded = 0;
        while (decoded < length) {
            Context ctx;
            ctx.stream = static_cast<uint8_t>(stream);
            ctx.neighbour_activity = 0;
            
            if (model == ContextModel::Extended && decoded > 0) {
                if (std::abs(data[start_idx + decoded - 1]) > 2) {
                    ctx.neighbour_activity = 1;
                }
            }
            
            auto& table = ctx_data.tables[get_context_index(ctx)];
            uint32_t mag = ad.decode(table);
            table.increment(mag);
            
            if (mag == 0) {
                uint32_t run = ad.decode(ctx_data.run_table) + 1;
                ctx_data.run_table.increment(run - 1);
                
                for (uint32_t i = 0; i < run; ++i) {
                    data[start_idx + decoded++] = 0;
                }
            } else {
                uint32_t remainder = 0;
                if (mag > 1) {
                    for (int j = mag - 2; j >= 0; --j) {
                        remainder |= (ad.decode(ctx_data.uniform_bit) << j);
                    }
                }
                uint32_t zz = (1u << (mag - 1)) | remainder;
                data[start_idx + decoded++] = zigzag_decode(zz);
            }
        }
    };
    
    uint32_t length = width * height;
    process_stream(0, length, ContextStream::Meter);
    
    if (has_precision) {
        process_stream(length, length, ContextStream::Precision);
    }
}

void analyze_symbols(const std::vector<int32_t>& data, uint32_t width, uint32_t height, ContextModel model, bool has_precision, std::vector<int32_t>& mag_classes, std::vector<int32_t>& run_lengths, std::unordered_map<Context, uint32_t>& context_sizes, uint32_t& remainder_bits) {
    
    auto process_stream = [&](uint32_t start_idx, uint32_t length, ContextStream stream) {
        uint32_t zero_run = 0;
        Context run_context;
        
        for (uint32_t i = 0; i < length; ++i) {
            int32_t val = data[start_idx + i];
            
            Context ctx;
            ctx.stream = static_cast<uint8_t>(stream);
            ctx.neighbour_activity = 0;
            
            if (model == ContextModel::Extended && i > 0) {
                if (std::abs(data[start_idx + i - 1]) > 2) {
                    ctx.neighbour_activity = 1;
                }
            }
            
            if (val == 0) {
                if (zero_run == 0) run_context = ctx;
                zero_run++;
                
                if (zero_run == 255 || i == length - 1) {
                    mag_classes.push_back(0);
                    run_lengths.push_back(zero_run);
                    context_sizes[run_context]++;
                    zero_run = 0;
                }
            } else {
                if (zero_run > 0) {
                    mag_classes.push_back(0);
                    run_lengths.push_back(zero_run);
                    context_sizes[run_context]++;
                    zero_run = 0;
                }
                
                uint32_t zz = zigzag_encode(val);
                uint32_t mag = get_magnitude_class(zz);
                
                mag_classes.push_back(mag);
                context_sizes[ctx]++;
                
                if (mag > 1) {
                    remainder_bits += (mag - 1);
                }
            }
        }
    };
    
    uint32_t length = width * height;
    process_stream(0, length, ContextStream::Meter);
    
    if (has_precision) {
        process_stream(length, length, ContextStream::Precision);
    }
}

} // namespace xtm::coding
