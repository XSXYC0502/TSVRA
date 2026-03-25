#include "config.hpp"
#include "simulator.hpp"
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
        // Create configuration
        tsvra::Config config;

        // Parse command-line arguments
        if (!config.parse_command_line(argc, argv)) {
            return 0; // User requested help or other non-error exit
        }

        // --print-defaults: output default config JSON and exit
        if (config.get_print_defaults()) {
            std::cout << config.to_json() << std::endl;
            return 0;
        }

        // Validate configuration
        if (!config.validate()) {
            std::cerr << "Configuration validation failed\n";
            return 1;
        }

        bool json_mode = config.get_json_stream();

        // Print configuration (skip in json mode or output to stderr)
        if (!json_mode) {
            config.print();
        }

        // Create simulator
        tsvra::Simulator simulator(config);

        // Initialize
        simulator.initialize();

        // Run simulation
        simulator.run();

        if (!json_mode) {
            // Print statistics
            const auto& stats = simulator.get_statistics();
            stats.print_summary();

            // Export results to CSV
            std::string summary_file = config.get_output_prefix() + "_summary.csv";
            std::string requests_file = config.get_output_prefix() + "_requests.csv";

            stats.export_to_csv(summary_file);
            stats.export_requests_to_csv(requests_file);

            std::cout << "Simulation complete, results saved\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error\n";
        return -1;
    }
}
