#include "doctest.h"

#include "kr32/container/container.h"
#include "kr32/os/embedded_os.h"

#include <cstdint>

#if !defined(KR32_HAS_EMBEDDED_OS) || !KR32_HAS_EMBEDDED_OS
TEST_CASE("embedded KR32OS images are unavailable when disabled") {
    CHECK(!kr32::os::embedded_os_available());
}
#else
TEST_CASE("embedded KR32OS images are available") {
    const kr32::os::EmbeddedOs& images = kr32::os::embedded_os();
    CHECK(kr32::os::embedded_os_available());
    CHECK(images.bios.size() > 4);
    CHECK(images.kernel.size() > 4);
    CHECK(images.bios[0] == 'K');
    CHECK(images.bios[1] == '3');
    CHECK(images.bios[2] == '2');
    CHECK(images.bios[3] == 0);
    CHECK(images.kernel[0] == 'K');
    CHECK(images.kernel[1] == '3');
    CHECK(images.kernel[2] == '2');
    CHECK(images.kernel[3] == 0);
    CHECK(images.bios_crc ==
          kr32::container::crc32(images.bios.data(), images.bios.size()));
    CHECK(images.kernel_crc ==
          kr32::container::crc32(images.kernel.data(), images.kernel.size()));
    CHECK(!images.build_id.empty());
}
#endif
