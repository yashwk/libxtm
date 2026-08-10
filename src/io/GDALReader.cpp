#include "xtm/io/GDALReader.hpp"
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <ogr_spatialref.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <limits>
#include <cmath>
#include <stdexcept>

namespace xtm::io {


TerrainBuffer read_gdal(const std::string& path) {
    GDALAllRegister();

    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!dataset) {
        throw std::runtime_error("Failed to open GDAL dataset: " + path);
    }

    GDALRasterBand* band = dataset->GetRasterBand(1);
    if (!band) {
        GDALClose(dataset);
        throw std::runtime_error("No raster band found in dataset: " + path);
    }

    const int width = band->GetXSize();
    const int height = band->GetYSize();

    TerrainBuffer buffer(width, height);

    double transform[6] = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    if (dataset->GetGeoTransform(transform) == CE_None) {
        buffer.transform.origin_x = transform[0];
        buffer.transform.pixel_width = transform[1];
        buffer.transform.rotation_x = transform[2];
        buffer.transform.origin_y = transform[3];
        buffer.transform.rotation_y = transform[4];
        buffer.transform.pixel_height = transform[5];
    }

    int has_nodata = 0;
    double nodata = band->GetNoDataValue(&has_nodata);
    if (has_nodata) {
        buffer.nodata_value = nodata;
    }

    const char* wkt = dataset->GetProjectionRef();
    if (wkt && *wkt) {
        buffer.wkt_projection = wkt;
    }
    CPLErr err = band->RasterIO(GF_Read, 0, 0, width, height,
                                buffer.data(), width, height, GDT_Float64,
                                0, 0);

    GDALClose(dataset);

    if (err != CE_None) {
        throw std::runtime_error("Failed to read raster data");
    }

    return buffer;
}

terrain::IntGrid read_gdal_quantized(const std::string& path, double precision, RasterInfo& info) {
    GDALAllRegister();

    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!dataset) {
        throw std::runtime_error("Failed to open GDAL dataset: " + path);
    }

    GDALRasterBand* band = dataset->GetRasterBand(1);
    if (!band) {
        GDALClose(dataset);
        throw std::runtime_error("No raster band found in dataset: " + path);
    }

    const std::uint32_t width = band->GetXSize();
    const std::uint32_t height = band->GetYSize();

    terrain::IntGrid grid;
    grid.width = width;
    grid.height = height;
    grid.data.resize(static_cast<std::size_t>(width) * height);
    grid.nodata_mask.resize(static_cast<std::size_t>(width) * height, 0);

    info.width = width;
    info.height = height;

    double transform[6] = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    if (dataset->GetGeoTransform(transform) == CE_None) {
        info.transform.origin_x = transform[0];
        info.transform.pixel_width = transform[1];
        info.transform.rotation_x = transform[2];
        info.transform.origin_y = transform[3];
        info.transform.rotation_y = transform[4];
        info.transform.pixel_height = transform[5];
    }

    int has_nodata = 0;
    double nodata = band->GetNoDataValue(&has_nodata);
    if (has_nodata) {
        info.nodata_value = nodata;
    }

    const char* wkt = dataset->GetProjectionRef();
    if (wkt && *wkt) {
        info.wkt_projection = wkt;
    }

    // Bound GDAL's internal block cache; the windowed read would otherwise
    // accumulate most of the dataset's decoded tiles (default cap is 5% of RAM).
    GDALSetCacheMax(64 * 1024 * 1024);

    const std::uint32_t band_rows = 256;
    std::vector<double> window(static_cast<std::size_t>(width) * band_rows);
    TerrainView wview{window.data(), width, 0, info.transform, info.nodata_value, info.wkt_projection};

    double raw_sum = 0.0;
    double raw_sumsq = 0.0;
    double raw_min = std::numeric_limits<double>::max();
    double raw_max = std::numeric_limits<double>::lowest();
    std::size_t raw_valid = 0;

    CPLErr err = CE_None;
    for (std::uint32_t y0 = 0; y0 < height; y0 += band_rows) {
        int rows = std::min(band_rows, height - y0);
        err = band->RasterIO(GF_Read, 0, static_cast<int>(y0), static_cast<int>(width), rows,
                             window.data(), static_cast<int>(width), rows, GDT_Float64, 0, 0);
        if (err != CE_None) break;

        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * rows; ++i) {
            if (info.nodata_value.has_value() && window[i] == *info.nodata_value) continue;
            raw_sum += window[i];
            raw_sumsq += window[i] * window[i];
            if (window[i] < raw_min) raw_min = window[i];
            if (window[i] > raw_max) raw_max = window[i];
            raw_valid++;
        }

        wview.height = rows;
        auto wgrid = terrain::quantize_pixels(wview, precision);
        for (int y = 0; y < rows; ++y) {
            std::memcpy(grid.data.data() + static_cast<std::size_t>(y0 + y) * width,
                        wgrid.data.data() + static_cast<std::size_t>(y) * width,
                        sizeof(int32_t) * width);
            std::memcpy(grid.nodata_mask.data() + static_cast<std::size_t>(y0 + y) * width,
                        wgrid.nodata_mask.data() + static_cast<std::size_t>(y) * width,
                        sizeof(std::uint8_t) * width);
        }
    }

    GDALClose(dataset);

    if (err != CE_None) {
        throw std::runtime_error("Failed to read raster data");
    }

    info.raw_min = (raw_valid > 0) ? raw_min : 0.0;
    info.raw_max = (raw_valid > 0) ? raw_max : 0.0;
    info.raw_mean = (raw_valid > 0) ? raw_sum / static_cast<double>(raw_valid) : 0.0;
    double variance = (raw_valid > 0)
        ? raw_sumsq / static_cast<double>(raw_valid) - info.raw_mean * info.raw_mean
        : 0.0;
    info.raw_stddev = (variance > 0.0) ? std::sqrt(variance) : 0.0;
    info.raw_valid_pixels = raw_valid;

    if (info.nodata_value.has_value()) {
        terrain::inpaint(grid);
    }

    return grid;
}

} // namespace xtm::io
