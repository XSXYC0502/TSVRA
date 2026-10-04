#include "power_model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void close(double actual, double expected, const char* message) {
    if (std::abs(actual - expected) > 1.0e-10 * std::max(1.0, std::abs(expected))) {
        std::cerr << message << ": expected " << expected << ", got " << actual << '\n';
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void rejects(Callable callable, const char* message) {
    bool rejected = false;
    try { callable(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}

class TemporaryIni {
public:
    explicit TemporaryIni(const std::string& contents) {
        static uint64_t sequence = 0;
        path_ = std::filesystem::temp_directory_path() / ("tsvra-power-test-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(++sequence) + ".ini");
        std::ofstream file(path_);
        if (!file) throw std::runtime_error("Cannot create temporary power test input");
        file << contents;
    }
    ~TemporaryIni() { std::error_code error; std::filesystem::remove(path_, error); }
    std::string path() const { return path_.string(); }
private:
    std::filesystem::path path_;
};

void formulas_and_idle() {
    tsvra::PowerParameters parameters;
    close(parameters.vertical_hop_energy_pj(), 1.08, "64 TSV link energy is 1.08 pJ");
    close(parameters.horizontal_hop_energy_pj(), 2.4, "1000um wire link energy is 2.4 pJ");
    parameters.voltage_v = 0.5;
    close(parameters.vertical_hop_energy_pj(), 0.27, "Link energy follows V squared");
    parameters.total_toggle_probability = 1.0;
    close(parameters.vertical_hop_energy_pj(), 0.54, "Probability includes both transition directions");
    parameters.router_idle_mw = 0.25;
    tsvra::PowerModel model(parameters, 4);
    const auto empty = model.snapshot(0);
    close(empty.total_mw, 0.0, "Empty interval has zero average power");
    close(empty.total_energy_pj, 0.0, "No energy at time zero without events");
    const auto idle = model.snapshot(1000);
    close(idle.elapsed_seconds, 1.0e-6, "1GHz cycle mapping");
    close(idle.router_static_mw, 1.0, "Idle power scales with physical router count");
    close(idle.total_energy_pj, 1000.0, "Idle energy integrates over elapsed cycles");
    close(idle.router_dynamic_mw, 0.0, "Idle traffic has zero router event power");
    close(idle.horizontal_energy_pj, 0.0, "Idle traffic has zero link energy");
    close(idle.vertical_energy_pj, 0.0, "Idle traffic has zero TSV energy");
}

void event_accounting_and_intervals() {
    tsvra::PowerParameters parameters;
    parameters.router_idle_mw = 0.25;
    parameters.router_buffer_event_pj = 2.0;
    parameters.router_crossbar_event_pj = 3.0;
    parameters.router_control_event_pj = 1.0;
    parameters.redundant_vertical_capacitance_multiplier = 2.0;
    tsvra::PowerModel model(parameters, 4);
    model.record_hop(false);
    model.record_hop(true);
    model.record_hop(true, true);
    const auto first = model.snapshot(1000);
    check(first.router_events == 3 && first.horizontal_hops == 1 && first.vertical_hops == 2
        && first.redundant_vertical_hops == 1, "Exactly one router event per launched hop");
    close(first.router_dynamic_energy_pj, 18.0, "Component event energies sum once per hop");
    close(first.horizontal_energy_pj, 2.4, "Horizontal energy counts actual hops");
    close(first.vertical_energy_pj, 3.24, "Redundant hops included once in vertical energy");
    close(first.total_energy_pj, 1023.64, "Idle plus all actual events");
    close(first.total_mw, 1.02364, "pJ to mW average conversion");
    const auto repeat = model.snapshot(1000);
    close(repeat.total_energy_pj, first.total_energy_pj, "Snapshot does not reset counters");
    model.record_hop(false);
    const auto second = model.snapshot(2000);
    close(second.total_energy_pj - first.total_energy_pj, 1008.4,
        "Consecutive snapshots give interval idle plus new events");
    check(second.router_events - first.router_events == 1, "Interval counter delta");
    rejects([&]() { model.record_hop(false, true); }, "Reject redundant horizontal event");
    check(model.snapshot(2000).router_events == 4, "Rejected event leaves counters unchanged");
}

void editable_parameters_and_validation() {
    TemporaryIni valid("[metadata]\nparameter_version = custom-v2\ncalibration_status = provisional_custom\n"
        "[power]\nrouter_idle_mw = 0\nhorizontal_link_length_um = 2000 # selected geometry\n"
        "total_toggle_probability = 0.25\n");
    const auto loaded = tsvra::PowerParameters::load_ini(valid.path());
    close(loaded.horizontal_hop_energy_pj(), 2.4, "INI geometry and probability edits apply");
    check(loaded.parameter_version == "custom-v2", "INI parameter version preserved");
    check(loaded.to_json().find("\"calibration_status\":\"provisional_custom\"") != std::string::npos,
        "JSON provenance preserves calibration status");
    rejects([]() { tsvra::PowerModel invalid(tsvra::PowerParameters{}, 0); }, "Reject zero routers");
    for (const char* content : {"voltage_v = nan\n", "frequency_hz = 0\n",
         "router_idle_mw = -1\n", "flit_bits = 32\n", "physical_signal_tsvs = -64\n",
         "total_toggle_probability = 1.1\n", "horizontal_capacitance_ff_per_um = inf\n",
         "unknown_key = 1\n", "voltage_v = 1oops\n", "voltage_v=1\nvoltage_v=1\n"}) {
        TemporaryIni invalid(content);
        rejects([&]() { tsvra::PowerParameters::load_ini(invalid.path()); }, "Reject invalid power INI");
    }
    auto huge = loaded;
    huge.voltage_v = std::numeric_limits<double>::max();
    rejects([&]() { huge.validate(); }, "Reject derived energy overflow");
}

} // namespace

int main() {
    try {
        formulas_and_idle();
        event_accounting_and_intervals();
        editable_parameters_and_validation();
        std::cout << "Power formulas, idle integration, event accounting, intervals, and validation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Power test failed: " << error.what() << '\n';
        return 1;
    }
}
