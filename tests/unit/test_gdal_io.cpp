#include <gtest/gtest.h>
#include "xtm/io/GDALReader.hpp"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <random>

namespace {

class GDALIOTest : public ::testing::Test {
protected:
    std::string temp_dir_;

    void SetUp() override {
        // Create a unique temporary directory for this test
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999);
        temp_dir_ = std::filesystem::temp_directory_path() / ("xtm_test_gdal_" + std::to_string(dis(gen)));
        std::filesystem::create_directory(temp_dir_);
    }

    void TearDown() override {
        // Clean up the temporary directory
        if (std::filesystem::exists(temp_dir_)) {
            std::filesystem::remove_all(temp_dir_);
        }
    }

    // Creates a small single-band Float32 GeoTIFF with the given EPSG code
    // (or no projection when epsg == 0) and returns its path.
    std::string create_test_tiff(const std::string& filename, int epsg) {
        std::string path = (std::filesystem::path(temp_dir_) / filename).string();
        GDALAllRegister();
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!driver) throw std::runtime_error("GTiff driver not available");

        GDALDataset* ds = driver->Create(path.c_str(), 32, 32, 1, GDT_Float32, nullptr);
        if (!ds) throw std::runtime_error("failed to create test tiff");

        double gt[6] = {500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0};
        ds->SetGeoTransform(gt);

        if (epsg > 0) {
            OGRSpatialReference srs;
            srs.importFromEPSG(epsg);
            char* wkt = nullptr;
            srs.exportToWkt(&wkt);
            ds->SetProjection(wkt);
            CPLFree(wkt);
        }

        std::vector<float> floats(32 * 32);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                floats[y * 32 + x] = 1000.0f + x + y * 0.5f;
            }
        }
        GDALRasterBand* band = ds->GetRasterBand(1);
        CPLErr err = band->RasterIO(GF_Write, 0, 0, 32, 32, floats.data(), 32, 32, GDT_Float32, 0, 0);
        GDALClose(ds);
        if (err != CE_None) throw std::runtime_error("failed to write test tiff data");
        return path;
    }
};

} // namespace

TEST_F(GDALIOTest, ReadProjectedEpsgCode) {
    std::string path = create_test_tiff("test_epsg_3857.tif", 3857);

    auto buf = xtm::io::read_gdal(path);
    EXPECT_TRUE(buf.wkt_projection.find("3857") != std::string::npos || buf.wkt_projection.find("Pseudo-Mercator") != std::string::npos);
    EXPECT_EQ(buf.width(), 32u);
    EXPECT_EQ(buf.height(), 32u);
    EXPECT_DOUBLE_EQ(buf.transform.origin_x, 500000.0);
    EXPECT_DOUBLE_EQ(buf.transform.pixel_width, 10.0);
}

TEST_F(GDALIOTest, ReadGeographicEpsgCode) {
    std::string path = create_test_tiff("test_epsg_4326.tif", 4326);

    auto buf = xtm::io::read_gdal(path);
    EXPECT_TRUE(buf.wkt_projection.find("4326") != std::string::npos || buf.wkt_projection.find("WGS 84") != std::string::npos);
}

TEST_F(GDALIOTest, NoProjectionYieldsZeroEpsg) {
    std::string path = create_test_tiff("test_no_crs.tif", 0);

    auto buf = xtm::io::read_gdal(path);
    EXPECT_TRUE(buf.wkt_projection.empty());
}