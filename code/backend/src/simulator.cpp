#include "simulator.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <limits>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <cerrno>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

std::mt19937 make_rng(uint32_t seed, uint32_t stream) {
    std::seed_seq sequence{seed, stream};
    return std::mt19937(sequence);
}

uint32_t choose_seed(uint32_t configured) {
    if (configured != 0) return configured;
    const uint32_t sampled = std::random_device{}();
    return sampled == 0 ? 1u : sampled;
}

bool read_stdin_character(char& character) {
    // Do not block on std::cin: its stream lock can deadlock process shutdown
    // after a graceful stop while the controller still has stdin open.
#ifdef _WIN32
    DWORD count = 0;
    return ReadFile(GetStdHandle(STD_INPUT_HANDLE), &character, 1, &count, nullptr) && count == 1;
#else
    ssize_t count;
    do {
        count = ::read(STDIN_FILENO, &character, 1);
    } while (count < 0 && errno == EINTR);
    return count == 1;
#endif
}

struct StdinCommandBuffer {
    std::mutex mutex;
    std::queue<char> commands;
    std::once_flag reader_started;
    bool closed = false;
};

StdinCommandBuffer& get_stdin_command_buffer() {
    static auto* buffer = new StdinCommandBuffer();
    return *buffer;
}

void start_stdin_command_reader() {
    auto& buffer = get_stdin_command_buffer();
    std::call_once(buffer.reader_started, []() {
        std::thread([]() {
            char ch = '\0';
            while (read_stdin_character(ch)) {
                auto& shared = get_stdin_command_buffer();
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.commands.push(ch);
            }
            auto& shared = get_stdin_command_buffer();
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.closed = true;
        }).detach();
    });
}

bool exceeds_horizontal_distance_limit(const tsvra::Config& config, const tsvra::Request& req) {
    const uint32_t limit = config.get_max_horizontal_distance();
    return limit > 0 && req.get_horizontal_distance() > limit;
}

} // namespace

namespace tsvra {

Simulator::Simulator(const Config& config)
    : config_(config),
      actual_seed_(choose_seed(config.get_random_seed())),
      traffic_rng_(make_rng(actual_seed_, 0x54524146)),
      initial_failure_rng_(make_rng(actual_seed_, 0x494e4954)),
      runtime_failure_rng_(make_rng(actual_seed_, 0x52554e54)),
      failure_hotspot_rng_(make_rng(actual_seed_, 0x484f5453)),
      stats_(config),
      current_cycle_(0),
      json_stream_mode_(config.get_json_stream()),
      paused_(config.get_json_stream()),
      steps_remaining_(0),
      stop_requested_(false) {
    if (json_stream_mode_) {
        start_stdin_command_reader();
    }
}

// ========== JSON helper methods ==========

std::string Simulator::point_to_json(const Point& p) {
    std::ostringstream oss;
    oss << "{\"x\":" << p.x << ",\"y\":" << p.y << ",\"z\":" << p.z << "}";
    return oss.str();
}

std::string Simulator::path_to_json(const std::vector<Point>& path) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "[" << path[i].x << "," << path[i].y << "," << path[i].z << "]";
    }
    oss << "]";
    return oss.str();
}

std::string Simulator::escape_json_string(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c;
        }
    }
    return oss.str();
}

std::string Simulator::power_to_json() const {
    const auto& p = stats_.get_power();
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"elapsedCycles\":" << p.elapsed_cycles
        << ",\"elapsedSeconds\":" << p.elapsed_seconds
        << ",\"routerEvents\":" << p.router_events
        << ",\"horizontalHops\":" << p.horizontal_hops
        << ",\"verticalHops\":" << p.vertical_hops
        << ",\"redundantVerticalHops\":" << p.redundant_vertical_hops
        << ",\"routerStaticMw\":" << p.router_static_mw
        << ",\"routerDynamicMw\":" << p.router_dynamic_mw
        << ",\"horizontalMw\":" << p.horizontal_mw
        << ",\"verticalMw\":" << p.vertical_mw
        << ",\"totalMw\":" << p.total_mw
        << ",\"totalEnergyPj\":" << p.total_energy_pj
        << ",\"routerEnergyPj\":" << p.router_energy_pj
        << ",\"horizontalEnergyPj\":" << p.horizontal_energy_pj
        << ",\"verticalEnergyPj\":" << p.vertical_energy_pj
        << ",\"intervalTotalMw\":" << interval_total_mw_
        << ",\"energyPerCompletedFlitPj\":";
    if (stats_.get_completed_requests() == 0) out << "null";
    else out << p.total_energy_pj / static_cast<double>(stats_.get_completed_requests());
    out << '}';
    return out.str();
}

void Simulator::update_power_statistics() {
    const auto power = power_->snapshot(current_cycle_ + 1);
    const double seconds = power.elapsed_seconds - window_start_.elapsed_seconds;
    interval_total_mw_ = seconds > 0.0
        ? (power.total_energy_pj - window_start_.total_energy_pj) * 1.0e-9 / seconds : 0.0;
    stats_.update_power(power, actual_seed_, interval_total_mw_);
}

void Simulator::write_power_trace() {
    if (!power_trace_.is_open()) return;
    const auto& p = stats_.get_power();
    power_trace_ << std::setprecision(17)
        << p.elapsed_cycles << ',' << p.elapsed_seconds << ',' << p.router_events << ','
        << p.horizontal_hops << ',' << p.vertical_hops << ',' << p.redundant_vertical_hops << ','
        << p.router_static_mw << ',' << p.router_dynamic_mw << ',' << p.horizontal_mw << ','
        << p.vertical_mw << ',' << p.total_mw << ',' << interval_total_mw_ << ','
        << p.total_energy_pj << ',' << stats_.get_completed_requests() << ',';
    if (stats_.get_completed_requests() != 0) {
        power_trace_ << p.total_energy_pj / static_cast<double>(stats_.get_completed_requests());
    }
    power_trace_ << '\n';
    if (!power_trace_) throw std::runtime_error("Cannot write power trace: " + config_.get_output_prefix());
}

void Simulator::export_configuration(const std::string& filename) const {
    std::ofstream output(filename);
    if (!output) throw std::runtime_error("Cannot create configuration output: " + filename);
    output << "{\"simulation\":" << config_.to_json()
           << ",\"actual_seed\":" << actual_seed_
           << ",\"power_parameters\":" << power_->parameters().to_json()
           << ",\"actual_hotspot_centers\":[";
    const auto& centers = grid_->get_hotspot_centers();
    for (size_t i = 0; i < centers.size(); ++i) {
        if (i) output << ',';
        output << point_to_json(centers[i]);
    }
    output << "]}\n";
}

void Simulator::reset_cycle_accumulators() {
    cycle_new_requests_.clear();
    cycle_completed_.clear();
    cycle_failed_.clear();
    cycle_new_failures_.clear();
    cycle_reroutes_.clear();
}

void Simulator::poll_stdin_commands() {
    start_stdin_command_reader();

    std::queue<char> pending_commands;
    {
        auto& buffer = get_stdin_command_buffer();
        std::lock_guard<std::mutex> lock(buffer.mutex);
        std::swap(pending_commands, buffer.commands);
        // A closed stdin means there is no interactive controller. JSON can
        // then run as an ordinary pipe; web stdin remains open for credits.
        if (buffer.closed && pending_commands.empty() && steps_remaining_ == 0) paused_ = false;
    }

    while (!pending_commands.empty()) {
        const char ch = pending_commands.front();
        pending_commands.pop();
        if (ch == '\n' || ch == '\r') continue;

        switch (ch) {
            case 'p':
                paused_ = true;
                steps_remaining_ = 0;
                break;
            case 'r':
                steps_remaining_ = 0;
                paused_ = false;
                break;
            case 's':
                stop_requested_ = true;
                paused_ = false;
                steps_remaining_ = 0;
                break;
            case 'n':
                if (steps_remaining_ < std::numeric_limits<int>::max()) ++steps_remaining_;
                paused_ = false;
                break;
            default:
                break;
        }
    }
}

// ========== JSON emission methods ==========

void Simulator::emit_init_json() {
    std::ostringstream oss;
    oss << std::setprecision(17);
    oss << "{\"type\":\"init\",\"config\":{";
    oss << "\"gridSize\":" << config_.get_grid_size();
    oss << ",\"numLayers\":" << config_.get_num_layers();
    oss << ",\"gridFactor\":" << config_.get_grid_factor();

    // failure mode string
    std::string fm;
    switch (config_.get_failure_mode()) {
        case FailureMode::INITIAL_ONLY: fm = "a"; break;
        case FailureMode::RUNTIME_ONLY: fm = "b"; break;
        case FailureMode::COMBINED: fm = "c"; break;
    }
    oss << ",\"failureMode\":\"" << fm << "\"";
    oss << ",\"failureRate\":" << config_.get_failure_rate();
    oss << ",\"initialFailureRate\":" << config_.get_initial_failure_rate();
    oss << ",\"runtimeFailureRate\":" << config_.get_runtime_failure_rate();
    oss << ",\"verticalDelay\":" << config_.get_vertical_delay();
    oss << ",\"horizontalDelay\":" << config_.get_horizontal_delay();
    oss << ",\"routerDelay\":" << config_.get_router_delay();
    oss << ",\"requestProbability\":" << config_.get_request_probability();
    oss << ",\"actualSeed\":" << actual_seed_;
    oss << ",\"totalCycles\":" << config_.get_simulation_cycles();

    // Redundancy layout info
    std::string rl;
    switch (config_.get_redundancy_layout()) {
        case RedundancyLayout::LEGACY_CORNER4: rl = "corner4"; break;
        case RedundancyLayout::SHARED_SPARE:   rl = "shared"; break;
        case RedundancyLayout::NONE:           rl = "none"; break;
    }
    oss << ",\"redundancy\":{\"layout\":\"" << rl << "\"";
    oss << ",\"sparesPerLayer\":" << grid_->get_total_redundant_tsvs() / std::max(1u, grid_->get_num_layers() - 1);
    oss << ",\"redundancyRatio\":" << (grid_->get_total_tsvs() > 0 ? static_cast<double>(grid_->get_total_redundant_tsvs()) / grid_->get_total_tsvs() : 0.0);
    oss << "}";

    // Failure model info
    std::string fmodel;
    switch (config_.get_failure_model()) {
        case FailureModel::UNIFORM:   fmodel = "uniform"; break;
        case FailureModel::HOTSPOT: fmodel = "hotspot"; break;
    }
    oss << ",\"failureModel\":{\"type\":\"" << fmodel << "\"";
    const auto& hotspot = config_.get_hotspot_params();
    oss << ",\"hotspotCount\":" << hotspot.count;
    oss << ",\"hotspotRadius\":" << hotspot.radius;
    oss << ",\"hotspotMultiplier\":" << hotspot.multiplier;
    oss << ",\"centers\":[";
    const auto& centers = grid_->get_hotspot_centers();
    for (size_t i = 0; i < centers.size(); ++i) {
        if (i) oss << ',';
        oss << point_to_json(centers[i]);
    }
    oss << ']';
    oss << "}";
    oss << "}"; // config

    const auto& params = power_->parameters();
    oss << ",\"powerModel\":{\"technologyNm\":45,\"technology\":\"" << escape_json_string(params.technology)
        << "\",\"voltageV\":" << params.voltage_v
        << ",\"frequencyHz\":" << params.frequency_hz
        << ",\"flitBits\":" << params.flit_bits
        << ",\"toggleProbability\":" << params.total_toggle_probability
        << ",\"calibrationStatus\":\"" << escape_json_string(params.calibration_status)
        << "\",\"parameterSource\":\"" << escape_json_string(params.router_source) << "\"}";
    oss << ",\"powerParameters\":" << params.to_json();

    oss << ",\"failureHotspots\":[";
    for (size_t i = 0; i < centers.size(); ++i) {
        if (i) oss << ',';
        oss << point_to_json(centers[i]);
    }
    oss << "]";
    oss << ",\"failureProbabilities\":[";
    bool first_probability = true;
    for (uint32_t z = 0; z + 1 < grid_->get_num_layers(); ++z) {
        for (uint32_t y = 0; y < grid_->get_grid_size(); ++y) {
            for (uint32_t x = 0; x < grid_->get_grid_size(); ++x) {
                if (!first_probability) oss << ',';
                first_probability = false;
                oss << "{\"x\":" << x << ",\"y\":" << y << ",\"z\":" << z
                    << ",\"initial\":" << grid_->get_failure_probability(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z), false)
                    << ",\"runtime\":" << grid_->get_failure_probability(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z), true) << '}';
            }
        }
    }
    oss << ']';

    // Grid state
    oss << ",\"grid\":[";
    uint32_t gs = grid_->get_grid_size();
    uint32_t nl = grid_->get_num_layers();
    bool first_tsv = true;
    for (uint32_t z = 0; z < nl; ++z) {
        for (uint32_t y = 0; y < gs; ++y) {
            for (uint32_t x = 0; x < gs; ++x) {
                const TSV* tsv = grid_->get_tsv(
                    static_cast<int32_t>(x),
                    static_cast<int32_t>(y),
                    static_cast<int32_t>(z));
                if (!tsv) continue;
                if (!first_tsv) oss << ",";
                first_tsv = false;
                oss << "{\"x\":" << x << ",\"y\":" << y << ",\"z\":" << z
                    << ",\"redundant\":" << (tsv->is_redundant() ? "true" : "false")
                    << ",\"failed\":" << (tsv->is_failed() ? "true" : "false") << "}";
            }
        }
    }
    oss << "]";

    // Hotspot weights
    oss << ",\"hotspotWeights\":[";
    uint32_t nr = req_gen_->get_num_regions();
    for (uint32_t ry = 0; ry < nr; ++ry) {
        if (ry > 0) oss << ",";
        oss << "[";
        for (uint32_t rx = 0; rx < nr; ++rx) {
            if (rx > 0) oss << ",";
            oss << req_gen_->get_hotspot_weight(rx, ry);
        }
        oss << "]";
    }
    oss << "]";

    // Initial failures
    oss << ",\"initialFailures\":[";
    bool first_fail = true;
    for (uint32_t z = 0; z < nl; ++z) {
        for (uint32_t y = 0; y < gs; ++y) {
            for (uint32_t x = 0; x < gs; ++x) {
                const TSV* tsv = grid_->get_tsv(
                    static_cast<int32_t>(x),
                    static_cast<int32_t>(y),
                    static_cast<int32_t>(z));
                if (tsv && tsv->is_failed()) {
                    if (!first_fail) oss << ",";
                    first_fail = false;
                    oss << "{\"x\":" << x << ",\"y\":" << y << ",\"z\":" << z << "}";
                }
            }
        }
    }
    oss << "]";

    oss << "}";
    std::cout << oss.str() << std::endl;
}

void Simulator::emit_cycle_json() {
    std::ostringstream oss;
    oss << "{\"type\":\"cycle\",\"cycle\":" << current_cycle_;

    // updateVisuals flag (every 10 cycles)
    bool update_visuals = (current_cycle_ % 10 == 0);
    oss << ",\"updateVisuals\":" << (update_visuals ? "true" : "false");

    // events
    oss << ",\"events\":{";

    // newRequests
    oss << "\"newRequests\":[";
    for (size_t i = 0; i < cycle_new_requests_.size(); ++i) {
        if (i > 0) oss << ",";
        auto& r = cycle_new_requests_[i];
        oss << "{\"id\":" << r.id
            << ",\"sx\":" << r.sx << ",\"sy\":" << r.sy << ",\"sz\":" << r.sz
            << ",\"ex\":" << r.ex << ",\"ey\":" << r.ey << ",\"ez\":" << r.ez
            << ",\"time\":" << r.time
            << ",\"path\":" << path_to_json(r.path) << "}";
    }
    oss << "]";

    // completed
    oss << ",\"completed\":[";
    for (size_t i = 0; i < cycle_completed_.size(); ++i) {
        if (i > 0) oss << ",";
        auto& c = cycle_completed_[i];
        oss << "{\"id\":" << c.id << ",\"time\":" << c.time << ",\"latency\":" << c.latency << "}";
    }
    oss << "]";

    // failed
    oss << ",\"failed\":[";
    for (size_t i = 0; i < cycle_failed_.size(); ++i) {
        if (i > 0) oss << ",";
        auto& f = cycle_failed_[i];
        oss << "{\"id\":" << f.id << ",\"time\":" << f.time
            << ",\"reason\":\"" << escape_json_string(f.reason) << "\"}";
    }
    oss << "]";

    // newFailures
    oss << ",\"newFailures\":[";
    for (size_t i = 0; i < cycle_new_failures_.size(); ++i) {
        if (i > 0) oss << ",";
        auto& nf = cycle_new_failures_[i];
        oss << "{\"x\":" << nf.x << ",\"y\":" << nf.y << ",\"z\":" << nf.z
            << ",\"cycle\":" << nf.cycle << "}";
    }
    oss << "]";

    // reroutes
    oss << ",\"reroutes\":[";
    for (size_t i = 0; i < cycle_reroutes_.size(); ++i) {
        if (i > 0) oss << ",";
        auto& rr = cycle_reroutes_[i];
        oss << "{\"requestId\":" << rr.request_id
            << ",\"oldPath\":" << path_to_json(rr.old_path)
            << ",\"newPath\":" << path_to_json(rr.new_path)
            << ",\"redundantUsed\":" << point_to_json(rr.redundant_used) << "}";
    }
    oss << "]";

    oss << "}"; // end events

    // stats
    oss << ",\"stats\":{";
    oss << "\"pending\":" << pending_requests_.size();
    oss << ",\"transmitting\":" << transmitting_requests_.size();
    oss << ",\"totalRequests\":" << stats_.get_total_requests();
    oss << ",\"completed\":" << stats_.get_completed_requests();
    oss << ",\"failed\":" << stats_.get_failed_requests();

    oss << ",\"failedTSVs\":" << stats_.get_current_failed_tsvs();
    oss << ",\"avgLatency\":" << std::fixed << std::setprecision(1) << stats_.get_average_latency();
    oss << ",\"redundantUsages\":" << stats_.get_redundant_tsv_usages();
    oss << ",\"power\":" << power_to_json();
    oss << "}";

    // keyEvent
    bool key_event = !cycle_new_failures_.empty() ||
                     !cycle_failed_.empty() ||
                     !cycle_reroutes_.empty();
    oss << ",\"keyEvent\":" << (key_event ? "true" : "false");

    oss << "}";
    std::cout << oss.str() << std::endl;
}

void Simulator::emit_done_json() {
    std::ostringstream oss;
    oss << "{\"type\":\"done\",\"summary\":{";
    oss << "\"totalRequests\":" << stats_.get_total_requests();
    oss << ",\"completed\":" << stats_.get_completed_requests();
    oss << ",\"failed\":" << stats_.get_failed_requests();

    double success_rate = 0.0;
    if (stats_.get_total_requests() > 0) {
        success_rate = static_cast<double>(stats_.get_completed_requests()) * 100.0 /
                       static_cast<double>(stats_.get_total_requests());
    }
    oss << ",\"successRate\":" << std::fixed << std::setprecision(2) << success_rate;
    oss << ",\"avgLatency\":" << std::fixed << std::setprecision(1) << stats_.get_average_latency();
    oss << ",\"maxLatency\":" << stats_.get_max_latency();
    uint64_t min_lat = stats_.get_min_latency();
    if (stats_.get_completed_requests() == 0) min_lat = 0;
    oss << ",\"minLatency\":" << min_lat;

    // Redundant TSV and failure statistics
    oss << ",\"redundantTSVUsages\":" << stats_.get_redundant_tsv_usages();
    oss << ",\"failedTSVs\":" << stats_.get_current_failed_tsvs();
    oss << ",\"failedRedundantTSVs\":" << stats_.get_current_failed_redundant_tsvs();

    // Bypass diagnostic counters
    oss << ",\"failedVerticalEncounters\":" << stats_.get_failed_vertical_encounters();
    oss << ",\"spareFound\":" << stats_.get_spare_found();
    oss << ",\"spareUnavailable\":" << stats_.get_spare_unavailable();

    // In-flight requests
    oss << ",\"inFlightAtHorizon\":" << stats_.get_in_flight_at_horizon();
    oss << ",\"inFlightAtEnd\":" << stats_.get_in_flight();

    // Terminal delivery rate (excluding in-flight requests)
    uint64_t terminal_total = stats_.get_completed_requests() + stats_.get_failed_requests();
    double terminal_rate = 0.0;
    if (terminal_total > 0) {
        terminal_rate = static_cast<double>(stats_.get_completed_requests()) * 100.0 /
                        static_cast<double>(terminal_total);
    }
    oss << ",\"terminalDeliveryRate\":" << std::fixed << std::setprecision(2) << terminal_rate;

    // Route failure rate
    double route_fail_rate = 0.0;
    if (stats_.get_total_requests() > 0) {
        route_fail_rate = static_cast<double>(stats_.get_failed_requests()) * 100.0 /
                          static_cast<double>(stats_.get_total_requests());
    }
    oss << ",\"routeFailureRate\":" << std::fixed << std::setprecision(2) << route_fail_rate;

    oss << ",\"actualSeed\":" << actual_seed_;
    oss << ",\"power\":" << power_to_json();
    oss << "}}";
    std::cout << oss.str() << std::endl;
}

// ========== Core simulation methods ==========

void Simulator::initialize() {
    if (json_stream_mode_) {
        std::cerr << "Initializing simulation environment...\n";
    } else {
        std::cout << "Initializing simulation environment...\n";
    }

    // Create grid
    grid_ = std::make_unique<Grid>(config_);
    grid_->initialize(initial_failure_rng_, runtime_failure_rng_, failure_hotspot_rng_);

    // Create router
    router_ = std::make_unique<Router>(*grid_, config_);

    // Create request generator
    req_gen_ = std::make_unique<RequestGenerator>(config_, traffic_rng_);
    req_gen_->initialize_hotspots();
    const uint64_t routers = static_cast<uint64_t>(grid_->get_grid_size()) * grid_->get_grid_size() * grid_->get_num_layers();
    power_ = std::make_unique<PowerModel>(PowerParameters::load_ini(config_.get_power_config()), routers);
    window_start_ = power_->snapshot(0);
    stats_.update_power(window_start_, actual_seed_, 0.0);
    if (!json_stream_mode_) {
        power_trace_.open(config_.get_output_prefix() + "_power.csv");
        if (!power_trace_) throw std::runtime_error("Cannot create power trace: " + config_.get_output_prefix());
        power_trace_ << "elapsed_cycles,elapsed_seconds,router_events,horizontal_hops,vertical_hops,redundant_vertical_hops,"
                     << "router_static_mw,router_dynamic_mw,horizontal_mw,vertical_mw,total_mw,interval_total_mw,total_energy_pj,completed_flits,energy_per_completed_flit_pj\n";
    }

    // Print initial state
    auto& out = json_stream_mode_ ? std::cerr : std::cout;
    out << "Grid size: " << grid_->get_grid_size() << "x" << grid_->get_grid_size()
        << " x " << grid_->get_num_layers() << " layers\n";
    out << "Total TSVs: " << grid_->get_total_tsvs() << "\n";
    out << "Equivalent hardware routers: " << routers << "; actual seed: " << actual_seed_ << "\n";
    out << "Redundant TSVs: " << grid_->get_total_redundant_tsvs() << "\n";

    // Update initial failure statistics
    stats_.update_failure_stats(*grid_);
    if (config_.get_failure_mode() == FailureMode::INITIAL_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) {
        out << "Initial failed TSVs: " << grid_->count_failed_tsvs() << "\n";
    }

    out << "Initialization complete\n\n";
}

void Simulator::run() {
    auto& out = json_stream_mode_ ? std::cerr : std::cout;
    out << "Starting simulation; generation horizon: "
        << config_.get_simulation_cycles() << " cycles\n";
    if (json_stream_mode_) emit_init_json();

    const uint64_t horizon = config_.get_simulation_cycles();
    // CLI and web use the same generation/drain loop and event order.
    while (current_cycle_ < horizon || !pending_requests_.empty() || !transmitting_requests_.empty()) {
        if (json_stream_mode_) {
            poll_stdin_commands();
            while (paused_ && !stop_requested_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                poll_stdin_commands();
            }
        }
        if (stop_requested_) break;

        reset_cycle_accumulators();
        simulate_cycle(current_cycle_ < horizon);
        update_power_statistics();
        if (json_stream_mode_) {
            emit_cycle_json();
            if (steps_remaining_ > 0) {
                --steps_remaining_;
                paused_ = steps_remaining_ == 0;
            }
        } else if (current_cycle_ < horizon &&
                   ((current_cycle_ + 1) % 100 == 0 || current_cycle_ + 1 == horizon)) {
            print_progress();
        }
        if ((current_cycle_ + 1) % 100 == 0) {
            write_power_trace();
            window_start_ = stats_.get_power();
        }
        ++current_cycle_;
        if (current_cycle_ == horizon) {
            in_flight_at_horizon_ = pending_requests_.size() + transmitting_requests_.size();
            stats_.record_in_flight_at_horizon(in_flight_at_horizon_);
        }
    }

    stats_.update_failure_stats(*grid_);
    stats_.record_in_flight(pending_requests_.size() + transmitting_requests_.size());
    stats_.set_failed_vertical_encounters(grid_->get_failed_vertical_encounters());
    stats_.set_spare_found(grid_->get_spare_found());
    stats_.set_spare_unavailable(grid_->get_spare_unavailable());
    // Always retain a final trace sample, including runs shorter than a window.
    if (current_cycle_ % 100 != 0 || current_cycle_ == 0) write_power_trace();
    if (power_trace_.is_open()) power_trace_.close();
    if (json_stream_mode_) emit_done_json();
    else out << "Simulation complete (including drain).\n";
}

void Simulator::simulate_cycle(bool generate_requests) {
    if (generate_requests) generate_new_requests();
    update_transmitting_requests();
    if (config_.get_failure_mode() != FailureMode::INITIAL_ONLY) handle_runtime_failures();
    process_pending_requests();
}

void Simulator::generate_new_requests() {
    auto req_opt = req_gen_->generate_request(current_cycle_);
    if (req_opt.has_value()) {
        auto& req = req_opt.value();
        pending_requests_.push(req);
        stats_.record_request_generated();

        // Track for JSON streaming - path will be filled when routed
        if (json_stream_mode_) {
            JsonNewRequest jnr;
            jnr.id = req.id;
            jnr.sx = req.start.x;
            jnr.sy = req.start.y;
            jnr.sz = req.start.z;
            jnr.ex = req.end.x;
            jnr.ey = req.end.y;
            jnr.ez = req.end.z;
            jnr.time = req.generate_time;
            // path will be empty until routed; we update it in process_pending_requests
            cycle_new_requests_.push_back(std::move(jnr));
        }
    }
}

void Simulator::handle_runtime_failures() {
    const auto newly_failed = grid_->apply_runtime_failure(current_cycle_);
    if (newly_failed.empty()) return;
    stats_.update_failure_stats(*grid_);
    if (json_stream_mode_) {
        for (const auto& point : newly_failed) {
            cycle_new_failures_.push_back({point.x, point.y, point.z, current_cycle_});
        }
    }
}

void Simulator::process_pending_requests() {
    // Process all requests in the queue
    size_t queue_size = pending_requests_.size();
    for (size_t i = 0; i < queue_size; ++i) {
        Request req = pending_requests_.front();
        pending_requests_.pop();

        if (route_request(req)) {
            // Route succeeded, add to transmit queue
            req.status = RequestStatus::TRANSMITTING;
            increment_path_usage(req.path);

            // Update JSON tracking with the path
            if (json_stream_mode_) {
                // Find the matching new request entry and fill in path
                for (auto& jnr : cycle_new_requests_) {
                    if (jnr.id == req.id && jnr.path.empty()) {
                        jnr.path = req.path;
                        break;
                    }
                }
            }

            transmitting_requests_.push_back(req);
        } else {
            // Route failed
            req.status = RequestStatus::FAILED;
            stats_.record_request_failed(req);

            if (json_stream_mode_) {
                JsonFailed jf;
                jf.id = req.id;
                jf.time = current_cycle_;
                jf.reason = "no_route";
                cycle_failed_.push_back(jf);
            }
        }
    }
}

bool Simulator::route_request(Request& req) {
    // Use router to compute path
    auto result = router_->route(req.start, req.end, current_cycle_, req.horizontal_distance, 1);

    if (!result.has_value()) {
        // Route failed
        return false;
    }

    auto [path, complete_time] = result.value();

    // Update request info
    req.path = path;
    req.current_hop = 0;
    req.next_hop_time = current_cycle_;
    req.complete_time = complete_time;

    req.hop_reserved = false;
    req.hop_launched = false;

    return true;
}

bool Simulator::reroute_request(Request& req) {
    const Point actual_pos = req.path[req.current_hop];
    const auto result = router_->route(actual_pos, req.end, current_cycle_, req.horizontal_distance, 1);
    if (!result) return false;

    auto [new_path, complete_time] = *result;
    const auto old_path = json_stream_mode_ ? req.path : std::vector<Point>{};
    std::vector<Point> updated_path(req.path.begin(), req.path.begin() +
                                    static_cast<std::ptrdiff_t>(req.current_hop + 1));
    updated_path.insert(updated_path.end(), new_path.begin() + 1, new_path.end());
    req.path = std::move(updated_path);
    req.next_hop_time = current_cycle_;
    req.complete_time = complete_time;
    req.hop_reserved = false;
    req.hop_launched = false;

    // Track reroute for JSON streaming
    if (json_stream_mode_) {
        JsonReroute jr;
        jr.request_id = req.id;
        jr.old_path = old_path;
        jr.new_path = req.path;
        // Find redundant TSV used in new path segment
        jr.redundant_used = Point(0, 0, 0);
        for (size_t i = req.current_hop; i < req.path.size(); ++i) {
            TSV* tsv = grid_->get_tsv(req.path[i].x, req.path[i].y, req.path[i].z);
            if (tsv && tsv->is_redundant()) {
                jr.redundant_used = req.path[i];
                break;
            }
        }
        cycle_reroutes_.push_back(std::move(jr));
    }

    return true;
}

void Simulator::record_launched_hop(Request& req) {
    const Point& from = req.path[req.current_hop];
    const Point& to = req.path[req.current_hop + 1];
    const bool vertical = from.z != to.z;
    const TSV* channel = vertical ? grid_->get_vertical_channel(from, to) : nullptr;
    const bool redundant = channel && channel->is_redundant();
    power_->record_hop(vertical, redundant);
    if (redundant) stats_.record_redundant_tsv_usage();
    req.hop_launched = true;
}

void Simulator::reserve_next_hop(Request& req, TSV* resource) {
    req.hop_start_time = resource ? std::max(current_cycle_, resource->get_available_at()) : current_cycle_;
    const uint64_t delay = router_->calculate_path_delay({req.path[req.current_hop], req.path[req.current_hop + 1]});
    if (delay > std::numeric_limits<uint64_t>::max() - req.hop_start_time) {
        throw std::overflow_error("Hop arrival exceeds the 64-bit simulation clock");
    }
    req.next_hop_time = req.hop_start_time + delay;
    req.hop_reserved = true;
    req.hop_launched = false;
    if (resource) resource->occupy_until(req.next_hop_time);
    if (req.hop_start_time == current_cycle_) record_launched_hop(req);
}

void Simulator::update_transmitting_requests() {
    auto it = transmitting_requests_.begin();
    while (it != transmitting_requests_.end()) {
        Request& req = *it;
        bool failed = false;
        std::string reason;

        if (req.current_hop + 1 < req.path.size()) {
            const Point from = req.path[req.current_hop];
            const Point to = req.path[req.current_hop + 1];
            TSV* resource = from.z != to.z ? grid_->get_vertical_channel(from, to)
                                          : grid_->get_tsv(to.x, to.y, to.z);
            // A failed vertical channel cancels a waiting/in-flight hop. Already
            // launched energy stays in the totals; rerouting starts at its source.
            if (from.z != to.z && (!resource || resource->is_failed())) {
                decrement_path_usage(req.path);
                if (reroute_request(req)) {
                    increment_path_usage(req.path);
                    ++it;
                    continue;
                }
                failed = true;
                reason = "reroute_failed";
            } else {
                if (!req.hop_reserved) {
                    reserve_next_hop(req, resource);
                }
                if (!req.hop_launched && current_cycle_ >= req.hop_start_time) record_launched_hop(req);
                if (current_cycle_ < req.next_hop_time) {
                    ++it;
                    continue;
                }
                req.horizontal_distance += static_cast<uint64_t>(std::abs(to.x - from.x) + std::abs(to.y - from.y));
                ++req.current_hop;
                req.hop_reserved = false;
                req.hop_launched = false;
                if (exceeds_horizontal_distance_limit(config_, req)) {
                    decrement_path_usage(req.path);
                    failed = true;
                    reason = "horizontal_distance_exceeded";
                }
            }
        }

        if (failed) {
            req.status = RequestStatus::FAILED;
            req.complete_time = current_cycle_;
            stats_.record_request_failed(req);
            if (json_stream_mode_) cycle_failed_.push_back({req.id, current_cycle_, reason});
            it = transmitting_requests_.erase(it);
        } else if (req.current_hop + 1 >= req.path.size()) {
            req.status = RequestStatus::COMPLETED;
            req.completed = true;
            req.complete_time = current_cycle_;
            decrement_path_usage(req.path);
            stats_.record_request_completed(req);
            if (json_stream_mode_) cycle_completed_.push_back({req.id, current_cycle_, req.get_latency()});
            it = transmitting_requests_.erase(it);
        } else {
            // Reserve and launch the next hop on this arrival cycle so each hop
            // contributes exactly its link delay plus router delay, with no extra gap.
            const Point from = req.path[req.current_hop];
            const Point to = req.path[req.current_hop + 1];
            TSV* resource = from.z != to.z ? grid_->get_vertical_channel(from, to)
                                          : grid_->get_tsv(to.x, to.y, to.z);
            if (from.z == to.z || (resource && !resource->is_failed())) {
                reserve_next_hop(req, resource);
            }
            ++it;
        }
    }
}

void Simulator::increment_path_usage(const std::vector<Point>& path) {
    for (const auto& point : path) {
        TSV* tsv = grid_->get_tsv(point.x, point.y, point.z);
        if (tsv) {
            tsv->increment_usage();
        }
    }
}

void Simulator::decrement_path_usage(const std::vector<Point>& path) {
    for (const auto& point : path) {
        TSV* tsv = grid_->get_tsv(point.x, point.y, point.z);
        if (tsv) {
            tsv->decrement_usage();
        }
    }
}

void Simulator::print_progress() const {
    double progress = static_cast<double>(current_cycle_ + 1) * 100.0 /
                     static_cast<double>(config_.get_simulation_cycles());

    std::cout << "Progress: " << std::fixed << std::setprecision(1) << progress << "% "
              << "(" << current_cycle_ + 1 << "/" << config_.get_simulation_cycles() << ") "
              << "Requests: " << stats_.get_total_requests()
              << " (Completed: " << stats_.get_completed_requests()
              << ", Failed: " << stats_.get_failed_requests() << ")\n"
              << std::flush;
}

} // namespace tsvra
