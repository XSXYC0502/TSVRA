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
        uint64_t initial_horizontal_distance = 0
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
        double g_cost;      // Actual cost from start to current point
        double h_cost;      // Heuristic cost from current point to goal
        double f_cost;      // g_cost + h_cost
        Point parent;       // Parent node position
        bool has_parent;    // Whether it has a parent node
        Point redundant_via; // If redundant TSV bypass was used, stores the redundant TSV position
        bool used_redundant; // Whether a redundant TSV was used
        uint32_t hop_count; // Hop count from start (for L_max constraint)
        uint64_t horizontal_distance; // Horizontal distance from request origin (for H_max constraint)
        double risk_sum;    // Cumulative risk sum(-ln(1-pfail)) (for R_min constraint)

        AStarNode()
            : position(), g_cost(0), h_cost(0), f_cost(0),
              parent(), has_parent(false), redundant_via(), used_redundant(false),
              hop_count(0), horizontal_distance(0), risk_sum(0.0) {}

        AStarNode(Point pos, double g, double h, Point p, bool hp,
                  Point rv = Point(), bool ur = false,
                  uint32_t hc = 0, uint64_t hd = 0, double rs = 0.0)
            : position(pos), g_cost(g), h_cost(h), f_cost(g + h),
              parent(p), has_parent(hp), redundant_via(rv), used_redundant(ur),
              hop_count(hc), horizontal_distance(hd), risk_sum(rs) {}
    };
    
    // A* algorithm helper functions
    std::vector<Point> get_neighbors(const Point& pos) const;
    double calculate_move_cost(const Point& from, const Point& to) const;
    std::vector<Point> reconstruct_path(
        const std::vector<std::vector<std::vector<AStarNode>>>& nodes,
        const Point& start,
        const Point& end
    ) const;

    // Expand compressed path into unit-adjacent steps (horizontal segments expanded cell by cell)
    std::vector<Point> expand_path(const std::vector<Point>& compressed) const;
};

} // namespace tsvra

#endif // TSVRA_ROUTER_HPP
