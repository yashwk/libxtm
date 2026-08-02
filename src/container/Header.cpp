#include "xtm/container/Header.hpp"
#include <stdexcept>

namespace xtm::container {

void XtmHeader::write(std::ostream& os) const {
    os.write(magic, 4);
    os.write(reinterpret_cast<const char*>(&version), sizeof(version));
    os.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
    os.write(reinterpret_cast<const char*>(&min_x), sizeof(min_x));
    os.write(reinterpret_cast<const char*>(&min_y), sizeof(min_y));
    os.write(reinterpret_cast<const char*>(&max_x), sizeof(max_x));
    os.write(reinterpret_cast<const char*>(&max_y), sizeof(max_y));
    os.write(reinterpret_cast<const char*>(&epsg_crs), sizeof(epsg_crs));
    os.write(reinterpret_cast<const char*>(&grid_width), sizeof(grid_width));
    os.write(reinterpret_cast<const char*>(&grid_height), sizeof(grid_height));
    os.write(reinterpret_cast<const char*>(&res_x), sizeof(res_x));
    os.write(reinterpret_cast<const char*>(&res_y), sizeof(res_y));
    os.write(reinterpret_cast<const char*>(&index_offset), sizeof(index_offset));
    if (flags & FLAG_HAS_NODATA) {
        os.write(reinterpret_cast<const char*>(&nodata_value), sizeof(nodata_value));
    }
}

void XtmHeader::read(std::istream& is) {
    is.read(magic, 4);
    if (magic[0] != 'X' || magic[1] != 'T' || magic[2] != 'M' || magic[3] != '\0') {
        throw std::runtime_error("Invalid XTM magic signature");
    }
    is.read(reinterpret_cast<char*>(&version), sizeof(version));
    is.read(reinterpret_cast<char*>(&flags), sizeof(flags));
    is.read(reinterpret_cast<char*>(&min_x), sizeof(min_x));
    is.read(reinterpret_cast<char*>(&min_y), sizeof(min_y));
    is.read(reinterpret_cast<char*>(&max_x), sizeof(max_x));
    is.read(reinterpret_cast<char*>(&max_y), sizeof(max_y));
    is.read(reinterpret_cast<char*>(&epsg_crs), sizeof(epsg_crs));
    is.read(reinterpret_cast<char*>(&grid_width), sizeof(grid_width));
    is.read(reinterpret_cast<char*>(&grid_height), sizeof(grid_height));
    is.read(reinterpret_cast<char*>(&res_x), sizeof(res_x));
    is.read(reinterpret_cast<char*>(&res_y), sizeof(res_y));
    is.read(reinterpret_cast<char*>(&index_offset), sizeof(index_offset));
    if (flags & FLAG_HAS_NODATA) {
        is.read(reinterpret_cast<char*>(&nodata_value), sizeof(nodata_value));
    }
}

void BlockIndexEntry::write(std::ostream& os) const {
    os.write(reinterpret_cast<const char*>(&block_x), sizeof(block_x));
    os.write(reinterpret_cast<const char*>(&block_y), sizeof(block_y));
    os.write(reinterpret_cast<const char*>(&block_width), sizeof(block_width));
    os.write(reinterpret_cast<const char*>(&block_height), sizeof(block_height));
    os.write(reinterpret_cast<const char*>(&byte_offset), sizeof(byte_offset));
    os.write(reinterpret_cast<const char*>(&byte_length), sizeof(byte_length));
}

void BlockIndexEntry::read(std::istream& is) {
    is.read(reinterpret_cast<char*>(&block_x), sizeof(block_x));
    is.read(reinterpret_cast<char*>(&block_y), sizeof(block_y));
    is.read(reinterpret_cast<char*>(&block_width), sizeof(block_width));
    is.read(reinterpret_cast<char*>(&block_height), sizeof(block_height));
    is.read(reinterpret_cast<char*>(&byte_offset), sizeof(byte_offset));
    is.read(reinterpret_cast<char*>(&byte_length), sizeof(byte_length));
}

} // namespace xtm::container
