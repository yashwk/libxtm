// nanobind bindings for the file-level libxtm API (xtm::api).
// Surface: encode / decode / analyze / info / verify / version on paths.
// All heavy calls release the GIL; std::invalid_argument -> ValueError,
// std::runtime_error -> RuntimeError (nanobind default).
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/tuple.h>

#include "xtm/Api.hpp"
#include "xtm/analyzer/Analyzer.hpp"
#include "xtm/container/Header.hpp"
#include "xtm/Types.hpp"

#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace nb = nanobind;
using namespace xtm;

namespace {

// Option-string translation. Validation of numeric options stays in xtm::api
// (the single source of truth); only the enum strings are parsed here.
coding::ContextModel parse_context(const std::string& s) {
    if (s == "simple") return coding::ContextModel::Simple;
    if (s == "extended") return coding::ContextModel::Extended;
    throw std::invalid_argument("context must be 'simple' or 'extended'");
}

analyzer::PipelineType parse_pipeline(const std::string& s) {
    if (s == "predictor") return analyzer::PipelineType::Predictor;
    if (s == "wavelet") return analyzer::PipelineType::Wavelet;
    throw std::invalid_argument("pipeline must be 'predictor' or 'wavelet'");
}

api::EncodeOptions make_options(double precision, const std::string& pipeline,
                                const std::string& context, bool disable_quadtree,
                                uint32_t num_threads) {
    return api::EncodeOptions(precision, parse_context(context),
                              parse_pipeline(pipeline), disable_quadtree,
                              num_threads);
}

} // namespace

NB_MODULE(xtm, m) {
    m.doc() = "libxtm Python bindings: terrain compression (.xtm format). "
              "File-level API: encode/decode/analyze/info/verify.";
    m.attr("__version__") = api::version();

    // std::invalid_argument -> ValueError (nanobind's default translator
    // would map it to ValueError too; registered explicitly for clarity).
    nb::register_exception_translator([](const std::exception_ptr& p, void*) {
        try {
            std::rethrow_exception(p);
        } catch (const std::invalid_argument& e) {
            PyErr_SetString(PyExc_ValueError, e.what());
        }
    });

    // ---- result types -----------------------------------------------------

    nb::class_<api::EncodeResult>(m, "EncodeResult",
        "Result of a successful encode.")
        .def_ro("width", &api::EncodeResult::width)
        .def_ro("height", &api::EncodeResult::height)
        .def_ro("total_blocks", &api::EncodeResult::total_blocks)
        .def_ro("predictor_counts", &api::EncodeResult::predictor_counts,
                "Dict of frozen PredictorId -> block count "
                "(0=Gradient, 1=Left, 2=JpegLs, 3=Polynomial, 4=Gap, "
                "5=LeastSquares).")
        .def_ro("output_bytes", &api::EncodeResult::output_bytes)
        .def_ro("time_load_ms", &api::EncodeResult::time_load_ms)
        .def_ro("time_quadtree_ms", &api::EncodeResult::time_quadtree_ms)
        .def_ro("time_entropy_ms", &api::EncodeResult::time_entropy_ms)
        .def_ro("time_io_ms", &api::EncodeResult::time_io_ms)
        .def_ro("time_total_ms", &api::EncodeResult::time_total_ms);

    nb::class_<api::DecodeResult>(m, "DecodeResult",
        "Result of a successful decode.")
        .def_ro("blocks_decoded", &api::DecodeResult::blocks_decoded)
        .def_ro("width", &api::DecodeResult::width)
        .def_ro("height", &api::DecodeResult::height);

    nb::class_<GeoTransform>(m, "GeoTransform",
        "GDAL geotransform (origin + pixel size + rotation).")
        .def_ro("origin_x", &GeoTransform::origin_x)
        .def_ro("origin_y", &GeoTransform::origin_y)
        .def_ro("pixel_width", &GeoTransform::pixel_width)
        .def_ro("pixel_height", &GeoTransform::pixel_height)
        .def_ro("rotation_x", &GeoTransform::rotation_x)
        .def_ro("rotation_y", &GeoTransform::rotation_y);

    nb::class_<api::FileInfo>(m, "FileInfo",
        "Metadata of a .xtm file (header + block index summary).")
        .def_prop_ro("width", [](const api::FileInfo& i) { return i.header.grid_width; })
        .def_prop_ro("height", [](const api::FileInfo& i) { return i.header.grid_height; })
        .def_prop_ro("precision", [](const api::FileInfo& i) { return i.header.precision; })
        .def_prop_ro("pipeline_id", [](const api::FileInfo& i) { return i.header.pipeline_id; },
                "0 = predictor pipeline, 1 = wavelet.")
        .def_prop_ro("context_model", [](const api::FileInfo& i) { return i.header.context_model; },
                "0 = Simple, 1 = Extended.")
        .def_prop_ro("flags", [](const api::FileInfo& i) { return i.header.flags; })
        .def_prop_ro("nodata_value", [](const api::FileInfo& i) {
                    return (i.header.flags & container::XtmHeader::FLAG_HAS_NODATA)
                               ? std::optional<double>(i.header.nodata_value)
                               : std::optional<double>(std::nullopt);
                },
                "NoData value, or None when the source had none.")
        .def_prop_ro("wkt_projection", [](const api::FileInfo& i) { return i.header.wkt_projection; })
        .def_prop_ro("transform", [](const api::FileInfo& i) { return i.header.transform; })
        .def_prop_ro("index_offset", [](const api::FileInfo& i) { return i.header.index_offset; })
        .def_ro("block_count", &api::FileInfo::block_count)
        .def_ro("total_payload_bytes", &api::FileInfo::total_payload_bytes);

    nb::class_<api::VerifyResult>(m, "VerifyResult",
        "Outcome of verify().")
        .def_ro("passed", &api::VerifyResult::passed)
        .def_ro("message", &api::VerifyResult::message)
        .def_ro("blocks_checked", &api::VerifyResult::blocks_checked)
        .def_ro("pixels_checked", &api::VerifyResult::pixels_checked)
        .def_ro("mismatched_pixels", &api::VerifyResult::mismatched_pixels);

    // ---- analyzer report types --------------------------------------------

    nb::class_<analyzer::DigitPlaneEntropy>(m, "DigitPlaneEntropy",
        "Shannon entropy of one decimal digit plane of the quantized "
        "magnitudes (place 0 = units = finest plane).")
        .def_ro("place", &analyzer::DigitPlaneEntropy::place)
        .def_ro("bpp", &analyzer::DigitPlaneEntropy::bpp);

    nb::class_<analyzer::PrecisionEstimate>(m, "PrecisionEstimate",
        "Estimated coding cost at a coarser precision.")
        .def_ro("precision", &analyzer::PrecisionEstimate::precision)
        .def_ro("bpp", &analyzer::PrecisionEstimate::bpp)
        .def_ro("estimated_file_bytes", &analyzer::PrecisionEstimate::estimated_file_bytes);

    nb::class_<analyzer::PredictorPerformance>(m, "PredictorPerformance",
        "Per-predictor analysis result.")
        .def_prop_ro("id", [](const analyzer::PredictorPerformance& p) { return static_cast<int>(p.id); },
                "Frozen PredictorId (0=Gradient, 1=Left, 2=JpegLs, "
                "3=Polynomial, 4=Gap, 5=LeastSquares).")
        .def_ro("name", &analyzer::PredictorPerformance::name)
        .def_ro("selection_bpp", &analyzer::PredictorPerformance::selection_bpp)
        .def_ro("shannon_bpp", &analyzer::PredictorPerformance::shannon_bpp)
        .def_ro("usage_blocks", &analyzer::PredictorPerformance::usage_blocks)
        .def_ro("avg_abs_residual", &analyzer::PredictorPerformance::avg_abs_residual);

    nb::class_<analyzer::EntropyBudget>(m, "EntropyBudget",
        "Estimated bit budget breakdown (mirrors the encoder cost model).")
        .def_ro("magnitude_class_bpp", &analyzer::EntropyBudget::magnitude_class_bpp)
        .def_ro("zero_run_bpp", &analyzer::EntropyBudget::zero_run_bpp)
        .def_ro("remainder_bpp", &analyzer::EntropyBudget::remainder_bpp)
        .def_ro("params_bpp", &analyzer::EntropyBudget::params_bpp)
        .def_ro("overhead_bpp", &analyzer::EntropyBudget::overhead_bpp)
        .def_ro("total_bpp", &analyzer::EntropyBudget::total_bpp);

    nb::class_<analyzer::RawElevationStats>(m, "RawElevationStats",
        "Statistics of the original (unquantized) raster; NoData excluded.")
        .def_ro("min_val", &analyzer::RawElevationStats::min_val)
        .def_ro("max_val", &analyzer::RawElevationStats::max_val)
        .def_ro("mean", &analyzer::RawElevationStats::mean)
        .def_ro("stddev", &analyzer::RawElevationStats::stddev)
        .def_ro("valid_pixels", &analyzer::RawElevationStats::valid_pixels);

    nb::class_<analyzer::QuantizedStats>(m, "QuantizedStats",
        "Statistics of the quantized grid the encoder compresses.")
        .def_ro("min_val", &analyzer::QuantizedStats::min_val)
        .def_ro("max_val", &analyzer::QuantizedStats::max_val)
        .def_ro("mean", &analyzer::QuantizedStats::mean)
        .def_ro("stddev", &analyzer::QuantizedStats::stddev);

    nb::class_<analyzer::AnalysisReport>(m, "AnalysisReport",
        "Full decision report from analyze().")
        .def_ro("width", &analyzer::AnalysisReport::width)
        .def_ro("height", &analyzer::AnalysisReport::height)
        .def_ro("sample_count", &analyzer::AnalysisReport::sample_count)
        .def_ro("nodata_pixels", &analyzer::AnalysisReport::nodata_pixels)
        .def_ro("precision", &analyzer::AnalysisReport::precision)
        .def_ro("raw", &analyzer::AnalysisReport::raw)
        .def_ro("quantized", &analyzer::AnalysisReport::quantized)
        .def_ro("corr_h", &analyzer::AnalysisReport::corr_h)
        .def_ro("corr_v", &analyzer::AnalysisReport::corr_v)
        .def_ro("corr_d", &analyzer::AnalysisReport::corr_d)
        .def_ro("digit_planes", &analyzer::AnalysisReport::digit_planes)
        .def_ro("precision_estimates", &analyzer::AnalysisReport::precision_estimates)
        .def_ro("predictors", &analyzer::AnalysisReport::predictors)
        .def_prop_ro("chosen_predictor_id", [](const analyzer::AnalysisReport& r) { return static_cast<int>(r.chosen_predictor); },
                "Frozen PredictorId of the quadtree winner.")
        .def_prop_ro("chosen_predictor_name", [](const analyzer::AnalysisReport& r) {
                    for (const auto& p : r.predictors) {
                        if (p.id == r.chosen_predictor) return std::string(p.name);
                    }
                    return std::string("");
                })
        .def_ro("chosen_usage_pct", &analyzer::AnalysisReport::chosen_usage_pct)
        .def_ro("second_order_usage_pct", &analyzer::AnalysisReport::second_order_usage_pct)
        .def_prop_ro("residual_predictor_blocks", [](const analyzer::AnalysisReport& r) {
                    return std::vector<std::size_t>(r.residual_predictor_blocks.begin(),
                                                    r.residual_predictor_blocks.end());
                },
                "Blocks won by each residual predictor (0=None, 1=Average, "
                "2=Median, 3=Left, 4=Gradient, 5=Gap, 6=LeastSquares).")
        .def_ro("residual_pool_savings_bits", &analyzer::AnalysisReport::residual_pool_savings_bits)
        .def_ro("second_order_savings_bpp", &analyzer::AnalysisReport::second_order_savings_bpp)
        .def_ro("leaves_512", &analyzer::AnalysisReport::leaves_512)
        .def_ro("leaves_256", &analyzer::AnalysisReport::leaves_256)
        .def_ro("leaves_128", &analyzer::AnalysisReport::leaves_128)
        .def_ro("leaves_64", &analyzer::AnalysisReport::leaves_64)
        .def_ro("total_blocks", &analyzer::AnalysisReport::total_blocks)
        .def_ro("budget", &analyzer::AnalysisReport::budget)
        .def_ro("estimated_file_bytes", &analyzer::AnalysisReport::estimated_file_bytes)
        .def_ro("estimated_compression_ratio", &analyzer::AnalysisReport::estimated_compression_ratio)
        .def_ro("wavelet_evaluated", &analyzer::AnalysisReport::wavelet_evaluated)
        .def_ro("predictor_estimate_bpp", &analyzer::AnalysisReport::predictor_estimate_bpp)
        .def_ro("wavelet_estimate_bpp", &analyzer::AnalysisReport::wavelet_estimate_bpp)
        .def_ro("wavelet_recommended", &analyzer::AnalysisReport::wavelet_recommended);

    // ---- functions --------------------------------------------------------

    m.def("encode",
          [](const std::string& input, const std::string& output,
             double precision, const std::string& pipeline,
             const std::string& context, bool disable_quadtree,
             uint32_t num_threads) {
              auto options = make_options(precision, pipeline, context,
                                          disable_quadtree, num_threads);
              api::EncodeResult result;
              {
                  nb::gil_scoped_release release;
                  result = api::encode_file(input, output, options);
              }
              return result;
          },
          "Compress a raster (any GDAL-readable format) into .xtm.",
          nb::arg("input"), nb::arg("output"),
          nb::arg("precision") = 1.0,
          nb::arg("pipeline") = "predictor",
          nb::arg("context") = "simple",
          nb::arg("disable_quadtree") = false,
          nb::arg("num_threads") = 0);

    m.def("decode",
          [](const std::string& input, const std::string& output,
             std::optional<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> region,
             uint32_t num_threads) {
              api::DecodeOptions options;
              options.num_threads = num_threads;
              if (region.has_value()) {
                  const auto [rx, ry, rw, rh] = *region;
                  options = api::DecodeOptions(rx, ry, rw, rh, num_threads);
              }
              api::DecodeResult result;
              {
                  nb::gil_scoped_release release;
                  result = api::decode_file(input, output, options);
              }
              return result;
          },
          "Decompress .xtm to a GeoTIFF, optionally a bounding region "
          "(x, y, width, height). region=None decodes the full grid.",
          nb::arg("input"), nb::arg("output"),
          nb::arg("region") = nb::none(),
          nb::arg("num_threads") = 0);

    m.def("analyze",
          [](const std::string& input, double precision, bool wavelets,
             uint32_t num_threads) {
              auto options = make_options(precision, "predictor", "simple",
                                          false, num_threads);
              analyzer::AnalyzerOptions aopts;
              aopts.enable_wavelet_analysis = wavelets;
              analyzer::AnalysisReport report;
              {
                  nb::gil_scoped_release release;
                  report = api::analyze_file(input, options, aopts);
              }
              return report;
          },
          "Run the encoder's selection pipeline on a raster and return the "
          "decision report.",
          nb::arg("input"),
          nb::arg("precision") = 1.0,
          nb::arg("wavelets") = false,
          nb::arg("num_threads") = 0);

    m.def("info",
          [](const std::string& path) {
              api::FileInfo info;
              {
                  nb::gil_scoped_release release;
                  info = api::info_file(path);
              }
              return info;
          },
          "Read .xtm header and block index summary without decoding payloads.",
          nb::arg("path"));

    m.def("verify",
          [](const std::string& xtm_path, std::optional<std::string> tif_path) {
              api::VerifyResult result;
              {
                  nb::gil_scoped_release release;
                  result = api::verify_file(xtm_path, tif_path.value_or(""));
              }
              return result;
          },
          "Checksum-only verification (no tif), or decode-vs-source comparison.",
          nb::arg("xtm"), nb::arg("tif") = nb::none());

    m.def("version", &api::version, "Return the libxtm version string.");
}