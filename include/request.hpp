#ifndef TSVRA_REQUEST_HPP
#define TSVRA_REQUEST_HPP

#include <cstdint>
#include <vector>

namespace tsvra {

// 3D coordinate point
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

// Request status enumeration
enum class RequestStatus {
    PENDING,        // Awaiting routing
    TRANSMITTING,   // Transmitting
    COMPLETED,      // Completed
    FAILED          // Failed
};

// Request structure
struct Request {
    uint64_t id;                // Request ID
    Point start;                // Start point (x0, y0, z0)
    Point end;                  // End point (x1, y1, maxZ)
    uint64_t generate_time;     // Generation time
    uint64_t complete_time;     // Completion time (0 means not completed)
    uint64_t horizontal_distance;// Accumulated horizontal movement
    bool completed;             // Whether completed
    std::vector<Point> path;    // Path (computed by routing algorithm)
    RequestStatus status;       // Request status
    size_t current_hop;         // Current hop index along the path
    uint64_t next_hop_time;     // Time of next hop
    
    Request() 
        : id(0), start(), end(), generate_time(0), 
          complete_time(0), horizontal_distance(0), completed(false), status(RequestStatus::PENDING),
          current_hop(0), next_hop_time(0) {}
    
    Request(uint64_t id_, Point start_, Point end_, uint64_t gen_time)
        : id(id_), start(start_), end(end_), generate_time(gen_time),
          complete_time(0), horizontal_distance(0), completed(false), status(RequestStatus::PENDING),
          current_hop(0), next_hop_time(gen_time) {}
    
    uint64_t get_latency() const {
        return completed ? (complete_time - generate_time) : 0;
    }

    uint64_t get_horizontal_distance() const {
        return horizontal_distance;
    }
    
    // Get current position
    Point get_current_position() const {
        if (path.empty() || current_hop >= path.size()) {
            return start;
        }
        return path[current_hop];
    }
};

} // namespace tsvra

#endif // TSVRA_REQUEST_HPP
