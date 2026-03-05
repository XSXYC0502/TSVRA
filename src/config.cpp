#include "config.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <random>

namespace tsvra {

Config::Config()
    : num_layers_(2),
      grid_factor_(4),
      failure_mode_(FailureMode::INITIAL_ONLY),
      failure_rate_(1e-3),  // 调整为适合仿真的值（从1e-6调到1e-3）
      vertical_delay_(1),
      horizontal_delay_(1000),
      simulation_cycles_(100000),  // 默认仿真周期
      output_prefix_("tsvra_output"),
      random_seed_(0),
      lambda1_(0.0),
      lambda2_(0.0),
      max_path_length_(0),
      reliability_min_(0.0),
      json_stream_(false) {
}

bool Config::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // 解析键值对
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // 去除前后空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            if (key == "num_layers") {
                num_layers_ = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "grid_factor") {
                grid_factor_ = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "failure_mode") {
                failure_mode_ = parse_failure_mode(value);
            } else if (key == "failure_rate") {
                failure_rate_ = std::stod(value);
            } else if (key == "vertical_delay") {
                vertical_delay_ = std::stoull(value);
            } else if (key == "horizontal_delay") {
                horizontal_delay_ = std::stoull(value);
            } else if (key == "output_prefix") {
                output_prefix_ = value;
            } else if (key == "random_seed") {
                random_seed_ = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "simulation_cycles") {
                simulation_cycles_ = std::stoull(value);
            } else if (key == "lambda1") {
                lambda1_ = std::stod(value);
            } else if (key == "lambda2") {
                lambda2_ = std::stod(value);
            } else if (key == "max_path_length") {
                max_path_length_ = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "reliability_min") {
                reliability_min_ = std::stod(value);
            }
        }
    }
    
    return true;
}

bool Config::parse_command_line(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            std::cout << "TSVRA - TSV路由模拟器\n"
                      << "用法: tsvra [选项]\n"
                      << "选项:\n"
                      << "  --config <file>           从文件加载配置\n"
                      << "  --layers <n>              层数 (默认: 2)\n"
                      << "  --grid-factor <n>         网格因子 (默认: 4, 网格大小 = 4*n)\n"
                      << "  --failure-mode <a|b|c>    失效模式 (默认: a)\n"
                      << "  --failure-rate <rate>     失效率 (默认: 1e-6)\n"
                      << "  --vertical-delay <n>      垂直延迟 (默认: 1)\n"
                      << "  --horizontal-delay <n>    水平延迟 (默认: 1000)\n"
                      << "  --cycles <n>              仿真周期数 (默认: 100000)\n"
                      << "  --output <prefix>         输出文件前缀 (默认: tsvra_output)\n"
                      << "  --seed <n>                随机数种子 (默认: 0=随机)\n"
                      << "  --lambda1 <v>             拥塞权重λ1 (默认: 0.0=禁用)\n"
                      << "  --lambda2 <v>             风险权重λ2 (默认: 0.0=禁用)\n"
                      << "  --max-path-length <n>     最大路径跳数L_max (默认: 0=不限制)\n"
                      << "  --reliability-min <v>     最小路径可靠性R_min (默认: 0.0=不约束)\n"
                      << "  --json-stream             启用JSON流式输出模式\n"
                      << "  -h, --help                显示帮助信息\n";
            return false;
        } else if (arg == "--config" && i + 1 < argc) {
            load_from_file(argv[++i]);
        } else if (arg == "--layers" && i + 1 < argc) {
            num_layers_ = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--grid-factor" && i + 1 < argc) {
            grid_factor_ = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--failure-mode" && i + 1 < argc) {
            failure_mode_ = parse_failure_mode(argv[++i]);
        } else if (arg == "--failure-rate" && i + 1 < argc) {
            failure_rate_ = std::stod(argv[++i]);
        } else if (arg == "--vertical-delay" && i + 1 < argc) {
            vertical_delay_ = std::stoull(argv[++i]);
        } else if (arg == "--horizontal-delay" && i + 1 < argc) {
            horizontal_delay_ = std::stoull(argv[++i]);
        } else if (arg == "--cycles" && i + 1 < argc) {
            simulation_cycles_ = std::stoull(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_prefix_ = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            random_seed_ = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--lambda1" && i + 1 < argc) {
            lambda1_ = std::stod(argv[++i]);
        } else if (arg == "--lambda2" && i + 1 < argc) {
            lambda2_ = std::stod(argv[++i]);
        } else if (arg == "--max-path-length" && i + 1 < argc) {
            max_path_length_ = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--reliability-min" && i + 1 < argc) {
            reliability_min_ = std::stod(argv[++i]);
        } else if (arg == "--json-stream") {
            json_stream_ = true;
        }
    }
    
    return true;
}

bool Config::validate() const {
    if (num_layers_ < 2) {
        std::cerr << "错误: 层数必须至少为2" << std::endl;
        return false;
    }
    
    if (grid_factor_ < 1) {
        std::cerr << "错误: 网格因子必须至少为1" << std::endl;
        return false;
    }
    
    if (failure_rate_ < 0.0 || failure_rate_ > 1.0) {
        std::cerr << "错误: 失效率必须在0到1之间" << std::endl;
        return false;
    }
    
    if (vertical_delay_ == 0 || horizontal_delay_ == 0) {
        std::cerr << "错误: 传输延迟必须大于0" << std::endl;
        return false;
    }
    
    if (simulation_cycles_ == 0) {
        std::cerr << "错误: 仿真周期数必须大于0" << std::endl;
        return false;
    }

    if (lambda1_ < 0.0) {
        std::cerr << "错误: 拥塞权重λ1不能为负数" << std::endl;
        return false;
    }

    if (lambda2_ < 0.0) {
        std::cerr << "错误: 风险权重λ2不能为负数" << std::endl;
        return false;
    }

    if (reliability_min_ < 0.0 || reliability_min_ > 1.0) {
        std::cerr << "错误: 最小可靠性R_min必须在0到1之间" << std::endl;
        return false;
    }

    return true;
}

void Config::print() const {
    std::cout << "=== TSVRA 配置 ===\n"
              << "层数: " << num_layers_ << "\n"
              << "网格因子: " << grid_factor_ << " (网格大小: " << get_grid_size() << "x" << get_grid_size() << ")\n"
              << "失效模式: " << failure_mode_to_string(failure_mode_) << "\n"
              << "失效率: " << failure_rate_ << "\n"
              << "垂直延迟: " << vertical_delay_ << "\n"
              << "水平延迟: " << horizontal_delay_ << "\n"
              << "仿真周期: " << simulation_cycles_ << "\n"
              << "输出前缀: " << output_prefix_ << "\n"
              << "随机种子: " << (random_seed_ == 0 ? "随机" : std::to_string(random_seed_)) << "\n"
              << "拥塞权重λ1: " << lambda1_ << (lambda1_ == 0.0 ? " (禁用)" : "") << "\n"
              << "风险权重λ2: " << lambda2_ << (lambda2_ == 0.0 ? " (禁用)" : "") << "\n"
              << "最大路径长度L_max: " << (max_path_length_ == 0 ? "不限制" : std::to_string(max_path_length_)) << "\n"
              << "最小可靠性R_min: " << reliability_min_ << (reliability_min_ == 0.0 ? " (不约束)" : "") << "\n"
              << "==================\n";
}

FailureMode Config::parse_failure_mode(const std::string& mode_str) const {
    if (mode_str == "a" || mode_str == "INITIAL_ONLY") {
        return FailureMode::INITIAL_ONLY;
    } else if (mode_str == "b" || mode_str == "RUNTIME_ONLY") {
        return FailureMode::RUNTIME_ONLY;
    } else if (mode_str == "c" || mode_str == "COMBINED") {
        return FailureMode::COMBINED;
    }
    return FailureMode::INITIAL_ONLY;
}

std::string Config::failure_mode_to_string(FailureMode mode) const {
    switch (mode) {
        case FailureMode::INITIAL_ONLY:
            return "a (仅初始化失效)";
        case FailureMode::RUNTIME_ONLY:
            return "b (仅运行时失效)";
        case FailureMode::COMBINED:
            return "c (混合失效)";
    }
    return "未知";
}

} // namespace tsvra

