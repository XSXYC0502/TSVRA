#ifndef TSVRA_TSV_HPP
#define TSVRA_TSV_HPP

#include <cstdint>

namespace tsvra {

// TSV unit class
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
    
    // Usage count (number of active transmission paths through this TSV)
    uint32_t get_usage_count() const { return usage_count_; }
    void increment_usage() { ++usage_count_; }
    void decrement_usage() { if (usage_count_ > 0) --usage_count_; }

    // State modification
    void set_failed(bool failed) { is_failed_ = failed; }
    void set_available_at(uint64_t time) { available_at_ = time; }
    
    // Check if available at a given time
    bool is_available(uint64_t current_time) const {
        return !is_failed_ && current_time >= available_at_;
    }
    
    // Occupy the TSV until a specified time
    void occupy_until(uint64_t time) {
        if (time > available_at_) {
            available_at_ = time;
        }
    }
    
private:
    int32_t x_;                 // X coordinate
    int32_t y_;                 // Y coordinate
    int32_t z_;                 // Z coordinate (layer)
    bool is_redundant_;         // Whether this is a redundant TSV
    bool is_failed_;            // Whether this TSV has failed
    uint64_t available_at_;     // Time at which this TSV becomes free
    uint32_t usage_count_;      // Count of active transmission paths through this TSV
};

} // namespace tsvra

#endif // TSVRA_TSV_HPP

