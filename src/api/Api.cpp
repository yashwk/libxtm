#include "xtm/Api.hpp"
#include "xtm/io/GDALReader.hpp"
#include "xtm/io/GDALWriter.hpp"
#include "xtm/terrain/Quantization.hpp"
#include "xtm/container/IO.hpp"
#include "xtm/coding/Encoder.hpp"
#include "xtm/coding/Decoder.hpp"
#include "xtm/analyzer/Analyzer.hpp"
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace xtm::api {

namespace {

using clock = std::chrono::high_resolution_clock;

double elapsed_ms(clock::time_point from, clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

void validate_encode_options(const EncodeOptions& options) {
    if (options.precision <= 0.0) {
        throw std::invalid_argument("precision must be > 0");
    }
    if (options.pipeline_type == analyzer::PipelineType::Wavelet &&
        options.precision < 1.0) {
        throw std::invalid_argument(
            "wavelet pipeline requires precision >= 1.0");
    }
}

void validate_decode_options(const DecodeOptions& options) {
    const bool no_region = (options.region_width == 0 && options.region_height == 0);
    if (!no_region) {
        if (options.region_width == 0 || options.region_height == 0) {
            throw std::invalid_argument(
                "region must specify both width and height (or neither for the full grid)");
        }
    }
}

container::XtmHeader make_header(const io::RasterInfo& info, uint32_t width,
                                 uint32_t height, const EncodeOptions& options) {
    container::XtmHeader header;
    header.grid_width = width;
    header.grid_height = height;
    header.precision = options.precision;
    header.wkt_projection = info.wkt_projection;
    header.transform = info.transform;
    header.context_model =
        (options.context_model == coding::ContextModel::Extended) ? 1 : 0;
    header.pipeline_id =
        (options.pipeline_type == analyzer::PipelineType::Wavelet)
            ? container::XtmHeader::PIPELINE_WAVELET
            : container::XtmHeader::PIPELINE_PREDICTOR;
    if (info.nodata_value.has_value()) {
        header.flags |= container::XtmHeader::FLAG_HAS_NODATA;
        header.nodata_value = *info.nodata_value;
    }
    if (options.disable_quadtree) {
        header.flags |= container::XtmHeader::FLAG_DISABLE_QUADTREE;
    }
    return header;
}

} // namespace

EncodeResult encode_file(const std::string& input_raster,
                         const std::string& output_xtm,
                         const EncodeOptions& options) {
    validate_encode_options(options);

    const auto t_start = clock::now();

    io::RasterInfo rinfo;
    const auto t_load0 = clock::now();
    auto grid = io::read_gdal_quantized(input_raster, options.precision, rinfo);
    const auto t_load1 = clock::now();

    container::XtmHeader header = make_header(rinfo, grid.width, grid.height, options);

    container::XtmWriter writer(output_xtm, header);

    coding::PipelineContext ctx(options.precision, options.context_model,
                                options.pipeline_type, options.disable_quadtree,
                                options.num_threads);
    auto encode_result = coding::XtmEncoder::encode(grid, writer, ctx);
    writer.finalize();

    const auto t_end = clock::now();

    EncodeResult result;
    result.width = grid.width;
    result.height = grid.height;
    result.total_blocks = encode_result.total_blocks;
    for (const auto& [id, stats] : encode_result.predictor_stats) {
        result.predictor_counts[id] = stats.count;
    }
    std::error_code ec;
    result.output_bytes = std::filesystem::file_size(output_xtm, ec);
    result.time_load_ms = elapsed_ms(t_load0, t_load1);
    result.time_quadtree_ms = encode_result.time_quadtree;
    result.time_entropy_ms = encode_result.time_entropy;
    result.time_io_ms = encode_result.time_io;
    result.time_total_ms = elapsed_ms(t_start, t_end);
    return result;
}

DecodeResult decode_file(const std::string& input_xtm,
                         const std::string& output_raster,
                         const DecodeOptions& options) {
    validate_decode_options(options);

    container::XtmReader reader(input_xtm);
    const container::XtmHeader& header = reader.get_header();

    const uint32_t rx = options.region_x;
    const uint32_t ry = options.region_y;
    uint32_t rw = header.grid_width;
    uint32_t rh = header.grid_height;
    if (options.region_width > 0) {
        rw = options.region_width;
        rh = options.region_height;
    }

    terrain::IntGrid roi_grid;
    roi_grid.width = rw;
    roi_grid.height = rh;
    roi_grid.data.resize(static_cast<std::size_t>(rw) * rh, 0);
    roi_grid.nodata_mask.resize(static_cast<std::size_t>(rw) * rh, 0);

    auto dec_result = coding::XtmDecoder::decode(reader, roi_grid, rx, ry, rw, rh,
                                                 options.num_threads);

    GeoTransform gt = header.transform;
    gt.origin_x += static_cast<double>(rx) * gt.pixel_width +
                   static_cast<double>(ry) * gt.rotation_x;
    gt.origin_y += static_cast<double>(rx) * gt.rotation_y +
                   static_cast<double>(ry) * gt.pixel_height;

    std::optional<double> nodata_val = std::nullopt;
    if (header.flags & container::XtmHeader::FLAG_HAS_NODATA) {
        nodata_val = header.nodata_value;
    }

    io::GdalWriter writer(output_raster, rw, rh, gt, header.wkt_projection, nodata_val);
    const uint32_t band_rows = 256;
    std::vector<double> band(static_cast<std::size_t>(rw) * band_rows);
    for (uint32_t y0 = 0; y0 < rh; y0 += band_rows) {
        uint32_t rows = std::min(band_rows, rh - y0);
        terrain::dequantize_rows(roi_grid, y0, rows, header.precision, nodata_val,
                                 band.data());
        writer.write_rows(y0, band.data(), rows, rw);
    }

    DecodeResult result;
    result.blocks_decoded = dec_result.blocks_decoded;
    result.width = rw;
    result.height = rh;
    return result;
}

analyzer::AnalysisReport analyze_file(
    const std::string& input_raster,
    const EncodeOptions& options,
    const analyzer::AnalyzerOptions& analyzer_options,
    double* time_load_ms,
    double* time_analyze_ms) {
    validate_encode_options(options);

    const auto t0 = clock::now();
    io::RasterInfo info;
    auto grid = io::read_gdal_quantized(input_raster, options.precision, info);
    const auto t1 = clock::now();

    analyzer::RawElevationStats raw;
    raw.min_val = info.raw_min;
    raw.max_val = info.raw_max;
    raw.mean = info.raw_mean;
    raw.stddev = info.raw_stddev;
    raw.valid_pixels = info.raw_valid_pixels;

    coding::PipelineContext ctx(options.precision, coding::ContextModel::Simple,
                                analyzer::PipelineType::Predictor, false,
                                options.num_threads);
    auto report = analyzer::analyze_terrain(grid, raw, ctx, analyzer_options);
    const auto t2 = clock::now();

    if (time_load_ms) *time_load_ms = elapsed_ms(t0, t1);
    if (time_analyze_ms) *time_analyze_ms = elapsed_ms(t1, t2);
    return report;
}

FileInfo info_file(const std::string& xtm_path) {
    container::XtmReader reader(xtm_path);
    FileInfo info;
    info.header = reader.get_header();
    info.index = reader.get_index();
    info.block_count = info.index.size();
    for (const auto& entry : info.index) {
        info.total_payload_bytes += entry.byte_length;
    }
    return info;
}

VerifyResult verify_file(const std::string& xtm_path, const std::string& tif_path) {
    VerifyResult result;
    container::XtmReader reader(xtm_path);
    const auto& index = reader.get_index();

    if (tif_path.empty()) {
        result.blocks_checked = index.size();
        for (const auto& entry : index) {
            try {
                reader.read_block(entry);
            } catch (const std::exception& e) {
                result.message = "Block [" + std::to_string(entry.block_x) + "," +
                                 std::to_string(entry.block_y) + "] corrupt - " +
                                 e.what();
                return result;
            }
        }
        result.passed = true;
        result.message =
            "All " + std::to_string(index.size()) + " block checksums are valid.";
        return result;
    }

    const container::XtmHeader& header = reader.get_header();
    const uint32_t rw = header.grid_width;
    const uint32_t rh = header.grid_height;

    terrain::IntGrid roi_grid;
    roi_grid.width = rw;
    roi_grid.height = rh;
    roi_grid.data.resize(static_cast<std::size_t>(rw) * rh, 0);
    roi_grid.nodata_mask.resize(static_cast<std::size_t>(rw) * rh, 0);
    coding::XtmDecoder::decode(reader, roi_grid, 0, 0, rw, rh, 0);

    io::RasterInfo orig_info;
    auto orig_grid = io::read_gdal_quantized(tif_path, header.precision, orig_info);

    if (orig_info.width != rw || orig_info.height != rh) {
        result.message = "Dimension mismatch (" + std::to_string(orig_info.width) +
                         "x" + std::to_string(orig_info.height) + " vs " +
                         std::to_string(rw) + "x" + std::to_string(rh) + ")";
        return result;
    }

    std::uint64_t mismatches = 0;
    const std::size_t total = static_cast<std::size_t>(rw) * rh;
    result.pixels_checked = total;
    for (std::size_t i = 0; i < total; ++i) {
        const bool o_nodata = orig_grid.nodata_mask[i];
        const bool d_nodata = roi_grid.nodata_mask[i];
        if (o_nodata != d_nodata) {
            mismatches++;
        } else if (!o_nodata && orig_grid.data[i] != roi_grid.data[i]) {
            mismatches++;
        }
    }

    result.mismatched_pixels = mismatches;
    if (mismatches > 0) {
        result.message =
            std::to_string(mismatches) + " of " + std::to_string(total) +
            " pixels differ.";
    } else {
        result.passed = true;
        result.message =
            "Verification passed: " + std::to_string(total) +
            " pixels are identical.";
    }
    return result;
}

const char* version() {
    return XTM_VERSION;
}

} // namespace xtm::api