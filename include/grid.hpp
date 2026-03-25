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

// Spare site (with covered region information)
struct SpareSite {
    Point position;
    std::vector<uint32_t> covered_region_ids;
};

// TSV grid management class
class Grid {
public:
    explicit Grid(const Config& config);

    // Initialize grid
    void initialize(std::mt19937& rng);

    // Get TSV
    TSV* get_tsv(int32_t x, int32_t y, int32_t z);
    const TSV* get_tsv(int32_t x, int32_t y, int32_t z) const;

    // Check if coordinates are valid
    bool is_valid_position(int32_t x, int32_t y, int32_t z) const;

    // Get nearest redundant TSV (legacy interface, used by LegacyCorner4)
    std::optional<Point> get_nearest_redundant_tsv(int32_t x, int32_t y, int32_t z, uint64_t current_time) const;

    // Unified bypass spare search (dispatches based on redundancy layout)
    std::optional<Point> find_bypass_spare(Point failed_tsv, uint64_t current_time) const;

    // Get region ID
    uint32_t get_region_id(int32_t x, int32_t y) const;

    // Failure handling
    void apply_initial_failures(std::mt19937& rng);
    void apply_runtime_failure(std::mt19937& rng);

    // Statistics
    uint32_t count_failed_tsvs() const;
    uint32_t count_failed_redundant_tsvs() const;
    uint32_t get_total_tsvs() const { return grid_size_ * grid_size_ * num_layers_; }
    uint32_t get_total_redundant_tsvs() const;

    // Getters
    uint32_t get_grid_size() const { return grid_size_; }
    uint32_t get_num_layers() const { return num_layers_; }
    RedundancyLayout get_redundancy_layout() const { return redundancy_layout_; }
    const std::vector<SpareSite>& get_spare_sites() const { return spare_sites_; }

    // Static redundant position check (independent of Grid instance, used by RequestGenerator)
    static bool is_redundant_position_static(int32_t x, int32_t y, RedundancyLayout layout, uint32_t gridFactor = 4) {
        switch (layout) {
            case RedundancyLayout::LEGACY_CORNER4: {
                int32_t mod_x = x % 4;
                int32_t mod_y = y % 4;
                return (mod_x == 0 || mod_x == 3) && (mod_y == 0 || mod_y == 3);
            }
            case RedundancyLayout::SHARED_SPARE: {
                if (gridFactor <= 1) return false;
                int32_t stride = static_cast<int32_t>(gridFactor - 1);
                return (x >= 1) && (y >= 1) && ((x - 1) % stride == 0) && ((y - 1) % stride == 0);
            }
            case RedundancyLayout::NONE:
                return false;
        }
        return false;
    }

    // Bypass diagnostic counters
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

    // 3D TSV grid [z][y][x]
    std::vector<std::vector<std::vector<TSV>>> grid_;

    // Redundant TSV position list (used by LegacyCorner4 legacy interface)
    std::vector<Point> redundant_positions_;

    // Coverage model data (shared by SharedSpare and LegacyCorner4)
    std::vector<SpareSite> spare_sites_;
    std::vector<std::vector<uint32_t>> region_to_spares_;

    // Bypass diagnostic counters (mutable to allow modification in const methods)
    mutable uint64_t failed_vertical_encounters_;
    mutable uint64_t spare_found_;
    mutable uint64_t spare_unavailable_;

    // Redundancy layout construction
    void build_spare_sites();
    void build_legacy_corner4_sites();
    void build_shared_spare_sites();

    // Coverage range search (used by SharedSpare)
    std::optional<Point> select_spare(Point failed_tsv, uint64_t current_time) const;

    // Mean-preserving failure sampler
    std::vector<Point> sample_failures(std::mt19937& rng, bool runtime);

    // Helper functions
    bool is_redundant_position(int32_t x, int32_t y) const;
    double calculate_distance(int32_t x1, int32_t y1, int32_t x2, int32_t y2) const;

    // Internal recording methods (const to allow calling from const methods)
    void record_failed_vertical_encounter() const { ++failed_vertical_encounters_; }
    void record_spare_found() const { ++spare_found_; }
    void record_spare_unavailable() const { ++spare_unavailable_; }
};

} // namespace tsvra

#endif // TSVRA_GRID_HPP
