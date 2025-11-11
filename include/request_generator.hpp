#ifndef TSVRA_REQUEST_GENERATOR_HPP
#define TSVRA_REQUEST_GENERATOR_HPP

#include "request.hpp"
#include "config.hpp"
#include <random>
#include <vector>
#include <optional>

namespace tsvra {

// 请求生成器类
class RequestGenerator {
public:
    explicit RequestGenerator(const Config& config, std::mt19937& rng);
    
    // 初始化热点区域
    void initialize_hotspots();
    
    // 生成请求（每个时钟周期调用）
    std::optional<Request> generate_request(uint64_t current_time);
    
    // 获取热点权重（用于统计）
    double get_hotspot_weight(uint32_t region_x, uint32_t region_y) const;
    
    // 获取区域数量
    uint32_t get_num_regions() const { return num_regions_; }
    
private:
    const Config& config_;
    std::mt19937& rng_;
    uint32_t grid_size_;        // 网格大小
    uint32_t num_layers_;       // 层数
    uint32_t num_regions_;      // 热点区域数量（每边）
    uint64_t next_request_id_;  // 下一个请求ID
    
    // 热点权重 [region_y][region_x]
    // 每个4x4的TSV块是一个基本区域
    std::vector<std::vector<double>> hotspot_weights_;
    
    // 累积分布（用于快速采样）
    std::vector<double> cumulative_weights_;
    double total_weight_;
    
    // 随机数分布
    std::uniform_real_distribution<double> uniform_dist_;
    std::uniform_int_distribution<uint32_t> layer_dist_;
    
    // 辅助函数
    void build_cumulative_distribution();
    Point sample_position_from_region(uint32_t region_x, uint32_t region_y, int32_t z);
    Point sample_start_position();
    Point sample_end_position();
};

} // namespace tsvra

#endif // TSVRA_REQUEST_GENERATOR_HPP

