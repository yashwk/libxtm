#include <iostream>
#include <string>
#include "AnalyzeCmd.hpp"
#include "EncodeCmd.hpp"
#include "DecodeCmd.hpp"
#include "InfoCmd.hpp"
#include "VerifyCmd.hpp"

void print_usage() {
    std::cout << "Usage: xtm <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  analyze <input.tif>    Run terrain analyzer and print statistics\n";
    std::cout << "  encode <input.tif> -o <output.xtm> [--scale <value>]\n";
    std::cout << "  decode <input.xtm> -o <output.tif> [--region x y w h]\n";
    std::cout << "  info <input.xtm>       Print header and block index info\n";
    std::cout << "  verify <input.xtm> [input.tif]  Verify checksums or diff against source\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "analyze") {
        if (argc < 3) {
            std::cerr << "Usage: xtm analyze <input.tif> [--scale <value>]\n";
            return 1;
        }
        return xtm::cli::run_analyze(argc - 1, argv + 1);
    } else if (command == "encode") {
        return xtm::cli::run_encode(argc - 1, argv + 1);
    } else if (command == "decode") {
        return xtm::cli::run_decode(argc - 1, argv + 1);
    } else if (command == "info") {
        return xtm::cli::run_info(argc - 1, argv + 1);
    } else if (command == "verify") {
        return xtm::cli::run_verify(argc - 1, argv + 1);
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage();
        return 1;
    }

    return 0;
}
