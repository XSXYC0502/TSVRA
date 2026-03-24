#ifndef TSVRA_GRID_HPP
#define TSVRA_GRID_HPP

#include "tsv.hpp"
#include "request.hpp"
#include "config.hpp"
#include <vector>
#include <random>
#include <optional>
#include <algorithm>
#include <cmath>

namespace tsvra {

// 冗余位点（含覆盖区域信息）
struct SpareSite {
    Point position;
    std::vector<uint32_t> covered_region_ids;
};

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

    // 获取最近的冗余TSV（旧接口，LegacyCorner4使用）
    std::optional<Point> get_nearest_redundant_tsv(int32_t x, int32_t y, int32_t z, uint64_t current_time) const;

    // 统一旁路备用搜索（根据冗余布局分派）
    std::optional<Point> find_bypass_spare(Point failed_tsv, uint64_t current_time) const;

    // 获取区域ID
    uint32_t get_region_id(int32_t x, int32_t y) const;

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
    RedundancyLayout get_redundancy_layout() const { return redundancy_layout_; }
    const std::vector<SpareSite>& get_spare_sites() const { return spare_sites_; }

    // 静态冗余位置判断（不依赖Grid实例，供RequestGenerator使用）
    static bool is_redundant_position_static(int32_t x, int32_t y, RedundancyLayout layout) {
        switch (layout) {
            case RedundancyLayout::LEGACY_CORNER4: {
                int32_t mod_x = x % 4;
                int32_t mod_y = y % 4;
                return (mod_x == 0 || mod_x == 3) && (mod_y == 0 || mod_y == 3);
            }
            case RedundancyLayout::SHARED_SPARE:
                return (x >= 1) && (y >= 1) && ((x - 1) % 3 == 0) && ((y - 1) % 3 == 0);
            case RedundancyLayout::NONE:
                return false;
        }
        return false;
    }

    // 旁路诊断计数器
    uint64_t get_failed_vertical_encounters() const { return failed_vertical_encounters_; }
    uint64_t get_spare_found() const { return spare_found_; }
    uint64_t get_spare_unavailable() const { return spare_unavailable_; }

private:
    const Config& config_;
    uint32_t grid_size_;
    uint32_t num_layers_;
    double failure_rate_;
    RedundancyLayout redundancy_layout_;
    FailureModel failure_model_;
    ClusterParams cluster_params_;

    // 3D TSV网格 [z][y][x]
    std::vector<std::vector<std::vector<TSV>>> grid_;

    // 冗余TSV位置列表（LegacyCorner4旧接口使用）
    std::vector<Point> redundant_positions_;

    // 覆盖模型数据（SharedSpare和LegacyCorner4通用）
    std::vector<SpareSite> spare_sites_;
    std::vector<std::vector<uint32_t>> region_to_spares_;

    // 旁路诊断计数器（mutable允许const方法修改）
    mutable uint64_t failed_vertical_encounters_;
    mutable uint64_t spare_found_;
    mutable uint64_t spare_unavailable_;

    // 冗余布局构建
    void build_spare_sites();
    void build_legacy_corner4_sites();
    void build_shared_spare_sites();

    // 覆盖范围搜索（SharedSpare使用）
    std::optional<Point> select_spare(Point failed_tsv, uint64_t current_time) const;

    // 均值保持失效采样器
    std::vector<Point> sample_failures(std::mt19937& rng, bool runtime);

    // 辅助函数
    bool is_redundant_position(int32_t x, int32_t y) const;
    double calculate_distance(int32_t x1, int32_t y1, int32_t x2, int32_t y2) const;

    // 内部记录方法（const允许在const方法中调用）
    void record_failed_vertical_encounter() const { ++failed_vertical_encounters_; }
    void record_spare_found() const { ++spare_found_; }
    void record_spare_unavailable() const { ++spare_unavailable_; }
};

} // namespace tsvra

#endif // TSVRA_GRID_HPP
