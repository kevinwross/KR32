#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "kr32/isa/encoding.h"

namespace kr32::core {

inline constexpr std::uint32_t kGuardBase = 0x0000'0000;
inline constexpr std::uint32_t kGuardSize = 0x0001'0000;
inline constexpr std::uint32_t kRamBase = 0x0001'0000;
inline constexpr std::uint32_t kRomBase = 0xFFFF'0000;
inline constexpr std::uint32_t kRomSize = 0x0001'0000;
inline constexpr std::uint32_t kDefaultRamSize = 128U * 1024U * 1024U;

inline constexpr std::uint32_t kFlagZ = 1U << 0;
inline constexpr std::uint32_t kFlagN = 1U << 1;
inline constexpr std::uint32_t kFlagC = 1U << 2;
inline constexpr std::uint32_t kFlagV = 1U << 3;
inline constexpr std::uint32_t kFlagI = 1U << 4;
inline constexpr std::uint32_t kFlagK = 1U << 5;

inline constexpr std::uint32_t kVectorInvalidOpcode = 1;
inline constexpr std::uint32_t kVectorDivideByZero = 2;
inline constexpr std::uint32_t kVectorBreakpoint = 3;
inline constexpr std::uint32_t kVectorGeneralProtection = 5;
inline constexpr std::uint32_t kVectorAlignment = 6;

enum class FaultKind {
    None,
    InvalidOpcode,
    DivideByZero,
    Breakpoint,
    Alignment,
    GeneralProtection,
    Limit,
};

struct Fault {
    FaultKind kind = FaultKind::None;
    std::uint32_t vector = 0;
    std::uint32_t address = 0;
    std::uint32_t code = 0;
};

struct StepResult {
    enum class Kind {
        Retired,
        Halted,
        Raised,
    };

    Kind kind = Kind::Retired;
    Fault fault{};
};

class Machine {
public:
    struct Options {
        std::uint32_t ram_size = kDefaultRamSize;
    };

    Machine();
    explicit Machine(Options options);

    void reset(std::uint32_t pc = kRamBase, std::uint32_t sp = 0);

    [[nodiscard]] bool load_image(std::uint32_t address,
                                  const std::uint8_t* data,
                                  std::size_t size);
    [[nodiscard]] bool load_image(std::uint32_t address,
                                  const std::vector<std::uint8_t>& data);

    [[nodiscard]] StepResult step();
    [[nodiscard]] StepResult run(std::uint64_t max_steps);

    [[nodiscard]] std::uint32_t reg(std::size_t index) const;
    void set_reg(std::size_t index, std::uint32_t value);

    [[nodiscard]] std::uint32_t pc() const { return pc_; }
    void set_pc(std::uint32_t value) { pc_ = value; }

    [[nodiscard]] std::uint32_t sp() const { return sp_; }
    void set_sp(std::uint32_t value) { sp_ = value; }

    [[nodiscard]] std::uint32_t flags() const { return flags_; }
    void set_flags(std::uint32_t value) { flags_ = value; }

    [[nodiscard]] std::uint64_t ticks() const { return ticks_; }
    [[nodiscard]] bool halted() const { return halted_; }
    [[nodiscard]] const Fault& fault() const { return fault_; }

    [[nodiscard]] std::uint32_t ram_base() const { return kRamBase; }
    [[nodiscard]] std::uint32_t ram_size() const { return static_cast<std::uint32_t>(ram_.size()); }
    [[nodiscard]] std::uint32_t stack_top() const { return (kRamBase + ram_size()) & ~3U; }

    [[nodiscard]] bool read8(std::uint32_t address, std::uint8_t& value) const;
    [[nodiscard]] bool write8(std::uint32_t address, std::uint8_t value);
    [[nodiscard]] bool read16(std::uint32_t address, std::uint16_t& value) const;
    [[nodiscard]] bool write16(std::uint32_t address, std::uint16_t value);
    [[nodiscard]] bool read32(std::uint32_t address, std::uint32_t& value) const;
    [[nodiscard]] bool write32(std::uint32_t address, std::uint32_t value);

private:
    [[nodiscard]] StepResult execute(const isa::Instruction& instruction,
                                     std::uint32_t instruction_pc,
                                     std::uint32_t next_pc);
    [[nodiscard]] StepResult raise(FaultKind kind,
                                   std::uint32_t vector,
                                   std::uint32_t address,
                                   std::uint32_t code = 0);
    [[nodiscard]] std::uint32_t payload(const isa::Instruction& instruction) const;
    [[nodiscard]] bool effective_address(const isa::Instruction& instruction,
                                         std::uint32_t instruction_pc,
                                         std::uint32_t& address) const;
    [[nodiscard]] bool indexed_address(const isa::Instruction& instruction,
                                       std::uint32_t& address) const;
    [[nodiscard]] bool condition_holds(std::uint32_t condition) const;
    [[nodiscard]] bool flag(std::uint32_t bit) const { return (flags_ & bit) != 0; }
    void set_flag(std::uint32_t bit, bool value);
    void set_zn(std::uint32_t result);
    void set_add_flags(std::uint32_t left, std::uint32_t right, std::uint32_t result);
    void set_sub_flags(std::uint32_t left, std::uint32_t right, std::uint32_t result);

    std::vector<std::uint8_t> ram_;
    std::vector<std::uint8_t> rom_;
    std::uint32_t regs_[16] = {};
    std::uint32_t pc_ = kRamBase;
    std::uint32_t sp_ = 0;
    std::uint32_t flags_ = kFlagK;
    std::uint64_t ticks_ = 0;
    bool halted_ = false;
    Fault fault_{};
};

} // namespace kr32::core
