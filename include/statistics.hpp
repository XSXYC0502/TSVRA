#ifndef TSVRA_STATISTICS_HPP
#define TSVRA_STATISTICS_HPP

#include "request.hpp"
#include "config.hpp"
#include "grid.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace tsvra {

// 统计数据类
class Statistics {
public:
    explicit Statistics(const Config& config);
    
    // 记录请求
    void record_request_generated();
    void record_request_completed(const Request& req);
    void record_request_failed(const Request& req);
    
    // 记录TSV使用
    void record_redundant_tsv_usage();
    
    // 更新失效统计
    void update_failure_stats(const Grid& grid);
    
    // 输出统计信息
    void print_summary() const;
    void export_to_csv(const std::string& filename) const;
    void export_requests_to_csv(const std::string& filename) const;
    
    // 获取统计数据
    uint64_t get_total_requests() const { return total_requests_; }
    uint64_t get_completed_requests() const { return completed_requests_; }
    uint64_t get_failed_requests() const { return failed_requests_; }
    double get_average_latency() const;
    uint64_t get_max_latency() const { return max_latency_; }
    
private:
    const Config& config_;
    
    // 请求统计
    uint64_t total_requests_;
    uint64_t completed_requests_;
    uint64_t failed_requests_;
    
    // 延迟统计
    uint64_t total_latency_;
    uint64_t max_latency_;
    uint64_t min_latency_;
    std::vector<uint64_t> latency_samples_;
    
    // TSV统计
    uint64_t redundant_tsv_usages_;
    uint32_t current_failed_tsvs_;
    uint32_t current_failed_redundant_tsvs_;
    
    // 请求记录（用于导出）
    std::vector<Request> completed_requests_list_;
    std::vector<Request> failed_requests_list_;
};

} // namespace tsvra

#endif // TSVRA_STATISTICS_HPP

