#pragma once
#include "xtm/Terrain.hpp"
#include <string>

namespace xtm::io {

void write_gdal(const std::string& path, const TerrainView& view);

// Streaming GTiff writer: creates the dataset up front and writes row bands,
// so the full Float64 grid never needs to be resident.
class GdalWriter {
public:
    GdalWriter(const std::string& path, std::uint32_t width, std::uint32_t height,
               const GeoTransform& transform, const std::string& wkt_projection,
               std::optional<double> nodata_value);
    ~GdalWriter();

    void write_rows(std::uint32_t y0, const double* data, std::uint32_t nrows, std::uint32_t row_width);

    GdalWriter(const GdalWriter&) = delete;
    GdalWriter& operator=(const GdalWriter&) = delete;

private:
    void* dataset_ = nullptr; // GDALDataset*
};

}
