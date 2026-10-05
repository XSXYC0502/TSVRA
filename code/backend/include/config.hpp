#ifndef TSVRA_CONFIG_HPP
#define TSVRA_CONFIG_HPP

#include <cstdint>
#include <string>
#include <cmath>
#include <vector>
#include "request.hpp"

namespace tsvra {

// Redundancy layout enum
enum class RedundancyLayout {
    LEGACY_CORNER4,   // Legacy: 4 corner redundant TSVs per 4×4 region, global scan
    SHARED_SPARE,     // Literature-correct: 1 shared spare TSV per region, coverage-range search
    NONE              // No redundancy (baseline comparison)
};

// Failure distribution model enum
enum class FailureModel {
    UNIFORM,
    HOTSPOT,          // Fixed, interface-local Gaussian probability map
    CLUSTERED = HOTSPOT // Legacy spelling retained for source compatibility
};

// Clustering parameters
struct ClusterParams {
    double strength = 0.8;    // [0.0, 1.0]: 0=uniform, 1=maximum clustering
    uint32_t radius = 4;      // ≥1: Manhattan distance clustering kernel radius
};

struct HotspotParams {
    uint32_t count = 1;
    double radius = 4.0;      // Gaussian sigma, in grid cells
    double multiplier = 5.0;  // Peak relative weight before mean normalization
    std::vector<Point> centers; // Empty selects seeded random centers
};

// Failure mode enum
enum class FailureMode {
    INITIAL_ONLY,      // a: Failures only at initialization
    RUNTIME_ONLY,      // b: Failures only at runtime
    COMBINED           // c: Initialization + runtime failures
};

// Configuration class
class Config {
public:
    Config();

    // Load from configuration file
    bool load_from_file(const std::string& filename);

    // Parse from command-line arguments
    bool parse_command_line(int argc, char* argv[]);

    // Validate configuration
    bool validate() const;

    // Print configuration info
    void print() const;

    // Output configuration in JSON format (used for --print-defaults and init message)
    std::string to_json() const;

    // Getters
    uint32_t get_num_layers() const { return num_layers_; }
    uint32_t get_grid_factor() const { return grid_factor_; }
    uint32_t get_grid_size() const { return 4 * grid_factor_; }
    FailureMode get_failure_mode() const { return failure_mode_; }
    double get_failure_rate() const { return failure_rate_; }
    double get_initial_failure_rate() const { return initial_failure_rate_; }
    double get_runtime_failure_rate() const { return runtime_failure_rate_; }
    uint64_t get_vertical_delay() const { return vertical_delay_; }
    uint64_t get_horizontal_delay() const { return horizontal_delay_; }
    uint64_t get_router_delay() const { return router_delay_; }
    double get_request_probability() const { return request_probability_; }
    const std::string& get_power_config() const { return power_config_; }
    uint64_t get_simulation_cycles() const { return simulation_cycles_; }
    const std::string& get_output_prefix() const { return output_prefix_; }
    uint32_t get_random_seed() const { return random_seed_; }
    double get_lambda1() const { return lambda1_; }
    double get_lambda2() const { return lambda2_; }
    uint32_t get_max_path_length() const { return max_path_length_; }
    uint32_t get_max_horizontal_distance() const { return max_horizontal_distance_; }
    double get_reliability_min() const { return reliability_min_; }
    bool get_json_stream() const { return json_stream_; }
    RedundancyLayout get_redundancy_layout() const { return redundancy_layout_; }
    FailureModel get_failure_model() const { return failure_model_; }
    const ClusterParams& get_cluster_params() const { return cluster_params_; }
    const HotspotParams& get_hotspot_params() const { return hotspot_params_; }
    uint32_t get_hotspot_count() const { return hotspot_params_.count; }
    double get_hotspot_radius() const { return hotspot_params_.radius; }
    double get_hotspot_multiplier() const { return hotspot_params_.multiplier; }
    const std::vector<Point>& get_hotspot_centers() const { return hotspot_params_.centers; }
    bool get_print_defaults() const { return print_defaults_; }

    // Setters
    void set_num_layers(uint32_t n) { num_layers_ = n; }
    void set_grid_factor(uint32_t n) { grid_factor_ = n; }
    void set_failure_mode(FailureMode mode) { failure_mode_ = mode; }
    void set_failure_rate(double rate) {
        failure_rate_ = rate;
        if (!initial_failure_rate_explicit_) initial_failure_rate_ = rate;
        if (!runtime_failure_rate_explicit_) runtime_failure_rate_ = rate;
    }
    void set_initial_failure_rate(double rate) {
        initial_failure_rate_ = rate;
        initial_failure_rate_explicit_ = true;
    }
    void set_runtime_failure_rate(double rate) {
        runtime_failure_rate_ = rate;
        runtime_failure_rate_explicit_ = true;
    }
    void set_vertical_delay(uint64_t delay) { vertical_delay_ = delay; }
    void set_horizontal_delay(uint64_t delay) { horizontal_delay_ = delay; }
    void set_router_delay(uint64_t delay) { router_delay_ = delay; }
    void set_request_probability(double p) { request_probability_ = p; }
    void set_power_config(const std::string& path) { power_config_ = path; }
    void set_simulation_cycles(uint64_t cycles) { simulation_cycles_ = cycles; }
    void set_output_prefix(const std::string& prefix) { output_prefix_ = prefix; }
    void set_random_seed(uint32_t seed) { random_seed_ = seed; }
    void set_lambda1(double v) { lambda1_ = v; }
    void set_lambda2(double v) { lambda2_ = v; }
    void set_max_path_length(uint32_t v) { max_path_length_ = v; }
    void set_max_horizontal_distance(uint32_t v) { max_horizontal_distance_ = v; }
    void set_reliability_min(double v) { reliability_min_ = v; }
    void set_json_stream(bool v) { json_stream_ = v; }
    void set_redundancy_layout(RedundancyLayout v) { redundancy_layout_ = v; }
    void set_failure_model(FailureModel v) { failure_model_ = v; }
    void set_cluster_params(const ClusterParams& v) {
        cluster_params_ = v;
        hotspot_params_.radius = static_cast<double>(v.radius);
        hotspot_params_.multiplier = 1.0 + 5.0 * v.strength;
    }
    void set_hotspot_params(const HotspotParams& v) { hotspot_params_ = v; }
    void set_hotspot_count(uint32_t v) { hotspot_params_.count = v; }
    void set_hotspot_radius(double v) { hotspot_params_.radius = v; }
    void set_hotspot_multiplier(double v) { hotspot_params_.multiplier = v; }
    void set_hotspot_centers(const std::vector<Point>& v) { hotspot_params_.centers = v; }

private:
    uint32_t num_layers_;           // Number of layers, default 4 (literature optimal)
    uint32_t grid_factor_;          // N = 4 * grid_factor, default 4
    FailureMode failure_mode_;      // Failure mode
    double failure_rate_;           // TSV failure rate, default 1e-5 (literature calibrated)
    double initial_failure_rate_;
    double runtime_failure_rate_;
    bool initial_failure_rate_explicit_;
    bool runtime_failure_rate_explicit_;
    uint64_t vertical_delay_;       // Vertical transmission delay, default 5 (literature calibrated)
    uint64_t horizontal_delay_;     // Horizontal transmission delay, default 500 (literature calibrated)
    uint64_t router_delay_;         // Router processing delay per actual hop, default 15 cycles
    double request_probability_;
    std::string power_config_;
    uint64_t simulation_cycles_;    // Number of simulation cycles, default 100000 (literature minimum requirement)
    std::string output_prefix_;     // Output file prefix, default "code/output/tsvra_output"
    uint32_t random_seed_;          // Random seed, 0 means use random device
    double lambda1_;                // Congestion weight (λ1), default 0.0
    double lambda2_;                // Risk weight (λ2), default 0.0
    uint32_t max_path_length_;      // Maximum path length (L_max), 0 means unlimited
    uint32_t max_horizontal_distance_; // Maximum horizontal distance (H_max), 0 means unlimited
    double reliability_min_;        // Minimum path reliability (R_min), 0.0 means unconstrained
    bool json_stream_;              // JSON streaming output mode
    RedundancyLayout redundancy_layout_; // Redundancy layout strategy
    FailureModel failure_model_;    // Failure distribution model
    ClusterParams cluster_params_;  // Clustering parameters
    HotspotParams hotspot_params_;
    bool print_defaults_;           // --print-defaults flag

    // Helper functions
    FailureMode parse_failure_mode(const std::string& mode_str) const;
    std::string failure_mode_to_string(FailureMode mode) const;
    RedundancyLayout parse_redundancy_layout(const std::string& str) const;
    std::string redundancy_layout_to_string(RedundancyLayout layout) const;
    FailureModel parse_failure_model(const std::string& str) const;
    std::string failure_model_to_string(FailureModel model) const;
    void set_option(const std::string& key, const std::string& value);
};

} // namespace tsvra

#endif // TSVRA_CONFIG_HPP
