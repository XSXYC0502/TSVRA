#include "config.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace {

std::string trimmed(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

uint64_t parse_unsigned(const std::string& value, const std::string& key) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument(key + " requires a nonnegative integer, got '" + value + "'");
    try { return std::stoull(value); }
    catch (const std::exception&) { throw std::invalid_argument(key + " integer is too large: '" + value + "'"); }
}

uint32_t parse_u32(const std::string& value, const std::string& key) {
    const uint64_t result = parse_unsigned(value, key);
    if (result > std::numeric_limits<uint32_t>::max())
        throw std::invalid_argument(key + " must fit in a 32-bit unsigned integer");
    return static_cast<uint32_t>(result);
}

double parse_double(const std::string& value, const std::string& key) {
    size_t consumed = 0;
    double result = 0.0;
    try { result = std::stod(value, &consumed); }
    catch (const std::exception&) { throw std::invalid_argument(key + " requires a finite number, got '" + value + "'"); }
    if (consumed != value.size() || !std::isfinite(result))
        throw std::invalid_argument(key + " requires a finite number, got '" + value + "'");
    return result;
}

std::vector<tsvra::Point> parse_centers(const std::string& value) {
    if (value.empty()) return {};
    std::vector<tsvra::Point> result;
    std::istringstream input(value);
    std::string center;
    while (std::getline(input, center, ';')) {
        std::istringstream coordinates(center);
        std::string item;
        int32_t xyz[3] = {};
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::getline(coordinates, item, ','))
                throw std::invalid_argument("hotspot_centers requires 'x,y,z;x,y,z' with lower-interface z");
            const uint64_t parsed = parse_unsigned(trimmed(item), "hotspot_centers coordinate");
            if (parsed > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
                throw std::invalid_argument("hotspot_centers coordinates must fit in a nonnegative 32-bit integer");
            xyz[axis] = static_cast<int32_t>(parsed);
        }
        if (std::getline(coordinates, item, ',') || center.empty() || center.back() == ',')
            throw std::invalid_argument("hotspot_centers requires exactly three coordinates per center");
        result.emplace_back(xyz[0], xyz[1], xyz[2]);
    }
    if (value.back() == ';') throw std::invalid_argument("hotspot_centers contains an empty trailing center");
    return result;
}

std::string json_string(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '"' || c == '\\') out << '\\' << raw;
        else if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
        else out << raw;
    }
    out << '"';
    return out.str();
}

} // namespace

namespace tsvra {

Config::Config()
    : num_layers_(4), grid_factor_(4), failure_mode_(FailureMode::INITIAL_ONLY),
      failure_rate_(1e-5), initial_failure_rate_(1e-5), runtime_failure_rate_(1e-5),
      initial_failure_rate_explicit_(false), runtime_failure_rate_explicit_(false),
      vertical_delay_(5), horizontal_delay_(500), router_delay_(15),
      request_probability_(0.5), power_config_("code/config/power_model.ini"),
      simulation_cycles_(100000), output_prefix_("code/output/tsvra_output"), random_seed_(0),
      lambda1_(0.0), lambda2_(0.0), max_path_length_(0), max_horizontal_distance_(0),
      reliability_min_(0.0), json_stream_(false),
      redundancy_layout_(RedundancyLayout::SHARED_SPARE), failure_model_(FailureModel::UNIFORM),
      cluster_params_{0.8, 4}, hotspot_params_{}, print_defaults_(false) {}

void Config::set_option(const std::string& key, const std::string& value) {
    if (key == "num_layers") num_layers_ = parse_u32(value, key);
    else if (key == "grid_factor") grid_factor_ = parse_u32(value, key);
    else if (key == "failure_mode") failure_mode_ = parse_failure_mode(value);
    else if (key == "failure_rate") set_failure_rate(parse_double(value, key));
    else if (key == "initial_failure_rate") set_initial_failure_rate(parse_double(value, key));
    else if (key == "runtime_failure_rate") set_runtime_failure_rate(parse_double(value, key));
    else if (key == "vertical_delay") vertical_delay_ = parse_unsigned(value, key);
    else if (key == "horizontal_delay") horizontal_delay_ = parse_unsigned(value, key);
    else if (key == "router_delay") router_delay_ = parse_unsigned(value, key);
    else if (key == "simulation_cycles") simulation_cycles_ = parse_unsigned(value, key);
    else if (key == "output_prefix") output_prefix_ = value;
    else if (key == "random_seed") random_seed_ = parse_u32(value, key);
    else if (key == "request_probability") request_probability_ = parse_double(value, key);
    else if (key == "power_config") power_config_ = value;
    else if (key == "lambda1") lambda1_ = parse_double(value, key);
    else if (key == "lambda2") lambda2_ = parse_double(value, key);
    else if (key == "max_path_length") max_path_length_ = parse_u32(value, key);
    else if (key == "max_horizontal_distance") max_horizontal_distance_ = parse_u32(value, key);
    else if (key == "reliability_min") reliability_min_ = parse_double(value, key);
    else if (key == "redundancy") redundancy_layout_ = parse_redundancy_layout(value);
    else if (key == "failure_model") failure_model_ = parse_failure_model(value);
    else if (key == "hotspot_count") hotspot_params_.count = parse_u32(value, key);
    else if (key == "hotspot_radius") hotspot_params_.radius = parse_double(value, key);
    else if (key == "hotspot_multiplier") hotspot_params_.multiplier = parse_double(value, key);
    else if (key == "hotspot_centers") hotspot_params_.centers = parse_centers(value);
    else if (key == "cluster_strength") {
        cluster_params_.strength = parse_double(value, key);
        hotspot_params_.multiplier = 1.0 + 5.0 * cluster_params_.strength;
    } else if (key == "cluster_radius") {
        cluster_params_.radius = parse_u32(value, key);
        hotspot_params_.radius = static_cast<double>(cluster_params_.radius);
    } else if (key == "json_stream") {
        if (value == "true" || value == "1") json_stream_ = true;
        else if (value == "false" || value == "0") json_stream_ = false;
        else throw std::invalid_argument("json_stream must be true or false");
    } else throw std::invalid_argument("unknown configuration option '" + key + "'");
}

bool Config::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open config file: " << filename << '\n';
        return false;
    }
    std::string line;
    size_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        line = trimmed(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        try {
            const size_t separator = line.find('=');
            if (separator == std::string::npos) throw std::invalid_argument("expected key = value");
            set_option(trimmed(line.substr(0, separator)), trimmed(line.substr(separator + 1)));
        } catch (const std::exception& error) {
            throw std::invalid_argument(filename + ":" + std::to_string(line_number) + ": " + error.what());
        }
    }
    return true;
}

bool Config::parse_command_line(int argc, char* argv[]) {
    // Load files before CLI values, regardless of where --config appears.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "TSVRA - TSV Routing Simulator\nUsage: tsvra [options]\n"
                      << "  --config <file>           Load INI configuration\n"
                      << "  --layers <n>              Number of layers (default: 4)\n"
                      << "  --grid-factor <n>         Grid width = 4*n (default: 4)\n"
                      << "  --failure-mode <a|b|c>    Initial/runtime/combined failures\n"
                      << "  --failure-rate <p>        Legacy shorthand for both probabilities\n"
                      << "  --initial-failure-rate <p> Startup probability [0,1]\n"
                      << "  --runtime-failure-rate <p> Conditional per-cycle probability [0,1]\n"
                      << "  --failure-model <model>   uniform|hotspot (clustered is a legacy alias)\n"
                      << "  --hotspot-count <n>       Seeded centers (default: 1)\n"
                      << "  --hotspot-radius <r>      Gaussian sigma >0 (default: 4)\n"
                      << "  --hotspot-multiplier <m>  Peak weight >=1 (default: 5)\n"
                      << "  --hotspot-centers <list>  Manual 'x,y,z;x,y,z', z is lower interface\n"
                      << "  --cluster-strength <s>   Legacy alias: multiplier=1+5*s, s in [0,1]\n"
                      << "  --cluster-radius <r>     Legacy integer alias for hotspot radius\n"
                      << "  --vertical-delay <n>      Vertical link delay (default: 5)\n"
                      << "  --horizontal-delay <n>    Horizontal link delay (default: 500)\n"
                      << "  --router-delay <n>        Router delay per actual hop (default: 15)\n"
                      << "  --request-probability <p> Request probability per cycle (default: 0.5)\n"
                      << "  --power-config <file>     Power configuration (default: code/config/power_model.ini)\n"
                      << "  --cycles <n>              Simulation cycles (default: 100000)\n"
                      << "  --output <prefix>         Output prefix (default: code/output/tsvra_output)\n"
                      << "  --seed <n>                Seed (0=random)\n"
                      << "  --lambda1 <v>             Congestion weight >=0\n"
                      << "  --lambda2 <v>             Risk weight >=0\n"
                      << "  --max-path-length <n>     Maximum actual hops (0=unlimited)\n"
                      << "  --max-horizontal-distance <n> Maximum horizontal hops (0=unlimited)\n"
                      << "  --reliability-min <p>     Minimum route reliability [0,1]\n"
                      << "  --redundancy <layout>     shared|corner4|none\n"
                      << "  --json-stream             Enable JSON output\n"
                      << "  --print-defaults          Print configuration JSON and exit\n"
                      << "  -h, --help                Show help\n";
            return false;
        }
        if (arg == "--config") {
            if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0)
                throw std::invalid_argument("--config requires a filename");
            const std::string filename = argv[++i];
            if (!load_from_file(filename)) throw std::invalid_argument("failed to load --config '" + filename + "'");
        }
    }
    static const std::unordered_map<std::string, std::string> aliases = {
        {"--layers", "num_layers"}, {"--cycles", "simulation_cycles"},
        {"--output", "output_prefix"}, {"--seed", "random_seed"},
        {"--grid-factor", "grid_factor"}, {"--failure-mode", "failure_mode"},
        {"--failure-rate", "failure_rate"}, {"--initial-failure-rate", "initial_failure_rate"},
        {"--runtime-failure-rate", "runtime_failure_rate"}, {"--vertical-delay", "vertical_delay"},
        {"--horizontal-delay", "horizontal_delay"}, {"--router-delay", "router_delay"},
        {"--request-probability", "request_probability"}, {"--power-config", "power_config"},
        {"--lambda1", "lambda1"}, {"--lambda2", "lambda2"}, {"--max-path-length", "max_path_length"},
        {"--max-horizontal-distance", "max_horizontal_distance"}, {"--reliability-min", "reliability_min"},
        {"--redundancy", "redundancy"}, {"--failure-model", "failure_model"},
        {"--hotspot-count", "hotspot_count"}, {"--hotspot-radius", "hotspot_radius"},
        {"--hotspot-multiplier", "hotspot_multiplier"}, {"--hotspot-centers", "hotspot_centers"},
        {"--cluster-strength", "cluster_strength"}, {"--cluster-radius", "cluster_radius"}
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") { ++i; continue; }
        if (arg == "--json-stream") { json_stream_ = true; continue; }
        if (arg == "--print-defaults") { print_defaults_ = true; continue; }
        const auto option = aliases.find(arg);
        if (option == aliases.end())
            throw std::invalid_argument("unknown option '" + arg + "'; use --help to see supported options");
        if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0)
            throw std::invalid_argument(arg + " requires a value; use --help for its format");
        set_option(option->second, argv[++i]);
    }
    return true;
}

bool Config::validate() const {
    const auto error = [](const char* text) { std::cerr << "Error: " << text << '\n'; return false; };
    if (num_layers_ < 2 || num_layers_ > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
        return error("num_layers must be in [2,2147483647]");
    if (grid_factor_ < 1 || grid_factor_ > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) / 4)
        return error("grid_factor must be in [1,536870911]");
    if (redundancy_layout_ != RedundancyLayout::SHARED_SPARE &&
        redundancy_layout_ != RedundancyLayout::LEGACY_CORNER4 && redundancy_layout_ != RedundancyLayout::NONE)
        return error("redundancy must be shared, corner4, or none");
    if (redundancy_layout_ == RedundancyLayout::SHARED_SPARE && grid_factor_ < 3)
        return error("shared redundancy requires grid_factor >=3; choose corner4 or none for smaller grids");
    if (failure_mode_ != FailureMode::INITIAL_ONLY && failure_mode_ != FailureMode::RUNTIME_ONLY && failure_mode_ != FailureMode::COMBINED)
        return error("failure_mode must be a, b, or c");
    if (failure_model_ != FailureModel::UNIFORM && failure_model_ != FailureModel::HOTSPOT)
        return error("failure_model must be uniform or hotspot");
    const auto probability = [](double p) { return std::isfinite(p) && p >= 0.0 && p <= 1.0; };
    if (!probability(failure_rate_)) return error("failure_rate must be finite and in [0,1]");
    if (!probability(initial_failure_rate_)) return error("initial_failure_rate must be finite and in [0,1]");
    if (!probability(runtime_failure_rate_)) return error("runtime_failure_rate must be finite and in [0,1]");
    if (!probability(request_probability_)) return error("request_probability must be finite and in [0,1]");
    if (!probability(reliability_min_)) return error("reliability_min must be finite and in [0,1]");
    if (vertical_delay_ == 0 || horizontal_delay_ == 0) return error("vertical_delay and horizontal_delay must be >0");
    if (router_delay_ > std::numeric_limits<uint64_t>::max() - std::max(vertical_delay_, horizontal_delay_))
        return error("router_delay plus each link delay must fit in a 64-bit cycle count");
    if (simulation_cycles_ == 0) return error("simulation_cycles must be >0");
    if (!std::isfinite(lambda1_) || lambda1_ < 0.0) return error("lambda1 must be finite and >=0");
    if (!std::isfinite(lambda2_) || lambda2_ < 0.0) return error("lambda2 must be finite and >=0");
    if (!probability(cluster_params_.strength)) return error("cluster_strength must be finite and in [0,1]");
    if (cluster_params_.radius == 0) return error("cluster_radius must be >=1");
    if (!std::isfinite(hotspot_params_.radius) || hotspot_params_.radius <= 0.0)
        return error("hotspot_radius must be finite and >0");
    if (!std::isfinite(hotspot_params_.multiplier) || hotspot_params_.multiplier < 1.0)
        return error("hotspot_multiplier must be finite and >=1");
    if (hotspot_params_.count == 0 && hotspot_params_.centers.empty()) return error("hotspot_count must be >=1");
    for (const Point& p : hotspot_params_.centers) {
        if (p.x < 0 || p.y < 0 || p.z < 0 || static_cast<uint32_t>(p.x) >= get_grid_size() ||
            static_cast<uint32_t>(p.y) >= get_grid_size() || static_cast<uint32_t>(p.z) >= num_layers_ - 1)
            return error("hotspot_centers require 0<=x,y<grid_size and 0<=z<num_layers-1 (lower interface)");
    }
    if (power_config_.empty()) return error("power_config requires a nonempty file path");
    if (output_prefix_.empty()) return error("output_prefix must not be empty");
    return true;
}

void Config::print() const {
    std::cout << "=== TSVRA Config ===\nLayers: " << num_layers_ << "\nGrid: " << get_grid_size() << 'x' << get_grid_size()
              << "\nFailure mode: " << failure_mode_to_string(failure_mode_)
              << "\nFailure model: " << failure_model_to_string(failure_model_)
              << "\nInitial failure probability: " << initial_failure_rate_
              << "\nRuntime failure probability per cycle: " << runtime_failure_rate_
              << "\nVertical delay: " << vertical_delay_ << "\nHorizontal delay: " << horizontal_delay_
              << "\nRouter delay per hop: " << router_delay_ << "\nRequest probability: " << request_probability_
              << "\nPower config: " << power_config_ << "\nSimulation cycles: " << simulation_cycles_
              << "\nOutput prefix: " << output_prefix_
              << "\nRandom seed: " << (random_seed_ == 0 ? "random" : std::to_string(random_seed_))
              << "\nCongestion weight lambda1: " << lambda1_ << "\nRisk weight lambda2: " << lambda2_
              << "\nMax path hops: " << max_path_length_ << "\nMax horizontal hops: " << max_horizontal_distance_
              << "\nMin reliability: " << reliability_min_ << "\nRedundancy: " << redundancy_layout_to_string(redundancy_layout_);
    if (failure_model_ == FailureModel::HOTSPOT)
        std::cout << "\nHotspot centers: " << (hotspot_params_.centers.empty() ? hotspot_params_.count : hotspot_params_.centers.size())
                  << "\nHotspot radius: " << hotspot_params_.radius << "\nHotspot multiplier: " << hotspot_params_.multiplier;
    std::cout << "\n====================\n";
}

std::string Config::to_json() const {
    std::ostringstream out;
    out << std::setprecision(17) << "{\n"
        << "  \"num_layers\": " << num_layers_ << ",\n  \"grid_factor\": " << grid_factor_
        << ",\n  \"grid_size\": " << get_grid_size()
        << ",\n  \"failure_mode\": " << json_string(failure_mode_to_string(failure_mode_))
        << ",\n  \"failure_rate\": " << failure_rate_
        << ",\n  \"initial_failure_rate\": " << initial_failure_rate_
        << ",\n  \"runtime_failure_rate\": " << runtime_failure_rate_
        << ",\n  \"vertical_delay\": " << vertical_delay_ << ",\n  \"horizontal_delay\": " << horizontal_delay_
        << ",\n  \"router_delay\": " << router_delay_ << ",\n  \"request_probability\": " << request_probability_
        << ",\n  \"power_config\": " << json_string(power_config_)
        << ",\n  \"simulation_cycles\": " << simulation_cycles_
        << ",\n  \"output_prefix\": " << json_string(output_prefix_)
        << ",\n  \"random_seed\": " << random_seed_
        << ",\n  \"lambda1\": " << lambda1_ << ",\n  \"lambda2\": " << lambda2_
        << ",\n  \"max_path_length\": " << max_path_length_
        << ",\n  \"max_horizontal_distance\": " << max_horizontal_distance_
        << ",\n  \"reliability_min\": " << reliability_min_
        << ",\n  \"redundancy\": " << json_string(redundancy_layout_to_string(redundancy_layout_))
        << ",\n  \"failure_model\": " << json_string(failure_model_to_string(failure_model_))
        << ",\n  \"hotspot_count\": " << hotspot_params_.count
        << ",\n  \"hotspot_radius\": " << hotspot_params_.radius
        << ",\n  \"hotspot_multiplier\": " << hotspot_params_.multiplier
        << ",\n  \"hotspot_centers\": [";
    for (size_t i = 0; i < hotspot_params_.centers.size(); ++i) {
        if (i > 0) out << ',';
        const Point& p = hotspot_params_.centers[i];
        out << "{\"x\":" << p.x << ",\"y\":" << p.y << ",\"z\":" << p.z << '}';
    }
    out << "],\n  \"cluster_params\": {\"cluster_strength\": " << cluster_params_.strength
        << ", \"cluster_radius\": " << cluster_params_.radius << "}\n}";
    return out.str();
}

FailureMode Config::parse_failure_mode(const std::string& value) const {
    if (value == "a" || value == "INITIAL_ONLY") return FailureMode::INITIAL_ONLY;
    if (value == "b" || value == "RUNTIME_ONLY") return FailureMode::RUNTIME_ONLY;
    if (value == "c" || value == "COMBINED") return FailureMode::COMBINED;
    throw std::invalid_argument("failure_mode must be a, b, or c; got '" + value + "'");
}

std::string Config::failure_mode_to_string(FailureMode mode) const {
    switch (mode) {
        case FailureMode::INITIAL_ONLY: return "a";
        case FailureMode::RUNTIME_ONLY: return "b";
        case FailureMode::COMBINED: return "c";
    }
    return "unknown";
}

RedundancyLayout Config::parse_redundancy_layout(const std::string& value) const {
    if (value == "corner4" || value == "legacy") return RedundancyLayout::LEGACY_CORNER4;
    if (value == "shared" || value == "sharedspare") return RedundancyLayout::SHARED_SPARE;
    if (value == "none") return RedundancyLayout::NONE;
    throw std::invalid_argument("redundancy must be shared, corner4, or none; got '" + value + "'");
}

std::string Config::redundancy_layout_to_string(RedundancyLayout layout) const {
    switch (layout) {
        case RedundancyLayout::LEGACY_CORNER4: return "corner4";
        case RedundancyLayout::SHARED_SPARE: return "shared";
        case RedundancyLayout::NONE: return "none";
    }
    return "unknown";
}

FailureModel Config::parse_failure_model(const std::string& value) const {
    if (value == "uniform") return FailureModel::UNIFORM;
    if (value == "hotspot" || value == "clustered") return FailureModel::HOTSPOT;
    throw std::invalid_argument("failure_model must be uniform or hotspot; got '" + value + "'");
}

std::string Config::failure_model_to_string(FailureModel model) const {
    switch (model) {
        case FailureModel::UNIFORM: return "uniform";
        case FailureModel::HOTSPOT: return "hotspot";
    }
    return "unknown";
}

} // namespace tsvra
