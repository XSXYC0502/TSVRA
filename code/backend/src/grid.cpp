#include "grid.hpp"
#include <limits>
#include <numeric>
#include <array>
#include <stdexcept>

namespace {

// Scale a fixed weight map with saturation so its mean remains the requested p.
// Ascending prefix sums preserve the background when very large peaks saturate.
std::vector<double> normalize_weights(const std::vector<double>& weights, double base_probability) {
    const double p = std::clamp(base_probability, 0.0, 1.0);
    std::vector<double> probabilities(weights.size(), p);
    if (weights.empty() || p == 0.0 || p == 1.0) return probabilities;
    std::vector<size_t> order(weights.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return weights[a] < weights[b]; });
    const long double peak = weights[order.back()];
    std::vector<long double> prefix(weights.size() + 1, 0.0L);
    for (size_t i = 0; i < order.size(); ++i)
        prefix[i + 1] = prefix[i] + static_cast<long double>(weights[order[i]]) / peak;
    const long double target = static_cast<long double>(p) * static_cast<long double>(weights.size());
    size_t remaining = weights.size();
    long double scale = target / prefix[remaining];
    while (remaining > 0 && scale * (static_cast<long double>(weights[order[remaining - 1]]) / peak) >= 1.0L) {
        probabilities[order[remaining - 1]] = 1.0;
        --remaining;
        if (remaining == 0) break;
        const long double budget = target - static_cast<long double>(weights.size() - remaining);
        scale = std::max(0.0L, budget) / prefix[remaining];
    }
    for (size_t i = 0; i < remaining; ++i)
        probabilities[order[i]] = static_cast<double>(std::clamp(
            scale * (static_cast<long double>(weights[order[i]]) / peak), 0.0L, 1.0L));
    return probabilities;
}

std::mt19937 derived_rng(const std::mt19937& source, uint32_t salt) {
    std::mt19937 copy = source;
    std::array<uint32_t, 9> seeds{};
    seeds[0] = salt;
    for (size_t i = 1; i < seeds.size(); ++i) seeds[i] = static_cast<uint32_t>(copy());
    std::seed_seq sequence(seeds.begin(), seeds.end());
    return std::mt19937(sequence);
}

} // namespace

namespace tsvra {

Grid::Grid(const Config& config)
    : config_(config),
      grid_size_(config.get_grid_size()),
      num_layers_(config.get_num_layers()),
      redundancy_layout_(config.get_redundancy_layout()),
      failure_model_(config.get_failure_model()),
      hotspot_params_(config.get_hotspot_params()),
      failed_vertical_encounters_(0),
      spare_found_(0),
      spare_unavailable_(0) {
}

void Grid::initialize(std::mt19937& rng) {
    auto hotspot_rng = derived_rng(rng, 0x48535054U);
    initialize(rng, hotspot_rng);
}

void Grid::initialize(std::mt19937& initial_rng, std::mt19937& hotspot_rng) {
    auto runtime_rng = derived_rng(initial_rng, 0x52554e54U);
    initialize(initial_rng, runtime_rng, hotspot_rng);
}

void Grid::initialize(std::mt19937& initial_rng, std::mt19937& runtime_rng, std::mt19937& hotspot_rng) {
    if (num_layers_ < 2 || grid_size_ == 0)
        throw std::invalid_argument("grid initialization requires at least two layers and a positive grid width");
    grid_.clear();
    redundant_positions_.clear();
    spare_sites_.clear();
    region_to_spares_.clear();
    runtime_events_ = {};
    legacy_runtime_cycle_ = 0;
    failed_vertical_encounters_ = spare_found_ = spare_unavailable_ = 0;
    // Allocate grid space [z][y][x]
    grid_.resize(num_layers_);
    for (uint32_t z = 0; z < num_layers_; ++z) {
        grid_[z].resize(grid_size_);
        for (uint32_t y = 0; y < grid_size_; ++y) {
            grid_[z][y].resize(grid_size_);
            for (uint32_t x = 0; x < grid_size_; ++x) {
                bool is_redundant = z + 1 < num_layers_ && is_redundant_position(static_cast<int32_t>(x), static_cast<int32_t>(y));
                grid_[z][y][x] = TSV(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z), is_redundant);

                if (is_redundant) {
                    redundant_positions_.emplace_back(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z));
                }
            }
        }
    }

    // Build coverage model
    build_spare_sites();
    build_probability_maps(hotspot_rng);

    // Apply initial failures based on failure mode
    if (config_.get_failure_mode() == FailureMode::INITIAL_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) {
        apply_initial_failures(initial_rng);
    }
    if (config_.get_failure_mode() == FailureMode::RUNTIME_ONLY ||
        config_.get_failure_mode() == FailureMode::COMBINED) schedule_runtime_failures(runtime_rng);
}

// ===== Phase B: Coverage Model =====

uint32_t Grid::get_region_id(int32_t x, int32_t y) const {
    if (redundancy_layout_ == RedundancyLayout::SHARED_SPARE) {
        uint32_t gf = config_.get_grid_factor();
        uint32_t stride = (gf > 1) ? gf - 1 : 1;
        uint32_t blocks_per_row = (grid_size_ >= 2) ? (grid_size_ - 2) / stride + 1 : 1;
        uint32_t clamped_x = static_cast<uint32_t>(std::max(x, 0));
        uint32_t clamped_y = static_cast<uint32_t>(std::max(y, 0));
        uint32_t bi = std::min(clamped_x / stride, blocks_per_row - 1);
        uint32_t bj = std::min(clamped_y / stride, blocks_per_row - 1);
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
    // LegacyCorner4: each region has 4 corner redundant TSVs, each covering only its own region
    for (uint32_t z = 0; z + 1 < num_layers_; ++z) {
        for (uint32_t ri = 0; ri < regions_per_row; ++ri) {
            for (uint32_t rj = 0; rj < regions_per_row; ++rj) {
                uint32_t region_id = ri * regions_per_row + rj;
                int32_t bx = static_cast<int32_t>(ri * 4);
                int32_t by = static_cast<int32_t>(rj * 4);

                // 4 corner positions
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
    // Overlapping blocks, stride = grid_factor - 1. Block (bi,bj) covers [stride*bi, stride*bi+stride]
    // Redundant TSV located at (1+stride*bi, 1+stride*bj)
    uint32_t gf = config_.get_grid_factor();
    if (gf <= 1) return; // No redundancy when grid_factor=1
    uint32_t stride = gf - 1;
    uint32_t blocks_per_row = (grid_size_ >= 2) ? (grid_size_ - 2) / stride + 1 : 0;
    uint32_t total_blocks = blocks_per_row * blocks_per_row;
    region_to_spares_.resize(total_blocks);

    for (uint32_t z = 0; z + 1 < num_layers_; ++z) {
        for (uint32_t bi = 0; bi < blocks_per_row; ++bi) {
            for (uint32_t bj = 0; bj < blocks_per_row; ++bj) {
                int32_t sx = static_cast<int32_t>(1 + stride * bi);
                int32_t sy = static_cast<int32_t>(1 + stride * bj);
                if (sx >= static_cast<int32_t>(grid_size_) || sy >= static_cast<int32_t>(grid_size_)) continue;
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
    // Overlapping block model: a TSV may belong to multiple blocks, need to search spares in all containing blocks
    uint32_t gf = config_.get_grid_factor();
    if (gf <= 1) return std::nullopt;
    uint32_t stride = gf - 1;
    uint32_t blocks_per_row = (grid_size_ >= 2) ? (grid_size_ - 2) / stride + 1 : 0;
    if (blocks_per_row == 0) return std::nullopt;

    double min_dist = std::numeric_limits<double>::max();
    std::optional<Point> best;

    // Find all blocks containing (failed_tsv.x, failed_tsv.y)
    // Block (bi,bj) covers [stride*bi, stride*bi+stride]
    for (uint32_t bi = 0; bi < blocks_per_row; ++bi) {
        int32_t bx_start = static_cast<int32_t>(stride * bi);
        int32_t bx_end = bx_start + static_cast<int32_t>(stride);
        if (failed_tsv.x < bx_start || failed_tsv.x > bx_end) continue;

        for (uint32_t bj = 0; bj < blocks_per_row; ++bj) {
            int32_t by_start = static_cast<int32_t>(stride * bj);
            int32_t by_end = by_start + static_cast<int32_t>(stride);
            if (failed_tsv.y < by_start || failed_tsv.y > by_end) continue;

            // failed_tsv belongs to block (bi,bj), search its spares
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

// Fixed probability maps, independent of already failed channels.
void Grid::build_probability_maps(std::mt19937& hotspot_rng) {
    hotspot_centers_.clear();
    if (failure_model_ == FailureModel::HOTSPOT) {
        if (!std::isfinite(hotspot_params_.radius) || hotspot_params_.radius <= 0.0 ||
            !std::isfinite(hotspot_params_.multiplier) || hotspot_params_.multiplier < 1.0)
            throw std::invalid_argument("hotspot radius must be finite and >0, multiplier finite and >=1");
        hotspot_centers_ = hotspot_params_.centers;
        for (const Point& p : hotspot_centers_)
            if (!is_valid_channel(p.x, p.y, p.z))
                throw std::invalid_argument("hotspot center must identify an existing lower-layer vertical channel");
        if (hotspot_centers_.empty()) {
            std::uniform_int_distribution<int32_t> xy(0, static_cast<int32_t>(grid_size_) - 1);
            std::uniform_int_distribution<int32_t> z(0, static_cast<int32_t>(num_layers_) - 2);
            for (uint32_t i = 0; i < hotspot_params_.count; ++i) {
                // Explicit order avoids compiler-dependent function argument evaluation.
                const int32_t cx = xy(hotspot_rng);
                const int32_t cy = xy(hotspot_rng);
                const int32_t cz = z(hotspot_rng);
                hotspot_centers_.emplace_back(cx, cy, cz);
            }
        }
    }
    const size_t channel_count = get_total_tsvs();
    std::vector<double> weights(channel_count, 1.0);
    size_t index = 0;
    for (uint32_t z = 0; z + 1 < num_layers_; ++z) {
        for (uint32_t y = 0; y < grid_size_; ++y) {
            for (uint32_t x = 0; x < grid_size_; ++x, ++index) {
                double influence = 0.0;
                for (const Point& center : hotspot_centers_) {
                    if (center.z != static_cast<int32_t>(z)) continue;
                    const double dx = (static_cast<double>(x) - center.x) / hotspot_params_.radius;
                    const double dy = (static_cast<double>(y) - center.y) / hotspot_params_.radius;
                    const double gaussian = std::exp(-0.5 * (dx * dx + dy * dy));
                    influence = std::max(influence, gaussian);
                }
                weights[index] = 1.0 + (hotspot_params_.multiplier - 1.0) * influence;
            }
        }
    }
    const double initial_p = config_.get_initial_failure_rate();
    const double runtime_p = config_.get_runtime_failure_rate();
    if (!std::isfinite(initial_p) || !std::isfinite(runtime_p) || initial_p < 0.0 || runtime_p < 0.0)
        throw std::invalid_argument("failure probabilities must be finite and nonnegative");
    const auto initial = normalize_weights(weights, initial_p);
    const auto runtime = normalize_weights(weights, runtime_p);
    initial_probabilities_.assign(num_layers_ - 1,
        std::vector<std::vector<double>>(grid_size_, std::vector<double>(grid_size_, 0.0)));
    runtime_probabilities_ = initial_probabilities_;
    index = 0;
    for (uint32_t z = 0; z + 1 < num_layers_; ++z)
        for (uint32_t y = 0; y < grid_size_; ++y)
            for (uint32_t x = 0; x < grid_size_; ++x, ++index) {
                initial_probabilities_[z][y][x] = initial[index];
                runtime_probabilities_[z][y][x] = runtime[index];
            }
}

double Grid::get_failure_probability(int32_t x, int32_t y, int32_t lower_z, bool runtime) const {
    if (!is_valid_channel(x, y, lower_z)) return 0.0;
    const auto& map = get_probability_map(runtime);
    if (map.empty()) return 0.0;
    return map[static_cast<size_t>(lower_z)][static_cast<size_t>(y)][static_cast<size_t>(x)];
}

void Grid::apply_initial_failures(std::mt19937& rng) {
    for (uint32_t z = 0; z + 1 < num_layers_; ++z)
        for (uint32_t y = 0; y < grid_size_; ++y)
            for (uint32_t x = 0; x < grid_size_; ++x) {
                const double p = initial_probabilities_[z][y][x];
                if (p >= 1.0 || (p > 0.0 && std::generate_canonical<double, 53>(rng) < p))
                    grid_[z][y][x].set_failed(true);
            }
}

void Grid::schedule_runtime_failures(std::mt19937& runtime_rng) {
    std::vector<FailureEvent> events;
    events.reserve(get_total_tsvs());
    for (uint32_t z = 0; z + 1 < num_layers_; ++z)
        for (uint32_t y = 0; y < grid_size_; ++y)
            for (uint32_t x = 0; x < grid_size_; ++x) {
                if (grid_[z][y][x].is_failed()) continue;
                const double p = runtime_probabilities_[z][y][x];
                if (p <= 0.0) continue;
                uint64_t lifetime = 0;
                if (p < 1.0) {
                    // Geometric support is 0,1,...: cycle 0 is the first runtime trial.
                    const long double u = std::generate_canonical<double, 53>(runtime_rng);
                    const long double sampled = std::floor(std::log1p(-u) /
                        std::log1p(-static_cast<long double>(p)));
                    if (!std::isfinite(sampled) || sampled >= static_cast<long double>(std::numeric_limits<uint64_t>::max()))
                        continue; // Failure lies beyond every representable simulation horizon.
                    lifetime = static_cast<uint64_t>(sampled);
                }
                events.push_back({lifetime, Point{static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)}});
            }
    runtime_events_ = decltype(runtime_events_)(LaterFailure{}, std::move(events));
}

std::vector<Point> Grid::apply_runtime_failure(uint64_t current_cycle) {
    std::vector<Point> failed;
    while (!runtime_events_.empty() && runtime_events_.top().cycle <= current_cycle) {
        const Point p = runtime_events_.top().channel;
        runtime_events_.pop();
        TSV& channel = grid_[static_cast<size_t>(p.z)][static_cast<size_t>(p.y)][static_cast<size_t>(p.x)];
        if (channel.is_failed()) continue;
        channel.set_failed(true);
        failed.push_back(p);
    }
    return failed;
}

void Grid::apply_runtime_failure(std::mt19937& rng) {
    // Compatibility: randomness is consumed once when lifetimes are scheduled.
    (void)rng;
    apply_runtime_failure(legacy_runtime_cycle_++);
}

// ===== Original Methods =====

TSV* Grid::get_tsv(int32_t x, int32_t y, int32_t z) {
    if (!is_valid_position(x, y, z)) return nullptr;
    return &grid_[static_cast<size_t>(z)][static_cast<size_t>(y)][static_cast<size_t>(x)];
}

const TSV* Grid::get_tsv(int32_t x, int32_t y, int32_t z) const {
    if (!is_valid_position(x, y, z)) return nullptr;
    return &grid_[static_cast<size_t>(z)][static_cast<size_t>(y)][static_cast<size_t>(x)];
}

bool Grid::is_valid_channel(int32_t x, int32_t y, int32_t lower_z) const {
    return is_valid_position(x, y, lower_z) && lower_z + 1 < static_cast<int32_t>(num_layers_);
}

TSV* Grid::get_vertical_channel(const Point& from, const Point& to) {
    if (!is_valid_position(from.x, from.y, from.z) || !is_valid_position(to.x, to.y, to.z) ||
        from.x != to.x || from.y != to.y || std::abs(from.z - to.z) != 1) return nullptr;
    return get_tsv(from.x, from.y, std::min(from.z, to.z));
}

const TSV* Grid::get_vertical_channel(const Point& from, const Point& to) const {
    if (!is_valid_position(from.x, from.y, from.z) || !is_valid_position(to.x, to.y, to.z) ||
        from.x != to.x || from.y != to.y || std::abs(from.z - to.z) != 1) return nullptr;
    return get_tsv(from.x, from.y, std::min(from.z, to.z));
}

bool Grid::is_valid_position(int32_t x, int32_t y, int32_t z) const {
    return x >= 0 && x < static_cast<int32_t>(grid_size_) &&
           y >= 0 && y < static_cast<int32_t>(grid_size_) &&
           z >= 0 && z < static_cast<int32_t>(num_layers_);
}

std::optional<Point> Grid::get_nearest_redundant_tsv(int32_t x, int32_t y, int32_t z, uint64_t current_time) const {
    if (!is_valid_channel(x, y, z)) return std::nullopt;

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
    for (uint32_t z = 0; z + 1 < num_layers_; ++z) {
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
            // Four corners of each 4x4 region are redundant TSVs
            int32_t mod_x = x % 4;
            int32_t mod_y = y % 4;
            return (mod_x == 0 || mod_x == 3) && (mod_y == 0 || mod_y == 3);
        }
        case RedundancyLayout::SHARED_SPARE: {
            // Overlapping blocks, stride = grid_factor - 1, spare at (1+stride*i, 1+stride*j)
            uint32_t gf = config_.get_grid_factor();
            if (gf <= 1) return false;
            uint32_t stride = gf - 1;
            return (x >= 1) && (y >= 1) && ((x - 1) % static_cast<int32_t>(stride) == 0) && ((y - 1) % static_cast<int32_t>(stride) == 0);
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
