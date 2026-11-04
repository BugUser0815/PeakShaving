#include "../src/phase_control.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < 0.000001, message);
}
}

int main() {
    try {
        // The rollover must not turn growing import into a charging request.
        for (uint16_t base : {40, 80, 120}) {
            for (uint32_t raw : {65535u, 65536u, 131072u, 0xffffffffu}) {
                const std::vector<uint16_t> registers{
                    uint16_t(raw >> 16), uint16_t(raw), 0, 0};
                const auto power = phase_control::readActivePower(registers, base);
                require(power.importRaw == raw, "import loses its high word");
                require(power.exportRaw == 0, "unexpected export");
                near(power.netImportW(), double(raw) / 10.0, "incorrect import units");
            }
            const auto exported = phase_control::readActivePower({0, 0, 1, 9464}, base);
            require(exported.exportRaw == 75000, "export loses its high word");
            near(exported.netImportW(), -7500.0, "export wraps as unsigned");
            const auto mixed = phase_control::readActivePower({1, 0, 2, 0}, base);
            near(mixed.netImportW(), -6553.6, "import/export net is incorrect");
        }

        // Synthetic uneven load: 7.5 + 5 + 5 kW, with L1 above 6553.5 W.
        const auto l1 = phase_control::readActivePower({1, 9464, 0, 0}, 40);
        const auto l2 = phase_control::readActivePower({0, 50000, 0, 0}, 80);
        const auto l3 = phase_control::readActivePower({0, 50000, 0, 0}, 120);
        const double netW = l1.netImportW() + l2.netImportW() + l3.netImportW();
        near(netW, 17500.0, "synthetic sample is truncated to 10946.4 W");
        phase_control::Reading reading{{32.6, 21.7, 21.7}, {230, 230, 230}, netW};
        const auto decision = phase_control::calculate(reading, 11000, 0);
        require(decision.fakeImportW >= netW - 11000, "peak discharge is missing");
        require(decision.fakeExportW == 0, "charging requested above peak target");

        reading.signedCurrentA = {0, 0, 0};
        reading.netImportW = -netW;
        const auto exportDecision = phase_control::calculate(reading, 11000, 0);
        require(exportDecision.fakeImportW == 0, "real export causes discharge");
        near(exportDecision.fakeExportW, 28500.0, "incorrect real-export charging request");

        bool rejected = false;
        try { phase_control::readActivePower({1, 9464, 0}, 40); }
        catch (const std::out_of_range&) { rejected = true; }
        require(rejected, "incomplete U32 pair was accepted");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
