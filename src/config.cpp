#include "config.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <random>
#include <iomanip>

namespace {

void trim_ascii_whitespace(std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        value.clear();
        return;
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
}

} // namespace

namespace tsvra {

Config::Config()
    : num_layers_(4),
      grid_factor_(4),
      failure_mode_(FailureMode::INITIAL_ONLY),
      failure_rate_(1e-5),
      vertical_delay_(5),
      horizontal_delay_(500),
      simulation_cycles_(100000),
      output_prefix_("tsvra_output"),
      random_seed_(0),
      lambda1_(0.0),
      lambda2_(0.0),
      max_path_length_(0),
      max_horizontal_distance_(0),
      reliability_min_(0.0),
      json_stream_(false),
      redundancy_layout_(RedundancyLayout::SHARED_SPARE),
      failure_model_(FailureModel::UNIFORM),
      cluster_params_{0.8, 4},
      print_defaults_(false) {
}

bool Config::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open config file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // 去除前后空格和Windows风格换行残留
            trim_ascii_whitespace(key);
            trim_ascii_whitespace(value);

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
            } else if (key == "max_horizontal_distance") {
                max_horizontal_distance_ = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "reliability_min") {
                reliability_min_ = std::stod(value);
            } else if (key == "redundancy") {
                redundancy_layout_ = parse_redundancy_layout(value);
            } else if (key == "failure_model") {
                failure_model_ = parse_failure_model(value);
            } else if (key == "cluster_strength") {
                cluster_params_.strength = std::stod(value);
            } else if (key == "cluster_radius") {
                cluster_params_.radius = static_cast<uint32_t>(std::stoul(value));
            }
        }
    }

    return true;
}

bool Config::parse_command_line(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            std::cout << "TSVRA - TSV Routing Simulator\n"
                      << "Usage: tsvra [options]\n"
                      << "Options:\n"
                      << "  --config <file>           Load config from file\n"
                      << "  --layers <n>              Number of layers (default: 4)\n"
                      << "  --grid-factor <n>         Grid factor (default: 4, grid size = 4*n)\n"
                      << "  --failure-mode <a|b|c>    Failure mode (default: a)\n"
                      << "  --failure-rate <rate>      Failure rate (default: 1e-5)\n"
                      << "  --vertical-delay <n>      Vertical delay (default: 5)\n"
                      << "  --horizontal-delay <n>    Horizontal delay (default: 500)\n"
                      << "  --cycles <n>              Simulation cycles (default: 100000)\n"
                      << "  --output <prefix>         Output file prefix (default: tsvra_output)\n"
                      << "  --seed <n>                Random seed (default: 0=random)\n"
                      << "  --lambda1 <v>             Congestion weight lambda1 (default: 0.0=disabled)\n"
                      << "  --lambda2 <v>             Risk weight lambda2 (default: 0.0=disabled)\n"
                      << "  --max-path-length <n>     Max path hops L_max (default: 0=unlimited)\n"
                      << "  --max-horizontal-distance <n> Max horizontal hops H_max (default: 0=unlimited)\n"
                      << "  --reliability-min <v>     Min path reliability R_min (default: 0.0=unconstrained)\n"
                      << "  --redundancy <layout>     Redundancy layout: corner4|shared|none (default: shared)\n"
                      << "  --failure-model <model>   Failure model: uniform|clustered (default: uniform)\n"
                      << "  --cluster-strength <v>    Cluster strength [0.0-1.0] (default: 0.8)\n"
                      << "  --cluster-radius <n>      Cluster radius >=1 (default: 4)\n"
                      << "  --json-stream             Enable JSON streaming output mode\n"
                      << "  --print-defaults          Print default config JSON and exit\n"
                      << "  -h, --help                Show help\n";
            return false;
        } else if (arg == "--print-defaults") {
            print_defaults_ = true;
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
        } else if (arg == "--max-horizontal-distance" && i + 1 < argc) {
            max_horizontal_distance_ = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--reliability-min" && i + 1 < argc) {
            reliability_min_ = std::stod(argv[++i]);
        } else if (arg == "--redundancy" && i + 1 < argc) {
            redundancy_layout_ = parse_redundancy_layout(argv[++i]);
        } else if (arg == "--failure-model" && i + 1 < argc) {
            failure_model_ = parse_failure_model(argv[++i]);
        } else if (arg == "--cluster-strength" && i + 1 < argc) {
            cluster_params_.strength = std::stod(argv[++i]);
        } else if (arg == "--cluster-radius" && i + 1 < argc) {
            cluster_params_.radius = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--json-stream") {
            json_stream_ = true;
        }
    }

    return true;
}

bool Config::validate() const {
    if (num_layers_ < 2) {
        std::cerr << "Error: num_layers must be at least 2" << std::endl;
        return false;
    }

    if (grid_factor_ < 1) {
        std::cerr << "Error: grid_factor must be at least 1" << std::endl;
        return false;
    }

    if (redundancy_layout_ == RedundancyLayout::SHARED_SPARE && grid_factor_ < 3) {
        std::cerr << "Error: SharedSpare layout requires grid_factor >= 3" << std::endl;
        return false;
    }

    if (failure_rate_ < 0.0 || failure_rate_ > 1.0) {
        std::cerr << "Error: failure_rate must be between 0 and 1" << std::endl;
        return false;
    }

    if (vertical_delay_ == 0 || horizontal_delay_ == 0) {
        std::cerr << "Error: transmission delay must be > 0" << std::endl;
        return false;
    }

    if (simulation_cycles_ == 0) {
        std::cerr << "Error: simulation_cycles must be > 0" << std::endl;
        return false;
    }

    if (lambda1_ < 0.0) {
        std::cerr << "Error: congestion weight lambda1 must not be negative" << std::endl;
        return false;
    }

    if (lambda2_ < 0.0) {
        std::cerr << "Error: risk weight lambda2 must not be negative" << std::endl;
        return false;
    }

    if (reliability_min_ < 0.0 || reliability_min_ > 1.0) {
        std::cerr << "Error: reliability_min must be between 0 and 1" << std::endl;
        return false;
    }

    if (cluster_params_.strength < 0.0 || cluster_params_.strength > 1.0) {
        std::cerr << "Error: cluster_strength must be in [0.0, 1.0]" << std::endl;
        return false;
    }

    if (cluster_params_.radius < 1) {
        std::cerr << "Error: cluster_radius must be >= 1" << std::endl;
        return false;
    }

    return true;
}

void Config::print() const {
    std::cout << "=== TSVRA Config ===\n"
              << "Layers: " << num_layers_ << "\n"
              << "Grid factor: " << grid_factor_ << " (grid size: " << get_grid_size() << "x" << get_grid_size() << ")\n"
              << "Failure mode: " << failure_mode_to_string(failure_mode_) << "\n"
              << "Failure rate: " << failure_rate_ << "\n"
              << "Vertical delay: " << vertical_delay_ << "\n"
              << "Horizontal delay: " << horizontal_delay_ << "\n"
              << "Simulation cycles: " << simulation_cycles_ << "\n"
              << "Output prefix: " << output_prefix_ << "\n"
              << "Random seed: " << (random_seed_ == 0 ? "random" : std::to_string(random_seed_)) << "\n"
              << "Congestion weight lambda1: " << lambda1_ << (lambda1_ == 0.0 ? " (disabled)" : "") << "\n"
              << "Risk weight lambda2: " << lambda2_ << (lambda2_ == 0.0 ? " (disabled)" : "") << "\n"
              << "Max path length L_max: " << (max_path_length_ == 0 ? "unlimited" : std::to_string(max_path_length_)) << "\n"
              << "Max horizontal distance H_max: " << (max_horizontal_distance_ == 0 ? "unlimited" : std::to_string(max_horizontal_distance_)) << "\n"
              << "Min reliability R_min: " << reliability_min_ << (reliability_min_ == 0.0 ? " (unconstrained)" : "") << "\n"
              << "Redundancy layout: " << redundancy_layout_to_string(redundancy_layout_) << "\n"
              << "Failure model: " << failure_model_to_string(failure_model_) << "\n";
    if (failure_model_ == FailureModel::CLUSTERED) {
        std::cout << "  Cluster strength: " << cluster_params_.strength << "\n"
                  << "  Cluster radius: " << cluster_params_.radius << "\n";
    }
    std::cout << "====================\n";
}

std::string Config::to_json() const {
    std::ostringstream oss;
    oss << std::setprecision(10);
    oss << "{\n"
        << "  \"num_layers\": " << num_layers_ << ",\n"
        << "  \"grid_factor\": " << grid_factor_ << ",\n"
        << "  \"grid_size\": " << get_grid_size() << ",\n"
        << "  \"failure_mode\": \"" << failure_mode_to_string(failure_mode_) << "\",\n"
        << "  \"failure_rate\": " << failure_rate_ << ",\n"
        << "  \"vertical_delay\": " << vertical_delay_ << ",\n"
        << "  \"horizontal_delay\": " << horizontal_delay_ << ",\n"
        << "  \"simulation_cycles\": " << simulation_cycles_ << ",\n"
        << "  \"output_prefix\": \"" << output_prefix_ << "\",\n"
        << "  \"random_seed\": " << random_seed_ << ",\n"
        << "  \"lambda1\": " << lambda1_ << ",\n"
        << "  \"lambda2\": " << lambda2_ << ",\n"
        << "  \"max_path_length\": " << max_path_length_ << ",\n"
        << "  \"max_horizontal_distance\": " << max_horizontal_distance_ << ",\n"
        << "  \"reliability_min\": " << reliability_min_ << ",\n"
        << "  \"redundancy\": {\n"
        << "    \"layout\": \"" << redundancy_layout_to_string(redundancy_layout_) << "\",\n"
        << "    \"spares_per_region\": " << (redundancy_layout_ == RedundancyLayout::LEGACY_CORNER4 ? 4 : (redundancy_layout_ == RedundancyLayout::SHARED_SPARE ? 1 : 0)) << ",\n"
        << "    \"redundancy_ratio\": " << (redundancy_layout_ == RedundancyLayout::LEGACY_CORNER4 ? 0.25 : (redundancy_layout_ == RedundancyLayout::SHARED_SPARE ? 0.09765625 : 0.0)) << "\n"
        << "  },\n"
        << "  \"failure_model\": {\n"
        << "    \"type\": \"" << failure_model_to_string(failure_model_) << "\",\n"
        << "    \"cluster_strength\": " << cluster_params_.strength << ",\n"
        << "    \"cluster_radius\": " << cluster_params_.radius << "\n"
        << "  }\n"
        << "}";
    return oss.str();
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
            return "a";
        case FailureMode::RUNTIME_ONLY:
            return "b";
        case FailureMode::COMBINED:
            return "c";
    }
    return "unknown";
}

RedundancyLayout Config::parse_redundancy_layout(const std::string& str) const {
    if (str == "corner4" || str == "legacy") {
        return RedundancyLayout::LEGACY_CORNER4;
    } else if (str == "shared" || str == "sharedspare") {
        return RedundancyLayout::SHARED_SPARE;
    } else if (str == "none") {
        return RedundancyLayout::NONE;
    }
    return RedundancyLayout::SHARED_SPARE;
}

std::string Config::redundancy_layout_to_string(RedundancyLayout layout) const {
    switch (layout) {
        case RedundancyLayout::LEGACY_CORNER4: return "corner4";
        case RedundancyLayout::SHARED_SPARE:   return "shared";
        case RedundancyLayout::NONE:           return "none";
    }
    return "unknown";
}

FailureModel Config::parse_failure_model(const std::string& str) const {
    if (str == "uniform") {
        return FailureModel::UNIFORM;
    } else if (str == "clustered") {
        return FailureModel::CLUSTERED;
    }
    return FailureModel::UNIFORM;
}

std::string Config::failure_model_to_string(FailureModel model) const {
    switch (model) {
        case FailureModel::UNIFORM:   return "uniform";
        case FailureModel::CLUSTERED: return "clustered";
    }
    return "unknown";
}

} // namespace tsvra
