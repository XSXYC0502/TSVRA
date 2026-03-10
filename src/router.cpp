#include "router.hpp"
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <limits>

namespace tsvra {

// 哈希函数用于Point
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
    uint64_t current_time
) {
    // A*算法实现
    uint32_t grid_size = grid_.get_grid_size();
    uint32_t num_layers = grid_.get_num_layers();

    // 预计算动态代价参数
    const double lambda1 = config_.get_lambda1();
    const double lambda2 = config_.get_lambda2();
    const double failure_rate = config_.get_failure_rate();
    const uint32_t max_path_length = config_.get_max_path_length();
    const double reliability_min = config_.get_reliability_min();

    // φ(pfail) = -ln(1 - pfail)，单节点风险惩罚
    const double phi_fail = (failure_rate < 1.0) ? -std::log(1.0 - failure_rate) : 1e18;

    // R_min约束：∑ φ(pfail) ≤ -ln(R_min) 即 max_risk
    const bool reliability_active = (reliability_min > 0.0);
    const double max_risk = reliability_active
        ? ((reliability_min < 1.0) ? -std::log(reliability_min) : 0.0)
        : std::numeric_limits<double>::max();
    
    // 初始化节点网格
    std::vector<std::vector<std::vector<AStarNode>>> nodes(
        num_layers,
        std::vector<std::vector<AStarNode>>(
            grid_size,
            std::vector<AStarNode>(grid_size)
        )
    );
    
    // 优先队列，按f_cost排序（小的先出）
    auto cmp = [](const AStarNode& a, const AStarNode& b) { 
        return a.f_cost > b.f_cost; 
    };
    std::priority_queue<AStarNode, std::vector<AStarNode>, decltype(cmp)> open_set(cmp);
    
    // 已访问集合
    std::unordered_set<Point, PointHash> closed_set;
    
    // 初始化起始节点
    double h_start = heuristic_(start, end);
    AStarNode start_node(start, 0.0, h_start, Point(), false, Point(), false, 0, 0.0);
    open_set.push(start_node);
    nodes[static_cast<size_t>(start.z)][static_cast<size_t>(start.y)][static_cast<size_t>(start.x)] = start_node;
    
    while (!open_set.empty()) {
        AStarNode current = open_set.top();
        open_set.pop();
        
        // 到达终点
        if (current.position == end) {
            std::vector<Point> path = reconstruct_path(nodes, start, end);
            // 计算实际完成时间，考虑等待时间
            uint64_t actual_time = current_time;
            for (size_t i = 1; i < path.size(); ++i) {
                TSV* tsv = grid_.get_tsv(path[i].x, path[i].y, path[i].z);
                if (tsv) {
                    // 等待TSV空闲
                    if (tsv->get_available_at() > actual_time) {
                        actual_time = tsv->get_available_at();
                    }
                    // 加上传输延迟
                    actual_time += static_cast<uint64_t>(calculate_move_cost(path[i-1], path[i]));
                }
            }
            return std::make_pair(path, actual_time);
        }
        
        // 标记为已访问
        if (closed_set.find(current.position) != closed_set.end()) {
            continue;
        }
        closed_set.insert(current.position);
        
        // 探索邻居
        std::vector<Point> neighbors = get_neighbors(current.position);
        for (const Point& neighbor : neighbors) {
            if (closed_set.find(neighbor) != closed_set.end()) {
                continue;
            }
            
            // 检查TSV
            TSV* tsv = grid_.get_tsv(neighbor.x, neighbor.y, neighbor.z);
            if (!tsv) {
                continue;
            }
            
            // TSV仅连接相邻层，水平移动不使用TSV，因此不受TSV失效影响
            bool is_vertical_move = (neighbor.z != current.position.z);

            // 仅垂直移动需要检查TSV失效，水平移动跳过
            if (is_vertical_move && tsv->is_failed()) {
                auto redundant_opt = grid_.get_nearest_redundant_tsv(
                    neighbor.x, neighbor.y, neighbor.z, current_time);
                if (!redundant_opt.has_value()) {
                    // 没有可用的冗余TSV，跳过这个邻居
                    continue;
                }
                // 使用冗余TSV作为替代路径
                Point redundant_pos = redundant_opt.value();
                TSV* redundant_tsv = grid_.get_tsv(redundant_pos.x, redundant_pos.y, redundant_pos.z);
                if (!redundant_tsv || redundant_tsv->is_failed()) {
                    continue;
                }
                
                // 计算通过冗余TSV的代价
                // 从当前位置到冗余TSV，然后从冗余TSV到原目标邻居
                double cost_to_redundant = calculate_move_cost(current.position, redundant_pos);
                double cost_from_redundant = calculate_move_cost(redundant_pos, neighbor);
                double wait_time = 0;
                if (redundant_tsv->get_available_at() > current_time) {
                    wait_time = static_cast<double>(redundant_tsv->get_available_at() - current_time);
                }

                // L_max约束：绕行经过2个节点（冗余TSV + 目标邻居）
                uint32_t new_hop_count = current.hop_count + 2;
                if (max_path_length > 0 && new_hop_count > max_path_length) {
                    continue;
                }

                // R_min约束：2个节点的累积风险
                double new_risk_sum = current.risk_sum + 2.0 * phi_fail;
                if (new_risk_sum > max_risk) {
                    continue;
                }

                // 动态代价：c0 + wait + λ1*congestion + λ2*φ(pfail)
                double congestion_cost = lambda1 * static_cast<double>(redundant_tsv->get_usage_count());
                double risk_cost = lambda2 * 2.0 * phi_fail;
                double move_cost = cost_to_redundant + cost_from_redundant + wait_time + congestion_cost + risk_cost;
                double new_g_cost = current.g_cost + move_cost;
                double h_cost = heuristic_(neighbor, end);
                
                // 使用冗余TSV作为中间节点
                AStarNode& neighbor_node = nodes[static_cast<size_t>(neighbor.z)]
                                               [static_cast<size_t>(neighbor.y)]
                                               [static_cast<size_t>(neighbor.x)];
                
                if (!neighbor_node.has_parent || new_g_cost < neighbor_node.g_cost) {
                    neighbor_node = AStarNode(neighbor, new_g_cost, h_cost, current.position, true,
                                              redundant_pos, true, new_hop_count, new_risk_sum);
                    open_set.push(neighbor_node);
                }
                continue;
            }
            
            // TSV未失效，但可能被占用，允许等待
            uint64_t actual_arrival_time = current_time;
            if (tsv->get_available_at() > current_time) {
                // TSV被占用，需要等待
                actual_arrival_time = tsv->get_available_at();
            }

            // L_max约束
            uint32_t new_hop_count = current.hop_count + 1;
            if (max_path_length > 0 && new_hop_count > max_path_length) {
                continue;
            }

            // R_min约束
            double new_risk_sum = current.risk_sum + phi_fail;
            if (new_risk_sum > max_risk) {
                continue;
            }

            // 动态代价：c0 + wait + λ1*congestion + λ2*φ(pfail)
            double move_cost = calculate_move_cost(current.position, neighbor);
            double wait_cost = static_cast<double>(actual_arrival_time - current_time);
            double congestion_cost = lambda1 * static_cast<double>(tsv->get_usage_count());
            double risk_cost = lambda2 * phi_fail;
            double new_g_cost = current.g_cost + move_cost + wait_cost + congestion_cost + risk_cost;
            double h_cost = heuristic_(neighbor, end);
            
            AStarNode& neighbor_node = nodes[static_cast<size_t>(neighbor.z)]
                                           [static_cast<size_t>(neighbor.y)]
                                           [static_cast<size_t>(neighbor.x)];
            
            // 如果找到更好的路径，或者这是第一次访问
            if (!neighbor_node.has_parent || new_g_cost < neighbor_node.g_cost) {
                neighbor_node = AStarNode(neighbor, new_g_cost, h_cost, current.position, true,
                                          Point(), false, new_hop_count, new_risk_sum);
                open_set.push(neighbor_node);
            }
        }
    }
    
    // 没有找到路径
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
    neighbors.reserve(6); // 最多6个邻居（上下左右前后）
    
    // 水平方向（同层）
    const int32_t dx[] = {-1, 1, 0, 0};
    const int32_t dy[] = {0, 0, -1, 1};
    for (int i = 0; i < 4; ++i) {
        Point neighbor(pos.x + dx[i], pos.y + dy[i], pos.z);
        if (grid_.is_valid_position(neighbor.x, neighbor.y, neighbor.z)) {
            neighbors.push_back(neighbor);
        }
    }
    
    // 垂直方向（层间）
    if (grid_.is_valid_position(pos.x, pos.y, pos.z - 1)) {
        neighbors.emplace_back(pos.x, pos.y, pos.z - 1);
    }
    if (grid_.is_valid_position(pos.x, pos.y, pos.z + 1)) {
        neighbors.emplace_back(pos.x, pos.y, pos.z + 1);
    }
    
    return neighbors;
}

double Router::calculate_move_cost(const Point& from, const Point& to) const {
    // 垂直移动（TSV传输）
    if (from.x == to.x && from.y == to.y) {
        return static_cast<double>(config_.get_vertical_delay());
    }
    
    // 水平移动
    return static_cast<double>(config_.get_horizontal_delay());
}

std::vector<Point> Router::reconstruct_path(
    const std::vector<std::vector<std::vector<AStarNode>>>& nodes,
    const Point& start,
    const Point& end
) const {
    std::vector<Point> path;
    Point current = end;
    
    while (current != start) {
        path.push_back(current);
        const AStarNode& node = nodes[static_cast<size_t>(current.z)]
                                     [static_cast<size_t>(current.y)]
                                     [static_cast<size_t>(current.x)];
        if (!node.has_parent) {
            break;
        }
        // 如果使用了冗余TSV，先添加冗余TSV节点
        if (node.used_redundant && node.redundant_via != current && node.redundant_via != node.parent) {
            path.push_back(node.redundant_via);
        }
        current = node.parent;
    }
    
    path.push_back(start);
    std::reverse(path.begin(), path.end());
    
    return path;
}

} // namespace tsvra

