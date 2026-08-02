#include "xtm/io/GDALWriter.hpp"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <stdexcept>

namespace xtm::io {

void write_gdal(const std::string& path, const TerrainView& view, int32_t epsg_crs) {
    GDALAllRegister();
    
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        throw std::runtime_error("GDAL: GTiff driver not found");
    }
    
    char** options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
    options = CSLSetNameValue(options, "PREDICTOR", "3"); // Floating point predictor
    options = CSLSetNameValue(options, "TILED", "YES");
    
    GDALDataset* dataset = driver->Create(path.c_str(), view.width, view.height, 1, GDT_Float32, options);
    CSLDestroy(options);
    
    if (!dataset) {
        throw std::runtime_error("GDAL: Failed to create output file: " + path);
    }
    
    double geotransform[6] = {
        view.transform.origin_x,
        view.transform.pixel_width,
        0.0,
        view.transform.origin_y,
        0.0,
        view.transform.pixel_height
    };
    dataset->SetGeoTransform(geotransform);
    
    if (epsg_crs > 0) {
        OGRSpatialReference srs;
        srs.importFromEPSG(epsg_crs);
        char* wkt = nullptr;
        srs.exportToWkt(&wkt);
        dataset->SetProjection(wkt);
        CPLFree(wkt);
    }
    
    GDALRasterBand* band = dataset->GetRasterBand(1);
    
    if (view.nodata_value.has_value()) {
        band->SetNoDataValue(view.nodata_value.value());
    }
    
    // Write data row by row
    for (uint32_t y = 0; y < view.height; ++y) {
        const float* row_ptr = view.data + y * view.width;
        CPLErr err = band->RasterIO(GF_Write, 0, y, view.width, 1,
                                    (void*)row_ptr, view.width, 1, GDT_Float32, 0, 0);
        if (err != CE_None) {
            GDALClose(dataset);
            throw std::runtime_error("GDAL: Failed to write raster data");
        }
    }
    
    GDALClose(dataset);
}

} // namespace xtm::io
