#include "grid.hpp"
#include <limits>
#include <numeric>

namespace tsvra {

Grid::Grid(const Config& config)
    : config_(config),
      grid_size_(config.get_grid_size()),
      num_layers_(config.get_num_layers()),
      failure_rate_(config.get_failure_rate()),
      redundancy_layout_(config.get_redundancy_layout()),
      failure_model_(config.get_failure_model()),
      cluster_params_(config.get_cluster_params()),
      failed_vertical_encounters_(0),
      spare_found_(0),
      spare_unavailable_(0) {
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

    // 构建覆盖模型
    build_spare_sites();

    // 根据失效模式应用初始失效
    if (config_.get_failure_mode() == FailureMode::INITIAL_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) {
        apply_initial_failures(rng);
    }
}

// ===== Phase B: 覆盖模型 =====

uint32_t Grid::get_region_id(int32_t x, int32_t y) const {
    if (redundancy_layout_ == RedundancyLayout::SHARED_SPARE) {
        // 重叠块模型：块数 = (grid_size_ - 2) / 3 + 1
        uint32_t blocks_per_row = (grid_size_ >= 2) ? (grid_size_ - 2) / 3 + 1 : 1;
        uint32_t bi = std::min(static_cast<uint32_t>(std::max(x, 0) / 3), blocks_per_row - 1);
        uint32_t bj = std::min(static_cast<uint32_t>(std::max(y, 0) / 3), blocks_per_row - 1);
        return bi * blocks_per_row + bj;
    }
    uint32_t regions_per_row = grid_size_ / 4;
    return static_cast<uint32_t>(x / 4) * regions_per_row + static_cast<uint32_t>(y / 4);
}

void Grid::build_spare_sites() {
    switch (redundancy_layout_) {
        case RedundancyLayout::LEGACY_CORNER4: {
            uint32_t regions_per_row = grid_size_ / 4;
            uint32_t total_regions = regions_per_row * regions_per_row;
            region_to_spares_.resize(total_regions);
            build_legacy_corner4_sites();
            break;
        }
        case RedundancyLayout::SHARED_SPARE:
            build_shared_spare_sites();
            break;
        case RedundancyLayout::NONE:
            break;
    }
}

void Grid::build_legacy_corner4_sites() {
    uint32_t regions_per_row = grid_size_ / 4;
    // LegacyCorner4: 每个区域4个角冗余，每个只覆盖自己的区域
    for (uint32_t z = 0; z < num_layers_; ++z) {
        for (uint32_t ri = 0; ri < regions_per_row; ++ri) {
            for (uint32_t rj = 0; rj < regions_per_row; ++rj) {
                uint32_t region_id = ri * regions_per_row + rj;
                int32_t bx = static_cast<int32_t>(ri * 4);
                int32_t by = static_cast<int32_t>(rj * 4);

                // 4个角位置
                Point corners[4] = {
                    {bx, by, static_cast<int32_t>(z)},
                    {bx + 3, by, static_cast<int32_t>(z)},
                    {bx, by + 3, static_cast<int32_t>(z)},
                    {bx + 3, by + 3, static_cast<int32_t>(z)}
                };

                for (auto& corner : corners) {
                    uint32_t idx = static_cast<uint32_t>(spare_sites_.size());
                    spare_sites_.push_back({corner, {region_id}});
                    region_to_spares_[region_id].push_back(idx);
                }
            }
        }
    }
}

void Grid::build_shared_spare_sites() {
    // 重叠4×4块，步幅3。块(bi,bj)覆盖[3*bi, 3*bi+3]×[3*bj, 3*bj+3]
    // 冗余位于(1+3*bi, 1+3*bj)
    uint32_t blocks_per_row = (grid_size_ >= 2) ? (grid_size_ - 2) / 3 + 1 : 0;
    uint32_t total_blocks = blocks_per_row * blocks_per_row;
    region_to_spares_.resize(total_blocks);

    for (uint32_t z = 0; z < num_layers_; ++z) {
        for (uint32_t bi = 0; bi < blocks_per_row; ++bi) {
            for (uint32_t bj = 0; bj < blocks_per_row; ++bj) {
                int32_t sx = static_cast<int32_t>(1 + 3 * bi);
                int32_t sy = static_cast<int32_t>(1 + 3 * bj);
                Point pos{sx, sy, static_cast<int32_t>(z)};

                uint32_t block_id = bi * blocks_per_row + bj;
                uint32_t idx = static_cast<uint32_t>(spare_sites_.size());
                spare_sites_.push_back({pos, {block_id}});
                region_to_spares_[block_id].push_back(idx);
            }
        }
    }
}

std::optional<Point> Grid::find_bypass_spare(Point failed_tsv, uint64_t current_time) const {
    record_failed_vertical_encounter();

    std::optional<Point> result;
    switch (redundancy_layout_) {
        case RedundancyLayout::LEGACY_CORNER4:
            result = get_nearest_redundant_tsv(failed_tsv.x, failed_tsv.y, failed_tsv.z, current_time);
            break;
        case RedundancyLayout::SHARED_SPARE:
            result = select_spare(failed_tsv, current_time);
            break;
        case RedundancyLayout::NONE:
            result = std::nullopt;
            break;
    }

    if (result.has_value()) {
        record_spare_found();
    } else {
        record_spare_unavailable();
    }

    return result;
}

std::optional<Point> Grid::select_spare(Point failed_tsv, uint64_t current_time) const {
    // 重叠块模型：一个TSV可能属于多个块，需要搜索所有包含它的块的冗余
    uint32_t blocks_per_row = (grid_size_ >= 2) ? (grid_size_ - 2) / 3 + 1 : 0;
    if (blocks_per_row == 0) return std::nullopt;

    double min_dist = std::numeric_limits<double>::max();
    std::optional<Point> best;

    // 找到所有包含(failed_tsv.x, failed_tsv.y)的块
    // 块(bi,bj)覆盖 [3*bi, 3*bi+3]
    for (uint32_t bi = 0; bi < blocks_per_row; ++bi) {
        int32_t bx_start = static_cast<int32_t>(3 * bi);
        int32_t bx_end = bx_start + 3;
        if (failed_tsv.x < bx_start || failed_tsv.x > bx_end) continue;

        for (uint32_t bj = 0; bj < blocks_per_row; ++bj) {
            int32_t by_start = static_cast<int32_t>(3 * bj);
            int32_t by_end = by_start + 3;
            if (failed_tsv.y < by_start || failed_tsv.y > by_end) continue;

            // failed_tsv属于块(bi,bj)，搜索该块的冗余
            uint32_t block_id = bi * blocks_per_row + bj;
            if (block_id >= region_to_spares_.size()) continue;

            for (uint32_t idx : region_to_spares_[block_id]) {
                const SpareSite& site = spare_sites_[idx];
                if (site.position.z != failed_tsv.z) continue;

                const TSV* tsv = get_tsv(site.position.x, site.position.y, site.position.z);
                if (tsv && tsv->is_available(current_time)) {
                    double d = calculate_distance(failed_tsv.x, failed_tsv.y,
                                                  site.position.x, site.position.y);
                    if (d < min_dist) {
                        min_dist = d;
                        best = site.position;
                    }
                }
            }
        }
    }
    return best;
}

// ===== Phase C: 均值保持聚集失效采样 =====

std::vector<Point> Grid::sample_failures(std::mt19937& rng, bool runtime) {
    // 收集合格TSV（非顶层；若runtime则排除已失效）
    std::vector<Point> eligible;
    for (uint32_t z = 0; z < num_layers_ - 1; ++z) {
        for (uint32_t y = 0; y < grid_size_; ++y) {
            for (uint32_t x = 0; x < grid_size_; ++x) {
                const TSV& tsv = grid_[z][y][x];
                if (runtime && tsv.is_failed()) continue;
                eligible.emplace_back(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z));
            }
        }
    }

    if (eligible.empty()) return {};

    // 步骤1：从Binomial(N, failure_rate)采样失效数量（均值保持）
    std::binomial_distribution<uint32_t> binom(static_cast<uint32_t>(eligible.size()), failure_rate_);
    uint32_t k = binom(rng);
    if (k == 0) return {};
    k = std::min(k, static_cast<uint32_t>(eligible.size()));

    std::vector<Point> failures;
    failures.reserve(k);

    double strength = (failure_model_ == FailureModel::CLUSTERED) ? cluster_params_.strength : 0.0;
    uint32_t radius = cluster_params_.radius;

    if (strength == 0.0) {
        // 纯均匀：洗牌后取前k个
        std::shuffle(eligible.begin(), eligible.end(), rng);
        failures.assign(eligible.begin(), eligible.begin() + k);
    } else {
        // 聚集放置：逐次加权无放回抽样
        // 第一个失效：均匀随机
        std::uniform_int_distribution<size_t> uniform_idx(0, eligible.size() - 1);
        size_t first_idx = uniform_idx(rng);
        failures.push_back(eligible[first_idx]);
        eligible.erase(eligible.begin() + static_cast<ptrdiff_t>(first_idx));

        // 后续失效：按核函数加权
        for (uint32_t i = 1; i < k && !eligible.empty(); ++i) {
            std::vector<double> weights(eligible.size());
            for (size_t j = 0; j < eligible.size(); ++j) {
                // d_min = 到已选失效点的最小曼哈顿距离
                double d_min = std::numeric_limits<double>::max();
                for (const auto& f : failures) {
                    double d = std::abs(eligible[j].x - f.x) +
                               std::abs(eligible[j].y - f.y) +
                               std::abs(eligible[j].z - f.z);
                    if (d < d_min) d_min = d;
                }
                // w(p) = (1 - s) + s * exp(-d_min / r)
                weights[j] = (1.0 - strength) + strength * std::exp(-d_min / static_cast<double>(radius));
            }

            std::discrete_distribution<size_t> weighted_dist(weights.begin(), weights.end());
            size_t chosen_idx = weighted_dist(rng);
            failures.push_back(eligible[chosen_idx]);
            eligible.erase(eligible.begin() + static_cast<ptrdiff_t>(chosen_idx));
        }
    }

    return failures;
}

void Grid::apply_initial_failures(std::mt19937& rng) {
    auto failed = sample_failures(rng, /*runtime=*/false);
    for (const auto& p : failed) {
        grid_[static_cast<size_t>(p.z)][static_cast<size_t>(p.y)][static_cast<size_t>(p.x)].set_failed(true);
    }
}

void Grid::apply_runtime_failure(std::mt19937& rng) {
    auto failed = sample_failures(rng, /*runtime=*/true);
    for (const auto& p : failed) {
        grid_[static_cast<size_t>(p.z)][static_cast<size_t>(p.y)][static_cast<size_t>(p.x)].set_failed(true);
    }
}

// ===== 原有方法 =====

TSV* Grid::get_tsv(int32_t x, int32_t y, int32_t z) {
    if (!is_valid_position(x, y, z)) return nullptr;
    return &grid_[static_cast<size_t>(z)][static_cast<size_t>(y)][static_cast<size_t>(x)];
}

const TSV* Grid::get_tsv(int32_t x, int32_t y, int32_t z) const {
    if (!is_valid_position(x, y, z)) return nullptr;
    return &grid_[static_cast<size_t>(z)][static_cast<size_t>(y)][static_cast<size_t>(x)];
}

bool Grid::is_valid_position(int32_t x, int32_t y, int32_t z) const {
    return x >= 0 && x < static_cast<int32_t>(grid_size_) &&
           y >= 0 && y < static_cast<int32_t>(grid_size_) &&
           z >= 0 && z < static_cast<int32_t>(num_layers_);
}

std::optional<Point> Grid::get_nearest_redundant_tsv(int32_t x, int32_t y, int32_t z, uint64_t current_time) const {
    if (!is_valid_position(x, y, z)) return std::nullopt;

    double min_distance = std::numeric_limits<double>::max();
    std::optional<Point> nearest;

    for (const auto& pos : redundant_positions_) {
        if (pos.z != z) continue;

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

uint32_t Grid::count_failed_tsvs() const {
    uint32_t count = 0;
    for (uint32_t z = 0; z < num_layers_; ++z) {
        for (uint32_t y = 0; y < grid_size_; ++y) {
            for (uint32_t x = 0; x < grid_size_; ++x) {
                if (grid_[z][y][x].is_failed()) ++count;
            }
        }
    }
    return count;
}

uint32_t Grid::count_failed_redundant_tsvs() const {
    uint32_t count = 0;
    for (const auto& pos : redundant_positions_) {
        const TSV* tsv = get_tsv(pos.x, pos.y, pos.z);
        if (tsv && tsv->is_failed()) ++count;
    }
    return count;
}

uint32_t Grid::get_total_redundant_tsvs() const {
    return static_cast<uint32_t>(redundant_positions_.size());
}

bool Grid::is_redundant_position(int32_t x, int32_t y) const {
    switch (redundancy_layout_) {
        case RedundancyLayout::LEGACY_CORNER4: {
            // 每个4x4区域的四个角是冗余TSV
            int32_t mod_x = x % 4;
            int32_t mod_y = y % 4;
            return (mod_x == 0 || mod_x == 3) && (mod_y == 0 || mod_y == 3);
        }
        case RedundancyLayout::SHARED_SPARE: {
            // 重叠4×4块，步幅3，冗余位于(1+3i, 1+3j)
            return (x >= 1) && (y >= 1) && ((x - 1) % 3 == 0) && ((y - 1) % 3 == 0);
        }
        case RedundancyLayout::NONE:
            return false;
    }
    return false;
}

double Grid::calculate_distance(int32_t x1, int32_t y1, int32_t x2, int32_t y2) const {
    return static_cast<double>(std::abs(x1 - x2) + std::abs(y1 - y2));
}

} // namespace tsvra
