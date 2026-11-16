#include "doctest.h"

#include "kr32/version.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// The status line of docs/isa-v1.md carries the frozen identifier, for example
// "> **Status:** v1, frozen 2026-11-15." The binary reports the same value, so a
// document revision that forgets the constant (or the reverse) fails here.
std::string documented_isa_version() {
    std::ifstream file(std::string(KR32_SOURCE_DIR) + "/docs/isa-v1.md");
    std::string line;
    while (std::getline(file, line)) {
        const std::string marker = "**Status:**";
        const std::size_t at = line.find(marker);
        if (at == std::string::npos) {
            continue;
        }
        std::istringstream rest(line.substr(at + marker.size()));
        std::string word;
        while (rest >> word) {
            const std::string prefix = "v";
            if (word.rfind(prefix, 0) == 0 && word.size() > 1) {
                std::string version = word.substr(prefix.size());
                // Drop the punctuation a status sentence puts after the number.
                while (!version.empty()) {
                    const unsigned char last =
                        static_cast<unsigned char>(version.back());
                    if (std::isalnum(last) != 0 || version.back() == '.' ||
                        version.back() == '-') {
                        break;
                    }
                    version.pop_back();
                }
                return version;
            }
        }
    }
    return {};
}

} // namespace

TEST_CASE("version tuple") {
    CHECK(std::strcmp(kr32::kVersion, "0.1.0") == 0);
    CHECK(std::strcmp(kr32::kIsaVersion, "1") == 0);
    CHECK(std::strcmp(kr32::kContainerVersion, "1") == 0);
}

TEST_CASE("the ISA version matches the document") {
    const std::string documented = documented_isa_version();
    INFO(documented);
    REQUIRE_FALSE(documented.empty());
    CHECK(documented == kr32::kIsaVersion);
}