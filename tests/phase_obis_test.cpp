#include "../src/phase_obis.hpp"
#include <ObisData.hpp>
#include <SpeedwireEmeterProtocol.hpp>
#include <cassert>
#include <cmath>

int main() {
    std::vector<uint16_t> block(26);
    auto set = [&](size_t i, uint32_t value) {
        block[i] = value >> 16;
        block[i + 1] = value & 0xffff;
    };
    set(0, 75000);   // 7.5 kW, beyond one 16-bit word at 0.1 W resolution
    set(20, 25000);  // 25 A
    set(22, 230000); // 230 V
    set(24, 900);    // power factor 0.9
    auto phase = phase_obis::decode(block, 40);
    assert(phase[0] == 7500 && phase[6] == 25 && phase[7] == 230);
    assert(std::abs(phase[8] - 0.9) < 0.0001);
    set(24, 0xfffffc18); // -1.0 as signed 32-bit power factor
    assert(phase_obis::decode(block, 40)[8] == -1.0);
    std::array<double, 35> packet{};
    phase_obis::apply(packet, {phase, phase, phase});
    assert(packet[0] == 7500 && packet[9] == 75000 && packet[18] == 75000);
    assert(packet[6] == 25 && packet[15] == 25 && packet[24] == 25);
    assert(packet[7] == 230 && packet[16] == 230 && packet[25] == 230);
    auto encoded = [](const libspeedwire::ObisData& type, double value) {
        libspeedwire::ObisData item(type);
        item.measurementValues.addMeasurement(value, 0);
        auto bytes = item.toByteArray();
        return libspeedwire::SpeedwireEmeterProtocol::getObisValue4(bytes.data());
    };
    assert(encoded(libspeedwire::ObisData::PositiveActivePowerL1, packet[0]) == 75000);
    assert(encoded(libspeedwire::ObisData::PositiveActivePowerL2, packet[9] / 10) == 75000);
    assert(encoded(libspeedwire::ObisData::CurrentL1, packet[6]) == 25000);
    assert(encoded(libspeedwire::ObisData::VoltageL1, packet[7]) == 230000);
    auto quiet = phase;
    quiet[0] = 0;
    auto virtualPacket = phase_obis::virtualPhases({phase, quiet, quiet}, 3000, 0);
    assert(virtualPacket[0][0] == 3000 && virtualPacket[1][0] == 0 && virtualPacket[2][0] == 0);
    assert(std::abs(virtualPacket[0][6] - 3000.0 / 230) < 0.001);
    phase_obis::apply(packet, virtualPacket);
    assert(encoded(libspeedwire::ObisData::PositiveActivePowerL1, packet[0]) == 30000);
    assert(encoded(libspeedwire::ObisData::PositiveActivePowerL2, packet[9] / 10) == 0);
}
