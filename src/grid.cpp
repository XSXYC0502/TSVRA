#include "grid.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tsvra {

Grid::Grid(const Config& config)
    : config_(config),
      grid_size_(config.get_grid_size()),
      num_layers_(config.get_num_layers()),
      failure_rate_(config.get_failure_rate()) {
}

void Grid::initialize(std::mt19937& rng) {
    // 分配网格空间 [z][y][x]
    grid_.resize(num_layers_);
    for (uint32_t z = 0; z < num_layers_; ++z) {
        grid_[z].resize(grid_size_);
        for (uint32_t y = 0; y < grid_size_; ++y) {
            grid_[z][y].resize(grid_size_);
            for (uint32_t x = 0; x < grid_size_; ++x) {
                bool is_redundant = is_redundant_position(static_cast<int32_t>(x), static_cast<int32_t>(y));
                grid_[z][y][x] = TSV(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z), is_redundant);
                
                if (is_redundant) {
                    redundant_positions_.emplace_back(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z));
                }
            }
        }
    }
    
    // 根据失效模式应用初始失效
    if (config_.get_failure_mode() == FailureMode::INITIAL_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) {
        apply_initial_failures(rng);
    }
}

TSV* Grid::get_tsv(int32_t x, int32_t y, int32_t z) {
    if (!is_valid_position(x, y, z)) {
        return nullptr;
    }
    return &grid_[static_cast<size_t>(z)][static_cast<size_t>(y)][static_cast<size_t>(x)];
}

const TSV* Grid::get_tsv(int32_t x, int32_t y, int32_t z) const {
    if (!is_valid_position(x, y, z)) {
        return nullptr;
    }
    return &grid_[static_cast<size_t>(z)][static_cast<size_t>(y)][static_cast<size_t>(x)];
}

bool Grid::is_valid_position(int32_t x, int32_t y, int32_t z) const {
    return x >= 0 && x < static_cast<int32_t>(grid_size_) &&
           y >= 0 && y < static_cast<int32_t>(grid_size_) &&
           z >= 0 && z < static_cast<int32_t>(num_layers_);
}

std::optional<Point> Grid::get_nearest_redundant_tsv(int32_t x, int32_t y, int32_t z, uint64_t current_time) const {
    if (!is_valid_position(x, y, z)) {
        return std::nullopt;
    }
    
    double min_distance = std::numeric_limits<double>::max();
    std::optional<Point> nearest;
    
    // 在当前层查找最近的可用冗余TSV
    for (const auto& pos : redundant_positions_) {
        if (pos.z != z) {
            continue;
        }
        
        const TSV* tsv = get_tsv(pos.x, pos.y, pos.z);
        if (tsv && tsv->is_available(current_time)) {
            double dist = calculate_distance(x, y, pos.x, pos.y);
            if (dist < min_distance) {
                min_distance = dist;
                nearest = pos;
            }
        }
    }
    
    return nearest;
}

void Grid::apply_initial_failures(std::mt19937& rng) {
    std::bernoulli_distribution failure_dist(failure_rate_);

    // 顶层无TSV（无上层连接），仅对非顶层施加失效
    for (uint32_t z = 0; z < num_layers_ - 1; ++z) {
        for (uint32_t y = 0; y < grid_size_; ++y) {
            for (uint32_t x = 0; x < grid_size_; ++x) {
                if (failure_dist(rng)) {
                    grid_[z][y][x].set_failed(true);
                }
            }
        }
    }
}

void Grid::apply_runtime_failure(std::mt19937& rng) {
    std::bernoulli_distribution failure_dist(failure_rate_);

    // 顶层无TSV，仅对非顶层检查运行时失效
    for (uint32_t z = 0; z < num_layers_ - 1; ++z) {
        for (uint32_t y = 0; y < grid_size_; ++y) {
            for (uint32_t x = 0; x < grid_size_; ++x) {
                TSV& tsv = grid_[z][y][x];
                if (!tsv.is_failed() && failure_dist(rng)) {
                    tsv.set_failed(true);
                }
            }
        }
    }
}

uint32_t Grid::count_failed_tsvs() const {
    uint32_t count = 0;
    for (uint32_t z = 0; z < num_layers_; ++z) {
        for (uint32_t y = 0; y < grid_size_; ++y) {
            for (uint32_t x = 0; x < grid_size_; ++x) {
                if (grid_[z][y][x].is_failed()) {
                    ++count;
                }
            }
        }
    }
    return count;
}

uint32_t Grid::count_failed_redundant_tsvs() const {
    uint32_t count = 0;
    for (const auto& pos : redundant_positions_) {
        const TSV* tsv = get_tsv(pos.x, pos.y, pos.z);
        if (tsv && tsv->is_failed()) {
            ++count;
        }
    }
    return count;
}

uint32_t Grid::get_total_redundant_tsvs() const {
    return static_cast<uint32_t>(redundant_positions_.size());
}

bool Grid::is_redundant_position(int32_t x, int32_t y) const {
    // 每个4x4区域的四个顶角是冗余TSV
    // 顶角坐标: (4k, 4k), (4k+3, 4k), (4k, 4k+3), (4k+3, 4k+3)
    int32_t mod_x = x % 4;
    int32_t mod_y = y % 4;
    
    return (mod_x == 0 || mod_x == 3) && (mod_y == 0 || mod_y == 3);
}

double Grid::calculate_distance(int32_t x1, int32_t y1, int32_t x2, int32_t y2) const {
    // 曼哈顿距离
    return static_cast<double>(std::abs(x1 - x2) + std::abs(y1 - y2));
}

} // namespace tsvra

