#ifndef TSVRA_SIMULATOR_HPP
#define TSVRA_SIMULATOR_HPP

#include "config.hpp"
#include "grid.hpp"
#include "router.hpp"
#include "request_generator.hpp"
#include "statistics.hpp"
#include <random>
#include <queue>
#include <memory>

namespace tsvra {

// 仿真器类
class Simulator {
public:
    explicit Simulator(const Config& config);
    
    // 初始化仿真环境
    void initialize();
    
    // 运行仿真
    void run();
    
    // 获取统计信息
    const Statistics& get_statistics() const { return stats_; }
    
private:
    const Config& config_;
    std::mt19937 rng_;
    
    // 核心组件
    std::unique_ptr<Grid> grid_;
    std::unique_ptr<Router> router_;
    std::unique_ptr<RequestGenerator> req_gen_;
    Statistics stats_;
    
    // 待处理请求队列
    std::queue<Request> pending_requests_;
    
    // 传输中请求列表
    std::vector<Request> transmitting_requests_;
    
    // 当前时钟周期
    uint64_t current_cycle_;
    
    // 仿真步骤
    void simulate_cycle();
    void generate_new_requests();
    void handle_runtime_failures();
    void process_pending_requests();
    void update_transmitting_requests();
    bool route_request(Request& req);
    bool reroute_request(Request& req);
    void update_tsv_occupancy(const std::vector<Point>& path, uint64_t complete_time);
    void increment_path_usage(const std::vector<Point>& path);
    void decrement_path_usage(const std::vector<Point>& path);
    
    // 辅助函数
    void print_progress() const;
};

} // namespace tsvra

#endif // TSVRA_SIMULATOR_HPP

