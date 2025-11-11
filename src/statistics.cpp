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
      redundant_tsv_usages_(0),
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
    max_latency_ = std::max(max_latency_, latency);
    min_latency_ = std::min(min_latency_, latency);
    
    latency_samples_.push_back(latency);
    completed_requests_list_.push_back(req);
}

void Statistics::record_request_failed(const Request& req) {
    ++failed_requests_;
    failed_requests_list_.push_back(req);
}

void Statistics::record_redundant_tsv_usage() {
    ++redundant_tsv_usages_;
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

void Statistics::print_summary() const {
    std::cout << "\n=================================\n";
    std::cout << "       仿真统计结果\n";
    std::cout << "=================================\n\n";
    
    // 请求统计
    std::cout << "请求统计:\n";
    std::cout << "  总请求数:     " << total_requests_ << "\n";
    std::cout << "  完成请求数:   " << completed_requests_ << "\n";
    std::cout << "  失败请求数:   " << failed_requests_ << "\n";
    
    if (total_requests_ > 0) {
        double success_rate = static_cast<double>(completed_requests_) * 100.0 / static_cast<double>(total_requests_);
        std::cout << "  成功率:       " << std::fixed << std::setprecision(2) << success_rate << "%\n";
    }
    
    // 延迟统计
    std::cout << "\n延迟统计:\n";
    if (completed_requests_ > 0) {
        std::cout << "  平均延迟:     " << std::fixed << std::setprecision(2) << get_average_latency() << " 周期\n";
        std::cout << "  最大延迟:     " << max_latency_ << " 周期\n";
        std::cout << "  最小延迟:     " << min_latency_ << " 周期\n";
    } else {
        std::cout << "  无完成请求\n";
    }
    
    // TSV统计
    std::cout << "\nTSV统计:\n";
    std::cout << "  失效TSV数:         " << current_failed_tsvs_ << "\n";
    std::cout << "  失效冗余TSV数:     " << current_failed_redundant_tsvs_ << "\n";
    std::cout << "  冗余TSV使用次数:   " << redundant_tsv_usages_ << "\n";
    
    std::cout << "\n=================================\n\n";
}

void Statistics::export_to_csv(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法创建文件: " << filename << std::endl;
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
    
    file << "Failed TSVs," << current_failed_tsvs_ << "\n";
    file << "Failed Redundant TSVs," << current_failed_redundant_tsvs_ << "\n";
    file << "Redundant TSV Usages," << redundant_tsv_usages_ << "\n";
    
    file.close();
    std::cout << "统计数据已导出到: " << filename << std::endl;
}

void Statistics::export_requests_to_csv(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法创建文件: " << filename << std::endl;
        return;
    }
    
    file << "Request ID,Start X,Start Y,Start Z,End X,End Y,End Z,"
         << "Generate Time,Complete Time,Latency,Status\n";
    
    // 导出完成的请求
    for (const auto& req : completed_requests_list_) {
        file << req.id << ","
             << req.start.x << "," << req.start.y << "," << req.start.z << ","
             << req.end.x << "," << req.end.y << "," << req.end.z << ","
             << req.generate_time << "," << req.complete_time << ","
             << req.get_latency() << ",Completed\n";
    }
    
    // 导出失败的请求
    for (const auto& req : failed_requests_list_) {
        file << req.id << ","
             << req.start.x << "," << req.start.y << "," << req.start.z << ","
             << req.end.x << "," << req.end.y << "," << req.end.z << ","
             << req.generate_time << ",N/A,N/A,Failed\n";
    }
    
    file.close();
    std::cout << "请求数据已导出到: " << filename << std::endl;
}

} // namespace tsvra

