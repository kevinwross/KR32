#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "kr32/core/devices.h"
#include "kr32/core/machine.h"

namespace kr32::core {

// Cooperative multicore cluster. All cores share one RAM image and one device
// bus; the scheduler advances one core turn at a time for reproducibility.
class Cluster {
public:
    Cluster(std::uint32_t count, Machine::Options options = {}) {
        std::uint32_t ram_size = options.ram_size == 0 ? kDefaultRamSize : options.ram_size;
        ram_size = std::min(ram_size, kRomBase - kRamBase);
        shared_ram_ = std::make_shared<std::vector<std::uint8_t>>(
            static_cast<std::size_t>(ram_size), std::uint8_t{0});
        options.shared_ram = shared_ram_;
        options.ram_size = ram_size;
        cores_.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            options.core_id = index;
            cores_.push_back(std::make_unique<Machine>(options));
        }
        devices_.set_irq_handler([this](std::uint32_t line) { route_interrupt(line); });
        for (std::size_t index = 0; index < cores_.size(); ++index) {
            cores_[index]->attach_devices(&devices_, false);
        }
        devices_.attach_machine(*cores_.front());
    }

    [[nodiscard]] std::size_t size() const { return cores_.size(); }

    [[nodiscard]] Machine& core(std::size_t index) {
        return *cores_.at(index);
    }

    [[nodiscard]] const Machine& core(std::size_t index) const {
        return *cores_.at(index);
    }

    [[nodiscard]] DeviceBus& devices() { return devices_; }
    [[nodiscard]] const DeviceBus& devices() const { return devices_; }

    void raise_interrupt(std::size_t target, std::uint32_t line) {
        cores_.at(target)->raise_interrupt(line);
    }

    void send_ipi(std::size_t target, std::uint32_t line) {
        raise_interrupt(target, line);
    }

    // Advance one core turn. Returns the scheduled core's step result.
    [[nodiscard]] StepResult step() {
        if (cores_.empty()) {
            return {};
        }
        Machine& current = *cores_[scheduler_index_ % cores_.size()];
        ++scheduler_index_;
        const std::uint64_t before = current.write_generation();
        StepResult result = current.step();
        if (current.write_generation() != before) {
            for (auto& core : cores_) {
                if (core.get() != &current) {
                    core->invalidate_reservation();
                }
            }
        }
        devices_.tick();
        return result;
    }

    [[nodiscard]] StepResult run(std::uint64_t max_steps) {
        StepResult result;
        for (std::uint64_t index = 0; index < max_steps; ++index) {
            result = step();
            if (result.kind == StepResult::Kind::Raised) {
                return result;
            }
        }
        return result;
    }

    void reset() {
        for (auto& core : cores_) {
            core->reset();
        }
        devices_.reset();
        scheduler_index_ = 0;
    }

private:
    void route_interrupt(std::uint32_t line) {
        const std::uint32_t mask = devices_.irq_router().route(line);
        for (std::size_t index = 0; index < cores_.size(); ++index) {
            if ((mask & (1U << index)) != 0) {
                cores_[index]->raise_interrupt(line);
            }
        }
    }

    std::shared_ptr<std::vector<std::uint8_t>> shared_ram_;
    std::vector<std::unique_ptr<Machine>> cores_;
    DeviceBus devices_;
    std::uint32_t scheduler_index_ = 0;
};

} // namespace kr32::core
