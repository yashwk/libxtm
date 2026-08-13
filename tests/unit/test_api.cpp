#include <gtest/gtest.h>
#include "xtm/Api.hpp"
#include "xtm/io/GDALReader.hpp"
#include "xtm/terrain/Quantization.hpp"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class ApiTest : public ::testing::Test {
protected:
    std::string temp_dir_;

    void SetUp() override {
        GDALAllRegister();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999);
        temp_dir_ = (std::filesystem::temp_directory_path() /
             ("xtm_test_api_" + std::to_string(dis(gen)))).string();
        std::filesystem::create_directory(temp_dir_);
    }

    void TearDown() override {
        if (std::filesystem::exists(temp_dir_)) {
            std::filesystem::remove_all(temp_dir_);
        }
    }

    std::string temp_file(const std::string& name) {
        return (std::filesystem::path(temp_dir_) / name).string();
    }

    // Creates a Float64 GTiff with a ramp + noise field and a rectangular
    // NoData hole; returns its path.
    std::string make_raster(const std::string& name, int w, int h) {
        std::string path = temp_file(name);
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!driver) throw std::runtime_error("GTiff driver not available");

        GDALDataset* ds = driver->Create(path.c_str(), w, h, 1, GDT_Float64, nullptr);
        if (!ds) throw std::runtime_error("failed to create test tiff");

        double gt[6] = {500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0};
        ds->SetGeoTransform(gt);

        OGRSpatialReference srs;
        srs.importFromEPSG(3857);
        char* wkt = nullptr;
        srs.exportToWkt(&wkt);
        ds->SetProjection(wkt);
        CPLFree(wkt);

        const double nodata = -9999.0;
        GDALRasterBand* band = ds->GetRasterBand(1);
        band->SetNoDataValue(nodata);

        std::mt19937 rng(42);
        std::uniform_real_distribution<double> noise(-0.4, 0.4);
        std::vector<double> buf(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                bool in_hole = (x >= w / 2 - 16 && x < w / 2 + 16 &&
                                y >= h / 2 - 16 && y < h / 2 + 16);
                buf[y * w + x] = in_hole ? nodata
                                         : 1500.0 + 1.5 * x - 0.75 * y + noise(rng);
            }
        }
        CPLErr err = band->RasterIO(GF_Write, 0, 0, w, h, buf.data(), w, h,
                                    GDT_Float64, 0, 0);
        GDALClose(ds);
        if (err != CE_None) throw std::runtime_error("failed to write test tiff data");
        return path;
    }

    xtm::terrain::IntGrid read_grid(const std::string& path, double precision) {
        xtm::io::RasterInfo info;
        return xtm::io::read_gdal_quantized(path, precision, info);
    }

    void expect_grids_equal(const xtm::terrain::IntGrid& a,
                            const xtm::terrain::IntGrid& b) {
        EXPECT_EQ(a.width, b.width);
        EXPECT_EQ(a.height, b.height);
        ASSERT_EQ(a.data.size(), b.data.size());
        ASSERT_EQ(a.nodata_mask.size(), b.nodata_mask.size());
        EXPECT_EQ(a.nodata_mask, b.nodata_mask);
        for (std::size_t i = 0; i < a.data.size(); ++i) {
            if (a.nodata_mask[i]) continue; // masked cells are not coded
            EXPECT_EQ(a.data[i], b.data[i]);
        }
    }

    bool files_identical(const std::string& p1, const std::string& p2) {
        std::ifstream f1(p1, std::ios::binary);
        std::ifstream f2(p2, std::ios::binary);
        return std::equal(std::istreambuf_iterator<char>(f1),
                          std::istreambuf_iterator<char>(),
                          std::istreambuf_iterator<char>(f2));
    }
};

} // namespace

TEST_F(ApiTest, EncodeDecodeRoundtrip) {
    std::string tif = make_raster("input.tif", 600, 500);
    std::string xtm = temp_file("roundtrip.xtm");
    std::string out_tif = temp_file("roundtrip_out.tif");
    const double precision = 0.1;

    xtm::api::EncodeOptions enc(precision, xtm::coding::ContextModel::Extended);
    auto er = xtm::api::encode_file(tif, xtm, enc);
    EXPECT_EQ(er.width, 600u);
    EXPECT_EQ(er.height, 500u);
    EXPECT_GT(er.total_blocks, 0u);
    EXPECT_GT(er.output_bytes, 0u);
    EXPECT_TRUE(std::filesystem::exists(xtm));

    auto dr = xtm::api::decode_file(xtm, out_tif);
    EXPECT_EQ(dr.width, 600u);
    EXPECT_EQ(dr.height, 500u);
    EXPECT_GT(dr.blocks_decoded, 0u);
    EXPECT_TRUE(std::filesystem::exists(out_tif));

    auto orig = read_grid(tif, precision);
    auto decoded = read_grid(out_tif, precision);
    expect_grids_equal(orig, decoded);

    // Georeferencing preserved on a full decode.
    xtm::io::RasterInfo info;
    xtm::io::read_gdal_quantized(out_tif, precision, info);
    EXPECT_DOUBLE_EQ(info.transform.origin_x, 500000.0);
    EXPECT_DOUBLE_EQ(info.transform.origin_y, 4500000.0);
    EXPECT_FALSE(info.wkt_projection.empty());
    ASSERT_TRUE(info.nodata_value.has_value());
    EXPECT_DOUBLE_EQ(*info.nodata_value, -9999.0);
}

TEST_F(ApiTest, DisableQuadtreeRoundtrip) {
    std::string tif = make_raster("input.tif", 512, 512);
    std::string xtm = temp_file("fixed64.xtm");
    std::string out_tif = temp_file("fixed64_out.tif");

    xtm::api::EncodeOptions enc(1.0, xtm::coding::ContextModel::Simple,
                                xtm::analyzer::PipelineType::Predictor,
                                /*disable_quadtree=*/true);
    auto er = xtm::api::encode_file(tif, xtm, enc);
    EXPECT_GT(er.total_blocks, 0u);

    xtm::api::decode_file(xtm, out_tif);
    expect_grids_equal(read_grid(tif, 1.0), read_grid(out_tif, 1.0));
}

TEST_F(ApiTest, RoiDecodeEqualsFullDecodeCrop) {
    std::string tif = make_raster("input.tif", 1024, 768);
    std::string xtm = temp_file("roi.xtm");
    std::string full_tif = temp_file("full.tif");
    std::string roi_tif = temp_file("roi.tif");

    xtm::api::encode_file(tif, xtm);
    auto full_result = xtm::api::decode_file(xtm, full_tif);
    EXPECT_GT(full_result.blocks_decoded, 0u);

    const uint32_t rx = 100, ry = 50, rw = 400, rh = 300;
    auto dr = xtm::api::decode_file(xtm, roi_tif,
                                    xtm::api::DecodeOptions(rx, ry, rw, rh));
    EXPECT_EQ(dr.width, rw);
    EXPECT_EQ(dr.height, rh);
    EXPECT_GT(dr.blocks_decoded, 0u);
    // ROI inside one 512x512 superblock must touch strictly fewer quadtree
    // leaves than a full decode (decode granularity is the superblock).
    EXPECT_LT(dr.blocks_decoded, full_result.blocks_decoded);

    auto full = read_grid(full_tif, 1.0);
    auto roi = read_grid(roi_tif, 1.0);
    EXPECT_EQ(roi.width, rw);
    EXPECT_EQ(roi.height, rh);
    for (uint32_t y = 0; y < rh; ++y) {
        for (uint32_t x = 0; x < rw; ++x) {
            const auto& vf = full.data[static_cast<std::size_t>(ry + y) * full.width + rx + x];
            const auto& vr = roi.data[static_cast<std::size_t>(y) * rw + x];
            const bool nf = full.nodata_mask[static_cast<std::size_t>(ry + y) * full.width + rx + x];
            const bool nr = roi.nodata_mask[static_cast<std::size_t>(y) * rw + x];
            EXPECT_EQ(nf, nr);
            if (!nf) {
                EXPECT_EQ(vf, vr);
            }
        }
    }
}

TEST_F(ApiTest, EncodeIsByteDeterministic) {
    std::string tif = make_raster("input.tif", 512, 512);
    std::string xtm1 = temp_file("det1.xtm");
    std::string xtm2 = temp_file("det2.xtm");

    xtm::api::encode_file(tif, xtm1);
    xtm::api::encode_file(tif, xtm2);
    EXPECT_TRUE(files_identical(xtm1, xtm2));
}

TEST_F(ApiTest, AnalyzeSmoke) {
    std::string tif = make_raster("input.tif", 512, 512);
    xtm::api::EncodeOptions enc(1.0);
    xtm::analyzer::AnalyzerOptions aopts;
    aopts.enable_wavelet_analysis = true;
    double load_ms = 0.0;
    double analyze_ms = 0.0;
    auto report = xtm::api::analyze_file(tif, enc, aopts, &load_ms, &analyze_ms);

    EXPECT_EQ(report.width, 512u);
    EXPECT_EQ(report.height, 512u);
    EXPECT_EQ(report.sample_count, 512u * 512u);
    EXPECT_GT(report.nodata_pixels, 0u);
    EXPECT_EQ(report.predictors.size(), 6u);
    EXPECT_GT(report.total_blocks, 0u);
    EXPECT_GT(report.budget.total_bpp, 0.0);
    EXPECT_GT(report.estimated_file_bytes, 0.0);
    EXPECT_TRUE(report.wavelet_evaluated);
    EXPECT_GT(load_ms + analyze_ms, 0.0);
}

TEST_F(ApiTest, InfoAndVerify) {
    std::string tif = make_raster("input.tif", 512, 512);
    std::string xtm = temp_file("iv.xtm");

    auto er = xtm::api::encode_file(tif, xtm);

    auto info = xtm::api::info_file(xtm);
    EXPECT_EQ(info.header.grid_width, 512u);
    EXPECT_EQ(info.header.grid_height, 512u);
    EXPECT_EQ(info.block_count, er.total_blocks);
    EXPECT_GT(info.total_payload_bytes, 0u);
    EXPECT_LE(info.total_payload_bytes, er.output_bytes);

    auto v_checks = xtm::api::verify_file(xtm);
    EXPECT_TRUE(v_checks.passed);
    EXPECT_EQ(v_checks.blocks_checked, info.block_count);

    auto v_compare = xtm::api::verify_file(xtm, tif);
    EXPECT_TRUE(v_compare.passed);
    EXPECT_EQ(v_compare.pixels_checked, 512u * 512u);
    EXPECT_EQ(v_compare.mismatched_pixels, 0u);
}

TEST_F(ApiTest, VerifyDetectsCorruption) {
    std::string tif = make_raster("input.tif", 512, 512);
    std::string xtm = temp_file("corrupt.xtm");
    xtm::api::encode_file(tif, xtm);

    // Flip a byte inside the payload region (header + payloads + index).
    std::fstream f(xtm, std::ios::in | std::ios::out | std::ios::binary);
    std::streamsize size = std::filesystem::file_size(xtm);
    ASSERT_GT(size, 0);
    f.seekg(size / 3);
    char c = 0;
    f.read(&c, 1);
    f.seekp(size / 3);
    f.write(&c, 1); // no-op flip guard; XOR below does the actual flip
    f.seekp(size / 3);
    char flipped = static_cast<char>(c ^ 0xFF);
    f.write(&flipped, 1);
    f.close();

    auto v = xtm::api::verify_file(xtm);
    EXPECT_FALSE(v.passed);
    EXPECT_TRUE(v.message.find("corrupt") != std::string::npos);
}

TEST_F(ApiTest, InvalidOptionsThrow) {
    std::string tif = make_raster("input.tif", 64, 64);

    EXPECT_THROW(xtm::api::encode_file(tif, temp_file("p0.xtm"),
                                       xtm::api::EncodeOptions(0.0)),
                 std::invalid_argument);
    EXPECT_THROW(xtm::api::encode_file(tif, temp_file("pw.xtm"),
                                       xtm::api::EncodeOptions(
                                           0.5,
                                           xtm::coding::ContextModel::Simple,
                                           xtm::analyzer::PipelineType::Wavelet,
                                           false, 0)),
                 std::invalid_argument);
    EXPECT_THROW(xtm::api::decode_file(temp_file("missing.xtm"),
                                       temp_file("out.tif")),
                 std::runtime_error);
    EXPECT_THROW(xtm::api::encode_file(temp_file("missing.tif"),
                                       temp_file("out.xtm")),
                 std::runtime_error);

    // Region with width but no height is ambiguous.
    EXPECT_THROW(xtm::api::decode_file(temp_file("missing.xtm"),
                                       temp_file("out.tif"),
                                       xtm::api::DecodeOptions(0, 0, 100, 0)),
                 std::invalid_argument);
}

TEST_F(ApiTest, VersionString) {
    EXPECT_NE(xtm::api::version(), nullptr);
    EXPECT_GT(std::string(xtm::api::version()).size(), 0u);
}