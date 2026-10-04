#ifndef TSVRA_ROUTER_HPP
#define TSVRA_ROUTER_HPP

#include "grid.hpp"
#include "request.hpp"
#include "config.hpp"
#include <functional>
#include <vector>
#include <optional>

namespace tsvra {

// Heuristic function type
using HeuristicFunc = std::function<double(const Point&, const Point&)>;

// A* router class
class Router {
public:
    explicit Router(Grid& grid, const Config& config);
    
    // Set heuristic function
    void set_heuristic(HeuristicFunc heuristic) { heuristic_ = heuristic; }
    
    // Compute route for a request
    // Returns path and estimated completion time
    std::optional<std::pair<std::vector<Point>, uint64_t>> route(
        const Point& start, 
        const Point& end, 
        uint64_t current_time,
        uint64_t initial_horizontal_distance = 0,
        uint64_t dispatch_delay = 0
    );
    
    // Calculate transmission delay of a path
    uint64_t calculate_path_delay(const std::vector<Point>& path) const;

    // Number of bypass detours actually used in the last route() call
    uint32_t get_last_bypass_count() const { return last_bypass_count_; }
    
    // Default Manhattan distance heuristic function
    static double manhattan_distance(const Point& a, const Point& b);
    
private:
    Grid& grid_;
    const Config& config_;
    HeuristicFunc heuristic_;
    mutable uint32_t last_bypass_count_ = 0; // Number of bypasses used in the last route() call
    
    // A* node
    struct AStarNode {
        Point position;
        double g_cost = 0.0;      // Routing objective, including congestion/risk penalties
        uint64_t arrival_time = 0; // Physical time, including actual hop delay and waiting
        size_t parent = static_cast<size_t>(-1);
        Point redundant_via;
        bool used_redundant = false;
        uint64_t hop_count = 0;
        uint64_t horizontal_distance = 0;
        double risk_sum = 0.0;
        bool active = true;
    };
    
    // A* algorithm helper functions
    std::vector<Point> get_neighbors(const Point& pos) const;
    uint64_t calculate_move_delay(const Point& from, const Point& to) const;
    std::vector<Point> reconstruct_path(
        const std::vector<AStarNode>& nodes,
        size_t end_index
    ) const;

    // Expand compressed path into unit-adjacent steps (horizontal segments expanded cell by cell)
    std::vector<Point> expand_path(const std::vector<Point>& compressed) const;
};

} // namespace tsvra

#endif // TSVRA_ROUTER_HPP
