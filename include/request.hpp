#ifndef TSVRA_REQUEST_HPP
#define TSVRA_REQUEST_HPP

#include <cstdint>
#include <vector>

namespace tsvra {

// 三维坐标点
struct Point {
    int32_t x;
    int32_t y;
    int32_t z;
    
    Point() : x(0), y(0), z(0) {}
    Point(int32_t x_, int32_t y_, int32_t z_) : x(x_), y(y_), z(z_) {}
    
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    bool operator!=(const Point& other) const {
        return !(*this == other);
    }
};

// 请求状态枚举
enum class RequestStatus {
    PENDING,        // 待路由
    TRANSMITTING,   // 传输中
    COMPLETED,      // 已完成
    FAILED          // 失败
};

// 请求结构
struct Request {
    uint64_t id;                // 请求ID
    Point start;                // 起始点 (x0, y0, z0)
    Point end;                  // 终止点 (x1, y1, maxZ)
    uint64_t generate_time;     // 生成时间
    uint64_t complete_time;     // 完成时间（0表示未完成）
    bool completed;             // 是否完成
    std::vector<Point> path;    // 路径（路由算法计算）
    RequestStatus status;       // 请求状态
    size_t current_hop;         // 当前传输到路径上的第几跳（索引）
    uint64_t next_hop_time;     // 下一跳的时间
    
    Request() 
        : id(0), start(), end(), generate_time(0), 
          complete_time(0), completed(false), status(RequestStatus::PENDING),
          current_hop(0), next_hop_time(0) {}
    
    Request(uint64_t id_, Point start_, Point end_, uint64_t gen_time)
        : id(id_), start(start_), end(end_), generate_time(gen_time),
          complete_time(0), completed(false), status(RequestStatus::PENDING),
          current_hop(0), next_hop_time(gen_time) {}
    
    uint64_t get_latency() const {
        return completed ? (complete_time - generate_time) : 0;
    }
    
    // 获取当前位置
    Point get_current_position() const {
        if (path.empty() || current_hop >= path.size()) {
            return start;
        }
        return path[current_hop];
    }
};

} // namespace tsvra

#endif // TSVRA_REQUEST_HPP

