#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "phase_control.hpp"

namespace phase_obis {
// KSEM register groups start at 40, 80 and 120. All values are 32 bit;
// the power factor is the only signed value in this group.
inline std::array<double, 9> decode(const std::vector<uint16_t>& block, uint16_t base) {
    constexpr uint16_t offsets[9] = {0, 2, 4, 6, 16, 18, 20, 22, 24};
    std::array<double, 9> values{};
    for (size_t j = 0; j < values.size(); ++j) {
        const uint32_t raw = phase_control::fullU32(block, base, base + offsets[j]);
        if (j < 6) values[j] = raw / 10.0;       // W, var, VA
        else if (j < 8) values[j] = raw / 1000.0; // A, V
        else values[j] = static_cast<int32_t>(raw) / 1000.0;
    }
    return values;
}

// Adapt engineering units to the unchanged legacy sendSma field expressions.
// The voltage expression is selected separately in sendSma.
inline void apply(std::array<double, 35>& packet,
                  const std::array<std::array<double, 9>, 3>& phases) {
    for (size_t p = 0; p < 3; ++p) {
        for (size_t j = 0; j < 6; ++j)
            packet[p * 9 + j] = (p == 0 && j == 0) ? phases[p][j] : phases[p][j] * 10.0;
        packet[p * 9 + 6] = phases[p][6];
        packet[p * 9 + 7] = phases[p][7];
        packet[p * 9 + 8] = phases[p][8] * 1000.0;
    }
}

// Build an internally consistent virtual meter: keep the requested total
// power, but distribute it according to the KSEM's physical phase directions.
// This is a controlled experiment, not a documented Sunny Island control API.
inline std::array<std::array<double, 9>, 3> virtualPhases(
    const std::array<std::array<double, 9>, 3>& physical,
    double importW, double exportW) {
    std::array<std::array<double, 9>, 3> result{};
    std::array<double, 3> importWeights{}, exportWeights{};
    double sumImport = 0, sumExport = 0;
    for (size_t p = 0; p < 3; ++p) {
        const double net = physical[p][0] - physical[p][1];
        importWeights[p] = std::max(0.0, net);
        exportWeights[p] = std::max(0.0, -net);
        sumImport += importWeights[p];
        sumExport += exportWeights[p];
    }
    for (size_t p = 0; p < 3; ++p) {
        const double plus = importW * (sumImport > 0 ? importWeights[p] / sumImport : 1.0 / 3);
        const double minus = exportW * (sumExport > 0 ? exportWeights[p] / sumExport : 1.0 / 3);
        const double voltage = physical[p][7];
        if (voltage < 180 || voltage > 260) throw std::invalid_argument("invalid KSEM voltage");
        result[p] = {plus, minus, 0, 0, plus, minus,
                     (plus + minus) / voltage, voltage, plus + minus > 0 ? 1.0 : 0.0};
    }
    return result;
}
} // namespace phase_obis
