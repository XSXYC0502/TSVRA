#ifndef TSVRA_STATISTICS_HPP
#define TSVRA_STATISTICS_HPP

#include "request.hpp"
#include "config.hpp"
#include "grid.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace tsvra {

// Statistics class
class Statistics {
public:
    explicit Statistics(const Config& config);
    
    // Record requests
    void record_request_generated();
    void record_request_completed(const Request& req);
    void record_request_failed(const Request& req);
    
    // Record TSV usage
    void record_redundant_tsv_usage();

    // Record bypass diagnostics
    void record_failed_vertical_encounter();
    void record_spare_found();
    void record_spare_unavailable();

    // Directly set bypass diagnostic counters (used when bulk-copying from Grid)
    void set_failed_vertical_encounters(uint64_t v) { failed_vertical_encounters_ = v; }
    void set_spare_found(uint64_t v) { spare_found_ = v; }
    void set_spare_unavailable(uint64_t v) { spare_unavailable_ = v; }

    // Record in-flight requests (at simulation end)
    void record_in_flight(uint64_t count);

    // Update failure statistics
    void update_failure_stats(const Grid& grid);
    
    // Output statistics
    void print_summary() const;
    void export_to_csv(const std::string& filename) const;
    void export_requests_to_csv(const std::string& filename) const;
    
    // Get statistics data
    uint64_t get_total_requests() const { return total_requests_; }
    uint64_t get_completed_requests() const { return completed_requests_; }
    uint64_t get_failed_requests() const { return failed_requests_; }
    double get_average_latency() const;
    double get_average_horizontal_distance() const;
    uint64_t get_max_latency() const { return max_latency_; }
    uint64_t get_min_latency() const { return min_latency_; }
    uint64_t get_total_latency() const { return total_latency_; }
    uint64_t get_total_horizontal_distance() const { return total_horizontal_distance_; }
    uint64_t get_redundant_tsv_usages() const { return redundant_tsv_usages_; }
    uint64_t get_failed_vertical_encounters() const { return failed_vertical_encounters_; }
    uint64_t get_spare_found() const { return spare_found_; }
    uint64_t get_spare_unavailable() const { return spare_unavailable_; }
    uint64_t get_in_flight() const { return in_flight_; }
    uint32_t get_current_failed_tsvs() const { return current_failed_tsvs_; }
    uint32_t get_current_failed_redundant_tsvs() const { return current_failed_redundant_tsvs_; }
    const std::vector<Request>& get_completed_requests_list() const { return completed_requests_list_; }
    const std::vector<Request>& get_failed_requests_list() const { return failed_requests_list_; }

private:
    const Config& config_;
    
    // Request statistics
    uint64_t total_requests_;
    uint64_t completed_requests_;
    uint64_t failed_requests_;
    
    // Latency statistics
    uint64_t total_latency_;
    uint64_t max_latency_;
    uint64_t min_latency_;
    std::vector<uint64_t> latency_samples_;
    uint64_t total_horizontal_distance_;
    
    // TSV statistics
    uint64_t redundant_tsv_usages_;
    uint64_t failed_vertical_encounters_;
    uint64_t spare_found_;
    uint64_t spare_unavailable_;
    uint64_t in_flight_;
    uint32_t current_failed_tsvs_;
    uint32_t current_failed_redundant_tsvs_;
    
    // Request records (for export)
    std::vector<Request> completed_requests_list_;
    std::vector<Request> failed_requests_list_;
};

} // namespace tsvra

#endif // TSVRA_STATISTICS_HPP
