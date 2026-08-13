#include "EncodeCmd.hpp"
#include "xtm/Api.hpp"
#include "xtm/predictor/Predictors.hpp"
#include <algorithm>
#include <cstdlib>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
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

std::string dim(std::string_view s) { return colorize(s, Color::Gray); }
std::string heading(std::string_view s) { return colorize(s, Color::Cyan, true); }

std::string thousands(long long n) {
    std::string s = std::to_string(n);
    bool neg = !s.empty() && s[0] == '-';
    int start = neg ? 1 : 0;
    for (int i = static_cast<int>(s.size()) - 3; i > start; i -= 3)
        s.insert(i, ",");
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

std::string strip_ansi(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && s[j] != 'm') ++j;
            i = j;
        } else {
            out += s[i];
        }
    }
    return out;
}

int display_width(std::string_view s) {
    std::string plain = strip_ansi(s);
    int w = 0;
    for (char ch : plain) {
        unsigned char c = static_cast<unsigned char>(ch);
        if ((c & 0xC0) != 0x80) ++w;
    }
    return w;
}

std::string pad_right(const std::string& s, int width) {
    int raw = display_width(s);
    if (raw >= width) return s;
    return s + std::string(width - raw, ' ');
}

int column_width(const std::vector<std::string>& lines) {
    int w = 0;
    for (const auto& l : lines) {
        w = std::max(w, display_width(l));
    }
    return w;
}

void print_side_by_side(const std::vector<std::string>& left,
                        const std::vector<std::string>& right) {
    int lw = column_width(left);
    size_t n = std::max(left.size(), right.size());
    for (size_t i = 0; i < n; ++i) {
        std::cout << pad_right(i < left.size() ? left[i] : std::string(), lw)
                  << "  "
                  << (i < right.size() ? right[i] : std::string())
                  << "\n";
    }
}

} // namespace

double parse_precision(const std::string& arg) {
    if (arg == "m" || arg == "meters" || arg == "meter") return 1.0;
    if (arg == "dm" || arg == "decimeter" || arg == "decimeters") return 0.1;
    if (arg == "cm" || arg == "centimeter" || arg == "centimeters") return 0.01;
    if (arg == "mm" || arg == "millimeter" || arg == "millimeters") return 0.001;
    try {
        return std::stod(arg);
    } catch (const std::exception&) {
        return -1.0;
    }
}

std::vector<std::string> pipeline_stat_lines(const api::EncodeResult& result,
                                             analyzer::PipelineType pipeline_type) {
    std::vector<std::string> lines;
    if (pipeline_type == analyzer::PipelineType::Wavelet) {
        // Format consumed by utils/benchmark_suite.py
        if (result.total_blocks > 0) {
            lines.push_back("  Wavelet Transform applied to " + term::thousands(result.total_blocks)
                          + " blocks (100.00%)");
        } else {
            lines.push_back("  No blocks processed.");
        }
        return lines;
    }

    if (result.total_blocks == 0) {
        lines.push_back("  No blocks processed.");
        return lines;
    }

    predictor::PredictorBank bank;
    std::vector<const predictor::Predictor*> predictors_list = bank.ordered();

    struct Stat {
        std::string name;
        uint32_t count;
    };

    std::vector<Stat> stats;
    for (const auto& kv : result.predictor_counts) {
        if (kv.second == 0) continue;
        std::string name = "Unknown";
        if (kv.first < predictors_list.size()) {
            name = predictors_list[kv.first]->name();
        }
        stats.push_back({name, kv.second});
    }

    // Sort by most used predictors descending
    std::sort(stats.begin(), stats.end(), [](const Stat& a, const Stat& b) {
        return a.count > b.count;
    });

    // Rows are parsed by utils/benchmark_suite.py via a predictor name -> id
    // map; the line prefix ("  <name> <blocks> <pct>%") is the contract.
    lines.push_back(std::format("  {:<16}{:>9}  {:>10}", "Predictor", "Blocks", "Usage"));
    lines.push_back("  " + std::string(16 + 9 + 2 + 10, '-'));
    for (const auto& s : stats) {
        double pct = (double)s.count / result.total_blocks * 100.0;
        lines.push_back(std::format("  {:<16}{:>9}  {:>9.1f}%",
            s.name, term::thousands(s.count), pct));
    }
    return lines;
}

std::vector<std::string> profiling_lines(const api::EncodeResult& result) {
    std::vector<std::string> lines;
    lines.push_back(std::format("  {:<20}{:>13}", "Stage", "Time (ms)"));
    lines.push_back("  " + std::string(20 + 13, '-'));

    struct Row { const char* name; double ms; bool total; };
    const Row rows[] = {
        {"Ingest/Quantize", result.time_load_ms, false},
        {"Quadtree/Prediction", result.time_quadtree_ms, false},
        {"Entropy Coding", result.time_entropy_ms, false},
        {"Container IO", result.time_io_ms, false},
        {"Total Execution", result.time_total_ms, true},
    };
    for (const auto& r : rows) {
        std::string value = std::format("{:.0f} ms", r.ms);
        if (r.total) {
            value = term::colorize(value, term::Color::Default, true);
        } else {
            value = term::dim(value);
        }
        lines.push_back(std::format("  {:<20}{:>13}", r.name, value));
    }
    return lines;
}

int run_encode(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: xtm encode <input.tif> -o <output.xtm>\n"
                  << "  [--precision <value|m|dm|cm|mm>] (alias: --scale; default 1.0)\n"
                  << "  [--pipeline predictor|wavelet] (default predictor; wavelet requires precision >= 1.0)\n"
                  << "  [--context simple|extended] (default simple)\n"
                  << "  [--disable-quadtree] (fixed 64x64 blocks)\n"
                  << "  [--threads <n>] (0 = all cores; default 0)\n"
                  << "  [--no-color] (disable ANSI colors)\n";
        return 1;
    }

    std::string input_file;
    std::string output_file;

    if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        std::cout << "Usage: xtm encode <input.tif> -o <output.xtm>\n"
                  << "  [--precision <value|m|dm|cm|mm>] (alias: --scale; default 1.0)\n"
                  << "  [--pipeline predictor|wavelet] (default predictor; wavelet requires precision >= 1.0)\n"
                  << "  [--context simple|extended] (default simple)\n"
                  << "  [--disable-quadtree] (fixed 64x64 blocks)\n"
                  << "  [--threads <n>] (0 = all cores; default 0)\n"
                  << "  [--no-color] (disable ANSI colors)\n";
        return 0;
    }
    double precision = 1.0;
    bool disable_quadtree = false;
    coding::ContextModel model = coding::ContextModel::Simple;
    analyzer::PipelineType pipeline_type = analyzer::PipelineType::Predictor;
    uint32_t num_threads = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool has_next = (i + 1 < argc);

        if (arg == "-o" && has_next) output_file = argv[++i];
        else if (arg == "--no-color") g_no_color = true;
        else if ((arg == "--scale" || arg == "--precision") && has_next) precision = parse_precision(argv[++i]);
        else if (arg == "--disable-quadtree") disable_quadtree = true;
        else if (arg == "--threads" && has_next) {
            try {
                long t = std::stol(argv[++i]);
                if (t < 0) throw std::out_of_range("negative");
                num_threads = static_cast<uint32_t>(t);
            } catch (const std::exception&) {
                std::cerr << "Error: invalid --threads value.\n";
                return 1;
            }
        } else if (arg == "--context" && has_next) {
            std::string c = argv[++i];
            if (c != "simple" && c != "extended") { std::cerr << "Unknown context: " << c << "\n"; return 1; }
            model = (c == "extended") ? coding::ContextModel::Extended : coding::ContextModel::Simple;
        } else if (arg == "--pipeline" && has_next) {
            std::string p = argv[++i];
            if (p != "predictor" && p != "wavelet") { std::cerr << "Unknown pipeline: " << p << "\n"; return 1; }
            pipeline_type = (p == "wavelet") ? analyzer::PipelineType::Wavelet : analyzer::PipelineType::Predictor;
        } else if (input_file.empty() && arg[0] != '-') input_file = arg;
        else {
            std::cerr << "Invalid or incomplete argument: " << arg << "\n";
            return 1;
        }
    }

    if (input_file.empty()) {
        std::cerr << "Error: Input file is required.\n";
        return 1;
    }

    if (output_file.empty()) {
        std::cerr << "Error: Output file (-o) is required.\n";
        return 1;
    }

    if (precision <= 0.0) {
        std::cerr << "Error: invalid --precision value.\n";
        return 1;
    }

    try {
        std::cout << "Loading dataset: " << input_file << "...\n";

        std::cout << "Encoding terrain (precision=" << precision << ") to " << output_file << "...\n";
        if (disable_quadtree) {
            std::cout << "Quadtree disabled (using fixed 64x64 blocks).\n";
        }

        std::cout << "Pipeline Type: " << (pipeline_type == analyzer::PipelineType::Wavelet ? "Wavelet" : "Predictor") << "\n";

        api::EncodeOptions options(precision, model, pipeline_type, disable_quadtree, num_threads);
        auto result = api::encode_file(input_file, output_file, options);

        std::cout << "Partitioned into " << term::thousands(result.total_blocks) << " independent blocks across superblocks.\n";

        auto stats_lines = pipeline_stat_lines(result, pipeline_type);
        auto prof_lines = profiling_lines(result);

        std::vector<std::string> left = {
            term::rule(pipeline_type == analyzer::PipelineType::Wavelet
                           ? "Wavelet Statistics" : "Predictor Statistics",
                       column_width(stats_lines))
        };
        left.insert(left.end(), stats_lines.begin(), stats_lines.end());

        std::vector<std::string> right = {
            term::rule("Pipeline Profiling", column_width(prof_lines))
        };
        right.insert(right.end(), prof_lines.begin(), prof_lines.end());

        std::cout << "\n";
        print_side_by_side(left, right);
        std::cout << "\n";

        std::cout << "Successfully encoded to " << output_file << "!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace xtm::cli