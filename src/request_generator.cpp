#include "request_generator.hpp"
#include "grid.hpp"
#include <algorithm>

namespace tsvra {

RequestGenerator::RequestGenerator(const Config& config, std::mt19937& rng)
    : config_(config),
      rng_(rng),
      grid_size_(config.get_grid_size()),
      num_layers_(config.get_num_layers()),
      num_regions_(config.get_grid_size() / 4),
      next_request_id_(0),
      total_weight_(0.0),
      uniform_dist_(0.0, 1.0),
      layer_dist_(0, config.get_num_layers() - 2) {
}

void RequestGenerator::initialize_hotspots() {
    // Initialize hotspot weight matrix
    hotspot_weights_.resize(num_regions_);
    for (uint32_t y = 0; y < num_regions_; ++y) {
        hotspot_weights_[y].resize(num_regions_);
        for (uint32_t x = 0; x < num_regions_; ++x) {
            // Use uniform distribution to generate heat values [0.5, 1.5]
            hotspot_weights_[y][x] = 0.5 + uniform_dist_(rng_);
        }
    }
    
    build_cumulative_distribution();
}

void RequestGenerator::build_cumulative_distribution() {
    cumulative_weights_.clear();
    total_weight_ = 0.0;
    
    for (uint32_t y = 0; y < num_regions_; ++y) {
        for (uint32_t x = 0; x < num_regions_; ++x) {
            total_weight_ += hotspot_weights_[y][x];
            cumulative_weights_.push_back(total_weight_);
        }
    }
}

std::optional<Request> RequestGenerator::generate_request(uint64_t current_time) {
    // Generate request with a certain probability
    // On average, generate 1 request every 2 cycles
    double request_probability = 0.5;
    if (uniform_dist_(rng_) > request_probability) {
        return std::nullopt;
    }
    
    // Generate start and end points
    Point start = sample_start_position();
    Point end = sample_end_position();
    
    // Create request
    Request req(next_request_id_++, start, end, current_time);
    return req;
}

double RequestGenerator::get_hotspot_weight(uint32_t region_x, uint32_t region_y) const {
    if (region_x >= num_regions_ || region_y >= num_regions_) {
        return 0.0;
    }
    return hotspot_weights_[region_y][region_x];
}

Point RequestGenerator::sample_position_from_region(uint32_t region_x, uint32_t region_y, int32_t z) {
    // Randomly select a non-redundant TSV within the 4x4 region
    std::uniform_int_distribution<int32_t> offset_dist(0, 3);

    int32_t base_x = static_cast<int32_t>(region_x * 4);
    int32_t base_y = static_cast<int32_t>(region_y * 4);

    // Resample until a non-redundant position is found (each 4x4 region always has non-redundant cells)
    RedundancyLayout layout = config_.get_redundancy_layout();
    for (int attempt = 0; attempt < 100; ++attempt) {
        int32_t x = base_x + offset_dist(rng_);
        int32_t y = base_y + offset_dist(rng_);
        if (!Grid::is_redundant_position_static(x, y, layout, config_.get_grid_factor())) {
            return Point(x, y, z);
        }
    }
    // Should not reach here, but safe fallback
    return Point(base_x + 1, base_y + 1, z);
}

Point RequestGenerator::sample_start_position() {
    // Sample region based on hotspot weights
    double rand_val = uniform_dist_(rng_) * total_weight_;

    auto it = std::lower_bound(cumulative_weights_.begin(), cumulative_weights_.end(), rand_val);
    size_t region_idx = static_cast<size_t>(it - cumulative_weights_.begin());

    uint32_t region_x = static_cast<uint32_t>(region_idx % num_regions_);
    uint32_t region_y = static_cast<uint32_t>(region_idx / num_regions_);

    int32_t z = static_cast<int32_t>(layer_dist_(rng_));
    
    return sample_position_from_region(region_x, region_y, z);
}

Point RequestGenerator::sample_end_position() {
    // End point is always on the outermost layer (maxZ)
    int32_t max_z = static_cast<int32_t>(num_layers_ - 1);
    
    // Sample region based on hotspot weights
    double rand_val = uniform_dist_(rng_) * total_weight_;

    auto it = std::lower_bound(cumulative_weights_.begin(), cumulative_weights_.end(), rand_val);
    size_t region_idx = static_cast<size_t>(it - cumulative_weights_.begin());

    uint32_t region_x = static_cast<uint32_t>(region_idx % num_regions_);
    uint32_t region_y = static_cast<uint32_t>(region_idx / num_regions_);
    
    return sample_position_from_region(region_x, region_y, max_z);
}

} // namespace tsvra
