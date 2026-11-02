#include "kr32/cxx/cxx.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_help() {
    std::printf(
        "Usage: kr32-cxx <input.cpp> [--emit asm|obj] [-o output]\n"
        "\n"
        "Standalone KR32 C++ compiler driver for development and tests.\n"
        "The input is compiled by the kr32_cxx library; assembly is written to\n"
        "stdout unless -o is supplied.\n"
        "\n"
        "Options:\n"
        "  --emit asm|obj   Select assembly or relocatable-object output\n"
        "  -o <path>        Write output to path\n"
        "  --source-name N  Override the diagnostic source name\n"
        "  -h, --help       Show this help\n");
}

bool read_file(const std::string& path, std::string& text, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path;
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool write_file(const std::string& path, std::string_view text, std::string& error) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "cannot write " + path;
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes,
                std::string& error) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "cannot write " + path;
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string input_path;
    std::string output_path;
    std::string source_name;
    kr32::cxx::Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            print_help();
            return 0;
        }
        if (argument == "--emit") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32-cxx: --emit needs asm or obj\n");
                return 2;
            }
            const std::string value = argv[++index];
            if (value == "asm" || value == "assembly") {
                options.emit = kr32::cxx::Emit::Assembly;
            } else if (value == "obj" || value == "object") {
                options.emit = kr32::cxx::Emit::Object;
            } else {
                std::fprintf(stderr, "kr32-cxx: unknown emit kind %s\n", value.c_str());
                return 2;
            }
            continue;
        }
        if (argument.rfind("--emit=", 0) == 0) {
            const std::string value = argument.substr(7);
            if (value == "asm" || value == "assembly") {
                options.emit = kr32::cxx::Emit::Assembly;
            } else if (value == "obj" || value == "object") {
                options.emit = kr32::cxx::Emit::Object;
            } else {
                std::fprintf(stderr, "kr32-cxx: unknown emit kind %s\n", value.c_str());
                return 2;
            }
            continue;
        }
        if (argument == "-o") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32-cxx: -o needs a path\n");
                return 2;
            }
            output_path = argv[++index];
            continue;
        }
        if (argument == "--source-name") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32-cxx: --source-name needs a value\n");
                return 2;
            }
            source_name = argv[++index];
            continue;
        }
        if (!argument.empty() && argument[0] == '-') {
            std::fprintf(stderr, "kr32-cxx: unknown option %s\n", argument.c_str());
            return 2;
        }
        if (!input_path.empty()) {
            std::fprintf(stderr, "kr32-cxx: only one input file is supported\n");
            return 2;
        }
        input_path = argument;
    }

    if (input_path.empty()) {
        std::fprintf(stderr, "kr32-cxx: no input file\n");
        print_help();
        return 2;
    }
    if (options.emit == kr32::cxx::Emit::Object && output_path.empty()) {
        std::fprintf(stderr, "kr32-cxx: object output requires -o\n");
        return 2;
    }

    std::string source;
    std::string error;
    if (!read_file(input_path, source, error)) {
        std::fprintf(stderr, "kr32-cxx: %s\n", error.c_str());
        return 1;
    }
    if (source_name.empty()) source_name = input_path;
    options.source_name = source_name;

    const kr32::cxx::Result result = kr32::cxx::compile(source, options);
    for (const kr32::cxx::Diagnostic& diagnostic : result.diagnostics) {
        std::fprintf(stderr, "%s:%d:%d: %s\n", source_name.c_str(), diagnostic.line,
                     diagnostic.column, diagnostic.message.c_str());
    }
    if (!result.ok) {
        return 1;
    }

    if (options.emit == kr32::cxx::Emit::Assembly) {
        if (output_path.empty()) {
            std::fwrite(result.assembly.data(), 1, result.assembly.size(), stdout);
        } else if (!write_file(output_path, result.assembly, error)) {
            std::fprintf(stderr, "kr32-cxx: %s\n", error.c_str());
            return 1;
        }
    } else if (!write_file(output_path, result.object, error)) {
        std::fprintf(stderr, "kr32-cxx: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
