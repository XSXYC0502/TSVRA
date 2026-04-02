#ifndef TSVRA_CONFIG_HPP
#define TSVRA_CONFIG_HPP

#include <cstdint>
#include <string>
#include <cmath>

namespace tsvra {

// Redundancy layout enum
enum class RedundancyLayout {
    LEGACY_CORNER4,   // Legacy: 4 corner redundant TSVs per 4×4 region, global scan
    SHARED_SPARE,     // Literature-correct: 1 shared spare TSV per region, coverage-range search
    NONE              // No redundancy (baseline comparison)
};

// Failure distribution model enum
enum class FailureModel {
    UNIFORM,          // Uniform i.i.d. Bernoulli (equivalent to cluster_strength=0)
    CLUSTERED         // Spatial clustering (with continuous parameters strength and radius)
};

// Clustering parameters
struct ClusterParams {
    double strength = 0.8;    // [0.0, 1.0]: 0=uniform, 1=maximum clustering
    uint32_t radius = 4;      // ≥1: Manhattan distance clustering kernel radius
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
    uint64_t get_vertical_delay() const { return vertical_delay_; }
    uint64_t get_horizontal_delay() const { return horizontal_delay_; }
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
    bool get_print_defaults() const { return print_defaults_; }

    // Setters
    void set_num_layers(uint32_t n) { num_layers_ = n; }
    void set_grid_factor(uint32_t n) { grid_factor_ = n; }
    void set_failure_mode(FailureMode mode) { failure_mode_ = mode; }
    void set_failure_rate(double rate) { failure_rate_ = rate; }
    void set_vertical_delay(uint64_t delay) { vertical_delay_ = delay; }
    void set_horizontal_delay(uint64_t delay) { horizontal_delay_ = delay; }
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
    void set_cluster_params(const ClusterParams& v) { cluster_params_ = v; }

private:
    uint32_t num_layers_;           // Number of layers, default 4 (literature optimal)
    uint32_t grid_factor_;          // N = 4 * grid_factor, default 4
    FailureMode failure_mode_;      // Failure mode
    double failure_rate_;           // TSV failure rate, default 1e-5 (literature calibrated)
    uint64_t vertical_delay_;       // Vertical transmission delay, default 5 (literature calibrated)
    uint64_t horizontal_delay_;     // Horizontal transmission delay, default 500 (literature calibrated)
    uint64_t simulation_cycles_;    // Number of simulation cycles, default 100000 (literature minimum requirement)
    std::string output_prefix_;     // Output file prefix, default "tsvra_output"
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
    bool print_defaults_;           // --print-defaults flag

    // Helper functions
    FailureMode parse_failure_mode(const std::string& mode_str) const;
    std::string failure_mode_to_string(FailureMode mode) const;
    RedundancyLayout parse_redundancy_layout(const std::string& str) const;
    std::string redundancy_layout_to_string(RedundancyLayout layout) const;
    FailureModel parse_failure_model(const std::string& str) const;
    std::string failure_model_to_string(FailureModel model) const;
};

} // namespace tsvra

#endif // TSVRA_CONFIG_HPP
