#include "simulator.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <limits>

#ifdef __linux__
#include <poll.h>
#include <unistd.h>
#endif

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
#ifdef __linux__
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;

    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n <= 0) break;
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
#endif
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
    stats_.update_failure_stats(*grid_);

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
    // If no requests completed, min_latency is max uint64 sentinel; output 0 instead
    if (stats_.get_completed_requests() == 0) min_lat = 0;
    oss << ",\"minLatency\":" << min_lat;
    oss << "}}";
    std::cout << oss.str() << std::endl;
}

// ========== Core simulation methods ==========

void Simulator::initialize() {
    if (json_stream_mode_) {
        std::cerr << "初始化仿真环境...\n";
    } else {
        std::cout << "初始化仿真环境...\n";
    }

    // 创建网格
    grid_ = std::make_unique<Grid>(config_);
    grid_->initialize(rng_);

    // 创建路由器
    router_ = std::make_unique<Router>(*grid_, config_);

    // 创建请求生成器
    req_gen_ = std::make_unique<RequestGenerator>(config_, rng_);
    req_gen_->initialize_hotspots();

    // 打印初始状态
    auto& out = json_stream_mode_ ? std::cerr : std::cout;
    out << "网格大小: " << grid_->get_grid_size() << "x" << grid_->get_grid_size()
        << " x " << grid_->get_num_layers() << " 层\n";
    out << "总TSV数: " << grid_->get_total_tsvs() << "\n";
    out << "冗余TSV数: " << grid_->get_total_redundant_tsvs() << "\n";

    // 更新初始失效统计
    stats_.update_failure_stats(*grid_);
    if (config_.get_failure_mode() == FailureMode::INITIAL_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) {
        out << "初始失效TSV数: " << grid_->count_failed_tsvs() << "\n";
    }

    out << "初始化完成\n\n";
}

void Simulator::run() {
    if (json_stream_mode_) {
        // Emit init JSON and start streaming
        emit_init_json();
        reset_cycle_accumulators();

        std::cerr << "开始仿真 (JSON流模式)...\n";
        std::cerr << "仿真周期: " << config_.get_simulation_cycles() << "\n\n";

        uint64_t total_cycles = config_.get_simulation_cycles();

        for (current_cycle_ = 0; current_cycle_ < total_cycles; ++current_cycle_) {
            // Check for stdin commands
            poll_stdin_commands();

            // Handle pause
            while (paused_ && !stop_requested_) {
                poll_stdin_commands();
                // Sleep briefly to avoid busy loop (1ms)
#ifdef __linux__
                usleep(1000);
#endif
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

        emit_done_json();

        // Update final stats
        stats_.update_failure_stats(*grid_);
    } else {
        // Normal mode (unchanged)
        std::cout << "开始仿真...\n";
        std::cout << "仿真周期: " << config_.get_simulation_cycles() << "\n\n";

        uint64_t total_cycles = config_.get_simulation_cycles();
        uint64_t progress_interval = total_cycles / 20; // 每5%显示一次进度

        for (current_cycle_ = 0; current_cycle_ < total_cycles; ++current_cycle_) {
            simulate_cycle();

            // 显示进度
            if (progress_interval > 0 && (current_cycle_ + 1) % progress_interval == 0) {
                print_progress();
            }
        }

        std::cout << "\n仿真完成!\n";

        // 更新最终统计
        stats_.update_failure_stats(*grid_);
    }
}

void Simulator::simulate_cycle() {
    // 1. 生成新请求
    generate_new_requests();

    // 2. 更新传输中的请求（检查是否到达下一跳）
    update_transmitting_requests();

    // 3. 处理运行时失效（如果启用）
    if (config_.get_failure_mode() == FailureMode::RUNTIME_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) {
        handle_runtime_failures();
    }

    // 4. 处理待处理请求
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
    // 处理队列中的所有请求
    size_t queue_size = pending_requests_.size();
    for (size_t i = 0; i < queue_size; ++i) {
        Request req = pending_requests_.front();
        pending_requests_.pop();

        if (route_request(req)) {
            // 路由成功，加入传输队列
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
            // 路由失败
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
    // 使用路由器计算路径
    auto result = router_->route(req.start, req.end, current_cycle_);

    if (!result.has_value()) {
        // 路由失败
        return false;
    }

    auto [path, complete_time] = result.value();

    // 更新请求信息
    req.path = path;
    req.current_hop = 0;
    req.next_hop_time = current_cycle_;
    req.complete_time = complete_time;

    // 检查是否使用了冗余TSV绕行失效TSV
    for (size_t i = 0; i < path.size(); ++i) {
        TSV* tsv = grid_->get_tsv(path[i].x, path[i].y, path[i].z);
        if (tsv && tsv->is_redundant()) {
            bool used_for_bypass = false;
            if (i > 0) {
                TSV* prev_tsv = grid_->get_tsv(path[i-1].x, path[i-1].y, path[i-1].z);
                if (prev_tsv && prev_tsv->is_failed()) {
                    used_for_bypass = true;
                }
            }
            if (!used_for_bypass && i + 1 < path.size()) {
                TSV* next_tsv = grid_->get_tsv(path[i+1].x, path[i+1].y, path[i+1].z);
                if (next_tsv && next_tsv->is_failed()) {
                    used_for_bypass = true;
                }
            }
            if (used_for_bypass) {
                stats_.record_redundant_tsv_usage();
            }
        }
    }

    return true;
}

bool Simulator::reroute_request(Request& req) {
    // 从当前位置重新路由到终点
    Point current_pos = req.get_current_position();
    auto result = router_->route(current_pos, req.end, current_cycle_);

    if (!result.has_value()) {
        // 重路由失败
        return false;
    }

    auto [new_path, complete_time] = result.value();

    // Save old path for JSON tracking before modifying
    std::vector<Point> old_path_copy;
    if (json_stream_mode_) {
        old_path_copy = req.path;
    }

    // 更新路径（保留已完成的部分+新路径）
    std::vector<Point> updated_path(req.path.begin(), req.path.begin() + static_cast<long>(req.current_hop) + 1);
    updated_path.insert(updated_path.end(), new_path.begin() + 1, new_path.end());

    req.path = updated_path;
    req.complete_time = complete_time;

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
    // 使用迭代器遍历，以便在遍历过程中删除元素
    auto it = transmitting_requests_.begin();
    while (it != transmitting_requests_.end()) {
        Request& req = *it;

        // 检查是否到达下一跳的时间
        if (current_cycle_ >= req.next_hop_time && req.current_hop < req.path.size()) {
            // 移动到下一跳
            req.current_hop++;

            // 检查是否到达终点
            if (req.current_hop >= req.path.size()) {
                // 到达终点，标记为完成
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

            // 检查下一跳的TSV是否失效
            Point next_pos = req.path[req.current_hop];
            TSV* next_tsv = grid_->get_tsv(next_pos.x, next_pos.y, next_pos.z);

            if (next_tsv && next_tsv->is_failed()) {
                // TSV失效，需要重新路由
                decrement_path_usage(req.path);
                if (reroute_request(req)) {
                    // 重路由成功，继续传输
                    increment_path_usage(req.path);
                    ++it;
                } else {
                    // 重路由失败，标记为失败
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

            // 计算到达下一跳的时间
            if (req.current_hop > 0) {
                Point prev_pos = req.path[req.current_hop - 1];
                uint64_t hop_delay = static_cast<uint64_t>(
                    router_->calculate_path_delay({prev_pos, next_pos}));
                req.next_hop_time = current_cycle_ + hop_delay;

                // 占用TSV
                if (next_tsv) {
                    next_tsv->occupy_until(req.next_hop_time);
                }
            }
        }

        ++it;
    }
}

void Simulator::update_tsv_occupancy(const std::vector<Point>& path, uint64_t complete_time) {
    // 标记路径上的所有TSV为占用状态（现在这个函数已经不再使用）
    // 传输过程中逐跳占用TSV
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

    std::cout << "进度: " << std::fixed << std::setprecision(1) << progress << "% "
              << "(" << current_cycle_ + 1 << "/" << config_.get_simulation_cycles() << ") "
              << "请求: " << stats_.get_total_requests()
              << " (完成: " << stats_.get_completed_requests()
              << ", 失败: " << stats_.get_failed_requests() << ")\n";
}

} // namespace tsvra
