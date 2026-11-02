#pragma once

#include <string>
#include <string_view>

#include "kr32/asm/assembler.h"
#include "kr32/container/container.h"

namespace kr32::assembler {

// Assembles a source file into a relocatable .k32 object. The object assembler
// supports the section directives and the integer instruction subset needed by
// the M3 build pipeline; later milestones extend the same object model.
[[nodiscard]] bool assemble_object(std::string_view source,
                                   container::Image& image,
                                   Error& error);

} // namespace kr32::assembler
