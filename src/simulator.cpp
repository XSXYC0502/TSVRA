#include "simulator.hpp"
#include <iostream>
#include <iomanip>

namespace tsvra {

Simulator::Simulator(const Config& config)
    : config_(config),
      rng_(config.get_random_seed() == 0 ? std::random_device{}() : config.get_random_seed()),
      stats_(config),
      current_cycle_(0) {
}

void Simulator::initialize() {
    std::cout << "初始化仿真环境...\n";
    
    // 创建网格
    grid_ = std::make_unique<Grid>(config_);
    grid_->initialize(rng_);
    
    // 创建路由器
    router_ = std::make_unique<Router>(*grid_, config_);
    
    // 创建请求生成器
    req_gen_ = std::make_unique<RequestGenerator>(config_, rng_);
    req_gen_->initialize_hotspots();
    
    // 打印初始状态
    std::cout << "网格大小: " << grid_->get_grid_size() << "x" << grid_->get_grid_size() 
              << " x " << grid_->get_num_layers() << " 层\n";
    std::cout << "总TSV数: " << grid_->get_total_tsvs() << "\n";
    std::cout << "冗余TSV数: " << grid_->get_total_redundant_tsvs() << "\n";
    
    // 更新初始失效统计
    stats_.update_failure_stats(*grid_);
    if (config_.get_failure_mode() == FailureMode::INITIAL_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) {
        std::cout << "初始失效TSV数: " << grid_->count_failed_tsvs() << "\n";
    }
    
    std::cout << "初始化完成\n\n";
}

void Simulator::run() {
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
        pending_requests_.push(req_opt.value());
        stats_.record_request_generated();
    }
}

void Simulator::handle_runtime_failures() {
    // 每个周期都检查运行时失效
    grid_->apply_runtime_failure(rng_);
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
            transmitting_requests_.push_back(req);
        } else {
            // 路由失败
            req.status = RequestStatus::FAILED;
            stats_.record_request_failed(req);
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
    
    // 更新路径（保留已完成的部分+新路径）
    std::vector<Point> updated_path(req.path.begin(), req.path.begin() + static_cast<long>(req.current_hop) + 1);
    updated_path.insert(updated_path.end(), new_path.begin() + 1, new_path.end());
    
    req.path = updated_path;
    req.complete_time = complete_time;
    
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

