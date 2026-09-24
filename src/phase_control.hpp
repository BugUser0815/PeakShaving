#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <cmath>
#include <stdexcept>

namespace phase_control {
inline uint32_t fullU32(const std::vector<uint16_t>& registers, uint16_t base, uint16_t address) {
    const size_t i = address - base;
    if (address < base || i + 1 >= registers.size())
        throw std::out_of_range("KSEM register range");
    return (uint32_t(registers[i]) << 16) | registers[i + 1];
}

constexpr double kMaxPhaseA = 20.0;
constexpr double kMaxDischargeW = 18000.0;

struct Reading {
    std::array<double, 3> signedCurrentA{};
    std::array<double, 3> voltageV{};
    double netImportW = 0.0;
};

struct Decision {
    double fakeImportW = 0.0;
    double fakeExportW = 0.0;
    double extraW = 0.0;
    double maxCurrentA = 0.0;
    double differenceA = 0.0;
    bool phaseActive = false;
};

// The three Sunny Islands receive one cumulative power request. The requested
// extra power is therefore translated into an equal contribution on each phase.
// This lowers a heavily imported phase but cannot remove the phase imbalance.
inline Decision calculate(const Reading& r, double peakW, double previousRequestW) {
    if (!std::isfinite(r.netImportW) || !std::isfinite(peakW) ||
        !std::isfinite(previousRequestW) || peakW <= 0.0 || previousRequestW < 0.0)
        throw std::invalid_argument("invalid phase control input");
    for (size_t i = 0; i < 3; ++i)
        if (!std::isfinite(r.signedCurrentA[i]) || !std::isfinite(r.voltageV[i]) ||
            r.voltageV[i] < 180.0 || r.voltageV[i] > 260.0 ||
            std::abs(r.signedCurrentA[i]) > 500.0)
            throw std::invalid_argument("invalid KSEM phase measurement");

    const double baseW = std::max(0.0, r.netImportW - peakW);
    // Estimate the current without the extra request from the preceding cycle.
    // This feed-forward term keeps the request steady when the SMA responds.
    std::array<double, 3> unassisted{};
    for (size_t i = 0; i < 3; ++i)
        unassisted[i] = r.signedCurrentA[i] + previousRequestW / (3.0 * r.voltageV[i]);
    const auto hi = std::max_element(unassisted.begin(), unassisted.end());
    Decision d;
    d.maxCurrentA = *hi;
    const auto [measuredLo, measuredHi] = std::minmax_element(r.signedCurrentA.begin(), r.signedCurrentA.end());
    d.differenceA = *measuredHi - *measuredLo; // diagnostics only
    d.phaseActive = d.maxCurrentA > kMaxPhaseA;
    double requestW = baseW;
    if (d.phaseActive) {
        const size_t hot = static_cast<size_t>(hi - unassisted.begin());
        const double phaseW = 3.0 * r.voltageV[hot] * (d.maxCurrentA - kMaxPhaseA);
        requestW = std::max(baseW, std::min({phaseW, kMaxDischargeW, std::max(0.0, r.netImportW + previousRequestW)}));
    }
    d.fakeImportW = requestW;
    d.extraW = std::max(0.0, requestW - baseW);
    // Do not request charging while a phase condition is active.
    d.fakeExportW = d.phaseActive ? 0.0 : std::max(0.0, peakW - r.netImportW);
    return d;
}
} // namespace phase_control
