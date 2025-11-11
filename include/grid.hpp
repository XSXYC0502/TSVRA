#ifndef TSVRA_GRID_HPP
#define TSVRA_GRID_HPP

#include "tsv.hpp"
#include "request.hpp"
#include "config.hpp"
#include <vector>
#include <random>
#include <optional>

namespace tsvra {

// TSV网格管理类
class Grid {
public:
    explicit Grid(const Config& config);
    
    // 初始化网格
    void initialize(std::mt19937& rng);
    
    // 获取TSV
    TSV* get_tsv(int32_t x, int32_t y, int32_t z);
    const TSV* get_tsv(int32_t x, int32_t y, int32_t z) const;
    
    // 检查坐标是否有效
    bool is_valid_position(int32_t x, int32_t y, int32_t z) const;
    
    // 获取最近的冗余TSV
    std::optional<Point> get_nearest_redundant_tsv(int32_t x, int32_t y, int32_t z, uint64_t current_time) const;
    
    // 失效处理
    void apply_initial_failures(std::mt19937& rng);
    void apply_runtime_failure(std::mt19937& rng);
    
    // 统计信息
    uint32_t count_failed_tsvs() const;
    uint32_t count_failed_redundant_tsvs() const;
    uint32_t get_total_tsvs() const { return grid_size_ * grid_size_ * num_layers_; }
    uint32_t get_total_redundant_tsvs() const;
    
    // Getters
    uint32_t get_grid_size() const { return grid_size_; }
    uint32_t get_num_layers() const { return num_layers_; }
    
private:
    const Config& config_;
    uint32_t grid_size_;        // N = 4 * grid_factor
    uint32_t num_layers_;       // 层数
    double failure_rate_;       // 失效率
    
    // 3D TSV网格 [z][y][x]
    std::vector<std::vector<std::vector<TSV>>> grid_;
    
    // 冗余TSV位置列表（用于快速查找）
    std::vector<Point> redundant_positions_;
    
    // 辅助函数
    bool is_redundant_position(int32_t x, int32_t y) const;
    void mark_redundant_tsvs();
    double calculate_distance(int32_t x1, int32_t y1, int32_t x2, int32_t y2) const;
};

} // namespace tsvra

#endif // TSVRA_GRID_HPP

