#ifndef TSVRA_CONFIG_HPP
#define TSVRA_CONFIG_HPP

#include <cstdint>
#include <string>
#include <cmath>

namespace tsvra {

// 冗余布局枚举
enum class RedundancyLayout {
    LEGACY_CORNER4,   // 旧版：每个4×4区域4个角冗余TSV，全局扫描
    SHARED_SPARE,     // 文献正确：每个区域1个共享冗余TSV，覆盖范围搜索
    NONE              // 无冗余（基准对比）
};

// 失效分布模型枚举
enum class FailureModel {
    UNIFORM,          // 均匀i.i.d. Bernoulli（等价于cluster_strength=0）
    CLUSTERED         // 空间聚集（带连续参数strength和radius）
};

// 聚集参数
struct ClusterParams {
    double strength = 0.8;    // [0.0, 1.0]：0=均匀，1=最大聚集
    uint32_t radius = 4;      // ≥1：曼哈顿距离聚集核半径
};

// 失效模式枚举
enum class FailureMode {
    INITIAL_ONLY,      // a: 仅初始化时失效
    RUNTIME_ONLY,      // b: 仅运行时失效
    COMBINED           // c: 初始化 + 运行时失效
};

// 配置类
class Config {
public:
    Config();

    // 从配置文件加载
    bool load_from_file(const std::string& filename);

    // 从命令行参数解析
    bool parse_command_line(int argc, char* argv[]);

    // 验证配置
    bool validate() const;

    // 输出配置信息
    void print() const;

    // 输出JSON格式配置（用于--print-defaults和init消息）
    std::string to_json() const;

    // Getters
    uint32_t get_num_layers() const { return num_layers_; }
    uint32_t get_grid_factor() const { return grid_factor_; }
    uint32_t get_grid_size() const { return 4 * grid_factor_; }
    FailureMode get_failure_mode() const { return failure_mode_; }
    double get_failure_rate() const { return failure_rate_; }
    uint64_t get_vertical_delay() const { return vertical_delay_; }
    uint64_t get_horizontal_delay() const { return horizontal_delay_; }
    uint64_t get_simulation_cycles() const { return simulation_cycles_; }
    const std::string& get_output_prefix() const { return output_prefix_; }
    uint32_t get_random_seed() const { return random_seed_; }
    double get_lambda1() const { return lambda1_; }
    double get_lambda2() const { return lambda2_; }
    uint32_t get_max_path_length() const { return max_path_length_; }
    double get_reliability_min() const { return reliability_min_; }
    bool get_json_stream() const { return json_stream_; }
    RedundancyLayout get_redundancy_layout() const { return redundancy_layout_; }
    FailureModel get_failure_model() const { return failure_model_; }
    const ClusterParams& get_cluster_params() const { return cluster_params_; }
    bool get_print_defaults() const { return print_defaults_; }

    // Setters
    void set_num_layers(uint32_t n) { num_layers_ = n; }
    void set_grid_factor(uint32_t n) { grid_factor_ = n; }
    void set_failure_mode(FailureMode mode) { failure_mode_ = mode; }
    void set_failure_rate(double rate) { failure_rate_ = rate; }
    void set_vertical_delay(uint64_t delay) { vertical_delay_ = delay; }
    void set_horizontal_delay(uint64_t delay) { horizontal_delay_ = delay; }
    void set_simulation_cycles(uint64_t cycles) { simulation_cycles_ = cycles; }
    void set_output_prefix(const std::string& prefix) { output_prefix_ = prefix; }
    void set_random_seed(uint32_t seed) { random_seed_ = seed; }
    void set_lambda1(double v) { lambda1_ = v; }
    void set_lambda2(double v) { lambda2_ = v; }
    void set_max_path_length(uint32_t v) { max_path_length_ = v; }
    void set_reliability_min(double v) { reliability_min_ = v; }
    void set_json_stream(bool v) { json_stream_ = v; }
    void set_redundancy_layout(RedundancyLayout v) { redundancy_layout_ = v; }
    void set_failure_model(FailureModel v) { failure_model_ = v; }
    void set_cluster_params(const ClusterParams& v) { cluster_params_ = v; }

private:
    uint32_t num_layers_;           // 层数，默认4（文献最优）
    uint32_t grid_factor_;          // N = 4 * grid_factor，默认4
    FailureMode failure_mode_;      // 失效模式
    double failure_rate_;           // TSV失效率，默认1e-5（文献校准）
    uint64_t vertical_delay_;       // 垂直传输延迟，默认5（文献校准）
    uint64_t horizontal_delay_;     // 水平传输延迟，默认500（文献校准）
    uint64_t simulation_cycles_;    // 仿真周期数，默认100000（文献最低要求）
    std::string output_prefix_;     // 输出文件前缀，默认"tsvra_output"
    uint32_t random_seed_;          // 随机数种子，0表示使用随机设备
    double lambda1_;                // 拥塞权重（λ1），默认0.0
    double lambda2_;                // 风险权重（λ2），默认0.0
    uint32_t max_path_length_;      // 最大路径长度（L_max），0表示不限制
    double reliability_min_;        // 最小路径可靠性（R_min），0.0表示不约束
    bool json_stream_;              // JSON流式输出模式
    RedundancyLayout redundancy_layout_; // 冗余布局策略
    FailureModel failure_model_;    // 失效分布模型
    ClusterParams cluster_params_;  // 聚集参数
    bool print_defaults_;           // --print-defaults标志

    // 辅助函数
    FailureMode parse_failure_mode(const std::string& mode_str) const;
    std::string failure_mode_to_string(FailureMode mode) const;
    RedundancyLayout parse_redundancy_layout(const std::string& str) const;
    std::string redundancy_layout_to_string(RedundancyLayout layout) const;
    FailureModel parse_failure_model(const std::string& str) const;
    std::string failure_model_to_string(FailureModel model) const;
};

} // namespace tsvra

#endif // TSVRA_CONFIG_HPP

