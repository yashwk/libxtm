#include "xtm/io/GDALWriter.hpp"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <stdexcept>

namespace xtm::io {

void write_gdal(const std::string& path, const TerrainView& view) {
    GDALAllRegister();
    
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        throw std::runtime_error("GDAL: GTiff driver not found");
    }
    
    char** options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
    options = CSLSetNameValue(options, "PREDICTOR", "3"); // Floating point predictor
    options = CSLSetNameValue(options, "TILED", "YES");
    
    GDALDataset* dataset = driver->Create(path.c_str(), view.width, view.height, 1, GDT_Float64, options);
    CSLDestroy(options);
    
    if (!dataset) {
        throw std::runtime_error("GDAL: Failed to create output file: " + path);
    }
    
    double geotransform[6] = {
        view.transform.origin_x,
        view.transform.pixel_width,
        view.transform.rotation_x,
        view.transform.origin_y,
        view.transform.rotation_y,
        view.transform.pixel_height
    };
    dataset->SetGeoTransform(geotransform);
    
    if (!view.wkt_projection.empty()) {
        dataset->SetProjection(view.wkt_projection.c_str());
    }
    
    GDALRasterBand* band = dataset->GetRasterBand(1);
    
    if (view.nodata_value.has_value()) {
        band->SetNoDataValue(view.nodata_value.value());
    }
    
    // Write data row by row
    for (uint32_t y = 0; y < view.height; ++y) {
        const double* row_ptr = view.data + y * view.width;
        CPLErr err = band->RasterIO(GF_Write, 0, y, view.width, 1,
                                    (void*)row_ptr, view.width, 1, GDT_Float64, 0, 0);
        if (err != CE_None) {
            GDALClose(dataset);
            throw std::runtime_error("GDAL: Failed to write raster data");
        }
    }
    
    GDALClose(dataset);
}

GdalWriter::GdalWriter(const std::string& path, std::uint32_t width, std::uint32_t height,
                       const GeoTransform& transform, const std::string& wkt_projection,
                       std::optional<double> nodata_value) {
    GDALAllRegister();

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        throw std::runtime_error("GDAL: GTiff driver not found");
    }

    char** options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
    options = CSLSetNameValue(options, "PREDICTOR", "3"); // Floating point predictor
    options = CSLSetNameValue(options, "TILED", "YES");

    GDALDataset* dataset = driver->Create(path.c_str(), static_cast<int>(width), static_cast<int>(height), 1, GDT_Float64, options);
    CSLDestroy(options);

    if (!dataset) {
        throw std::runtime_error("GDAL: Failed to create output file: " + path);
    }

    double geotransform[6] = {
        transform.origin_x,
        transform.pixel_width,
        transform.rotation_x,
        transform.origin_y,
        transform.rotation_y,
        transform.pixel_height
    };
    dataset->SetGeoTransform(geotransform);

    if (!wkt_projection.empty()) {
        dataset->SetProjection(wkt_projection.c_str());
    }

    GDALRasterBand* band = dataset->GetRasterBand(1);
    if (nodata_value.has_value()) {
        band->SetNoDataValue(nodata_value.value());
    }

    dataset_ = dataset;
}

GdalWriter::~GdalWriter() {
    if (dataset_) {
        GDALClose(static_cast<GDALDataset*>(dataset_));
    }
}

void GdalWriter::write_rows(std::uint32_t y0, const double* data, std::uint32_t nrows, std::uint32_t row_width) {
    GDALDataset* dataset = static_cast<GDALDataset*>(dataset_);
    GDALRasterBand* band = dataset->GetRasterBand(1);
    CPLErr err = band->RasterIO(GF_Write, 0, static_cast<int>(y0),
                                static_cast<int>(row_width), static_cast<int>(nrows),
                                const_cast<double*>(data), static_cast<int>(row_width), static_cast<int>(nrows),
                                GDT_Float64, 0, 0);
    if (err != CE_None) {
        throw std::runtime_error("GDAL: Failed to write raster data");
    }
}

} // namespace xtm::io
