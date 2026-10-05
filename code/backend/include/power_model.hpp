#ifndef TSVRA_POWER_MODEL_HPP
#define TSVRA_POWER_MODEL_HPP

#include <cstdint>
#include <string>

namespace tsvra {

// Coefficients describe one equivalent router at each grid point. Event energy
// is per launched 64-bit flit hop; idle power includes the continuously clocked
// router and leakage. See docs/power-model.md for calibration provenance.
struct PowerParameters {
    std::string parameter_version = "tsvra-power-v1-dsent09-bulk45";
    std::string calibration_status = "dsent_bulk45_reference";
    std::string technology = "45nm_bulk_CMOS_Bulk45LVT_340K";
    std::string router_source = "https://github.com/mit-carbon/Graphite/tree/1b4bb98f5bfb96ca394b71e7ef3e9e500a1e3997/contrib/dsent/dsent-core";
    std::string link_source = "DSENT_NOCS2012_TableI_KimLim_SLIP2010_Table2";
    std::string calibration_source_revision = "1b4bb98f5bfb96ca394b71e7ef3e9e500a1e3997";
    std::string calibration_configuration = "6ports_64bit_8VC_2buffers_per_VC_16per_port_clockHTree5_340K_1V_1GHz";

    double frequency_hz = 1.0e9;
    double voltage_v = 1.0;
    uint32_t flit_bits = 64;
    uint32_t physical_signal_tsvs = 64;
    double total_toggle_probability = 0.5;

    double router_idle_mw = 16.455745700000001;
    double router_buffer_event_pj = 4.5489791666666672;
    double router_crossbar_event_pj = 1.2392827083333335;
    double router_control_event_pj = 0.53700153769841275;

    double horizontal_link_length_um = 1000.0;
    double horizontal_capacitance_ff_per_um = 0.150;
    double tsv_width_um = 2.0;
    double tsv_height_um = 20.0;
    double tsv_capacitance_ff = 67.5;
    double redundant_vertical_capacitance_multiplier = 1.0;

    static PowerParameters load_ini(const std::string& path);
    void validate() const;
    std::string to_json() const;
    double router_event_energy_pj() const;
    double horizontal_hop_energy_pj() const;
    double vertical_hop_energy_pj(bool redundant = false) const;
};

struct PowerSnapshot {
    uint64_t elapsed_cycles = 0;
    double elapsed_seconds = 0.0;
    uint64_t router_count = 0;
    uint64_t router_events = 0;
    uint64_t horizontal_hops = 0;
    uint64_t vertical_hops = 0;
    uint64_t redundant_vertical_hops = 0;
    double router_static_mw = 0.0;
    double router_dynamic_mw = 0.0;
    double horizontal_mw = 0.0;
    double vertical_mw = 0.0;
    double total_mw = 0.0;
    double total_energy_pj = 0.0;
    double router_energy_pj = 0.0;
    double router_static_energy_pj = 0.0;
    double router_dynamic_energy_pj = 0.0;
    double horizontal_energy_pj = 0.0;
    double vertical_energy_pj = 0.0;
};

class PowerModel {
public:
    PowerModel(PowerParameters parameters, uint64_t router_count);
    void record_hop(bool vertical, bool redundant = false);
    PowerSnapshot snapshot(uint64_t elapsed_cycles) const;
    const PowerParameters& parameters() const { return parameters_; }

private:
    PowerParameters parameters_;
    uint64_t router_count_;
    uint64_t router_events_ = 0;
    uint64_t horizontal_hops_ = 0;
    uint64_t vertical_hops_ = 0;
    uint64_t redundant_vertical_hops_ = 0;
};

} // namespace tsvra

#endif // TSVRA_POWER_MODEL_HPP
