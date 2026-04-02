#include "statistics.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <limits>

namespace tsvra {

Statistics::Statistics(const Config& config)
    : config_(config),
      total_requests_(0),
      completed_requests_(0),
      failed_requests_(0),
      total_latency_(0),
      max_latency_(0),
      min_latency_(std::numeric_limits<uint64_t>::max()),
      total_horizontal_distance_(0),
      redundant_tsv_usages_(0),
      failed_vertical_encounters_(0),
      spare_found_(0),
      spare_unavailable_(0),
      in_flight_(0),
      current_failed_tsvs_(0),
      current_failed_redundant_tsvs_(0) {
}

void Statistics::record_request_generated() {
    ++total_requests_;
}

void Statistics::record_request_completed(const Request& req) {
    ++completed_requests_;
    
    uint64_t latency = req.get_latency();
    total_latency_ += latency;
    total_horizontal_distance_ += req.get_horizontal_distance();
    max_latency_ = std::max(max_latency_, latency);
    min_latency_ = std::min(min_latency_, latency);
    
    latency_samples_.push_back(latency);
    completed_requests_list_.push_back(req);
}

void Statistics::record_request_failed(const Request& req) {
    ++failed_requests_;
    total_horizontal_distance_ += req.get_horizontal_distance();
    failed_requests_list_.push_back(req);
}

void Statistics::record_redundant_tsv_usage() {
    ++redundant_tsv_usages_;
}

void Statistics::record_failed_vertical_encounter() {
    ++failed_vertical_encounters_;
}

void Statistics::record_spare_found() {
    ++spare_found_;
}

void Statistics::record_spare_unavailable() {
    ++spare_unavailable_;
}

void Statistics::record_in_flight(uint64_t count) {
    in_flight_ = count;
}

void Statistics::update_failure_stats(const Grid& grid) {
    current_failed_tsvs_ = grid.count_failed_tsvs();
    current_failed_redundant_tsvs_ = grid.count_failed_redundant_tsvs();
}

double Statistics::get_average_latency() const {
    if (completed_requests_ == 0) {
        return 0.0;
    }
    return static_cast<double>(total_latency_) / static_cast<double>(completed_requests_);
}

double Statistics::get_average_horizontal_distance() const {
    uint64_t terminal_requests = completed_requests_ + failed_requests_;
    if (terminal_requests == 0) {
        return 0.0;
    }
    return static_cast<double>(total_horizontal_distance_) / static_cast<double>(terminal_requests);
}

void Statistics::print_summary() const {
    std::cout << "\n=================================\n";
    std::cout << "    Simulation Statistics\n";
    std::cout << "=================================\n\n";
    
    // Request statistics
    std::cout << "Request Statistics:\n";
    std::cout << "  Total requests:     " << total_requests_ << "\n";
    std::cout << "  Completed requests: " << completed_requests_ << "\n";
    std::cout << "  Failed requests:    " << failed_requests_ << "\n";
    
    if (total_requests_ > 0) {
        double success_rate = static_cast<double>(completed_requests_) * 100.0 / static_cast<double>(total_requests_);
        std::cout << "  Success rate:       " << std::fixed << std::setprecision(2) << success_rate << "%\n";
    }
    
    std::cout << "\nHorizontal Distance Statistics:\n";
    if (completed_requests_ + failed_requests_ > 0) {
        std::cout << "  Average horizontal distance: "
                  << std::fixed << std::setprecision(2)
                  << get_average_horizontal_distance() << " hops\n";
    } else {
        std::cout << "  No terminal requests\n";
    }
    
    // TSV statistics
    std::cout << "\nTSV Statistics:\n";
    std::cout << "  Failed TSVs:            " << current_failed_tsvs_ << "\n";
    std::cout << "  Failed redundant TSVs:  " << current_failed_redundant_tsvs_ << "\n";
    std::cout << "  Redundant TSV usages:   " << redundant_tsv_usages_ << "\n";
    
    std::cout << "\n=================================\n\n";
}

void Statistics::export_to_csv(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to create file: " << filename << std::endl;
        return;
    }

    file << "Metric,Value\n";
    file << "Total Requests," << total_requests_ << "\n";
    file << "Completed Requests," << completed_requests_ << "\n";
    file << "Failed Requests," << failed_requests_ << "\n";
    
    if (total_requests_ > 0) {
        double success_rate = static_cast<double>(completed_requests_) * 100.0 / static_cast<double>(total_requests_);
        file << "Success Rate (%)," << success_rate << "\n";
    }
    
    if (completed_requests_ > 0) {
        file << "Average Latency," << get_average_latency() << "\n";
        file << "Max Latency," << max_latency_ << "\n";
        file << "Min Latency," << min_latency_ << "\n";
    }
    file << "Average Horizontal Distance," << get_average_horizontal_distance() << "\n";
    file << "Total Horizontal Distance," << total_horizontal_distance_ << "\n";
    
    file << "Failed TSVs," << current_failed_tsvs_ << "\n";
    file << "Failed Redundant TSVs," << current_failed_redundant_tsvs_ << "\n";
    file << "Redundant TSV Usages," << redundant_tsv_usages_ << "\n";
    file << "Failed Vertical Encounters," << failed_vertical_encounters_ << "\n";
    file << "Spare Found," << spare_found_ << "\n";
    file << "Spare Unavailable," << spare_unavailable_ << "\n";
    file << "In Flight At Horizon," << in_flight_ << "\n";

    // Calculate terminal delivery rate (excluding in-flight requests)
    uint64_t terminal_total = completed_requests_ + failed_requests_;
    if (terminal_total > 0) {
        double terminal_rate = static_cast<double>(completed_requests_) * 100.0 / static_cast<double>(terminal_total);
        file << "Terminal Delivery Rate (%)," << terminal_rate << "\n";
    }
    // Route failure rate
    if (total_requests_ > 0) {
        double route_fail_rate = static_cast<double>(failed_requests_) * 100.0 / static_cast<double>(total_requests_);
        file << "Route Failure Rate (%)," << route_fail_rate << "\n";
    }
    
    file.close();
    std::cout << "Statistics exported to: " << filename << std::endl;
}

void Statistics::export_requests_to_csv(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to create file: " << filename << std::endl;
        return;
    }

    file << "Request ID,Start X,Start Y,Start Z,End X,End Y,End Z,"
         << "Generate Time,Complete Time,Latency,Horizontal Distance,Status\n";

    // Export completed requests
    for (const auto& req : completed_requests_list_) {
        file << req.id << ","
             << req.start.x << "," << req.start.y << "," << req.start.z << ","
             << req.end.x << "," << req.end.y << "," << req.end.z << ","
             << req.generate_time << "," << req.complete_time << ","
             << req.get_latency() << "," << req.get_horizontal_distance() << ",Completed\n";
    }
    
    // Export failed requests
    for (const auto& req : failed_requests_list_) {
        file << req.id << ","
             << req.start.x << "," << req.start.y << "," << req.start.z << ","
             << req.end.x << "," << req.end.y << "," << req.end.z << ","
             << req.generate_time << ",N/A,N/A," << req.get_horizontal_distance() << ",Failed\n";
    }
    
    file.close();
    std::cout << "Request data exported to: " << filename << std::endl;
}

} // namespace tsvra
