#include "config.hpp"
#include "grid.hpp"
#include "router.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>

#define ASSERT_TRUE(expression) do { if (!(expression)) { \
    std::cerr << "FAIL: " << #expression << " at line " << __LINE__ << '\n'; return false; } } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, tolerance) ASSERT_TRUE(std::abs((a) - (b)) <= (tolerance))

namespace {

tsvra::Config config(uint32_t layers = 3, uint32_t factor = 1) {
    tsvra::Config result;
    result.set_num_layers(layers);
    result.set_grid_factor(factor);
    result.set_redundancy_layout(tsvra::RedundancyLayout::NONE);
    result.set_failure_rate(0.0);
    return result;
}

void initialize(tsvra::Grid& grid, uint32_t seed = 123) {
    std::mt19937 initial(seed), runtime(seed + 1), hotspot(seed + 2);
    grid.initialize(initial, runtime, hotspot);
}

double mean(const tsvra::Grid::ProbabilityMap& map) {
    long double sum = 0.0L;
    size_t count = 0;
    for (const auto& layer : map) for (const auto& row : layer) for (const double p : row) {
        sum += p;
        ++count;
    }
    return static_cast<double>(sum / static_cast<long double>(count));
}

bool parse(tsvra::Config& cfg, std::vector<std::string> arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) argv.push_back(argument.data());
    return cfg.parse_command_line(static_cast<int>(argv.size()), argv.data());
}

bool rejects(std::vector<std::string> arguments) {
    auto cfg = config();
    try { return !parse(cfg, std::move(arguments)) || !cfg.validate(); }
    catch (const std::invalid_argument&) { return true; }
}

bool defaults_and_legacy_probability() {
    tsvra::Config cfg;
    ASSERT_EQ(cfg.get_vertical_delay(), 5U);
    ASSERT_EQ(cfg.get_horizontal_delay(), 500U);
    ASSERT_EQ(cfg.get_router_delay(), 15U);
    ASSERT_EQ(cfg.get_request_probability(), 0.5);
    ASSERT_EQ(cfg.get_power_config(), "power_model.ini");
    cfg.set_failure_rate(0.2);
    ASSERT_EQ(cfg.get_initial_failure_rate(), 0.2);
    ASSERT_EQ(cfg.get_runtime_failure_rate(), 0.2);
    cfg.set_initial_failure_rate(0.7);
    cfg.set_failure_rate(0.3);
    ASSERT_EQ(cfg.get_initial_failure_rate(), 0.7);
    ASSERT_EQ(cfg.get_runtime_failure_rate(), 0.3);
    cfg.set_runtime_failure_rate(0.01);
    cfg.set_failure_rate(0.4);
    ASSERT_EQ(cfg.get_initial_failure_rate(), 0.7);
    ASSERT_EQ(cfg.get_runtime_failure_rate(), 0.01);
    ASSERT_TRUE(cfg.validate());
    return true;
}

bool cli_validation_and_aliases() {
    auto cfg = config();
    ASSERT_TRUE(parse(cfg, {"tsvra", "--initial-failure-rate", "0.3", "--failure-rate", "0.1",
        "--runtime-failure-rate", "0.004", "--failure-model", "clustered", "--cluster-strength", "0.6",
        "--cluster-radius", "3", "--hotspot-centers", "0,1,0;2,3,1", "--router-delay", "17",
        "--request-probability", "0.25", "--power-config", "model.ini"}));
    ASSERT_TRUE(cfg.validate());
    ASSERT_EQ(cfg.get_failure_model(), tsvra::FailureModel::HOTSPOT);
    ASSERT_EQ(cfg.get_initial_failure_rate(), 0.3);
    ASSERT_EQ(cfg.get_runtime_failure_rate(), 0.004);
    ASSERT_EQ(cfg.get_hotspot_multiplier(), 4.0);
    ASSERT_EQ(cfg.get_hotspot_radius(), 3.0);
    ASSERT_EQ(cfg.get_hotspot_centers().size(), 2U);
    ASSERT_EQ(cfg.get_hotspot_centers()[1].z, 1);
    ASSERT_EQ(cfg.get_router_delay(), 17U);
    ASSERT_EQ(cfg.get_request_probability(), 0.25);
    ASSERT_EQ(cfg.get_power_config(), "model.ini");
    ASSERT_TRUE(rejects({"tsvra", "--unknown", "1"}));
    ASSERT_TRUE(rejects({"tsvra", "--router-delay"}));
    ASSERT_TRUE(rejects({"tsvra", "--failure-model", "misspelled"}));
    ASSERT_TRUE(rejects({"tsvra", "--failure-mode", "d"}));
    ASSERT_TRUE(rejects({"tsvra", "--redundancy", "magic"}));
    ASSERT_TRUE(rejects({"tsvra", "--failure-rate", "nan"}));
    ASSERT_TRUE(rejects({"tsvra", "--lambda2", "inf"}));
    ASSERT_TRUE(rejects({"tsvra", "--layers", "-1"}));
    ASSERT_TRUE(rejects({"tsvra", "--layers", "2abc"}));
    ASSERT_TRUE(rejects({"tsvra", "--seed", "4294967296"}));
    ASSERT_TRUE(rejects({"tsvra", "--request-probability", "1.2"}));
    ASSERT_TRUE(rejects({"tsvra", "--hotspot-radius", "0"}));
    ASSERT_TRUE(rejects({"tsvra", "--hotspot-multiplier", "0.5"}));
    ASSERT_TRUE(rejects({"tsvra", "--hotspot-centers", "0,0,2"}));
    ASSERT_TRUE(rejects({"tsvra", "--hotspot-centers", "0,0,0;"}));
    ASSERT_TRUE(rejects({"tsvra", "--hotspot-centers", "0,0,0,1"}));
    cfg.set_lambda1(std::numeric_limits<double>::quiet_NaN());
    ASSERT_TRUE(!cfg.validate());
    return true;
}

bool ini_values_and_cli_precedence() {
    const auto path = std::filesystem::temp_directory_path() / "tsvra_failure_tests_config.ini";
    {
        std::ofstream out(path);
        out << "num_layers = 3\r\ngrid_factor = 1\r\nredundancy = none\r\n"
            << "initial_failure_rate = 0.2\r\nfailure_rate = 0.01\r\nrouter_delay = 21\r\n"
            << "hotspot_centers = 0,0,0;3,3,1\r\nrequest_probability = 0.75\r\n";
    }
    auto cfg = config();
    ASSERT_TRUE(parse(cfg, {"tsvra", "--router-delay", "30", "--config", path.string()}));
    ASSERT_TRUE(cfg.validate());
    ASSERT_EQ(cfg.get_router_delay(), 30U);
    ASSERT_EQ(cfg.get_initial_failure_rate(), 0.2);
    ASSERT_EQ(cfg.get_runtime_failure_rate(), 0.01);
    ASSERT_EQ(cfg.get_hotspot_centers().size(), 2U);
    {
        std::ofstream out(path);
        out << "not_a_setting = 4\n";
    }
    bool rejected = false;
    try { cfg.load_from_file(path.string()); }
    catch (const std::invalid_argument& error) {
        rejected = std::string(error.what()).find(":1:") != std::string::npos;
    }
    std::filesystem::remove(path);
    ASSERT_TRUE(rejected);
    return true;
}

bool fixed_normalized_maps_and_interface_locality() {
    auto cfg = config();
    cfg.set_failure_mode(tsvra::FailureMode::COMBINED);
    cfg.set_initial_failure_rate(0.8);
    cfg.set_runtime_failure_rate(0.2);
    cfg.set_failure_model(tsvra::FailureModel::HOTSPOT);
    cfg.set_hotspot_radius(0.05);
    cfg.set_hotspot_multiplier(1000.0);
    cfg.set_hotspot_centers({{1, 1, 0}});
    tsvra::Grid grid(cfg);
    initialize(grid);
    ASSERT_EQ(grid.get_total_tsvs(), 32U);
    ASSERT_EQ(grid.get_probability_map(false).size(), 2U);
    ASSERT_NEAR(mean(grid.get_probability_map(false)), 0.8, 1e-14);
    ASSERT_NEAR(mean(grid.get_probability_map(true)), 0.2, 1e-14);
    ASSERT_EQ(grid.get_failure_probability(1, 1, 0, false), 1.0);
    ASSERT_EQ(grid.get_failure_probability(1, 1, 0, true), 1.0);
    ASSERT_EQ(grid.get_failure_probability(1, 1, 2, true), 0.0);
    ASSERT_EQ(grid.get_failure_probability(0, 0, 1, true), grid.get_failure_probability(1, 1, 1, true));
    ASSERT_TRUE(!grid.get_tsv(1, 1, 2)->is_failed());
    const auto initial = grid.get_probability_map(false);
    const auto runtime = grid.get_probability_map(true);
    grid.apply_runtime_failure(1000);
    ASSERT_EQ(grid.get_probability_map(false), initial);
    ASSERT_EQ(grid.get_probability_map(true), runtime);
    ASSERT_NEAR(mean(grid.get_probability_map(true)), 0.2, 1e-14);
    return true;
}

bool gaussian_max_and_extreme_multiplier() {
    auto cfg = config();
    cfg.set_failure_mode(tsvra::FailureMode::RUNTIME_ONLY);
    cfg.set_failure_model(tsvra::FailureModel::HOTSPOT);
    cfg.set_failure_rate(0.0001);
    cfg.set_hotspot_radius(1.0);
    cfg.set_hotspot_multiplier(5.0);
    cfg.set_hotspot_centers({{0, 0, 0}, {2, 0, 0}});
    tsvra::Grid grid(cfg);
    initialize(grid);
    const double baseline = grid.get_failure_probability(0, 0, 1, true);
    ASSERT_NEAR(grid.get_failure_probability(0, 0, 0, true) / baseline, 5.0, 1e-12);
    ASSERT_NEAR(grid.get_failure_probability(1, 0, 0, true) / baseline, 1.0 + 4.0 * std::exp(-0.5), 1e-12);
    cfg.set_hotspot_centers({{0, 0, 0}, {2, 0, 0}, {0, 0, 0}});
    tsvra::Grid duplicate(cfg);
    initialize(duplicate);
    ASSERT_EQ(grid.get_probability_map(true), duplicate.get_probability_map(true));
    cfg.set_hotspot_radius(0.01);
    cfg.set_hotspot_multiplier(std::numeric_limits<double>::max());
    cfg.set_initial_failure_rate(0.6);
    cfg.set_runtime_failure_rate(0.9);
    tsvra::Grid extreme(cfg);
    initialize(extreme);
    ASSERT_NEAR(mean(extreme.get_probability_map(false)), 0.6, 1e-14);
    ASSERT_NEAR(mean(extreme.get_probability_map(true)), 0.9, 1e-14);
    for (const auto& layer : extreme.get_probability_map(true)) for (const auto& row : layer) for (double p : row)
        ASSERT_TRUE(std::isfinite(p) && p >= 0.0 && p <= 1.0);
    return true;
}

bool deterministic_centers_and_streams() {
    auto cfg = config(4, 2);
    cfg.set_failure_mode(tsvra::FailureMode::RUNTIME_ONLY);
    cfg.set_failure_model(tsvra::FailureModel::HOTSPOT);
    cfg.set_hotspot_count(4);
    cfg.set_runtime_failure_rate(0.01);
    std::mt19937 initial_a(1), runtime_a(2), hotspot_a(3);
    std::mt19937 initial_b(777), runtime_b(888), hotspot_b(3);
    tsvra::Grid a(cfg), b(cfg);
    a.initialize(initial_a, runtime_a, hotspot_a);
    b.initialize(initial_b, runtime_b, hotspot_b);
    ASSERT_EQ(a.get_hotspot_centers(), b.get_hotspot_centers());
    ASSERT_EQ(a.get_probability_map(true), b.get_probability_map(true));
    ASSERT_EQ(initial_a, std::mt19937(1)); // No startup draws in runtime-only mode.
    cfg.set_hotspot_centers({{1, 2, 0}, {3, 4, 1}});
    std::mt19937 initial_c(1), runtime_c(2), hotspot_c(3);
    tsvra::Grid manual(cfg);
    manual.initialize(initial_c, runtime_c, hotspot_c);
    ASSERT_EQ(hotspot_c, std::mt19937(3)); // Manual centers do not consume a stream.
    cfg.set_failure_model(tsvra::FailureModel::UNIFORM);
    std::mt19937 initial_d(1), runtime_d(2), hotspot_d(3);
    tsvra::Grid uniform(cfg);
    uniform.initialize(initial_d, runtime_d, hotspot_d);
    ASSERT_EQ(hotspot_d, std::mt19937(3));
    ASSERT_TRUE(uniform.get_hotspot_centers().empty());
    ASSERT_NEAR(mean(uniform.get_probability_map(true)), 0.01, 1e-15);
    return true;
}

bool zero_one_absorbing_failures_and_top_layer() {
    auto cfg = config();
    cfg.set_failure_mode(tsvra::FailureMode::RUNTIME_ONLY);
    tsvra::Grid zero(cfg);
    initialize(zero);
    ASSERT_EQ(zero.get_pending_runtime_failures(), 0U);
    ASSERT_TRUE(zero.apply_runtime_failure(1000000).empty());
    cfg.set_runtime_failure_rate(1.0);
    tsvra::Grid one(cfg);
    initialize(one);
    ASSERT_EQ(one.get_pending_runtime_failures(), 32U);
    ASSERT_EQ(one.apply_runtime_failure(0).size(), 32U);
    ASSERT_EQ(one.get_pending_runtime_failures(), 0U);
    ASSERT_EQ(one.count_failed_tsvs(), 32U);
    ASSERT_TRUE(one.apply_runtime_failure(0).empty());
    ASSERT_TRUE(one.apply_runtime_failure(100).empty());
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
        ASSERT_TRUE(!one.get_tsv(x, y, 2)->is_failed());
    cfg.set_failure_mode(tsvra::FailureMode::COMBINED);
    cfg.set_initial_failure_rate(1.0);
    tsvra::Grid initial_all(cfg);
    initialize(initial_all);
    ASSERT_EQ(initial_all.count_failed_tsvs(), 32U);
    ASSERT_EQ(initial_all.get_pending_runtime_failures(), 0U);
    initial_all.get_tsv(0, 0, 0)->set_failed(false);
    ASSERT_EQ(initial_all.count_failed_tsvs(), 31U); // Manual changes remain visible to diagnostics.
    return true;
}

bool geometric_survival_and_cycle_jumps() {
    auto cfg = config(2, 2);
    cfg.set_failure_mode(tsvra::FailureMode::RUNTIME_ONLY);
    cfg.set_runtime_failure_rate(0.03);
    uint64_t failures = 0;
    constexpr uint32_t repetitions = 512;
    for (uint32_t seed = 0; seed < repetitions; ++seed) {
        tsvra::Grid grid(cfg);
        initialize(grid, seed * 7 + 1);
        failures += grid.apply_runtime_failure(9).size(); // Ten Bernoulli-equivalent trials.
    }
    const double observed = static_cast<double>(failures) / (64.0 * repetitions);
    const double expected = 1.0 - std::pow(0.97, 10);
    ASSERT_NEAR(observed, expected, 0.012);
    tsvra::Grid stepped(cfg), jumped(cfg);
    initialize(stepped);
    initialize(jumped);
    size_t total = 0;
    for (uint64_t cycle = 0; cycle <= 37; ++cycle) total += stepped.apply_runtime_failure(cycle).size();
    ASSERT_EQ(jumped.apply_runtime_failure(37).size(), total);
    ASSERT_EQ(stepped.get_pending_runtime_failures(), jumped.get_pending_runtime_failures());
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
        ASSERT_EQ(stepped.get_tsv(x, y, 0)->is_failed(), jumped.get_tsv(x, y, 0)->is_failed());
    ASSERT_TRUE(jumped.apply_runtime_failure(37).empty());
    return true;
}

bool lower_z_failure_availability_and_horizontal_independence() {
    auto cfg = config(2);
    cfg.set_max_path_length(1);
    tsvra::Grid grid(cfg);
    initialize(grid);
    tsvra::Router router(grid, cfg);
    const tsvra::Point lower{0, 0, 0}, upper{0, 0, 1};
    ASSERT_EQ(grid.get_vertical_channel(lower, upper), grid.get_tsv(0, 0, 0));
    ASSERT_EQ(grid.get_vertical_channel(upper, lower), grid.get_tsv(0, 0, 0));
    ASSERT_TRUE(grid.get_vertical_channel(lower, {1, 0, 1}) == nullptr);
    grid.get_tsv(0, 0, 0)->set_failed(true);
    ASSERT_TRUE(!router.route(lower, upper, 0));
    ASSERT_TRUE(!router.route(upper, lower, 0));
    ASSERT_TRUE(router.route(lower, {1, 0, 0}, 0).has_value());
    grid.get_tsv(0, 0, 0)->set_failed(false);
    grid.get_tsv(0, 0, 0)->set_available_at(100);
    grid.get_tsv(0, 0, 1)->set_available_at(999999);
    grid.get_tsv(0, 0, 1)->set_failed(true);
    const auto up = router.route(lower, upper, 0);
    const auto down = router.route(upper, lower, 0);
    ASSERT_TRUE(up && down);
    ASSERT_EQ(up->second, 120U);
    ASSERT_EQ(down->second, 120U);
    ASSERT_EQ(router.calculate_path_delay({lower, upper}), 20U);
    ASSERT_EQ(router.calculate_path_delay({lower, {1, 0, 0}}), 515U);
    return true;
}

bool penalties_leave_physical_time_and_duration_reliability() {
    auto cfg = config(2);
    cfg.set_failure_mode(tsvra::FailureMode::RUNTIME_ONLY);
    cfg.set_runtime_failure_rate(0.01);
    cfg.set_max_path_length(1);
    cfg.set_lambda1(100000.0);
    cfg.set_lambda2(1000000.0);
    tsvra::Grid grid(cfg);
    initialize(grid);
    grid.get_tsv(0, 0, 0)->increment_usage();
    tsvra::Router router(grid, cfg);
    const auto direct = router.route({0, 0, 0}, {0, 0, 1}, 100);
    ASSERT_TRUE(direct);
    ASSERT_EQ(direct->second, 120U);
    const auto dispatched = router.route({0, 0, 0}, {0, 0, 1}, 100, 0, 3);
    ASSERT_TRUE(dispatched);
    ASSERT_EQ(dispatched->second, 123U);
    cfg.set_reliability_min(0.80); // 0.99^23 = 0.79361... includes dispatch exposure.
    ASSERT_TRUE(!router.route({0, 0, 0}, {0, 0, 1}, 100, 0, 3));
    cfg.set_reliability_min(0.79);
    ASSERT_TRUE(router.route({0, 0, 0}, {0, 0, 1}, 100, 0, 3));
    ASSERT_TRUE(!router.route({0, 0, 0}, {0, 0, 1}, std::numeric_limits<uint64_t>::max(), 0, 1));
    cfg.set_reliability_min(0.82); // 0.99^20 = 0.8179...
    ASSERT_TRUE(!router.route({0, 0, 0}, {0, 0, 1}, 100));
    cfg.set_reliability_min(0.81);
    ASSERT_TRUE(router.route({0, 0, 0}, {0, 0, 1}, 100));
    grid.get_tsv(0, 0, 0)->set_available_at(135);
    cfg.set_reliability_min(0.6); // 55 cycles of exposure, 0.99^55 = 0.57535...
    ASSERT_TRUE(!router.route({0, 0, 0}, {0, 0, 1}, 100));
    cfg.set_reliability_min(0.57);
    const auto waited = router.route({0, 0, 0}, {0, 0, 1}, 100);
    ASSERT_TRUE(waited);
    ASSERT_EQ(waited->second, 155U);
    return true;
}

bool hotspot_risk_selects_a_detour() {
    auto cfg = config(2);
    cfg.set_failure_mode(tsvra::FailureMode::RUNTIME_ONLY);
    cfg.set_runtime_failure_rate(0.0001);
    cfg.set_failure_model(tsvra::FailureModel::HOTSPOT);
    cfg.set_hotspot_centers({{0, 0, 0}});
    cfg.set_hotspot_radius(0.01);
    cfg.set_hotspot_multiplier(100.0);
    cfg.set_max_path_length(3);
    tsvra::Grid grid(cfg);
    initialize(grid);
    tsvra::Router router(grid, cfg);
    const auto fast = router.route({0, 0, 0}, {0, 0, 1}, 0);
    ASSERT_TRUE(fast);
    ASSERT_EQ(fast->second, 20U);
    cfg.set_lambda2(1000000.0);
    const auto safe = router.route({0, 0, 0}, {0, 0, 1}, 0);
    ASSERT_TRUE(safe);
    ASSERT_EQ(safe->first.size(), 4U);
    ASSERT_EQ(safe->second, 1050U);
    ASSERT_EQ(safe->second, router.calculate_path_delay(safe->first));
    cfg.set_lambda2(0.0);
    cfg.set_reliability_min(0.99);
    const auto reliable = router.route({0, 0, 0}, {0, 0, 1}, 0);
    ASSERT_TRUE(reliable);
    ASSERT_EQ(reliable->second, 1050U);
    return true;
}

bool bypass_delays_and_constraints() {
    auto cfg = config(2, 4);
    cfg.set_redundancy_layout(tsvra::RedundancyLayout::SHARED_SPARE);
    cfg.set_max_path_length(5);
    cfg.set_max_horizontal_distance(4);
    tsvra::Grid grid(cfg);
    initialize(grid);
    ASSERT_EQ(grid.get_total_redundant_tsvs(), 25U);
    ASSERT_EQ(grid.get_spare_sites().size(), 25U);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
        if (!grid.get_tsv(x, y, 0)->is_redundant()) grid.get_tsv(x, y, 0)->set_failed(true);
    tsvra::Router router(grid, cfg);
    const auto result = router.route({0, 0, 0}, {0, 0, 1}, 0);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->first.size(), 6U);
    ASSERT_EQ(router.get_last_bypass_count(), 1U);
    ASSERT_EQ(result->second, 2080U); // Four horizontal hops and one vertical, each with router delay.
    ASSERT_EQ(router.calculate_path_delay(result->first), 2080U);
    cfg.set_max_path_length(4);
    ASSERT_TRUE(!router.route({0, 0, 0}, {0, 0, 1}, 0));
    cfg.set_max_path_length(5);
    cfg.set_max_horizontal_distance(3);
    ASSERT_TRUE(!router.route({0, 0, 0}, {0, 0, 1}, 0));
    return true;
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, bool(*)()>> tests = {
        {"defaults_and_legacy_probability", defaults_and_legacy_probability},
        {"cli_validation_and_aliases", cli_validation_and_aliases},
        {"ini_values_and_cli_precedence", ini_values_and_cli_precedence},
        {"fixed_normalized_maps_and_interface_locality", fixed_normalized_maps_and_interface_locality},
        {"gaussian_max_and_extreme_multiplier", gaussian_max_and_extreme_multiplier},
        {"deterministic_centers_and_streams", deterministic_centers_and_streams},
        {"zero_one_absorbing_failures_and_top_layer", zero_one_absorbing_failures_and_top_layer},
        {"geometric_survival_and_cycle_jumps", geometric_survival_and_cycle_jumps},
        {"lower_z_failure_availability_and_horizontal_independence", lower_z_failure_availability_and_horizontal_independence},
        {"penalties_leave_physical_time_and_duration_reliability", penalties_leave_physical_time_and_duration_reliability},
        {"hotspot_risk_selects_a_detour", hotspot_risk_selects_a_detour},
        {"bypass_delays_and_constraints", bypass_delays_and_constraints}
    };
    size_t failed = 0;
    for (const auto& [name, test] : tests) {
        const bool passed = test();
        std::cout << name << ": " << (passed ? "PASS" : "FAIL") << '\n';
        if (!passed) ++failed;
    }
    std::cout << tests.size() - failed << '/' << tests.size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
