#include "AnalyzeCmd.hpp"
#include "xtm/Api.hpp"
#include "xtm/analyzer/Analyzer.hpp"
#include "xtm/container/Header.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
  #include <io.h>
  #define ISATTY _isatty
  #define FILENO _fileno
#else
  #include <sys/ioctl.h>
  #include <unistd.h>
  #define ISATTY isatty
  #define FILENO fileno
#endif

namespace xtm::cli {

namespace {

// ---- terminal helpers (inlined from docs/term_fmt.hpp) ----

bool g_no_color = false;

namespace term {

bool supports_color() {
    if (g_no_color) return false;
    if (std::getenv("NO_COLOR")) return false;
    if (!ISATTY(FILENO(stdout))) return false;
    const char* t = std::getenv("TERM");
    return t && std::string_view(t) != "dumb";
}

int width(int fallback = 80) {
#ifndef _WIN32
    struct winsize w {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) return w.ws_col;
#endif
    return fallback;
}

enum class Color { Default, Red, Green, Yellow, Blue, Cyan, Magenta, Gray };

std::string colorize(std::string_view text, Color c, bool bold = false) {
    if (!supports_color()) return std::string(text);
    static const char* codes[] = {"39", "31", "32", "33", "34", "36", "35", "90"};
    std::string out = "\033[";
    if (bold) out += "1;";
    out += codes[static_cast<int>(c)];
    out += "m";
    out += text;
    out += "\033[0m";
    return out;
}

std::string ok(std::string_view s)   { return colorize(s, Color::Green); }
std::string warn(std::string_view s) { return colorize(s, Color::Yellow); }
std::string dim(std::string_view s)  { return colorize(s, Color::Gray); }
std::string heading(std::string_view s) { return colorize(s, Color::Cyan, true); }

std::string thousands(long long n) {
    std::string s = std::to_string(n);
    bool neg = !s.empty() && s[0] == '-';
    int start = neg ? 1 : 0;
    for (int i = static_cast<int>(s.size()) - 3; i > start; i -= 3)
        s.insert(i, ",");
    return s;
}

std::string bar(double fraction, int w = 20) {
    fraction = std::clamp(fraction, 0.0, 1.0);
    int filled = static_cast<int>(std::round(fraction * w));
    std::string s;
    for (int i = 0; i < w; ++i) s += (i < filled) ? "\u2588" : "\u2591";
    return s;
}

std::string sparkline(const std::vector<double>& values) {
    static const char* ticks[] = {"\u2581","\u2582","\u2583","\u2584","\u2585","\u2586","\u2587","\u2588"};
    if (values.empty()) return "";
    double lo = *std::min_element(values.begin(), values.end());
    double hi = *std::max_element(values.begin(), values.end());
    double range = (hi - lo) > 1e-12 ? (hi - lo) : 1.0;
    std::string s;
    for (double v : values) {
        int idx = std::clamp(static_cast<int>((v - lo) / range * 7.0), 0, 7);
        s += ticks[idx];
    }
    return s;
}

std::string rule(std::string_view title, int total_width = -1) {
    int w = (total_width > 0) ? total_width : width();
    int n = std::max(0, w - static_cast<int>(title.size()) - 4);
    std::string line = "\u2500\u2500 " + std::string(title) + " ";
    for (int i = 0; i < n; ++i) line += "\u2500";
    return heading(line);
}

} // namespace term

// ---- JSON helpers ----

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string json_num(double v) {
    return std::format("{:g}", v);
}

std::string json_int(double v) {
    return std::to_string(static_cast<long long>(std::llround(v)));
}

using analyzer::AnalysisReport;

std::string plane_label(const AnalysisReport& report, int place) {
    double unit = report.precision;
    for (int k = 0; k < place; ++k) unit *= 10.0;
    if (unit >= 1.0) return std::format("{:.0f} m", unit);
    if (unit < 0.01) return std::format("{:.4f} m", unit);
    return std::format("{:.2f} m", unit);
}

void print_json(const AnalysisReport& r, const std::string& path,
                double load_ms, double analyze_ms) {
    const auto& raw = r.raw;
    const auto& P = raw.percentiles;

    std::string json;
    json.reserve(8192);
    json += "{\n";
    json += "  \"dataset\": {\n";
    json += "    \"path\": \"" + json_escape(path) + "\",\n";
    json += "    \"dimensions\": [" + std::to_string(r.width) + ", " + std::to_string(r.height) + "],\n";
    json += "    \"pixel_count\": " + std::to_string(r.sample_count) + ",\n";
    json += "    \"nodata_count\": " + std::to_string(r.nodata_pixels) + ",\n";
    json += "    \"crs\": \"" + json_escape(r.crs) + "\",\n";
    json += "    \"pixel_units\": \"" + json_escape(r.pixel_units) + "\",\n";
    json += "    \"pixel_resolution\": [" + json_num(r.pixel_width) + ", " + json_num(r.pixel_height) + "],\n";
    json += "    \"bounding_box\": [" + json_num(r.bbox_min_x) + ", " + json_num(r.bbox_min_y)
         + ", " + json_num(r.bbox_max_x) + ", " + json_num(r.bbox_max_y) + "],\n";
    json += "    \"elevation\": {\n";
    json += "      \"min\": " + json_num(raw.min_val) + ",\n";
    json += "      \"max\": " + json_num(raw.max_val) + ",\n";
    json += "      \"mean\": " + json_num(raw.mean) + ",\n";
    json += "      \"stddev\": " + json_num(raw.stddev) + ",\n";
    json += "      \"valid_pixels\": " + std::to_string(raw.valid_pixels) + ",\n";
    json += "      \"percentiles\": {\"p1\": " + json_num(P[0]) + ", \"p25\": " + json_num(P[1])
         + ", \"p50\": " + json_num(P[2]) + ", \"p75\": " + json_num(P[3]) + ", \"p99\": " + json_num(P[4]) + "},\n";
    json += "      \"histogram\": [";
    for (std::size_t i = 0; i < raw.elevation_histogram.size(); ++i) {
        if (i > 0) json += ", ";
        json += json_num(raw.elevation_histogram[i]);
    }
    json += "]\n";
    json += "    },\n";
    json += "    \"spatial_correlation\": {\"h\": " + json_num(r.corr_h)
         + ", \"v\": " + json_num(r.corr_v) + ", \"d\": " + json_num(r.corr_d) + "}\n";
    json += "  },\n";

    json += "  \"compressibility\": {\n";
    json += "    \"precision\": " + json_num(r.precision) + ",\n";
    json += "    \"bpp\": " + json_num(r.budget.total_bpp) + ",\n";
    json += "    \"estimated_bytes\": " + json_int(r.estimated_file_bytes) + ",\n";
    json += "    \"estimated_bytes_stddev\": " + json_int(r.estimated_bytes_stddev) + ",\n";
    json += "    \"compression_ratio\": " + json_num(r.estimated_compression_ratio) + ",\n";
    json += "    \"precision_estimates\": [";
    for (std::size_t i = 0; i < r.precision_estimates.size(); ++i) {
        if (i > 0) json += ", ";
        const auto& pe = r.precision_estimates[i];
        json += "{\"precision\": " + json_num(pe.precision) + ", \"bpp\": " + json_num(pe.bpp)
             + ", \"estimated_bytes\": " + json_int(pe.estimated_file_bytes) + "}";
    }
    json += "],\n";
    json += "    \"digit_planes\": [";
    for (std::size_t i = 0; i < r.digit_planes.size(); ++i) {
        if (i > 0) json += ", ";
        json += "{\"place\": " + std::to_string(r.digit_planes[i].place)
             + ", \"bpp\": " + json_num(r.digit_planes[i].bpp) + "}";
    }
    json += "]\n";
    json += "  },\n";

    json += "  \"predictors\": [";
    for (std::size_t i = 0; i < r.predictors.size(); ++i) {
        if (i > 0) json += ", ";
        const auto& p = r.predictors[i];
        double usage = r.total_blocks > 0
            ? static_cast<double>(p.usage_blocks) / static_cast<double>(r.total_blocks) : 0.0;
        json += "{\"rank\": " + std::to_string(i + 1) + ", \"name\": \"" + p.name
             + "\", \"selection_bpp\": " + json_num(p.selection_bpp)
             + ", \"shannon_bpp\": " + json_num(p.shannon_bpp)
             + ", \"usage\": " + json_num(usage)
             + ", \"avg_abs_residual\": " + json_num(p.avg_abs_residual) + "}";
    }
    json += "],\n";

    std::string chosen_name = "";
    for (const auto& p : r.predictors) {
        if (p.id == r.chosen_predictor) chosen_name = p.name;
    }
    json += "  \"encoder_choice\": \"" + json_escape(chosen_name) + "\",\n";
    json += "  \"encoder_choice_usage_pct\": " + json_num(r.chosen_usage_pct) + ",\n";
    json += "  \"residual_reprediction\": {\"usage_pct\": " + json_num(r.second_order_usage_pct)
         + ", \"savings_bpp\": " + json_num(r.second_order_savings_bpp)
         + ", \"savings_bytes\": " + json_int(r.residual_pool_savings_bits / 8.0) + "},\n";

    json += "  \"quadtree\": {\"leaves\": {\"512\": " + std::to_string(r.leaves_512)
         + ", \"256\": " + std::to_string(r.leaves_256)
         + ", \"128\": " + std::to_string(r.leaves_128)
         + ", \"64\": " + std::to_string(r.leaves_64)
         + "}, \"total_blocks\": " + std::to_string(r.total_blocks) + "},\n";

    json += "  \"entropy_budget\": {\"magnitude\": " + json_num(r.budget.magnitude_class_bpp)
         + ", \"zero_runs\": " + json_num(r.budget.zero_run_bpp)
         + ", \"remainder\": " + json_num(r.budget.remainder_bpp)
         + ", \"parameters\": " + json_num(r.budget.params_bpp)
         + ", \"overhead\": " + json_num(r.budget.overhead_bpp)
         + ", \"total\": " + json_num(r.budget.total_bpp) + "},\n";

    if (r.wavelet_evaluated) {
        json += "  \"wavelet\": {\"evaluated\": true, \"predictor_estimate_bpp\": "
             + json_num(r.predictor_estimate_bpp)
             + ", \"wavelet_estimate_bpp\": " + json_num(r.wavelet_estimate_bpp)
             + ", \"recommended\": " + (r.wavelet_recommended ? "true" : "false") + "},\n";
    }

    json += "  \"timing_ms\": {\"load\": " + json_int(load_ms)
         + ", \"quadtree_build\": " + json_int(r.time_quadtree_ms)
         + ", \"predictor_eval\": " + json_int(r.time_predictor_eval_ms)
         + ", \"entropy_calc\": " + json_int(r.time_entropy_ms)
         + ", \"total_analysis\": " + json_int(analyze_ms) + "}\n";
    json += "}\n";

    std::cout << json;
}

void print_usage() {
    std::cerr << "Usage: xtm analyze <input_raster> [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --wavelet, -w       Evaluate the DWT pipeline against the predictor pipeline\n";
    std::cerr << "  --precision <value> Precision factor for analysis (default 1.0)\n";
    std::cerr << "  --json              Machine-readable JSON report on stdout\n";
    std::cerr << "  --compact           Only the Dataset Overview and Summary sections\n";
    std::cerr << "  --no-color          Disable ANSI colors\n";
}

} // namespace

double parse_precision(const std::string& arg);

int run_analyze(int argc, char** argv) {
    std::string input_path;
    double precision = 1.0;
    bool enable_wavelet = false;
    bool json_mode = false;
    bool compact = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--wavelet" || arg == "-w") {
            enable_wavelet = true;
        } else if (arg == "--json") {
            json_mode = true;
        } else if (arg == "--compact") {
            compact = true;
        } else if (arg == "--no-color") {
            g_no_color = true;
        } else if ((arg == "--scale" || arg == "--precision") && i + 1 < argc) {
            precision = parse_precision(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (input_path.empty() && arg[0] != '-') {
            input_path = arg;
        }
    }

    if (input_path.empty()) {
        print_usage();
        return 1;
    }

    try {
        if (!json_mode) {
            std::cout << "Loading dataset: " << input_path << "...\n";
            std::cout << "Analyzing terrain (precision=" << precision << ")...\n";
        }

        api::EncodeOptions enc_options(precision);
        analyzer::AnalyzerOptions options;
        options.enable_wavelet_analysis = enable_wavelet;

        double load_ms = 0.0;
        double analyze_ms = 0.0;
        auto report = api::analyze_file(input_path, enc_options, options, &load_ms, &analyze_ms);

        if (json_mode) {
            print_json(report, input_path, load_ms, analyze_ms);
            return 0;
        }

        const int cols = term::width();

        int section_no = 0;
        auto print_header = [&section_no](const std::string& title) {
            std::cout << "\n" << term::rule(std::to_string(++section_no) + ". " + title) << "\n";
        };

        std::vector<std::pair<std::string, std::string>> kv_rows;
        auto kv = [&](const std::string& label, const std::string& value) {
            kv_rows.emplace_back(label, value);
        };
        auto flush_kv = [&]() {
            std::size_t w = 0;
            for (const auto& [l, v] : kv_rows) w = std::max(w, l.size() + 2);
            for (const auto& [l, v] : kv_rows) {
                std::cout << "  " << std::left
                          << std::setw(static_cast<int>(w)) << (l + ":")
                          << v << "\n";
            }
            kv_rows.clear();
        };

        // ---- 1. Dataset Overview ----
        print_header("Dataset Overview");
        {
            kv("Dimensions", term::dim(std::format("{} x {} ({} pixels)",
                term::thousands(report.width), term::thousands(report.height),
                term::thousands(report.sample_count))));

            std::string nd = std::format("{} ({:.2f}%)",
                term::thousands(report.nodata_pixels),
                report.sample_count > 0
                    ? 100.0 * report.nodata_pixels / report.sample_count : 0.0);
            if (report.nodata_pixels > 0) {
                double pct = 100.0 * report.nodata_pixels / report.sample_count;
                if (pct > 10.0) nd = term::warn(nd);
            }
            kv("NoData pixels", nd);

            if (report.raw.valid_pixels > 0) {
                kv("Raw Elevation", std::format("[{:.2f}, {:.2f}]  mean: {:.2f}  stddev: {:.2f}  ({} valid)",
                    report.raw.min_val, report.raw.max_val, report.raw.mean, report.raw.stddev,
                    term::thousands(report.raw.valid_pixels)));

                const auto& P = report.raw.percentiles;
                kv("Percentiles", std::format("p1 {:.1f}, p25 {:.1f}, p50 {:.1f}, p75 {:.1f}, p99 {:.1f}",
                    P[0], P[1], P[2], P[3], P[4]));

                if (cols >= 80 && !report.raw.elevation_histogram.empty()) {
                    kv("Elevation distribution", term::sparkline(report.raw.elevation_histogram));
                }
            }

            kv("Quantized @ " + std::format("{:g}", report.precision),
               std::format("[{:.2f}, {:.2f}]  mean: {:.2f}  stddev: {:.2f}",
                   report.quantized.min_val, report.quantized.max_val,
                   report.quantized.mean, report.quantized.stddev));

            std::string corr = std::format("H: {:.4f}  V: {:.4f}  D: {:.4f}",
                report.corr_h, report.corr_v, report.corr_d);
            if (report.corr_h >= 0.95 && report.corr_v >= 0.95 && report.corr_d >= 0.95) {
                corr = term::ok(corr);
            } else if (report.corr_h < 0.3 || report.corr_v < 0.3) {
                corr = term::warn(corr);
            }
            kv("Spatial correlation", corr);

            if (report.has_georeference) {
                kv("Reference system", term::dim(report.crs.empty() ? "unknown" : report.crs));
                std::string units = report.pixel_units;
                kv("Pixel size", term::dim(std::format("{:g} x {:g}{}{}",
                    report.pixel_width, report.pixel_height,
                    units.empty() ? "" : " ", units)));
                kv("Bounding box", term::dim(std::format("[{:.4f}, {:.4f}, {:.4f}, {:.4f}]{}{}",
                    report.bbox_min_x, report.bbox_min_y, report.bbox_max_x, report.bbox_max_y,
                    units.empty() ? "" : " ", units)));
            }
            flush_kv();
        }

        // ---- 2. Compressibility & Precision Guidance ----
        if (!compact) {
            print_header("Compressibility & Precision Guidance");
            {
                std::string cost = std::format("{:.4f} bpp at precision {:.4f}",
                    report.budget.total_bpp, report.precision);
                if (report.budget.total_bpp > 8.0) cost = term::warn(cost);
                kv("Estimated coding cost", cost);

                std::string size_str = std::format("{:.2f} MB",
                    report.estimated_file_bytes / (1024.0 * 1024.0));
                if (report.estimated_bytes_stddev > 0.0) {
                    size_str += std::format(" ± {:.2f} MB",
                        report.estimated_bytes_stddev / (1024.0 * 1024.0));
                }
                kv("Estimated file size", size_str);

                std::string ratio = std::format("{:.2f}:1 vs raw Float32",
                    report.estimated_compression_ratio);
                if (report.estimated_compression_ratio >= 5.0) {
                    ratio = term::ok(ratio);
                } else if (report.estimated_compression_ratio < 2.0) {
                    ratio = term::warn(ratio);
                }
                kv("Est. compression ratio", ratio);
                flush_kv();

                if (report.precision_estimates.size() > 1) {
                    std::cout << "\n  Precision options:\n";
                    std::cout << std::format("  {:<10}{:<10}{:<14}{}\n",
                        "Precision", "Est. bpp", "Est. size", "Note");
                    std::cout << std::format("  {}\n", std::string(10 + 10 + 14 + 20, '-'));
                    for (std::size_t k = 0; k < report.precision_estimates.size(); ++k) {
                        const auto& pe = report.precision_estimates[k];
                        std::string note;
                        if (k > 0) {
                            note = std::format("saves {:.2f} bpp",
                                report.precision_estimates[0].bpp - pe.bpp);
                        } else {
                            note = "(current)";
                        }
                        std::cout << std::format("  {:<10}{:<10.2f}{:<14}{}\n",
                            std::format("{:g}", pe.precision), pe.bpp,
                            std::format("{:.2f} MB", pe.estimated_file_bytes / (1024.0 * 1024.0)),
                            note);
                    }
                }

                if (!report.digit_planes.empty()) {
                    if (report.digit_planes.size() == 1) {
                        const auto& dp = report.digit_planes[0];
                        kv("Digit-plane entropy",
                           std::format("{} = {:.4f} bpp", plane_label(report, dp.place), dp.bpp));
                        flush_kv();
                    } else {
                        std::cout << "\n  Digit-plane entropies:\n";
                        std::cout << std::format("  {:<14}{:>10}\n", "Digit plane", "Entropy");
                        std::cout << std::format("  {}\n", std::string(14 + 10, '-'));
                        for (const auto& dp : report.digit_planes) {
                            std::cout << std::format("  {:<14}{:>10.4f}\n",
                                plane_label(report, dp.place), dp.bpp);
                        }
                    }
                }
            }
        }

        // ---- 3. Predictor Analysis ----
        if (!compact) {
            print_header("Predictor Analysis");
            {
                const bool wide = cols >= 100;
                if (wide) {
                    std::cout << std::format("  {:>4} {:<14}{:>13} {:>11}  {:<15} {:>8}% {:>9}\n",
                        "Rank", "Predictor", "Selection bpp", "Shannon bpp",
                        "Usage", "", "Avg |res|");
                    std::cout << std::format("  {}\n",
                        std::string(4 + 1 + 14 + 13 + 1 + 11 + 2 + 15 + 1 + 8 + 1 + 1 + 9, '-'));
                } else {
                    std::cout << std::format("  {:<6}{:<16}{:>13} {:>12}{:>8}% {:>9}\n",
                        "Rank", "Predictor", "Selection bpp", "Shannon bpp",
                        "Usage", "Avg |res|");
                    std::cout << std::format("  {}\n",
                        std::string(6 + 16 + 13 + 1 + 12 + 8 + 1 + 1 + 9, '-'));
                }

                int rank = 1;
                for (const auto& p : report.predictors) {
                    double usage_pct = report.total_blocks > 0
                        ? 100.0 * p.usage_blocks / report.total_blocks : 0.0;
                    std::string row;
                    if (wide) {
                        std::string usage_bar = term::bar(usage_pct / 100.0, 15);
                        if (p.usage_blocks > 0) usage_bar.replace(0, 3, "\u2588");
                        row = std::format("  {:>4} {:<14}{:>13.4f} {:>11.4f}  {:<15} {:>8.1f}% {:>9.2f}",
                            rank++, p.name, p.selection_bpp, p.shannon_bpp,
                            usage_bar, usage_pct, p.avg_abs_residual);
                    } else {
                        row = std::format("  {:<6}{:<16}{:>13.4f} {:>12.4f}{:>8.1f}% {:>9.2f}",
                            rank++, p.name, p.selection_bpp, p.shannon_bpp,
                            usage_pct, p.avg_abs_residual);
                    }
                    std::cout << row;
                    if (p.id == report.chosen_predictor) {
                        std::cout << term::dim("  \u2190 encoder pick");
                    }
                    std::cout << "\n";
                }

                const auto& min_shan = *std::min_element(report.predictors.begin(),
                    report.predictors.end(),
                    [](const auto& a, const auto& b) { return a.shannon_bpp < b.shannon_bpp; });
                const auto& chosen = *std::find_if(report.predictors.begin(), report.predictors.end(),
                    [&](const auto& p) { return p.id == report.chosen_predictor; });
                double chosen_pct = report.total_blocks > 0
                    ? 100.0 * chosen.usage_blocks / report.total_blocks : 0.0;
                if (chosen.id != min_shan.id &&
                    chosen.usage_blocks > min_shan.usage_blocks &&
                    chosen.selection_bpp < min_shan.selection_bpp) {
                    std::cout << term::warn(std::format(
                        "  Note: {} dominates by usage ({:.1f}%) despite {} having lower\n",
                        chosen.name, chosen_pct, min_shan.name))
                              << term::warn(
                        "        Shannon entropy \u2014 selection overhead outweighs per-pixel savings here.\n");
                }

                const char* chosen_name = "";
                for (const auto& p : report.predictors) {
                    if (p.id == report.chosen_predictor) chosen_name = p.name;
                }
                kv("Encoder will choose", std::string(chosen_name));
                flush_kv();

                std::cout << "\n  Residual re-prediction:\n";
                std::string trig = std::format("on {:.1f}% of blocks; est. savings {:.4f} bpp ({} B)",
                    report.second_order_usage_pct, report.second_order_savings_bpp,
                    term::thousands(static_cast<long long>(report.residual_pool_savings_bits / 8.0)));
                kv("Triggered", trig);
                const char* resid_names[7] = {"None", "Average", "Median", "Left", "Gradient", "Gap", "LeastSq."};
                std::ostringstream winners;
                for (std::size_t i = 1; i < 7; ++i) {
                    winners << resid_names[i] << " " << term::thousands(static_cast<long long>(report.residual_predictor_blocks[i]));
                    if (i < 6) winners << " | ";
                }
                kv("Pool winners", winners.str());
                flush_kv();
            }
        }

        // ---- 4. Quadtree Analysis ----
        if (!compact) {
            print_header("Quadtree Analysis");
            {
                struct LeafRow { const char* label; std::size_t count; };
                const LeafRow sizes[4] = {
                    {"512x512", report.leaves_512}, {"256x256", report.leaves_256},
                    {"128x128", report.leaves_128}, {"64x64", report.leaves_64},
                };
                const std::size_t max_count = std::max({
                    report.leaves_512, report.leaves_256, report.leaves_128, report.leaves_64 });

                if (cols >= 60 && max_count > 0) {
                    std::cout << std::format("  {:<10}{:>7}  {:<20}{:>9}\n",
                        "Leaf size", "Count", "Share", "Share %");
                    std::cout << std::format("  {}\n", std::string(10 + 7 + 2 + 20 + 9, '-'));
                    for (const auto& s : sizes) {
                        if (s.count == 0) continue;
                        double frac = static_cast<double>(s.count) / static_cast<double>(max_count);
                        std::string share = term::bar(frac, 20);
                        if (s.count > 0) share.replace(0, 3, "\u2588");
                        std::cout << std::format("  {:<10}{:>7}  {:<20}{:>8.1f}%\n",
                            s.label, term::thousands(static_cast<long long>(s.count)),
                            share,
                            100.0 * static_cast<double>(s.count) / static_cast<double>(report.total_blocks));
                    }
                } else {
                    kv("Leaves", std::format("512x512: {}   256x256: {}   128x128: {}   64x64: {}",
                        term::thousands(report.leaves_512), term::thousands(report.leaves_256),
                        term::thousands(report.leaves_128), term::thousands(report.leaves_64)));
                }

                kv("Total blocks", term::dim(term::thousands(report.total_blocks)));
                std::string area;
                if (report.total_blocks > 0) {
                    double avg_px = static_cast<double>(report.sample_count) / report.total_blocks;
                    area = std::format("{:.1f} px ({:.0f}x{:.0f})", avg_px,
                        std::sqrt(avg_px), std::sqrt(avg_px));
                }
                kv("Avg block area", term::dim(area));
                kv("Partition + header overhead",
                   std::format("{:.4f} bpp", report.budget.overhead_bpp));
                flush_kv();
            }
        }

        // ---- 5. Entropy Budget ----
        if (!compact) {
            print_header("Entropy Budget");
            {
                const auto& b = report.budget;
                kv("Magnitude classes", std::format("{:.4f} bpp", b.magnitude_class_bpp));
                kv("Zero runs", std::format("{:.4f} bpp", b.zero_run_bpp));
                kv("Remainder bits", std::format("{:.4f} bpp", b.remainder_bpp));
                kv("Parameters", std::format("{:.4f} bpp", b.params_bpp));
                kv("Overhead", std::format("{:.4f} bpp", b.overhead_bpp));
                kv("TOTAL", term::colorize(std::format("{:.4f} bpp", b.total_bpp),
                                           term::Color::Default, true));
                flush_kv();

                if (cols >= 100 && b.total_bpp > 0.0) {
                    const double parts[5] = {b.magnitude_class_bpp, b.zero_run_bpp,
                        b.remainder_bpp, b.params_bpp, b.overhead_bpp};
                    const char* chars[5] = {"\u2588", "\u2589", "\u258A", "\u258B", "\u258D"};
                    const term::Color colors[5] = {term::Color::Green, term::Color::Cyan,
                        term::Color::Yellow, term::Color::Magenta, term::Color::Gray};
                    const char* names[5] = {"mag", "runs", "rem", "params", "ovh"};

                    const int barw = 30;
                    int widths[5] = {0, 0, 0, 0, 0};
                    int used = 0;
                    for (int i = 0; i < 5; ++i) {
                        int n = (i == 4)
                            ? barw - used
                            : std::lround(parts[i] / b.total_bpp * barw);
                        n = std::clamp(n, 0, barw - used);
                        widths[i] = n;
                        used += n;
                    }
                    std::string bar_s;
                    for (int i = 0; i < 5; ++i) {
                        std::string seg;
                        for (int j = 0; j < widths[i]; ++j) seg += chars[i];
                        bar_s += term::colorize(seg, colors[i]);
                    }
                    std::cout << "  Budget split: " << bar_s << "\n";
                    std::cout << "  ";
                    for (int i = 0; i < 5; ++i) {
                        if (i > 0) std::cout << "  ";
                        std::cout << term::colorize(std::string(chars[i]) + " " + names[i], colors[i])
                                  << std::format(" {:.4f}", parts[i]);
                    }
                    std::cout << "\n";
                }
            }
        }

        // ---- 6. Wavelet Evaluation ----
        if (!compact && report.wavelet_evaluated) {
            print_header("Wavelet Evaluation");
            std::string wv = std::format("{:.4f} bpp", report.wavelet_estimate_bpp);
            if (report.wavelet_estimate_bpp < report.predictor_estimate_bpp) {
                wv = term::ok(wv);
            } else if (report.wavelet_estimate_bpp > report.predictor_estimate_bpp) {
                wv = term::warn(wv);
            }
            kv("Predictor estimate", std::format("{:.4f} bpp", report.predictor_estimate_bpp));
            kv("DWT estimate", wv);

            double gain = report.predictor_estimate_bpp - report.wavelet_estimate_bpp;
            std::string g = std::format("{:+.4f} bpp", gain);
            if (gain > 0.0) g = term::ok(g);
            else if (gain < 0.0) g = term::warn(g);
            kv("Wavelet gain", g);

            std::string rec = report.wavelet_recommended
                ? term::ok("use the wavelet pipeline") : term::warn("stay with the predictor pipeline");
            kv("Recommendation", rec);
            flush_kv();
        }

        // ---- 7. Summary ----
        print_header("Summary");
        {
            const double mb = report.estimated_file_bytes / (1024.0 * 1024.0);
            const double ratio = report.estimated_compression_ratio;
            const bool corr_good = report.corr_h >= 0.95 && report.corr_v >= 0.95;
            const bool corr_bad = report.corr_h < 0.3 || report.corr_v < 0.3;

            std::vector<std::string> tldr;
            if (ratio >= 5.0) {
                tldr.push_back(term::ok(std::format("\u2713 {:.2f}:1 compression ({:.2f} MB)", ratio, mb)));
            }
            if (corr_good) {
                tldr.push_back(term::ok(std::format("\u2713 Terrain smooth, predictors correlate well (H/V/D \u2248 1.0)")));
            } else if (corr_bad) {
                tldr.push_back(term::warn(std::format("\u26a0 Low spatial correlation (H {:.2f}, V {:.2f}) \u2014 expect modest compression",
                    report.corr_h, report.corr_v)));
            }
            const auto& chosen = *std::find_if(report.predictors.begin(), report.predictors.end(),
                [&](const auto& p) { return p.id == report.chosen_predictor; });
            if (report.total_blocks > 0 && chosen.usage_blocks > 0) {
                double usage_pct = 100.0 * chosen.usage_blocks / report.total_blocks;
                if (usage_pct >= 30.0) {
                    tldr.push_back(term::warn(std::format("\u26a0 {:.1f}% of blocks fall back to {} predictor",
                        usage_pct, chosen.name)));
                }
            }
            if (report.wavelet_evaluated && report.wavelet_recommended) {
                tldr.push_back(term::ok(std::format("\u2713 Wavelet pipeline estimated {:.4f} bpp cheaper",
                    report.predictor_estimate_bpp - report.wavelet_estimate_bpp)));
            }

            if (!tldr.empty()) {
                std::cout << "\n";
                for (const auto& line : tldr) std::cout << "  " << line << "\n";
                std::cout << "\n";
            }

            std::string est = std::format("{:.2f} MB ({:.4f} bpp, ~{:.2f}:1 vs raw Float32)",
                mb, report.budget.total_bpp, ratio);
            kv("Estimated size at precision " + std::format("{:g}", report.precision), est);

            kv("Residual re-prediction saves",
               std::format("{:.4f} bpp ({} B)", report.second_order_savings_bpp,
                   term::thousands(static_cast<long long>(report.residual_pool_savings_bits / 8.0))));

            if (report.precision_estimates.size() > 1) {
                const auto& finest = report.precision_estimates[0];
                std::size_t best = 1;
                double best_save = 0.0;
                for (std::size_t k = 1; k < report.precision_estimates.size(); ++k) {
                    double save = finest.bpp - report.precision_estimates[k].bpp;
                    if (save > best_save) {
                        best_save = save;
                        best = k;
                    }
                }
                const auto& pe = report.precision_estimates[best];
                kv("Recommended precision",
                   std::format("the biggest 10x step saves {:.2f} bpp ({:g} m -> ~{:.2f} MB)",
                       best_save, pe.precision, pe.estimated_file_bytes / (1024.0 * 1024.0)));
            } else {
                kv("Recommended precision", "current precision only");
            }

            if (report.wavelet_evaluated) {
                kv("Recommended pipeline",
                   report.wavelet_recommended ? "wavelet" : "predictor");
            }

            std::string terrain_note;
            if (report.corr_h > 0.9 && report.corr_v > 0.9) {
                terrain_note = "smooth and highly correlated; predictors should perform well";
            } else if (report.corr_h < 0.3 || report.corr_v < 0.3) {
                terrain_note = "noisy/low-correlation; expect modest compression";
            }
            if (!terrain_note.empty()) {
                std::string note = terrain_note;
                if (report.corr_h > 0.9 && report.corr_v > 0.9) note = term::ok(note);
                else note = term::warn(note);
                kv("Terrain character", note);
            }

            if (report.time_quadtree_ms + report.time_predictor_eval_ms + report.time_entropy_ms > 0.0) {
                double total = report.time_quadtree_ms + report.time_predictor_eval_ms
                             + report.time_entropy_ms;
                kv("Phase timing", term::dim(std::format(
                    "quadtree {:.1f}%, predictor eval {:.1f}%, entropy {:.1f}%",
                    100.0 * report.time_quadtree_ms / total,
                    100.0 * report.time_predictor_eval_ms / total,
                    100.0 * report.time_entropy_ms / total)));
            }
            kv("Compute", term::dim(std::format("load {:.0f} ms, analysis {:.0f} ms", load_ms, analyze_ms)));
            flush_kv();
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace xtm::cli