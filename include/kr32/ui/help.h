#pragma once

#include <string>
#include <string_view>

namespace kr32::ui {

[[nodiscard]] std::string instruction_help(std::string_view mnemonic);
[[nodiscard]] std::string addressing_help(std::string_view instruction,
                                          std::string_view mnemonic);

} // namespace kr32::ui
