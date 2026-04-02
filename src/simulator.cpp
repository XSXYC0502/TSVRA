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

namespace {

struct StdinCommandBuffer {
    std::mutex mutex;
    std::queue<char> commands;
    std::once_flag reader_started;
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
            while (std::cin.get(ch)) {
                auto& shared = get_stdin_command_buffer();
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.commands.push(ch);
            }
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
      rng_(config.get_random_seed() == 0 ? std::random_device{}() : config.get_random_seed()),
      stats_(config),
      current_cycle_(0),
      json_stream_mode_(config.get_json_stream()),
      paused_(false),
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
                paused_ = false;
                break;
            case 's':
                stop_requested_ = true;
                paused_ = false;
                steps_remaining_ = 0;
                break;
            case 'n':
                steps_remaining_++;
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
    oss << ",\"verticalDelay\":" << config_.get_vertical_delay();
    oss << ",\"horizontalDelay\":" << config_.get_horizontal_delay();
    oss << ",\"totalCycles\":" << config_.get_simulation_cycles();

    // Redundancy layout info
    std::string rl;
    switch (config_.get_redundancy_layout()) {
        case RedundancyLayout::LEGACY_CORNER4: rl = "corner4"; break;
        case RedundancyLayout::SHARED_SPARE:   rl = "shared"; break;
        case RedundancyLayout::NONE:           rl = "none"; break;
    }
    oss << ",\"redundancy\":{\"layout\":\"" << rl << "\"";
    oss << ",\"sparesPerLayer\":" << grid_->get_total_redundant_tsvs() / std::max(1u, grid_->get_num_layers());
    oss << ",\"redundancyRatio\":" << (grid_->get_total_tsvs() > 0 ? static_cast<double>(grid_->get_total_redundant_tsvs()) / grid_->get_total_tsvs() : 0.0);
    oss << "}";

    // Failure model info
    std::string fmodel;
    switch (config_.get_failure_model()) {
        case FailureModel::UNIFORM:   fmodel = "uniform"; break;
        case FailureModel::CLUSTERED: fmodel = "clustered"; break;
    }
    oss << ",\"failureModel\":{\"type\":\"" << fmodel << "\"";
    oss << ",\"clusterStrength\":" << config_.get_cluster_params().strength;
    oss << ",\"clusterRadius\":" << config_.get_cluster_params().radius;
    oss << "}";

    oss << "}";

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

    stats_.update_failure_stats(*grid_);
    oss << ",\"failedTSVs\":" << stats_.get_current_failed_tsvs();
    oss << ",\"avgLatency\":" << std::fixed << std::setprecision(1) << stats_.get_average_latency();
    oss << ",\"redundantUsages\":" << stats_.get_redundant_tsv_usages();
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
    oss << ",\"inFlightAtHorizon\":" << stats_.get_in_flight();

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
    grid_->initialize(rng_);

    // Create router
    router_ = std::make_unique<Router>(*grid_, config_);

    // Create request generator
    req_gen_ = std::make_unique<RequestGenerator>(config_, rng_);
    req_gen_->initialize_hotspots();

    // Print initial state
    auto& out = json_stream_mode_ ? std::cerr : std::cout;
    out << "Grid size: " << grid_->get_grid_size() << "x" << grid_->get_grid_size()
        << " x " << grid_->get_num_layers() << " layers\n";
    out << "Total TSVs: " << grid_->get_total_tsvs() << "\n";
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
    if (json_stream_mode_) {
        // Emit init JSON and start streaming
        emit_init_json();
        reset_cycle_accumulators();

        std::cerr << "Starting simulation (JSON stream mode)...\n";
        std::cerr << "Simulation cycles: " << config_.get_simulation_cycles() << "\n\n";

        // Give the controller a brief window to deliver startup commands such as the
        // immediate pause used by the web UI lockstep mode.
        poll_stdin_commands();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        poll_stdin_commands();

        uint64_t total_cycles = config_.get_simulation_cycles();

        for (current_cycle_ = 0; current_cycle_ < total_cycles; ++current_cycle_) {
            // Check for stdin commands
            poll_stdin_commands();

            // Handle pause
            while (paused_ && !stop_requested_) {
                poll_stdin_commands();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            if (stop_requested_) {
                break;
            }

            simulate_cycle();

            emit_cycle_json();
            reset_cycle_accumulators();

            // Lockstep: decrement credit, pause when exhausted
            if (steps_remaining_ > 0) {
                steps_remaining_--;
            } else {
                paused_ = true;
            }
        }

        // Drain phase: after cycle limit, stop generating new requests, let in-flight complete
        if (!stop_requested_) {
            while (!pending_requests_.empty() || !transmitting_requests_.empty()) {
                poll_stdin_commands();
                while (paused_ && !stop_requested_) {
                    poll_stdin_commands();
#ifdef __linux__
                    usleep(1000);
#endif
                }
                if (stop_requested_) break;

                current_cycle_++;

                // No new requests, only advance existing transmissions
                update_transmitting_requests();
                if (config_.get_failure_mode() == FailureMode::RUNTIME_ONLY ||
                    config_.get_failure_mode() == FailureMode::COMBINED) {
                    handle_runtime_failures();
                }
                process_pending_requests();

                emit_cycle_json();
                reset_cycle_accumulators();

                if (steps_remaining_ > 0) {
                    steps_remaining_--;
                } else {
                    paused_ = true;
                }
            }
        }

        // Update final statistics (before emit_done_json)
        stats_.update_failure_stats(*grid_);

        // Record in-flight count (should be 0 after drain, unless manually stopped)
        uint64_t in_flight = pending_requests_.size() + transmitting_requests_.size();
        stats_.record_in_flight(in_flight);

        // Copy bypass diagnostic counters (from grid to stats, O(1) assignment)
        stats_.set_failed_vertical_encounters(grid_->get_failed_vertical_encounters());
        stats_.set_spare_found(grid_->get_spare_found());
        stats_.set_spare_unavailable(grid_->get_spare_unavailable());

        emit_done_json();
    } else {
        // Normal mode (unchanged)
        std::cout << "Starting simulation...\n";
        std::cout << "Simulation cycles: " << config_.get_simulation_cycles() << "\n\n";

        uint64_t total_cycles = config_.get_simulation_cycles();
        constexpr uint64_t progress_interval = 100;

        for (current_cycle_ = 0; current_cycle_ < total_cycles; ++current_cycle_) {
            simulate_cycle();

            // Show progress every 100 cycles and always emit the last cycle.
            if ((current_cycle_ + 1) % progress_interval == 0 ||
                current_cycle_ + 1 == total_cycles) {
                print_progress();
            }
        }

        // Drain phase: stop generating new requests, let in-flight complete
        while (!pending_requests_.empty() || !transmitting_requests_.empty()) {
            current_cycle_++;
            update_transmitting_requests();
            if (config_.get_failure_mode() == FailureMode::RUNTIME_ONLY ||
                config_.get_failure_mode() == FailureMode::COMBINED) {
                handle_runtime_failures();
            }
            process_pending_requests();
        }

        std::cout << "\nSimulation complete!\n";

        // Update final statistics
        stats_.update_failure_stats(*grid_);

        // Record in-flight count (should be 0 after drain)
        uint64_t in_flight = pending_requests_.size() + transmitting_requests_.size();
        stats_.record_in_flight(in_flight);

        // Copy bypass diagnostic counters (O(1) assignment)
        stats_.set_failed_vertical_encounters(grid_->get_failed_vertical_encounters());
        stats_.set_spare_found(grid_->get_spare_found());
        stats_.set_spare_unavailable(grid_->get_spare_unavailable());
    }
}

void Simulator::simulate_cycle() {
    // 1. Generate new requests
    generate_new_requests();

    // 2. Update transmitting requests (check if reached next hop)
    update_transmitting_requests();

    // 3. Handle runtime failures (if enabled)
    if (config_.get_failure_mode() == FailureMode::RUNTIME_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) {
        handle_runtime_failures();
    }

    // 4. Process pending requests
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
    if (json_stream_mode_) {
        // Snapshot failed state before applying new failures
        uint32_t gs = grid_->get_grid_size();
        uint32_t nl = grid_->get_num_layers();

        // Build a fast snapshot of current failure state
        // We use a flat vector: index = z*gs*gs + y*gs + x
        size_t total = static_cast<size_t>(gs) * gs * nl;
        std::vector<bool> was_failed(total, false);
        for (uint32_t z = 0; z < nl; ++z) {
            for (uint32_t y = 0; y < gs; ++y) {
                for (uint32_t x = 0; x < gs; ++x) {
                    const TSV* tsv = grid_->get_tsv(
                        static_cast<int32_t>(x),
                        static_cast<int32_t>(y),
                        static_cast<int32_t>(z));
                    if (tsv && tsv->is_failed()) {
                        was_failed[z * gs * gs + y * gs + x] = true;
                    }
                }
            }
        }

        // Apply runtime failures
        grid_->apply_runtime_failure(rng_);

        // Diff to find new failures
        for (uint32_t z = 0; z < nl; ++z) {
            for (uint32_t y = 0; y < gs; ++y) {
                for (uint32_t x = 0; x < gs; ++x) {
                    const TSV* tsv = grid_->get_tsv(
                        static_cast<int32_t>(x),
                        static_cast<int32_t>(y),
                        static_cast<int32_t>(z));
                    size_t idx = z * gs * gs + y * gs + x;
                    if (tsv && tsv->is_failed() && !was_failed[idx]) {
                        JsonNewFailure nf;
                        nf.x = static_cast<int32_t>(x);
                        nf.y = static_cast<int32_t>(y);
                        nf.z = static_cast<int32_t>(z);
                        nf.cycle = current_cycle_;
                        cycle_new_failures_.push_back(nf);
                    }
                }
            }
        }
    } else {
        // Original behavior
        grid_->apply_runtime_failure(rng_);
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
    auto result = router_->route(req.start, req.end, current_cycle_, req.horizontal_distance);

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

    // Record bypass usage count (precisely counted by Router in reconstruct_path)
    for (uint32_t i = 0; i < router_->get_last_bypass_count(); ++i) {
        stats_.record_redundant_tsv_usage();
    }

    return true;
}

bool Simulator::reroute_request(Request& req) {
    // Reroute from the last reached position to the destination
    // Note: current_hop has been incremented in update_transmitting_requests,
    // pointing to the failed next hop; actual position is current_hop-1
    Point actual_pos = (req.current_hop > 0) ? req.path[req.current_hop - 1] : req.path[0];
    auto result = router_->route(actual_pos, req.end, current_cycle_, req.horizontal_distance);

    if (!result.has_value()) {
        // Reroute failed
        return false;
    }

    auto [new_path, complete_time] = result.value();

    // Save old path for JSON tracking before modifying
    std::vector<Point> old_path_copy;
    if (json_stream_mode_) {
        old_path_copy = req.path;
    }

    // Update path (keep already-reached portion + new path)
    // actual_pos = path[current_hop-1], new path starts from actual_pos
    size_t keep_up_to = (req.current_hop > 0) ? req.current_hop - 1 : 0;
    std::vector<Point> updated_path(req.path.begin(), req.path.begin() + static_cast<long>(keep_up_to) + 1);
    updated_path.insert(updated_path.end(), new_path.begin() + 1, new_path.end());

    req.path = updated_path;
    req.current_hop = keep_up_to; // Reset to actual position
    req.next_hop_time = current_cycle_; // Reset hop time
    req.complete_time = complete_time;

    // Record bypass usage count for rerouted segment
    for (uint32_t i = 0; i < router_->get_last_bypass_count(); ++i) {
        stats_.record_redundant_tsv_usage();
    }

    // Track reroute for JSON streaming
    if (json_stream_mode_) {
        JsonReroute jr;
        jr.request_id = req.id;
        jr.old_path = std::move(old_path_copy);
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

void Simulator::update_transmitting_requests() {
    // Use iterator traversal to allow element removal during iteration
    auto it = transmitting_requests_.begin();
    while (it != transmitting_requests_.end()) {
        Request& req = *it;

        // Check if it's time to advance to the next hop
        if (current_cycle_ >= req.next_hop_time && req.current_hop < req.path.size()) {
            if (req.current_hop > 0) {
                const Point& from = req.path[req.current_hop - 1];
                const Point& to = req.path[req.current_hop];
                req.horizontal_distance += static_cast<uint64_t>(
                    std::abs(to.x - from.x) + std::abs(to.y - from.y));

                if (exceeds_horizontal_distance_limit(config_, req)) {
                    decrement_path_usage(req.path);
                    req.status = RequestStatus::FAILED;
                    stats_.record_request_failed(req);

                    if (json_stream_mode_) {
                        JsonFailed jf;
                        jf.id = req.id;
                        jf.time = current_cycle_;
                        jf.reason = "horizontal_distance_exceeded";
                        cycle_failed_.push_back(jf);
                    }

                    it = transmitting_requests_.erase(it);
                    continue;
                }
            }

            // Advance to next hop
            req.current_hop++;

            // Check if destination reached
            if (req.current_hop >= req.path.size()) {
                // Reached destination, mark as completed
                req.status = RequestStatus::COMPLETED;
                req.completed = true;
                req.complete_time = current_cycle_;
                decrement_path_usage(req.path);
                stats_.record_request_completed(req);

                if (json_stream_mode_) {
                    JsonCompleted jc;
                    jc.id = req.id;
                    jc.time = current_cycle_;
                    jc.latency = req.get_latency();
                    cycle_completed_.push_back(jc);
                }

                it = transmitting_requests_.erase(it);
                continue;
            }

            // Check if next hop TSV has failed (only vertical moves need checking; horizontal moves don't use TSVs)
            Point curr_pos = req.path[req.current_hop - 1];
            Point next_pos = req.path[req.current_hop];
            bool is_vertical_move = (curr_pos.z != next_pos.z);
            TSV* next_tsv = grid_->get_tsv(next_pos.x, next_pos.y, next_pos.z);

            if (is_vertical_move && next_tsv && next_tsv->is_failed()) {
                // TSV failed, need to reroute
                decrement_path_usage(req.path);
                if (reroute_request(req)) {
                    // Reroute succeeded, continue transmitting
                    increment_path_usage(req.path);
                    ++it;
                } else {
                    // Reroute failed, mark as failed
                    req.status = RequestStatus::FAILED;
                    stats_.record_request_failed(req);

                    if (json_stream_mode_) {
                        JsonFailed jf;
                        jf.id = req.id;
                        jf.time = current_cycle_;
                        jf.reason = "reroute_failed";
                        cycle_failed_.push_back(jf);
                    }

                    it = transmitting_requests_.erase(it);
                }
                continue;
            }

            // Calculate time to reach next hop
            if (req.current_hop > 0) {
                Point prev_pos = req.path[req.current_hop - 1];
                uint64_t hop_delay = router_->calculate_path_delay({prev_pos, next_pos});

                // B4: Wait for TSV to be free before starting transmission
                uint64_t start_time = current_cycle_;
                if (next_tsv && next_tsv->get_available_at() > current_cycle_) {
                    start_time = next_tsv->get_available_at();
                }
                req.next_hop_time = start_time + hop_delay;

                // Reserve the TSV
                if (next_tsv) {
                    next_tsv->occupy_until(req.next_hop_time);
                }
            }
        }

        ++it;
    }
}

void Simulator::update_tsv_occupancy(const std::vector<Point>& path, uint64_t complete_time) {
    // Mark all TSVs along the path as occupied (this function is no longer used).
    // TSVs are now reserved hop by hop during transmission.
    for (const auto& point : path) {
        TSV* tsv = grid_->get_tsv(point.x, point.y, point.z);
        if (tsv) {
            tsv->occupy_until(complete_time);
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
