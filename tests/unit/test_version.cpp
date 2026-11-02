#include "doctest.h"

#include "kr32/version.h"

#include <cstring>

TEST_CASE("version tuple") {
    CHECK(std::strcmp(kr32::kVersion, "0.1.0") == 0);
    CHECK(std::strcmp(kr32::kIsaVersion, "0.2-draft") == 0);
    CHECK(std::strcmp(kr32::kContainerVersion, "1") == 0);
}
