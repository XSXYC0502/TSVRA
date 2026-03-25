#ifndef TSVRA_REQUEST_GENERATOR_HPP
#define TSVRA_REQUEST_GENERATOR_HPP

#include "request.hpp"
#include "config.hpp"
#include <random>
#include <vector>
#include <optional>

namespace tsvra {

// Request generator class
class RequestGenerator {
public:
    explicit RequestGenerator(const Config& config, std::mt19937& rng);
    
    // Initialize hotspot regions
    void initialize_hotspots();
    
    // Generate request (called every clock cycle)
    std::optional<Request> generate_request(uint64_t current_time);
    
    // Get hotspot weight (for statistics)
    double get_hotspot_weight(uint32_t region_x, uint32_t region_y) const;
    
    // Get number of regions
    uint32_t get_num_regions() const { return num_regions_; }
    
private:
    const Config& config_;
    std::mt19937& rng_;
    uint32_t grid_size_;        // Grid size
    uint32_t num_layers_;       // Number of layers
    uint32_t num_regions_;      // Number of hotspot regions (per side)
    uint64_t next_request_id_;  // Next request ID
    
    // Hotspot weights [region_y][region_x]
    // Each 4x4 TSV block is a basic region
    std::vector<std::vector<double>> hotspot_weights_;
    
    // Cumulative distribution (for fast sampling)
    std::vector<double> cumulative_weights_;
    double total_weight_;
    
    // Random number distributions
    std::uniform_real_distribution<double> uniform_dist_;
    std::uniform_int_distribution<uint32_t> layer_dist_;
    
    // Helper functions
    void build_cumulative_distribution();
    Point sample_position_from_region(uint32_t region_x, uint32_t region_y, int32_t z);
    Point sample_start_position();
    Point sample_end_position();
};

} // namespace tsvra

#endif // TSVRA_REQUEST_GENERATOR_HPP

