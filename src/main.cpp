#include "config.hpp"
#include "simulator.hpp"
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
        // 创建配置
        tsvra::Config config;

        // 解析命令行参数
        if (!config.parse_command_line(argc, argv)) {
            return 0; // 用户请求帮助或其他非错误退出
        }

        // --print-defaults: 输出默认配置JSON并退出
        if (config.get_print_defaults()) {
            std::cout << config.to_json() << std::endl;
            return 0;
        }

        // 验证配置
        if (!config.validate()) {
            std::cerr << "配置验证失败\n";
            return 1;
        }

        bool json_mode = config.get_json_stream();

        // 打印配置（json模式下跳过或输出到stderr）
        if (!json_mode) {
            config.print();
        }

        // 创建仿真器
        tsvra::Simulator simulator(config);

        // 初始化
        simulator.initialize();

        // 运行仿真
        simulator.run();

        if (!json_mode) {
            // 打印统计结果
            const auto& stats = simulator.get_statistics();
            stats.print_summary();

            // 导出结果到CSV
            std::string summary_file = config.get_output_prefix() + "_summary.csv";
            std::string requests_file = config.get_output_prefix() + "_requests.csv";

            stats.export_to_csv(summary_file);
            stats.export_requests_to_csv(requests_file);

            std::cout << "仿真结束，结果已保存\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "未知错误\n";
        return -1;
    }
}
