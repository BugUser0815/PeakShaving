#include "../src/phase_control.hpp"
#include <cassert>
#include <cmath>
using phase_control::Reading;
int main() {
    // 7.5 kW in 0.1 W units needs both Modbus words.
    assert(phase_control::fullU32({1, 9464}, 40, 40) == 75000);
    Reading r{{15, 15, 15}, {230, 230, 230}, 13000};
    auto d = phase_control::calculate(r, 11000, 0);
    assert(d.fakeImportW == 2000 && d.extraW == 0 && !d.phaseActive);
    r = {{20, -5, 5}, {230, 230, 230}, 4600};
    d = phase_control::calculate(r, 11000, 0);
    assert(!d.phaseActive && d.differenceA == 25 && d.fakeImportW == 0);
    r = {{20.1, 5, 5}, {230, 230, 230}, 6923};
    d = phase_control::calculate(r, 11000, 0);
    assert(d.phaseActive && std::abs(d.fakeImportW - 69) < 0.01);
    r = {{31, 29, 29}, {230, 230, 230}, 20000};
    d = phase_control::calculate(r, 11000, 0);
    assert(d.phaseActive && d.fakeImportW == 9000); // existing 11 kW peak wins
    r = {{31, 10, 10}, {230, 230, 230}, 11730};
    d = phase_control::calculate(r, 11000, 0);
    assert(d.phaseActive && std::abs(d.fakeImportW - 7590) < 0.01);
    assert(d.fakeExportW == 0);
    // After 7.59 kW of balanced response, the imbalance persists. The
    // feed-forward term retains the request without an on/off oscillation.
    r.signedCurrentA = {{20, -1, -1}};
    r.netImportW = 4140;
    auto next = phase_control::calculate(r, 11000, d.fakeImportW);
    assert(next.phaseActive && std::abs(next.fakeImportW - 7590) < 0.01);
    r = {{35, 34, 33}, {230, 230, 230}, 18000};
    d = phase_control::calculate(r, 11000, 0);
    assert(d.phaseActive && d.fakeImportW == 10350); // 20 A phase target wins
    r = {{25, 2, 3}, {230, 230, 230}, 8000};
    d = phase_control::calculate(r, 11000, 0);
    assert(d.phaseActive && std::abs(d.fakeImportW - 3450) < 0.01);
    r.netImportW = 1000;
    d = phase_control::calculate(r, 11000, 0);
    assert(d.fakeImportW == 1000);
    r.netImportW = -1000;
    d = phase_control::calculate(r, 11000, 0);
    assert(d.fakeImportW == 0 && d.fakeExportW == 0);
    r = {{10, 10, 10}, {230, 230, 230}, 1000};
    d = phase_control::calculate(r, 11000, 0);
    assert(!d.phaseActive && d.fakeExportW == 10000);
    r.voltageV[0] = 0;
    try { phase_control::calculate(r, 11000, 0); assert(false); }
    catch (const std::invalid_argument&) {}
}
