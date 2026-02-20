#ifndef TSVRA_TSV_HPP
#define TSVRA_TSV_HPP

#include <cstdint>

namespace tsvra {

// TSV单元类
class TSV {
public:
    TSV();
    TSV(int32_t x, int32_t y, int32_t z, bool is_redundant = false);
    
    // Getters
    int32_t get_x() const { return x_; }
    int32_t get_y() const { return y_; }
    int32_t get_z() const { return z_; }
    bool is_redundant() const { return is_redundant_; }
    bool is_failed() const { return is_failed_; }
    uint64_t get_available_at() const { return available_at_; }
    
    // 拥塞计数（活跃传输路径经过此TSV的数量）
    uint32_t get_usage_count() const { return usage_count_; }
    void increment_usage() { ++usage_count_; }
    void decrement_usage() { if (usage_count_ > 0) --usage_count_; }

    // 状态修改
    void set_failed(bool failed) { is_failed_ = failed; }
    void set_available_at(uint64_t time) { available_at_ = time; }
    
    // 检查在指定时间是否可用
    bool is_available(uint64_t current_time) const {
        return !is_failed_ && current_time >= available_at_;
    }
    
    // 占用TSV直到指定时间
    void occupy_until(uint64_t time) {
        if (time > available_at_) {
            available_at_ = time;
        }
    }
    
private:
    int32_t x_;                 // X坐标
    int32_t y_;                 // Y坐标
    int32_t z_;                 // Z坐标（层）
    bool is_redundant_;         // 是否为冗余TSV
    bool is_failed_;            // 是否失效
    uint64_t available_at_;     // 空闲时间点
    uint32_t usage_count_;      // 活跃传输路径经过此TSV的计数
};

} // namespace tsvra

#endif // TSVRA_TSV_HPP

