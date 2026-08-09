#include "xtm/io/GDALReader.hpp"
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <ogr_spatialref.h>
#include <cstdlib>
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

} // namespace xtm::io
