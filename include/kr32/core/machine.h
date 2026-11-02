#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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
inline constexpr std::uint32_t kVectorPageFault = 4;
inline constexpr std::uint32_t kVectorGeneralProtection = 5;
inline constexpr std::uint32_t kVectorAlignment = 6;
inline constexpr std::uint32_t kVectorDoubleFault = 7;

inline constexpr std::uint32_t kPageSize = 0x1000;
inline constexpr std::uint32_t kPageOffsetMask = 0x0FFF;
inline constexpr std::uint32_t kPageFrameMask = 0xFFFF'F000;

inline constexpr std::uint32_t kPteValid = 1U << 0;
inline constexpr std::uint32_t kPteWritable = 1U << 1;
inline constexpr std::uint32_t kPteUser = 1U << 2;
inline constexpr std::uint32_t kPteExecutable = 1U << 3;
inline constexpr std::uint32_t kPteAccessed = 1U << 4;
inline constexpr std::uint32_t kPteDirty = 1U << 5;
inline constexpr std::uint32_t kPteGlobal = 1U << 6;
inline constexpr std::uint32_t kPtePageSize = 1U << 7;

inline constexpr std::uint32_t kMctlPaging = 1U << 0;
inline constexpr std::uint32_t kMctlWriteProtect = 1U << 1;

enum class FaultKind {
    None,
    InvalidOpcode,
    DivideByZero,
    Breakpoint,
    PageFault,
    GeneralProtection,
    Alignment,
    DoubleFault,
    SoftwareInterrupt,
    Limit,
};

class DeviceBus;

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
        FaultDelivered,
    };

    Kind kind = Kind::Retired;
    Fault fault{};
};

class Machine {
public:
    struct Options {
        std::uint32_t ram_size = kDefaultRamSize;
        std::uint32_t core_id = 0;
        bool exceptions_enabled = true;
        std::shared_ptr<std::vector<std::uint8_t>> shared_ram;
        std::function<bool(Machine&, std::uint32_t)> syscall_handler;

        Options() = default;
        Options(std::uint32_t ram, std::uint32_t core = 0, bool exceptions = true)
            : ram_size(ram), core_id(core), exceptions_enabled(exceptions) {}
    };

    Machine();
    explicit Machine(Options options);

    // Boot reset: PC comes from the first ROM word, SP/KSP are top of RAM.
    void reset();

    // Explicit reset used by raw-image tests and the CLI.
    void reset(std::uint32_t pc, std::uint32_t sp = 0);

    [[nodiscard]] bool load_image(std::uint32_t address,
                                  const std::uint8_t* data,
                                  std::size_t size);
    [[nodiscard]] bool load_image(std::uint32_t address,
                                  const std::vector<std::uint8_t>& data);
    void attach_devices(DeviceBus* devices, bool tick_devices = true);
    [[nodiscard]] DeviceBus* devices() const { return devices_; }
    void invalidate_reservation();
    [[nodiscard]] std::uint64_t write_generation() const { return write_generation_; }

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

    [[nodiscard]] bool paging_enabled() const { return (mctl_ & kMctlPaging) != 0; }
    [[nodiscard]] bool kernel_mode() const { return flag(kFlagK); }
    [[nodiscard]] std::uint32_t core_id() const { return core_id_; }

    using SyscallHandler = std::function<bool(Machine&, std::uint32_t)>;
    void set_syscall_handler(SyscallHandler handler);
    void halt(std::uint32_t exit_code);
    [[nodiscard]] bool exit_code_set() const { return exit_code_set_; }
    [[nodiscard]] std::uint32_t exit_code() const { return exit_code_; }

    [[nodiscard]] std::uint64_t fpr(std::size_t index) const;
    void set_fpr(std::size_t index, std::uint64_t value);
    [[nodiscard]] float tile_value(std::size_t tile, std::size_t index) const;
    void set_tile_value(std::size_t tile, std::size_t index, float value);
    [[nodiscard]] std::uint32_t tsp() const { return tsp_; }
    void set_tsp(std::uint32_t value) { tsp_ = value; }

    // Inter-processor block hook: set a pending hardware interrupt line.
    void raise_interrupt(std::uint32_t line);

    [[nodiscard]] std::uint32_t ram_base() const { return kRamBase; }
    [[nodiscard]] std::uint32_t ram_size() const {
        return static_cast<std::uint32_t>(ram_->size());
    }
    [[nodiscard]] std::uint32_t stack_top() const { return (kRamBase + ram_size()) & ~15U; }

    // Virtual accesses use the current mode and page tables.
    [[nodiscard]] bool read8(std::uint32_t address, std::uint8_t& value) const;
    [[nodiscard]] bool write8(std::uint32_t address, std::uint8_t value);
    [[nodiscard]] bool read16(std::uint32_t address, std::uint16_t& value) const;
    [[nodiscard]] bool write16(std::uint32_t address, std::uint16_t value);
    [[nodiscard]] bool read32(std::uint32_t address, std::uint32_t& value) const;
    [[nodiscard]] bool write32(std::uint32_t address, std::uint32_t value);

    // Physical accesses bypass translation; used for page-table setup/debug.
    [[nodiscard]] bool read_physical8(std::uint32_t address, std::uint8_t& value) const;
    [[nodiscard]] bool write_physical8(std::uint32_t address, std::uint8_t value);
    [[nodiscard]] bool read_physical16(std::uint32_t address, std::uint16_t& value) const;
    [[nodiscard]] bool write_physical16(std::uint32_t address, std::uint16_t value);
    [[nodiscard]] bool read_physical32(std::uint32_t address, std::uint32_t& value) const;
    [[nodiscard]] bool write_physical32(std::uint32_t address, std::uint32_t value);

    [[nodiscard]] bool read_control_register(std::uint32_t number, std::uint32_t& value) const;
    [[nodiscard]] bool write_control_register(std::uint32_t number, std::uint32_t value);

private:
    enum class Access {
        Fetch,
        Load,
        Store,
    };

    [[nodiscard]] StepResult execute(const isa::Instruction& instruction,
                                     std::uint32_t instruction_pc,
                                     std::uint32_t next_pc);
    [[nodiscard]] StepResult execute_fp(const isa::Instruction& instruction,
                                        std::uint32_t instruction_pc,
                                        std::uint32_t next_pc);
    [[nodiscard]] StepResult execute_matrix(const isa::Instruction& instruction,
                                            std::uint32_t instruction_pc,
                                            std::uint32_t next_pc);
    [[nodiscard]] StepResult raise(FaultKind kind,
                                   std::uint32_t vector,
                                   std::uint32_t address,
                                   std::uint32_t code = 0);
    [[nodiscard]] StepResult handle_fault(Fault fault, std::uint32_t victim_pc);
    [[nodiscard]] bool deliver(std::uint32_t vector,
                               std::uint32_t code,
                               std::uint32_t victim_pc);
    [[nodiscard]] bool translate(std::uint32_t virtual_address,
                                 Access access,
                                 std::uint32_t& physical_address,
                                 Fault& fault);
    [[nodiscard]] bool read_virtual8(std::uint32_t address,
                                     Access access,
                                     std::uint8_t& value,
                                     Fault& fault);
    [[nodiscard]] bool read_virtual16(std::uint32_t address,
                                      Access access,
                                      std::uint16_t& value,
                                      Fault& fault);
    [[nodiscard]] bool read_virtual32(std::uint32_t address,
                                      Access access,
                                      std::uint32_t& value,
                                      Fault& fault);
    [[nodiscard]] bool write_virtual8(std::uint32_t address,
                                      std::uint8_t value,
                                      Fault& fault);
    [[nodiscard]] bool write_virtual16(std::uint32_t address,
                                       std::uint16_t value,
                                       Fault& fault);
    [[nodiscard]] bool write_virtual32(std::uint32_t address,
                                       std::uint32_t value,
                                       Fault& fault);
    [[nodiscard]] bool push32(std::uint32_t value);
    [[nodiscard]] bool pop32(std::uint32_t& value);

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
    [[nodiscard]] Fault memory_fault(FaultKind kind,
                                     std::uint32_t vector,
                                     std::uint32_t address,
                                     std::uint32_t code) const;

    std::shared_ptr<std::vector<std::uint8_t>> ram_;
    std::vector<std::uint8_t> rom_;
    std::uint32_t regs_[16] = {};
    std::uint32_t pc_ = kRomBase;
    std::uint32_t sp_ = 0;
    std::uint32_t flags_ = kFlagK;
    std::uint64_t ticks_ = 0;
    bool halted_ = false;
    Fault fault_{};

    std::uint32_t vtbr_ = 0;
    std::uint32_t esr_ = 0;
    std::uint32_t ksp_ = 0;
    std::uint32_t ptbr_ = 0;
    std::uint32_t far_ = 0;
    std::uint32_t mctl_ = 0;
    std::uint32_t imr_ = 0;
    std::uint32_t irq_ = 0;
    std::uint32_t core_id_ = 0;
    std::uint32_t tsp_ = 0;
    bool exceptions_enabled_ = true;
    SyscallHandler syscall_handler_;
    DeviceBus* devices_ = nullptr;
    bool tick_devices_ = true;
    std::uint64_t write_generation_ = 0;
    bool exit_code_set_ = false;
    std::uint32_t exit_code_ = 0;

    std::uint64_t fpr_[16] = {};
    float tiles_[16][16] = {};

    bool reservation_valid_ = false;
    std::uint32_t reservation_address_ = 0;
};

} // namespace kr32::core
