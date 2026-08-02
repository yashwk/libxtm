#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/ZigZag.hpp"
#include <cmath>

namespace xtm::coding {

static uint32_t get_magnitude_class(uint32_t val) {
    if (val == 0) return 0;
    return 32 - __builtin_clz(val);
}

static void extract_subbands(uint32_t width, uint32_t height, uint32_t max_levels,
                             std::vector<std::pair<uint32_t, uint32_t>>& ll,
                             std::vector<std::pair<uint32_t, uint32_t>>& lh,
                             std::vector<std::pair<uint32_t, uint32_t>>& hl,
                             std::vector<std::pair<uint32_t, uint32_t>>& hh) {
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            Subband sb = Subband::LL;
            for (uint32_t level = 1; level <= max_levels; ++level) {
                uint32_t cur_w = width >> (level - 1);
                uint32_t cur_h = height >> (level - 1);
                uint32_t half_w = cur_w / 2;
                uint32_t half_h = cur_h / 2;
                
                if (x >= half_w || y >= half_h) {
                    if (x >= half_w && y >= half_h) sb = Subband::HH;
                    else if (x >= half_w) sb = Subband::HL;
                    else sb = Subband::LH;
                    break;
                }
            }
            if (sb == Subband::LL) ll.push_back({x, y});
            else if (sb == Subband::LH) lh.push_back({x, y});
            else if (sb == Subband::HL) hl.push_back({x, y});
            else hh.push_back({x, y});
        }
    }
}

std::vector<Symbol> generate_symbols(const std::vector<int32_t>& data, uint32_t width, uint32_t height, uint32_t max_levels, ContextModel model) {
    std::vector<std::pair<uint32_t, uint32_t>> coords_LL, coords_LH, coords_HL, coords_HH;
    extract_subbands(width, height, max_levels, coords_LL, coords_LH, coords_HL, coords_HH);
    
    std::vector<Symbol> symbols;
    
    auto process_subband = [&](const std::vector<std::pair<uint32_t, uint32_t>>& coords, Subband sb) {
        uint32_t zero_run = 0;
        Context run_context;
        
        for (size_t i = 0; i < coords.size(); ++i) {
            uint32_t x = coords[i].first;
            uint32_t y = coords[i].second;
            int32_t val = data[y * width + x];
            
            Context ctx;
            ctx.subband = static_cast<uint8_t>(sb);
            
            ctx.neighbour_activity = 0;
            if (model == ContextModel::Extended && i > 0) {
                uint32_t prev_x = coords[i-1].first;
                uint32_t prev_y = coords[i-1].second;
                if (std::abs(data[prev_y * width + prev_x]) > 2) {
                    ctx.neighbour_activity = 1;
                }
            }
            
            if (val == 0) {
                if (zero_run == 0) run_context = ctx;
                zero_run++;
                
                if (zero_run == 255 || i == coords.size() - 1) {
                    Symbol sym;
                    sym.magnitude_class = 0;
                    sym.run_length = zero_run;
                    sym.context = run_context;
                    symbols.push_back(sym);
                    zero_run = 0;
                }
            } else {
                if (zero_run > 0) {
                    Symbol sym;
                    sym.magnitude_class = 0;
                    sym.run_length = zero_run;
                    sym.context = run_context;
                    symbols.push_back(sym);
                    zero_run = 0;
                }
                
                uint32_t zz = zigzag_encode(val);
                uint32_t mag = get_magnitude_class(zz);
                uint32_t remainder = zz & ((1u << (mag - 1)) - 1);
                
                Symbol sym;
                sym.magnitude_class = mag;
                sym.remainder = remainder;
                sym.context = ctx;
                symbols.push_back(sym);
            }
        }
    };
    
    process_subband(coords_LL, Subband::LL);
    process_subband(coords_LH, Subband::LH);
    process_subband(coords_HL, Subband::HL);
    process_subband(coords_HH, Subband::HH);
    
    return symbols;
}

void reconstruct_symbols(std::vector<int32_t>& data, uint32_t width, uint32_t height, uint32_t max_levels, const std::vector<Symbol>& symbols) {
    std::vector<std::pair<uint32_t, uint32_t>> coords_LL, coords_LH, coords_HL, coords_HH;
    extract_subbands(width, height, max_levels, coords_LL, coords_LH, coords_HL, coords_HH);
    
    data.assign(width * height, 0);
    
    size_t sym_idx = 0;
    
    auto decode_subband = [&](const std::vector<std::pair<uint32_t, uint32_t>>& coords) {
        size_t coord_idx = 0;
        while (coord_idx < coords.size() && sym_idx < symbols.size()) {
            const Symbol& sym = symbols[sym_idx++];
            
            if (sym.magnitude_class == 0) {
                // Zero run
                coord_idx += sym.run_length;
            } else {
                uint32_t zz = (1u << (sym.magnitude_class - 1)) | sym.remainder;
                int32_t val = zigzag_decode(zz);
                
                uint32_t x = coords[coord_idx].first;
                uint32_t y = coords[coord_idx].second;
                data[y * width + x] = val;
                coord_idx++;
            }
        }
    };
    
    decode_subband(coords_LL);
    decode_subband(coords_LH);
    decode_subband(coords_HL);
    decode_subband(coords_HH);
}

} // namespace xtm::coding
