#include "router.hpp"
#include <queue>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace tsvra {

Router::Router(Grid& grid, const Config& config)
    : grid_(grid), config_(config), heuristic_([&config](const Point& a, const Point& b) {
        const double horizontal = static_cast<double>(std::abs(a.x - b.x)) + std::abs(a.y - b.y);
        const double vertical = static_cast<double>(std::abs(a.z - b.z));
        return horizontal * (static_cast<double>(config.get_horizontal_delay()) + static_cast<double>(config.get_router_delay()))
             + vertical * (static_cast<double>(config.get_vertical_delay()) + static_cast<double>(config.get_router_delay()));
    }) {}

double Router::manhattan_distance(const Point& a, const Point& b) {
    return static_cast<double>(std::abs(a.x - b.x)) + std::abs(a.y - b.y) + std::abs(a.z - b.z);
}

std::optional<std::pair<std::vector<Point>, uint64_t>> Router::route(
    const Point& start, const Point& end, uint64_t current_time, uint64_t initial_horizontal_distance,
    uint64_t dispatch_delay
) {
    last_bypass_count_ = 0;
    if (!grid_.is_valid_position(start.x, start.y, start.z) || !grid_.is_valid_position(end.x, end.y, end.z))
        return std::nullopt;
    if (dispatch_delay > std::numeric_limits<uint64_t>::max() - current_time) return std::nullopt;
    const uint32_t max_path_length = config_.get_max_path_length();
    const uint32_t max_horizontal_distance = config_.get_max_horizontal_distance();
    if (max_horizontal_distance > 0 && initial_horizontal_distance > max_horizontal_distance) return std::nullopt;
    const double lambda1 = config_.get_lambda1();
    const double lambda2 = config_.get_lambda2();
    const bool runtime_risk = config_.get_failure_mode() != FailureMode::INITIAL_ONLY;
    const bool reliability_active = config_.get_reliability_min() > 0.0;
    const double max_risk = reliability_active ? -std::log(config_.get_reliability_min())
                                              : std::numeric_limits<double>::infinity();
    const size_t grid_size = grid_.get_grid_size();
    const auto position_index = [grid_size](const Point& p) {
        return (static_cast<size_t>(p.z) * grid_size + static_cast<size_t>(p.y)) * grid_size + static_cast<size_t>(p.x);
    };
    std::vector<AStarNode> nodes;
    std::vector<std::vector<size_t>> labels(grid_size * grid_size * grid_.get_num_layers());
    struct OpenEntry { double f_cost; size_t index; };
    const auto compare = [](const OpenEntry& a, const OpenEntry& b) {
        if (a.f_cost != b.f_cost) return a.f_cost > b.f_cost;
        return a.index > b.index;
    };
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, decltype(compare)> open(compare);
    AStarNode first;
    first.position = start;
    first.arrival_time = current_time + dispatch_delay;
    first.g_cost = static_cast<double>(dispatch_delay);
    first.horizontal_distance = initial_horizontal_distance;
    nodes.push_back(first);
    labels[position_index(start)].push_back(0);
    open.push({first.g_cost + heuristic_(start, end), 0});

    // Keep multiple labels when time/cost or a constraint leaves different usable futures.
    const auto dominates = [&](const AStarNode& a, const AStarNode& b) {
        return a.g_cost <= b.g_cost && a.arrival_time <= b.arrival_time &&
               (!reliability_active || a.risk_sum <= b.risk_sum) &&
               (max_path_length == 0 || a.hop_count <= b.hop_count) &&
               (max_horizontal_distance == 0 || a.horizontal_distance <= b.horizontal_distance);
    };
    while (!open.empty()) {
        const size_t current_index = open.top().index;
        open.pop();
        if (!nodes[current_index].active) continue;
        const AStarNode current = nodes[current_index];
        if (current.position == end) {
            auto path = expand_path(reconstruct_path(nodes, current_index));
            if (path.empty() || path.front() != start || path.back() != end) return std::nullopt;
            for (size_t i = 1; i < path.size(); ++i)
                if (manhattan_distance(path[i - 1], path[i]) != 1.0) return std::nullopt;
            return std::make_pair(std::move(path), current.arrival_time);
        }
        for (const Point& neighbor : get_neighbors(current.position)) {
            const bool vertical = neighbor.z != current.position.z;
            const TSV* resource = vertical ? grid_.get_vertical_channel(current.position, neighbor)
                                           : grid_.get_tsv(neighbor.x, neighbor.y, neighbor.z);
            if (!resource) continue;
            std::vector<Point> segment{current.position, neighbor};
            Point spare;
            bool used_spare = false;
            if (vertical && resource->is_failed()) {
                const Point failed{neighbor.x, neighbor.y, std::min(current.position.z, neighbor.z)};
                const auto bypass = grid_.find_bypass_spare(failed, current.arrival_time);
                if (!bypass) continue;
                spare = *bypass;
                const Point entry{spare.x, spare.y, current.position.z};
                const Point exit{spare.x, spare.y, neighbor.z};
                segment = expand_path({current.position, entry, exit, neighbor});
                used_spare = true;
            } else if (vertical && resource->is_redundant() && neighbor != end && neighbor != start) {
                continue; // Spare vertical channels are reserved for bypasses.
            }
            AStarNode candidate;
            candidate.position = neighbor;
            candidate.parent = current_index;
            candidate.redundant_via = spare;
            candidate.used_redundant = used_spare;
            candidate.arrival_time = current.arrival_time;
            candidate.hop_count = current.hop_count;
            candidate.horizontal_distance = current.horizontal_distance;
            candidate.risk_sum = current.risk_sum;
            double congestion = 0.0;
            double segment_risk = 0.0;
            bool usable = true;
            for (size_t i = 1; i < segment.size(); ++i) {
                const Point& from = segment[i - 1];
                const Point& to = segment[i];
                if (from == to) continue;
                const bool channel_hop = from.z != to.z;
                const TSV* hop_resource = channel_hop ? grid_.get_vertical_channel(from, to)
                                                     : grid_.get_tsv(to.x, to.y, to.z);
                if (!hop_resource || (channel_hop && hop_resource->is_failed())) { usable = false; break; }
                ++candidate.hop_count;
                if (!channel_hop) ++candidate.horizontal_distance;
                if ((max_path_length > 0 && candidate.hop_count > max_path_length) ||
                    (max_horizontal_distance > 0 && candidate.horizontal_distance > max_horizontal_distance)) {
                    usable = false; break;
                }
                const uint64_t departure = std::max(candidate.arrival_time, hop_resource->get_available_at());
                const uint64_t delay = calculate_move_delay(from, to);
                if (delay > std::numeric_limits<uint64_t>::max() - departure) { usable = false; break; }
                candidate.arrival_time = departure + delay;
                congestion += static_cast<double>(hop_resource->get_usage_count());
                if (channel_hop) {
                    const double p = grid_.get_failure_probability(from.x, from.y, std::min(from.z, to.z), runtime_risk);
                    const double hazard = p >= 1.0 ? std::numeric_limits<double>::infinity() : -std::log1p(-p);
                    // Runtime p is conditional per cycle. Expose each channel from planning
                    // until crossing completion; initial-only p remains a design-risk proxy.
                    const double exposure = runtime_risk ? static_cast<double>(candidate.arrival_time - current_time) : 1.0;
                    segment_risk += hazard * exposure;
                    candidate.risk_sum = current.risk_sum + segment_risk;
                    if (reliability_active && candidate.risk_sum > max_risk) { usable = false; break; }
                }
            }
            if (!usable) continue;
            const double physical_increment = static_cast<double>(candidate.arrival_time - current.arrival_time);
            const double risk_penalty = lambda2 > 0.0 ? lambda2 * segment_risk : 0.0;
            candidate.g_cost = current.g_cost + physical_increment + lambda1 * congestion + risk_penalty;
            auto& existing = labels[position_index(neighbor)];
            bool dominated = false;
            for (const size_t index : existing)
                if (nodes[index].active && dominates(nodes[index], candidate)) { dominated = true; break; }
            if (dominated) continue;
            for (const size_t index : existing)
                if (nodes[index].active && dominates(candidate, nodes[index])) nodes[index].active = false;
            const size_t candidate_index = nodes.size();
            nodes.push_back(candidate);
            existing.push_back(candidate_index);
            open.push({candidate.g_cost + heuristic_(neighbor, end), candidate_index});
        }
    }
    return std::nullopt;
}

uint64_t Router::calculate_move_delay(const Point& from, const Point& to) const {
    const uint64_t link_delay = from.z != to.z ? config_.get_vertical_delay() : config_.get_horizontal_delay();
    if (config_.get_router_delay() > std::numeric_limits<uint64_t>::max() - link_delay)
        throw std::overflow_error("router delay plus link delay overflows the cycle count");
    return link_delay + config_.get_router_delay();
}

uint64_t Router::calculate_path_delay(const std::vector<Point>& path) const {
    uint64_t total = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == path[i - 1]) continue;
        const uint64_t delay = calculate_move_delay(path[i - 1], path[i]);
        if (delay > std::numeric_limits<uint64_t>::max() - total)
            throw std::overflow_error("path transmission delay overflows the cycle count");
        total += delay;
    }
    return total;
}

std::vector<Point> Router::get_neighbors(const Point& pos) const {
    std::vector<Point> neighbors;
    neighbors.reserve(6);
    const int32_t dx[] = {-1, 1, 0, 0};
    const int32_t dy[] = {0, 0, -1, 1};
    for (int i = 0; i < 4; ++i) {
        const Point neighbor{pos.x + dx[i], pos.y + dy[i], pos.z};
        if (grid_.is_valid_position(neighbor.x, neighbor.y, neighbor.z)) neighbors.push_back(neighbor);
    }
    if (grid_.is_valid_position(pos.x, pos.y, pos.z - 1)) neighbors.emplace_back(pos.x, pos.y, pos.z - 1);
    if (grid_.is_valid_position(pos.x, pos.y, pos.z + 1)) neighbors.emplace_back(pos.x, pos.y, pos.z + 1);
    return neighbors;
}

std::vector<Point> Router::reconstruct_path(const std::vector<AStarNode>& nodes, size_t end_index) const {
    std::vector<Point> path;
    last_bypass_count_ = 0;
    size_t index = end_index;
    while (nodes[index].parent != static_cast<size_t>(-1)) {
        const AStarNode& node = nodes[index];
        const AStarNode& parent = nodes[node.parent];
        path.push_back(node.position);
        if (node.used_redundant) {
            const Point exit{node.redundant_via.x, node.redundant_via.y, node.position.z};
            const Point entry{node.redundant_via.x, node.redundant_via.y, parent.position.z};
            if (exit != node.position) path.push_back(exit);
            if (entry != parent.position) path.push_back(entry);
            ++last_bypass_count_;
        }
        index = node.parent;
    }
    path.push_back(nodes[index].position);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<Point> Router::expand_path(const std::vector<Point>& compressed) const {
    if (compressed.empty()) return {};
    std::vector<Point> expanded{compressed.front()};
    for (size_t i = 1; i < compressed.size(); ++i) {
        const Point& to = compressed[i];
        Point from = expanded.back();
        if (from == to) continue;
        if (from.z != to.z) { expanded.push_back(to); continue; }
        const int32_t dx = to.x > from.x ? 1 : -1;
        const int32_t dy = to.y > from.y ? 1 : -1;
        while (from.x != to.x) { from.x += dx; expanded.push_back(from); }
        while (from.y != to.y) { from.y += dy; expanded.push_back(from); }
    }
    return expanded;
}

} // namespace tsvra
