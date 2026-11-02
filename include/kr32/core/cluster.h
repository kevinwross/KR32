#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "kr32/core/machine.h"

namespace kr32::core {

// The M2 multicore foundation: independent cores with distinct COREID values
// and a minimal inter-processor interrupt hook. Shared memory and the full IPB
// mailbox arrive with the multicore milestone.
class Cluster {
public:
    Cluster(std::uint32_t count, Machine::Options options = {}) {
        cores_.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            options.core_id = index;
            cores_.push_back(std::make_unique<Machine>(options));
        }
    }

    [[nodiscard]] std::size_t size() const { return cores_.size(); }

    [[nodiscard]] Machine& core(std::size_t index) {
        return *cores_.at(index);
    }

    [[nodiscard]] const Machine& core(std::size_t index) const {
        return *cores_.at(index);
    }

    void raise_interrupt(std::size_t target, std::uint32_t line) {
        cores_.at(target)->raise_interrupt(line);
    }

private:
    std::vector<std::unique_ptr<Machine>> cores_;
};

} // namespace kr32::core
