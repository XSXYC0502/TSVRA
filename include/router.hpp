#ifndef TSVRA_ROUTER_HPP
#define TSVRA_ROUTER_HPP

#include "grid.hpp"
#include "request.hpp"
#include "config.hpp"
#include <functional>
#include <vector>
#include <optional>

namespace tsvra {

// 启发函数类型
using HeuristicFunc = std::function<double(const Point&, const Point&)>;

// A*路由器类
class Router {
public:
    explicit Router(Grid& grid, const Config& config);
    
    // 设置启发函数
    void set_heuristic(HeuristicFunc heuristic) { heuristic_ = heuristic; }
    
    // 为请求计算路由
    // 返回路径和预计完成时间
    std::optional<std::pair<std::vector<Point>, uint64_t>> route(
        const Point& start, 
        const Point& end, 
        uint64_t current_time
    );
    
    // 计算路径的传输时间
    uint64_t calculate_path_delay(const std::vector<Point>& path) const;

    // 上次route()调用中实际使用旁路绕行的次数
    uint32_t get_last_bypass_count() const { return last_bypass_count_; }
    
    // 默认曼哈顿距离启发函数
    static double manhattan_distance(const Point& a, const Point& b);
    
private:
    Grid& grid_;
    const Config& config_;
    HeuristicFunc heuristic_;
    mutable uint32_t last_bypass_count_ = 0; // 上次route()调用中实际使用旁路的次数
    
    // A*节点
    struct AStarNode {
        Point position;
        double g_cost;      // 从起点到当前点的实际代价
        double h_cost;      // 从当前点到终点的启发式代价
        double f_cost;      // g_cost + h_cost
        Point parent;       // 父节点位置
        bool has_parent;    // 是否有父节点
        Point redundant_via; // 如果使用了冗余TSV绕行，记录冗余TSV位置
        bool used_redundant; // 是否使用了冗余TSV
        uint32_t hop_count; // 从起点的跳数（用于L_max约束）
        double risk_sum;    // 累计风险 ∑(-ln(1-pfail))（用于R_min约束）

        AStarNode()
            : position(), g_cost(0), h_cost(0), f_cost(0),
              parent(), has_parent(false), redundant_via(), used_redundant(false),
              hop_count(0), risk_sum(0.0) {}

        AStarNode(Point pos, double g, double h, Point p, bool hp,
                  Point rv = Point(), bool ur = false,
                  uint32_t hc = 0, double rs = 0.0)
            : position(pos), g_cost(g), h_cost(h), f_cost(g + h),
              parent(p), has_parent(hp), redundant_via(rv), used_redundant(ur),
              hop_count(hc), risk_sum(rs) {}
    };
    
    // A*算法辅助函数
    std::vector<Point> get_neighbors(const Point& pos) const;
    double calculate_move_cost(const Point& from, const Point& to) const;
    std::vector<Point> reconstruct_path(
        const std::vector<std::vector<std::vector<AStarNode>>>& nodes,
        const Point& start,
        const Point& end
    ) const;

    // 将压缩路径展开为单元相邻步（水平段逐格展开）
    std::vector<Point> expand_path(const std::vector<Point>& compressed) const;
};

} // namespace tsvra

#endif // TSVRA_ROUTER_HPP

