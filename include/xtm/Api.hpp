#pragma once
#include "xtm/Terrain.hpp"
#include "xtm/analyzer/Analyzer.hpp"
#include "xtm/analyzer/PipelineType.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/container/Header.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xtm::api {

// High-level file-oriented API. These functions wrap the low-level pipeline
// (io / terrain / coding / container) into the same file-level flows the CLI
// commands implement; the CLI delegates to them. All functions throw
// std::invalid_argument on invalid options and std::runtime_error on I/O or
// codec failures. Parallel work uses num_threads workers (0 = hardware
// concurrency).

struct EncodeOptions {
    double precision = 1.0;
    coding::ContextModel context_model = coding::ContextModel::Simple;
    analyzer::PipelineType pipeline_type = analyzer::PipelineType::Predictor;
    bool disable_quadtree = false;
    uint32_t num_threads = 0;

    EncodeOptions() = default;
    EncodeOptions(double precision_,
                  coding::ContextModel context_model_ = coding::ContextModel::Simple,
                  analyzer::PipelineType pipeline_type_ = analyzer::PipelineType::Predictor,
                  bool disable_quadtree_ = false,
                  uint32_t num_threads_ = 0)
        : precision(precision_), context_model(context_model_),
          pipeline_type(pipeline_type_), disable_quadtree(disable_quadtree_),
          num_threads(num_threads_) {}
};

struct EncodeResult {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t total_blocks = 0;
    // PredictorId -> block count (predictor pipeline only).
    std::map<uint32_t, uint32_t> predictor_counts;
    uint64_t output_bytes = 0;
    double time_load_ms = 0.0;
    double time_quadtree_ms = 0.0;
    double time_entropy_ms = 0.0;
    double time_io_ms = 0.0;
    double time_total_ms = 0.0;
};

struct DecodeOptions {
    uint32_t region_x = 0;
    uint32_t region_y = 0;
    uint32_t region_width = 0;  // 0 = full grid
    uint32_t region_height = 0; // 0 = full grid
    uint32_t num_threads = 0;

    DecodeOptions() = default;
    DecodeOptions(uint32_t region_x_, uint32_t region_y_,
                  uint32_t region_width_, uint32_t region_height_,
                  uint32_t num_threads_ = 0)
        : region_x(region_x_), region_y(region_y_),
          region_width(region_width_), region_height(region_height_),
          num_threads(num_threads_) {}
};

struct DecodeResult {
    uint32_t blocks_decoded = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct FileInfo {
    container::XtmHeader header;
    std::vector<container::BlockIndexEntry> index;
    std::size_t block_count = 0;
    uint64_t total_payload_bytes = 0;
};

struct VerifyResult {
    bool passed = false;
    std::string message;
    uint64_t blocks_checked = 0;
    uint64_t pixels_checked = 0;
    uint64_t mismatched_pixels = 0;
};

// Compress a raster (any GDAL-readable format) into a .xtm container.
EncodeResult encode_file(const std::string& input_raster,
                         const std::string& output_xtm,
                         const EncodeOptions& options = EncodeOptions());

// Decompress a .xtm container to a GeoTIFF (optionally a bounding region).
DecodeResult decode_file(const std::string& input_xtm,
                         const std::string& output_raster,
                         const DecodeOptions& options = DecodeOptions());

// Run the encoder's selection pipeline on a raster and return the decision
// report. Optional out-params receive the load/analysis wall times (ms).
analyzer::AnalysisReport analyze_file(
    const std::string& input_raster,
    const EncodeOptions& options = EncodeOptions(),
    const analyzer::AnalyzerOptions& analyzer_options = analyzer::AnalyzerOptions(),
    double* time_load_ms = nullptr,
    double* time_analyze_ms = nullptr);

// Read the .xtm header and block index without decoding payloads.
FileInfo info_file(const std::string& xtm_path);

// Checksum-only verification (tif_path empty) or decode-vs-source comparison.
VerifyResult verify_file(const std::string& xtm_path,
                         const std::string& tif_path = "");

const char* version();

} // namespace xtm::api