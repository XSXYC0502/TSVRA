#include "router.hpp"
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>

namespace tsvra {

// Hash function for Point
struct PointHash {
    std::size_t operator()(const Point& p) const {
        return std::hash<int32_t>()(p.x) ^ 
               (std::hash<int32_t>()(p.y) << 1) ^ 
               (std::hash<int32_t>()(p.z) << 2);
    }
};

Router::Router(Grid& grid, const Config& config)
    : grid_(grid), config_(config), heuristic_(manhattan_distance) {
}

double Router::manhattan_distance(const Point& a, const Point& b) {
    return static_cast<double>(std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z));
}

std::optional<std::pair<std::vector<Point>, uint64_t>> Router::route(
    const Point& start, 
    const Point& end, 
    uint64_t current_time,
    uint64_t initial_horizontal_distance
) {
    // A* algorithm implementation
    uint32_t grid_size = grid_.get_grid_size();
    uint32_t num_layers = grid_.get_num_layers();

    // Pre-compute dynamic cost parameters
    const double lambda1 = config_.get_lambda1();
    const double lambda2 = config_.get_lambda2();
    const double failure_rate = config_.get_failure_rate();
    const uint32_t max_path_length = config_.get_max_path_length();
    const uint32_t max_horizontal_distance = config_.get_max_horizontal_distance();
    const double reliability_min = config_.get_reliability_min();

    // phi(pfail) = -ln(1 - pfail), single-node risk penalty
    const double phi_fail = (failure_rate < 1.0) ? -std::log(1.0 - failure_rate) : 1e18;

    // R_min constraint: sum(phi(pfail)) <= -ln(R_min), i.e. max_risk
    const bool reliability_active = (reliability_min > 0.0);
    const double max_risk = reliability_active
        ? ((reliability_min < 1.0) ? -std::log(reliability_min) : 0.0)
        : std::numeric_limits<double>::max();
    
    // Initialize node grid
    std::vector<std::vector<std::vector<AStarNode>>> nodes(
        num_layers,
        std::vector<std::vector<AStarNode>>(
            grid_size,
            std::vector<AStarNode>(grid_size)
        )
    );
    
    // Priority queue, sorted by f_cost (smallest first)
    auto cmp = [](const AStarNode& a, const AStarNode& b) { 
        return a.f_cost > b.f_cost; 
    };
    std::priority_queue<AStarNode, std::vector<AStarNode>, decltype(cmp)> open_set(cmp);
    
    // Visited set
    std::unordered_set<Point, PointHash> closed_set;
    
    // Initialize start node
    double h_start = heuristic_(start, end);
    AStarNode start_node(start, 0.0, h_start, Point(), false, Point(), false, 0,
                         initial_horizontal_distance, 0.0);
    open_set.push(start_node);
    nodes[static_cast<size_t>(start.z)][static_cast<size_t>(start.y)][static_cast<size_t>(start.x)] = start_node;
    
    while (!open_set.empty()) {
        AStarNode current = open_set.top();
        open_set.pop();
        
        // Reached the goal
        if (current.position == end) {
            std::vector<Point> path = expand_path(reconstruct_path(nodes, start, end));

            // Path integrity validation
            if (path.empty() || path.front() != start || path.back() != end) {
                return std::nullopt;
            }
            for (size_t i = 1; i < path.size(); ++i) {
                int32_t d = std::abs(path[i].x - path[i-1].x)
                          + std::abs(path[i].y - path[i-1].y)
                          + std::abs(path[i].z - path[i-1].z);
                if (d != 1) {
                    return std::nullopt;
                }
            }

            // Calculate actual completion time, considering wait times
            uint64_t actual_time = current_time;
            for (size_t i = 1; i < path.size(); ++i) {
                TSV* tsv = grid_.get_tsv(path[i].x, path[i].y, path[i].z);
                if (tsv) {
                    // Wait for TSV to become free
                    if (tsv->get_available_at() > actual_time) {
                        actual_time = tsv->get_available_at();
                    }
                    // Add transmission delay
                    actual_time += static_cast<uint64_t>(calculate_move_cost(path[i-1], path[i]));
                }
            }
            return std::make_pair(path, actual_time);
        }
        
        // Mark as visited
        if (closed_set.find(current.position) != closed_set.end()) {
            continue;
        }
        closed_set.insert(current.position);
        
        // Explore neighbors
        std::vector<Point> neighbors = get_neighbors(current.position);
        for (const Point& neighbor : neighbors) {
            if (closed_set.find(neighbor) != closed_set.end()) {
                continue;
            }
            
            // Check TSV
            TSV* tsv = grid_.get_tsv(neighbor.x, neighbor.y, neighbor.z);
            if (!tsv) {
                continue;
            }
            
            // TSV only connects adjacent layers; horizontal moves don't use TSVs, so they are unaffected by TSV failures
            bool is_vertical_move = (neighbor.z != current.position.z);

            // Only vertical moves need to check TSV failure; skip for horizontal moves
            if (is_vertical_move && tsv->is_failed()) {
                // B5: Use estimated arrival time instead of routing start time
                uint64_t estimated_arrival = current_time + static_cast<uint64_t>(current.g_cost);
                auto redundant_opt = grid_.find_bypass_spare(neighbor, estimated_arrival);
                if (!redundant_opt.has_value()) {
                    // No available redundant TSV, skip this neighbor
                    continue;
                }
                // Use redundant TSV as alternative path
                Point redundant_pos = redundant_opt.value();
                TSV* redundant_tsv = grid_.get_tsv(redundant_pos.x, redundant_pos.y, redundant_pos.z);
                if (!redundant_tsv || redundant_tsv->is_failed()) {
                    continue;
                }
                
                // Calculate cost through redundant TSV (based on Manhattan distance)
                // Segment 1: current position -> redundant TSV (same-layer horizontal move)
                int32_t manhattan_to_spare = std::abs(current.position.x - redundant_pos.x)
                                           + std::abs(current.position.y - redundant_pos.y);
                double cost_to_redundant = static_cast<double>(manhattan_to_spare)
                                         * static_cast<double>(config_.get_horizontal_delay());

                // Segment 2: redundant TSV vertical crossing + horizontal move to target (x,y)
                int32_t manhattan_from_spare = std::abs(redundant_pos.x - neighbor.x)
                                            + std::abs(redundant_pos.y - neighbor.y);
                double cost_from_redundant = static_cast<double>(config_.get_vertical_delay())
                                           + static_cast<double>(manhattan_from_spare)
                                           * static_cast<double>(config_.get_horizontal_delay());
                double wait_time = 0;
                uint64_t spare_arrival = estimated_arrival + static_cast<uint64_t>(cost_to_redundant);
                if (redundant_tsv->get_available_at() > spare_arrival) {
                    wait_time = static_cast<double>(redundant_tsv->get_available_at() - spare_arrival);
                }

                // L_max constraint: bypass traverses manhattan_to + 1 (vertical) + manhattan_from hops
                uint32_t bypass_hops = static_cast<uint32_t>(manhattan_to_spare) + 1
                                     + static_cast<uint32_t>(manhattan_from_spare);
                uint32_t new_hop_count = current.hop_count + bypass_hops;
                if (max_path_length > 0 && new_hop_count > max_path_length) {
                    continue;
                }

                uint64_t horizontal_increment = static_cast<uint64_t>(manhattan_to_spare)
                                              + static_cast<uint64_t>(manhattan_from_spare);
                uint64_t new_horizontal_distance = current.horizontal_distance + horizontal_increment;
                if (max_horizontal_distance > 0 &&
                    new_horizontal_distance > max_horizontal_distance) {
                    continue;
                }

                // R_min constraint: cumulative risk of 2 nodes
                double new_risk_sum = current.risk_sum + 2.0 * phi_fail;
                if (new_risk_sum > max_risk) {
                    continue;
                }

                // Dynamic cost: c0 + wait + lambda1*congestion + lambda2*phi(pfail)
                double congestion_cost = lambda1 * static_cast<double>(redundant_tsv->get_usage_count());
                double risk_cost = lambda2 * 2.0 * phi_fail;
                double move_cost = cost_to_redundant + cost_from_redundant + wait_time + congestion_cost + risk_cost;
                double new_g_cost = current.g_cost + move_cost;
                double h_cost = heuristic_(neighbor, end);

                AStarNode& neighbor_node = nodes[static_cast<size_t>(neighbor.z)]
                                               [static_cast<size_t>(neighbor.y)]
                                               [static_cast<size_t>(neighbor.x)];
                
                if (!neighbor_node.has_parent || new_g_cost < neighbor_node.g_cost) {
                    neighbor_node = AStarNode(neighbor, new_g_cost, h_cost, current.position, true,
                                              redundant_pos, true, new_hop_count,
                                              new_horizontal_distance, new_risk_sum);
                    open_set.push(neighbor_node);
                }
                continue;
            }

            // B6: Redundant TSVs are reserved for bypass routing only.
            // Exception: allow the route to end on a redundant coordinate.
            if (tsv->is_redundant() && neighbor != end && neighbor != start) {
                continue;
            }

            // The TSV is healthy, but it may be occupied, so waiting is allowed.
            // B5: Use the estimated arrival time.
            uint64_t est_arrival_normal = current_time + static_cast<uint64_t>(current.g_cost);
            uint64_t actual_arrival_time = est_arrival_normal;
            if (tsv->get_available_at() > est_arrival_normal) {
                // The TSV is occupied and the route must wait.
                actual_arrival_time = tsv->get_available_at();
            }

            // L_max constraint
            uint32_t new_hop_count = current.hop_count + 1;
            if (max_path_length > 0 && new_hop_count > max_path_length) {
                continue;
            }

            uint64_t horizontal_increment = is_vertical_move ? 0ull : 1ull;
            uint64_t new_horizontal_distance = current.horizontal_distance + horizontal_increment;
            if (max_horizontal_distance > 0 &&
                new_horizontal_distance > max_horizontal_distance) {
                continue;
            }

            // R_min constraint
            double new_risk_sum = current.risk_sum + phi_fail;
            if (new_risk_sum > max_risk) {
                continue;
            }

            // Dynamic cost: c0 + wait + lambda1*congestion + lambda2*phi(pfail)
            double move_cost = calculate_move_cost(current.position, neighbor);
            double wait_cost = static_cast<double>(actual_arrival_time - est_arrival_normal);
            double congestion_cost = lambda1 * static_cast<double>(tsv->get_usage_count());
            double risk_cost = lambda2 * phi_fail;
            double new_g_cost = current.g_cost + move_cost + wait_cost + congestion_cost + risk_cost;
            double h_cost = heuristic_(neighbor, end);
            
            AStarNode& neighbor_node = nodes[static_cast<size_t>(neighbor.z)]
                                           [static_cast<size_t>(neighbor.y)]
                                           [static_cast<size_t>(neighbor.x)];
            
            // Update the node if this is the first visit or the better path.
            if (!neighbor_node.has_parent || new_g_cost < neighbor_node.g_cost) {
                neighbor_node = AStarNode(neighbor, new_g_cost, h_cost, current.position, true,
                                          Point(), false, new_hop_count,
                                          new_horizontal_distance, new_risk_sum);
                open_set.push(neighbor_node);
            }
        }
    }
    
    // No path found
    return std::nullopt;
}

uint64_t Router::calculate_path_delay(const std::vector<Point>& path) const {
    if (path.size() < 2) {
        return 0;
    }
    
    uint64_t total_delay = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        total_delay += static_cast<uint64_t>(calculate_move_cost(path[i - 1], path[i]));
    }
    
    return total_delay;
}

std::vector<Point> Router::get_neighbors(const Point& pos) const {
    std::vector<Point> neighbors;
    neighbors.reserve(6); // At most 6 neighbors: up, down, left, right, front, back
    
    // Horizontal neighbors on the same layer
    const int32_t dx[] = {-1, 1, 0, 0};
    const int32_t dy[] = {0, 0, -1, 1};
    for (int i = 0; i < 4; ++i) {
        Point neighbor(pos.x + dx[i], pos.y + dy[i], pos.z);
        if (grid_.is_valid_position(neighbor.x, neighbor.y, neighbor.z)) {
            neighbors.push_back(neighbor);
        }
    }
    
    // Vertical neighbors across layers
    if (grid_.is_valid_position(pos.x, pos.y, pos.z - 1)) {
        neighbors.emplace_back(pos.x, pos.y, pos.z - 1);
    }
    if (grid_.is_valid_position(pos.x, pos.y, pos.z + 1)) {
        neighbors.emplace_back(pos.x, pos.y, pos.z + 1);
    }
    
    return neighbors;
}

double Router::calculate_move_cost(const Point& from, const Point& to) const {
    // Vertical movement through a TSV
    if (from.x == to.x && from.y == to.y) {
        return static_cast<double>(config_.get_vertical_delay());
    }
    
    // Horizontal movement
    return static_cast<double>(config_.get_horizontal_delay());
}

std::vector<Point> Router::reconstruct_path(
    const std::vector<std::vector<std::vector<AStarNode>>>& nodes,
    const Point& start,
    const Point& end
) const {
    std::vector<Point> path;
    Point current = end;
    last_bypass_count_ = 0;
    
    while (current != start) {
        const AStarNode& node = nodes[static_cast<size_t>(current.z)]
                                     [static_cast<size_t>(current.y)]
                                     [static_cast<size_t>(current.x)];
        if (!node.has_parent) {
            break;
        }
        // If the path used a redundant TSV bypass:
        // Expand to: destination(target_z), spare_exit(target_z), spare_entry(source_z)
        // After reversing: spare_entry(source_z) -> spare_exit(target_z) -> destination(target_z)
        if (node.used_redundant && node.redundant_via != current && node.redundant_via != node.parent) {
            Point spare_exit{node.redundant_via.x, node.redundant_via.y, current.z};
            Point spare_entry{node.redundant_via.x, node.redundant_via.y, node.parent.z};

            // Avoid duplicate points in degenerate segments.
            // Normal case: push current, spare_exit, spare_entry
            // After reversing: entry -> exit -> current
            // Degenerate case: skip spare_exit when spare_exit == current
            // Degenerate case: skip spare_entry when spare_entry == parent
            // because parent is pushed in the next loop iteration.
            path.push_back(current);
            if (spare_exit != current) {
                path.push_back(spare_exit);
            }
            if (spare_entry != node.parent) {
                path.push_back(spare_entry);
            }
            ++last_bypass_count_;
        } else {
            path.push_back(current);
        }
        current = node.parent;
    }
    
    path.push_back(start);
    std::reverse(path.begin(), path.end());
    
    return path;
}

std::vector<Point> Router::expand_path(const std::vector<Point>& compressed) const {
    if (compressed.empty()) return compressed;
    std::vector<Point> expanded;
    expanded.reserve(compressed.size() * 2);
    expanded.push_back(compressed[0]);
    for (size_t i = 1; i < compressed.size(); ++i) {
        const Point& from = compressed[i - 1];
        const Point& to = compressed[i];
        if (from.z == to.z && (from.x != to.x || from.y != to.y)) {
            // Same-layer horizontal movement: expand one grid cell at a time, x first, then y.
            Point cur = from;
            int32_t dx = (to.x > from.x) ? 1 : (to.x < from.x) ? -1 : 0;
            int32_t dy = (to.y > from.y) ? 1 : (to.y < from.y) ? -1 : 0;
            while (cur.x != to.x) {
                cur.x += dx;
                expanded.push_back(cur);
            }
            while (cur.y != to.y) {
                cur.y += dy;
                expanded.push_back(cur);
            }
        } else {
            // Vertical movement or already-adjacent points: append directly.
            expanded.push_back(to);
        }
    }
    return expanded;
}

} // namespace tsvra
