#include "power_model.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace tsvra {
namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string json_string(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (const char c : text) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(static_cast<unsigned char>(c))
                        << std::dec << std::setfill(' ');
                } else {
                    out << c;
                }
        }
    }
    out << '"';
    return out.str();
}

const std::map<std::string, double PowerParameters::*> numeric_fields = {
    {"frequency_hz", &PowerParameters::frequency_hz},
    {"voltage_v", &PowerParameters::voltage_v},
    {"total_toggle_probability", &PowerParameters::total_toggle_probability},
    {"router_idle_mw", &PowerParameters::router_idle_mw},
    {"router_buffer_event_pj", &PowerParameters::router_buffer_event_pj},
    {"router_crossbar_event_pj", &PowerParameters::router_crossbar_event_pj},
    {"router_control_event_pj", &PowerParameters::router_control_event_pj},
    {"horizontal_link_length_um", &PowerParameters::horizontal_link_length_um},
    {"horizontal_capacitance_ff_per_um", &PowerParameters::horizontal_capacitance_ff_per_um},
    {"tsv_width_um", &PowerParameters::tsv_width_um},
    {"tsv_height_um", &PowerParameters::tsv_height_um},
    {"tsv_capacitance_ff", &PowerParameters::tsv_capacitance_ff},
    {"redundant_vertical_capacitance_multiplier", &PowerParameters::redundant_vertical_capacitance_multiplier},
};

const std::map<std::string, std::string PowerParameters::*> text_fields = {
    {"parameter_version", &PowerParameters::parameter_version},
    {"calibration_status", &PowerParameters::calibration_status},
    {"technology", &PowerParameters::technology},
    {"router_source", &PowerParameters::router_source},
    {"link_source", &PowerParameters::link_source},
    {"calibration_source_revision", &PowerParameters::calibration_source_revision},
    {"calibration_configuration", &PowerParameters::calibration_configuration},
};

void require_finite_nonnegative(double value, const std::string& field) {
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument("Power parameter '" + field + "' must be finite and nonnegative");
    }
}

double link_energy(double capacitance_ff, uint32_t bits, double voltage, double toggle_probability) {
    // fF * V^2 = fJ; 1 fJ = 0.001 pJ. Probability includes BOTH transition directions.
    return 0.5 * capacitance_ff * 1.0e-3 * voltage * voltage
        * static_cast<double>(bits) * toggle_probability;
}

} // namespace

PowerParameters PowerParameters::load_ini(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open power parameters: " + path);
    PowerParameters parameters;
    std::unordered_set<std::string> seen;
    std::string line;
    uint64_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        if (line == "[power]" || line == "[metadata]") continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) {
            throw std::invalid_argument(path + ":" + std::to_string(line_number) + ": expected key=value");
        }
        const std::string key = trim(line.substr(0, equal));
        const std::string value = trim(line.substr(equal + 1));
        if (!seen.insert(key).second) throw std::invalid_argument("Duplicate power parameter: " + key);
        try {
            if (const auto field = text_fields.find(key); field != text_fields.end()) {
                if (value.empty()) throw std::invalid_argument("empty metadata value");
                parameters.*(field->second) = value;
            } else if (const auto numeric = numeric_fields.find(key); numeric != numeric_fields.end()) {
                size_t parsed = 0;
                const double number = std::stod(value, &parsed);
                if (parsed != value.size()) throw std::invalid_argument("invalid numeric suffix");
                parameters.*(numeric->second) = number;
            } else if (key == "flit_bits" || key == "physical_signal_tsvs") {
                size_t parsed = 0;
                if (value.empty() || value.front() == '-') throw std::invalid_argument("invalid unsigned integer");
                const auto number = std::stoull(value, &parsed);
                if (parsed != value.size() || number > std::numeric_limits<uint32_t>::max()) {
                    throw std::invalid_argument("invalid unsigned integer");
                }
                if (key == "flit_bits") parameters.flit_bits = static_cast<uint32_t>(number);
                else parameters.physical_signal_tsvs = static_cast<uint32_t>(number);
            } else {
                throw std::invalid_argument("unknown power parameter");
            }
        } catch (const std::exception& error) {
            throw std::invalid_argument(path + ":" + std::to_string(line_number)
                + ": '" + key + "': " + error.what());
        }
    }
    if (file.bad()) throw std::runtime_error("Cannot read power parameters: " + path);
    parameters.validate();
    return parameters;
}

void PowerParameters::validate() const {
    for (const auto& [key, field] : numeric_fields) require_finite_nonnegative(this->*field, key);
    for (const auto& [key, field] : text_fields) {
        if ((this->*field).empty()) throw std::invalid_argument("Power metadata '" + key + "' is empty");
    }
    if (frequency_hz <= 0.0 || voltage_v <= 0.0) {
        throw std::invalid_argument("Power frequency_hz and voltage_v must be positive");
    }
    if (flit_bits != 64 || physical_signal_tsvs != 64) {
        throw std::invalid_argument("This architecture uses 64-bit flits and 64 physical signal TSVs per channel");
    }
    if (total_toggle_probability > 1.0) throw std::invalid_argument("Power total_toggle_probability must be in [0,1]");
    if (horizontal_link_length_um <= 0.0 || tsv_width_um <= 0.0 || tsv_height_um <= 0.0
        || redundant_vertical_capacitance_multiplier <= 0.0) {
        throw std::invalid_argument("Power link dimensions and redundant capacitance multiplier must be positive");
    }
    require_finite_nonnegative(router_event_energy_pj(), "router_event_energy_pj");
    require_finite_nonnegative(horizontal_hop_energy_pj(), "horizontal_hop_energy_pj");
    require_finite_nonnegative(vertical_hop_energy_pj(true), "vertical_hop_energy_pj");
}

std::string PowerParameters::to_json() const {
    validate();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << '{';
    bool first = true;
    const auto separator = [&]() {
        if (!first) out << ',';
        first = false;
    };
    for (const auto& [key, field] : text_fields) {
        separator();
        out << json_string(key) << ':' << json_string(this->*field);
    }
    for (const auto& [key, field] : numeric_fields) {
        separator();
        out << json_string(key) << ':' << this->*field;
    }
    out << ",\"flit_bits\":" << flit_bits
        << ",\"physical_signal_tsvs\":" << physical_signal_tsvs
        << ",\"router_event_energy_pj\":" << router_event_energy_pj()
        << ",\"horizontal_hop_energy_pj\":" << horizontal_hop_energy_pj()
        << ",\"vertical_hop_energy_pj\":" << vertical_hop_energy_pj()
        << ",\"redundant_vertical_hop_energy_pj\":" << vertical_hop_energy_pj(true) << '}';
    return out.str();
}

double PowerParameters::router_event_energy_pj() const {
    return router_buffer_event_pj + router_crossbar_event_pj + router_control_event_pj;
}

double PowerParameters::horizontal_hop_energy_pj() const {
    return link_energy(horizontal_link_length_um * horizontal_capacitance_ff_per_um,
        flit_bits, voltage_v, total_toggle_probability);
}

double PowerParameters::vertical_hop_energy_pj(bool redundant) const {
    return link_energy(tsv_capacitance_ff * (redundant ? redundant_vertical_capacitance_multiplier : 1.0),
        physical_signal_tsvs, voltage_v, total_toggle_probability);
}

PowerModel::PowerModel(PowerParameters parameters, uint64_t router_count)
    : parameters_(std::move(parameters)), router_count_(router_count) {
    parameters_.validate();
    if (router_count_ == 0) throw std::invalid_argument("Power model must contain at least one router");
}

void PowerModel::record_hop(bool vertical, bool redundant) {
    if (redundant && !vertical) throw std::invalid_argument("A redundant TSV hop must be vertical");
    if (router_events_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Power event counter overflow");
    ++router_events_;
    if (vertical) {
        ++vertical_hops_;
        if (redundant) ++redundant_vertical_hops_;
    } else {
        ++horizontal_hops_;
    }
}

PowerSnapshot PowerModel::snapshot(uint64_t elapsed_cycles) const {
    PowerSnapshot result;
    result.elapsed_cycles = elapsed_cycles;
    result.elapsed_seconds = static_cast<double>(elapsed_cycles) / parameters_.frequency_hz;
    result.router_count = router_count_;
    result.router_events = router_events_;
    result.horizontal_hops = horizontal_hops_;
    result.vertical_hops = vertical_hops_;
    result.redundant_vertical_hops = redundant_vertical_hops_;
    const double idle_mw = static_cast<double>(router_count_) * parameters_.router_idle_mw;
    result.router_static_energy_pj = idle_mw * result.elapsed_seconds * 1.0e9;
    result.router_dynamic_energy_pj = static_cast<double>(router_events_) * parameters_.router_event_energy_pj();
    result.router_energy_pj = result.router_static_energy_pj + result.router_dynamic_energy_pj;
    result.horizontal_energy_pj = static_cast<double>(horizontal_hops_) * parameters_.horizontal_hop_energy_pj();
    result.vertical_energy_pj = static_cast<double>(vertical_hops_ - redundant_vertical_hops_)
        * parameters_.vertical_hop_energy_pj()
        + static_cast<double>(redundant_vertical_hops_) * parameters_.vertical_hop_energy_pj(true);
    result.total_energy_pj = result.router_energy_pj + result.horizontal_energy_pj + result.vertical_energy_pj;
    if (!std::isfinite(result.total_energy_pj) || !std::isfinite(result.elapsed_seconds)) {
        throw std::overflow_error("Power snapshot exceeds finite numeric range");
    }
    if (elapsed_cycles > 0) {
        const double denominator = result.elapsed_seconds * 1.0e9;
        result.router_static_mw = idle_mw;
        result.router_dynamic_mw = result.router_dynamic_energy_pj / denominator;
        result.horizontal_mw = result.horizontal_energy_pj / denominator;
        result.vertical_mw = result.vertical_energy_pj / denominator;
        result.total_mw = idle_mw + result.router_dynamic_mw + result.horizontal_mw + result.vertical_mw;
        if (!std::isfinite(result.total_mw)) throw std::overflow_error("Power average exceeds finite numeric range");
    }
    return result;
}

} // namespace tsvra
