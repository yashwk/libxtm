#include "xtm/container/Header.hpp"
#include <stdexcept>

namespace xtm::container {

namespace {
constexpr uint16_t SUPPORTED_VERSION_MIN = 2;
constexpr uint16_t SUPPORTED_VERSION_MAX = 3;

void check_stream(std::istream& is, const char* what) {
    if (!is) {
        throw std::runtime_error(std::string("Corrupt XTM: failed to read ") + what);
    }
}
} // namespace

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
    os.write(reinterpret_cast<const char*>(&context_model), sizeof(context_model));
    os.write(reinterpret_cast<const char*>(&index_offset), sizeof(index_offset));
    if (flags & FLAG_HAS_NODATA) {
        os.write(reinterpret_cast<const char*>(&nodata_value), sizeof(nodata_value));
    }
}

void XtmHeader::read(std::istream& is) {
    is.read(magic, 4);
    check_stream(is, "magic");
    if (magic[0] != 'X' || magic[1] != 'T' || magic[2] != 'M' || magic[3] != '\0') {
        throw std::runtime_error("Invalid XTM magic signature");
    }
    is.read(reinterpret_cast<char*>(&version), sizeof(version));
    check_stream(is, "version");
    if (version < SUPPORTED_VERSION_MIN || version > SUPPORTED_VERSION_MAX) {
        throw std::runtime_error("Unsupported XTM format version " + std::to_string(version));
    }
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
    is.read(reinterpret_cast<char*>(&context_model), sizeof(context_model));
    if (context_model > 1) {
        throw std::runtime_error("Corrupt XTM: invalid context model " + std::to_string(context_model));
    }
    is.read(reinterpret_cast<char*>(&index_offset), sizeof(index_offset));
    check_stream(is, "header");
    if (flags & FLAG_HAS_NODATA) {
        is.read(reinterpret_cast<char*>(&nodata_value), sizeof(nodata_value));
        check_stream(is, "nodata value");
    }
}

void BlockIndexEntry::write(std::ostream& os) const {
    os.write(reinterpret_cast<const char*>(&block_x), sizeof(block_x));
    os.write(reinterpret_cast<const char*>(&block_y), sizeof(block_y));
    os.write(reinterpret_cast<const char*>(&block_width), sizeof(block_width));
    os.write(reinterpret_cast<const char*>(&block_height), sizeof(block_height));
    os.write(reinterpret_cast<const char*>(&byte_offset), sizeof(byte_offset));
    os.write(reinterpret_cast<const char*>(&byte_length), sizeof(byte_length));
    os.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
}

void BlockIndexEntry::read(std::istream& is, uint16_t version) {
    is.read(reinterpret_cast<char*>(&block_x), sizeof(block_x));
    is.read(reinterpret_cast<char*>(&block_y), sizeof(block_y));
    is.read(reinterpret_cast<char*>(&block_width), sizeof(block_width));
    is.read(reinterpret_cast<char*>(&block_height), sizeof(block_height));
    is.read(reinterpret_cast<char*>(&byte_offset), sizeof(byte_offset));
    is.read(reinterpret_cast<char*>(&byte_length), sizeof(byte_length));
    if (version >= 3) {
        is.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
    } else {
        checksum = 0;
    }
    if (!is) {
        throw std::runtime_error("Corrupt XTM: failed to read block index entry");
    }
}

} // namespace xtm::container
