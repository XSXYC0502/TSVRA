#include "config.hpp"
#include "tsv.hpp"
#include "grid.hpp"
#include "router.hpp"
#include "request_generator.hpp"
#include "statistics.hpp"
#include "simulator.hpp"
#include <iostream>
#include <set>
#include <cstdlib>
#include <cmath>
#include <algorithm>

// ============================================================
// Simple test framework
// ============================================================
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "  FAIL: " << #expr << " (line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "  FAIL: " << #a << " == " << #b \
                      << " (" << (a) << " != " << (b) << ") (line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "  FAIL: " << #a << " != " << #b \
                      << " (both == " << (a) << ") (line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define ASSERT_GT(a, b) \
    do { \
        if (!((a) > (b))) { \
            std::cerr << "  FAIL: " << #a << " > " << #b \
                      << " (" << (a) << " <= " << (b) << ") (line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define ASSERT_GE(a, b) \
    do { \
        if (!((a) >= (b))) { \
            std::cerr << "  FAIL: " << #a << " >= " << #b \
                      << " (" << (a) << " < " << (b) << ") (line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define ASSERT_LT(a, b) \
    do { \
        if (!((a) < (b))) { \
            std::cerr << "  FAIL: " << #a << " < " << #b \
                      << " (" << (a) << " >= " << (b) << ") (line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define ASSERT_LE(a, b) \
    do { \
        if (!((a) <= (b))) { \
            std::cerr << "  FAIL: " << #a << " <= " << #b \
                      << " (" << (a) << " > " << (b) << ") (line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

static void run_test(const char* name, bool (*test_fn)()) {
    tests_run++;
    std::cout << "TEST " << name << " ... ";
    std::cout.flush();
    if (test_fn()) {
        tests_passed++;
        std::cout << "PASS\n";
    } else {
        tests_failed++;
        std::cout << "FAIL\n";
    }
}

// ============================================================
// Helper: build a minimal Config
// ============================================================
static tsvra::Config make_config(
    uint32_t layers = 3,
    uint32_t grid_factor = 1,
    uint64_t cycles = 2000,
    uint32_t seed = 42,
    tsvra::RedundancyLayout layout = tsvra::RedundancyLayout::SHARED_SPARE,
    tsvra::FailureMode fmode = tsvra::FailureMode::INITIAL_ONLY,
    double frate = 1e-5
) {
    tsvra::Config cfg;
    cfg.set_num_layers(layers);
    cfg.set_grid_factor(grid_factor);
    cfg.set_simulation_cycles(cycles);
    cfg.set_random_seed(seed);
    cfg.set_redundancy_layout(layout);
    cfg.set_failure_mode(fmode);
    cfg.set_failure_rate(frate);
    return cfg;
}
// ============================================================
// TSV unit tests
// ============================================================
bool test_tsv_default_state() {
    tsvra::TSV tsv(2, 3, 1, false);
    ASSERT_EQ(tsv.get_x(), 2);
    ASSERT_EQ(tsv.get_y(), 3);
    ASSERT_EQ(tsv.get_z(), 1);
    ASSERT_FALSE(tsv.is_failed());
    ASSERT_FALSE(tsv.is_redundant());
    ASSERT_TRUE(tsv.is_available(0));
    ASSERT_EQ(tsv.get_usage_count(), 0u);
    return true;
}

bool test_tsv_redundant_flag() {
    tsvra::TSV tsv(0, 0, 0, true);
    ASSERT_TRUE(tsv.is_redundant());
    return true;
}

bool test_tsv_fail_unavailable() {
    tsvra::TSV tsv(0, 0, 0);
    tsv.set_failed(true);
    ASSERT_TRUE(tsv.is_failed());
    ASSERT_FALSE(tsv.is_available(9999));
    tsv.set_failed(false);
    ASSERT_TRUE(tsv.is_available(0));
    return true;
}

bool test_tsv_occupy_until() {
    tsvra::TSV tsv(0, 0, 0);
    tsv.occupy_until(100);
    ASSERT_FALSE(tsv.is_available(50));
    ASSERT_FALSE(tsv.is_available(99));
    ASSERT_TRUE(tsv.is_available(100));
    ASSERT_TRUE(tsv.is_available(200));
    // occupy_until should only extend, never shrink
    tsv.occupy_until(50);
    ASSERT_FALSE(tsv.is_available(99));
    ASSERT_TRUE(tsv.is_available(100));
    return true;
}

bool test_tsv_usage_count() {
    tsvra::TSV tsv(0, 0, 0);
    tsv.increment_usage();
    tsv.increment_usage();
    ASSERT_EQ(tsv.get_usage_count(), 2u);
    tsv.decrement_usage();
    ASSERT_EQ(tsv.get_usage_count(), 1u);
    tsv.decrement_usage();
    ASSERT_EQ(tsv.get_usage_count(), 0u);
    // decrement at zero must not underflow
    tsv.decrement_usage();
    ASSERT_EQ(tsv.get_usage_count(), 0u);
    return true;
}

// ============================================================
// Config tests
// ============================================================
bool test_config_defaults() {
    tsvra::Config cfg;
    ASSERT_EQ(cfg.get_num_layers(), 4u);
    ASSERT_EQ(cfg.get_grid_factor(), 4u);
    ASSERT_EQ(cfg.get_grid_size(), 16u);  // 4*4
    ASSERT_GT(cfg.get_simulation_cycles(), 0u);
    ASSERT_GT(cfg.get_vertical_delay(), 0u);
    ASSERT_GT(cfg.get_horizontal_delay(), 0u);
    return true;
}

bool test_config_grid_size_formula() {
    tsvra::Config cfg;
    cfg.set_grid_factor(3);
    ASSERT_EQ(cfg.get_grid_size(), 12u);
    cfg.set_grid_factor(2);
    ASSERT_EQ(cfg.get_grid_size(), 8u);
    cfg.set_grid_factor(1);
    ASSERT_EQ(cfg.get_grid_size(), 4u);
    return true;
}

bool test_config_setters_roundtrip() {
    tsvra::Config cfg;
    cfg.set_num_layers(6);
    cfg.set_failure_rate(3e-5);
    cfg.set_vertical_delay(10);
    cfg.set_horizontal_delay(800);
    cfg.set_simulation_cycles(50000);
    cfg.set_random_seed(123);
    cfg.set_max_horizontal_distance(12);
    ASSERT_EQ(cfg.get_num_layers(), 6u);
    ASSERT_EQ(cfg.get_random_seed(), 123u);
    ASSERT_EQ(cfg.get_vertical_delay(), 10u);
    ASSERT_EQ(cfg.get_horizontal_delay(), 800u);
    ASSERT_EQ(cfg.get_simulation_cycles(), 50000u);
    ASSERT_EQ(cfg.get_max_horizontal_distance(), 12u);
    return true;
}
// ============================================================
// Grid tests
// ============================================================
bool test_grid_dimensions() {
    auto cfg = make_config(3, 2);  // 3 layers, grid_factor=2 -> 8x8
    std::mt19937 rng(42);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    ASSERT_EQ(grid.get_grid_size(), 8u);
    ASSERT_EQ(grid.get_num_layers(), 3u);
    ASSERT_EQ(grid.get_total_tsvs(), 8u * 8u * 3u);
    return true;
}

bool test_grid_valid_position() {
    auto cfg = make_config(3, 1);  // 4x4x3
    std::mt19937 rng(1);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    ASSERT_TRUE(grid.is_valid_position(0, 0, 0));
    ASSERT_TRUE(grid.is_valid_position(3, 3, 2));
    ASSERT_FALSE(grid.is_valid_position(-1, 0, 0));
    ASSERT_FALSE(grid.is_valid_position(0, -1, 0));
    ASSERT_FALSE(grid.is_valid_position(0, 0, -1));
    ASSERT_FALSE(grid.is_valid_position(4, 0, 0));  // out of 4x4
    ASSERT_FALSE(grid.is_valid_position(0, 4, 0));
    ASSERT_FALSE(grid.is_valid_position(0, 0, 3));  // only layers 0..2
    return true;
}

bool test_grid_get_tsv() {
    auto cfg = make_config(2, 1);  // 4x4x2
    std::mt19937 rng(7);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                const tsvra::TSV* tsv = grid.get_tsv(x, y, z);
                ASSERT_TRUE(tsv != nullptr);
                ASSERT_EQ(tsv->get_x(), x);
                ASSERT_EQ(tsv->get_y(), y);
                ASSERT_EQ(tsv->get_z(), z);
            }
    ASSERT_TRUE(grid.get_tsv(-1, 0, 0) == nullptr);
    ASSERT_TRUE(grid.get_tsv(0, 99, 0) == nullptr);
    return true;
}

bool test_grid_no_failures_by_default() {
    // With failure_rate=0 and INITIAL_ONLY, no TSVs should fail
    tsvra::Config cfg = make_config(2, 1, 1000, 42,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    std::mt19937 rng(42);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    ASSERT_EQ(grid.count_failed_tsvs(), 0u);
    return true;
}

bool test_grid_redundant_tsv_count_shared_spare() {
    // grid_factor=4 -> 16x16, SharedSpare: 5x5=25 spares per layer
    auto cfg = make_config(2, 4, 1000, 1,
        tsvra::RedundancyLayout::SHARED_SPARE);
    std::mt19937 rng(1);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    // 25 spares per layer * 2 layers = 50
    ASSERT_EQ(grid.get_total_redundant_tsvs(), 50u);
    return true;
}

bool test_grid_redundant_tsv_count_none() {
    auto cfg = make_config(2, 4, 1000, 1,
        tsvra::RedundancyLayout::NONE);
    std::mt19937 rng(1);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    ASSERT_EQ(grid.get_total_redundant_tsvs(), 0u);
    return true;
}

bool test_grid_find_bypass_spare_shared() {
    auto cfg = make_config(2, 4, 1000, 1,
        tsvra::RedundancyLayout::SHARED_SPARE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    std::mt19937 rng(1);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    // Fail a normal (non-redundant) TSV at (0,0,0) and look for bypass
    tsvra::TSV* tsv = grid.get_tsv(0, 0, 0);
    ASSERT_TRUE(tsv != nullptr);
    // Only test if not already redundant
    if (!tsv->is_redundant()) {
        tsv->set_failed(true);
        auto spare = grid.find_bypass_spare({0, 0, 0}, 0);
        // Should find a spare (region has spare at (1,1) for SharedSpare)
        ASSERT_TRUE(spare.has_value());
    }
    return true;
}
// ============================================================
// Router tests
// ============================================================
bool test_router_simple_vertical() {
    // Minimal 4x4 grid, 2 layers, route from (0,0,0) to (0,0,1)
    auto cfg = make_config(2, 1, 1000, 1,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    std::mt19937 rng(1);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    tsvra::Router router(grid, cfg);
    tsvra::Point start(0, 0, 0), end(0, 0, 1);
    auto result = router.route(start, end, 0);
    ASSERT_TRUE(result.has_value());
    auto [path, t] = result.value();
    ASSERT_FALSE(path.empty());
    ASSERT_EQ(path.front().z, 0);
    ASSERT_EQ(path.back().z, 1);
    ASSERT_GT(t, 0u);
    return true;
}

bool test_router_path_connectivity() {
    // Every consecutive step must be adjacent (Manhattan distance 1)
    auto cfg = make_config(3, 1, 1000, 42,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    std::mt19937 rng(42);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    tsvra::Router router(grid, cfg);
    tsvra::Point start(0, 0, 0), end(3, 3, 2);
    auto result = router.route(start, end, 0);
    ASSERT_TRUE(result.has_value());
    auto [path, t] = result.value();
    ASSERT_GE(path.size(), 2u);
    for (size_t i = 1; i < path.size(); ++i) {
        int dx = std::abs(path[i].x - path[i-1].x);
        int dy = std::abs(path[i].y - path[i-1].y);
        int dz = std::abs(path[i].z - path[i-1].z);
        int manhattan = dx + dy + dz;
        ASSERT_EQ(manhattan, 1);
    }
    return true;
}

bool test_router_no_route_when_all_failed() {
    // 3-layer grid: route (0,0,0)->(0,0,2). Block entire layer 1 so no
    // path can cross from layer 0 to layer 2.
    auto cfg = make_config(3, 1, 1000, 5,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    std::mt19937 rng(5);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    // Fail all TSVs in layer 1 (intermediate) — cuts every upward path
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            tsvra::TSV* tsv = grid.get_tsv(x, y, 1);
            if (tsv) tsv->set_failed(true);
        }
    tsvra::Router router(grid, cfg);
    auto result = router.route({0,0,0}, {0,0,2}, 0);
    ASSERT_FALSE(result.has_value());
    return true;
}

bool test_router_delay_calculation() {
    auto cfg = make_config(2, 1, 1000, 1,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    cfg.set_vertical_delay(5);
    cfg.set_horizontal_delay(500);
    std::mt19937 rng(1);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    tsvra::Router router(grid, cfg);
    // Pure vertical path: (0,0,0)->(0,0,1) costs vertical_delay
    std::vector<tsvra::Point> path = {{0,0,0},{0,0,1}};
    uint64_t delay = router.calculate_path_delay(path);
    ASSERT_EQ(delay, 5u);
    // Pure horizontal: (0,0,0)->(1,0,0) costs horizontal_delay
    std::vector<tsvra::Point> hpath = {{0,0,0},{1,0,0}};
    uint64_t hdelay = router.calculate_path_delay(hpath);
    ASSERT_EQ(hdelay, 500u);
    return true;
}

bool test_router_manhattan_heuristic() {
    tsvra::Point a(0, 0, 0), b(3, 4, 2);
    double d = tsvra::Router::manhattan_distance(a, b);
    ASSERT_EQ(static_cast<int>(d), 3 + 4 + 2);
    // symmetric
    ASSERT_EQ(tsvra::Router::manhattan_distance(b, a), d);
    // same point
    ASSERT_EQ(tsvra::Router::manhattan_distance(a, a), 0.0);
    return true;
}

bool test_router_horizontal_distance_limit() {
    auto cfg = make_config(3, 1, 1000, 42,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    cfg.set_max_horizontal_distance(5);
    std::mt19937 rng(42);
    tsvra::Grid grid(cfg);
    grid.initialize(rng);
    tsvra::Router router(grid, cfg);
    auto result = router.route({0, 0, 0}, {3, 3, 2}, 0);
    ASSERT_FALSE(result.has_value());

    cfg.set_max_horizontal_distance(6);
    std::mt19937 rng2(42);
    tsvra::Grid grid2(cfg);
    grid2.initialize(rng2);
    tsvra::Router router2(grid2, cfg);
    auto exact_result = router2.route({0, 0, 0}, {3, 3, 2}, 0);
    ASSERT_TRUE(exact_result.has_value());

    uint64_t horizontal_distance = 0;
    const auto& path = exact_result->first;
    for (size_t i = 1; i < path.size(); ++i) {
        horizontal_distance += static_cast<uint64_t>(
            std::abs(path[i].x - path[i - 1].x) + std::abs(path[i].y - path[i - 1].y));
    }
    ASSERT_EQ(horizontal_distance, 6u);
    return true;
}
// ============================================================
// RequestGenerator tests
// ============================================================
bool test_reqgen_no_top_layer_start() {
    auto cfg = make_config(4, 1, 0, 42);
    std::mt19937 rng(42);
    tsvra::RequestGenerator gen(cfg, rng);
    gen.initialize_hotspots();
    int32_t top = static_cast<int32_t>(cfg.get_num_layers() - 1);
    for (uint64_t c = 0; c < 100000; ++c) {
        auto req = gen.generate_request(c);
        if (req) {
            ASSERT_LT(req->start.z, top);
            ASSERT_EQ(req->end.z, top);
        }
    }
    return true;
}

bool test_reqgen_all_start_layers_covered() {
    auto cfg = make_config(4, 1, 0, 7);
    std::mt19937 rng(7);
    tsvra::RequestGenerator gen(cfg, rng);
    gen.initialize_hotspots();
    std::set<int32_t> seen_layers;
    for (uint64_t c = 0; c < 200000; ++c) {
        auto req = gen.generate_request(c);
        if (req) seen_layers.insert(req->start.z);
    }
    // layers 0..num_layers-2 should all appear
    for (int32_t z = 0; z < 3; ++z)
        ASSERT_TRUE(seen_layers.count(z) > 0);
    return true;
}

bool test_reqgen_positions_in_bounds() {
    auto cfg = make_config(3, 2, 0, 99);  // 8x8x3
    std::mt19937 rng(99);
    tsvra::RequestGenerator gen(cfg, rng);
    gen.initialize_hotspots();
    uint32_t gs = cfg.get_grid_size();
    int32_t top = static_cast<int32_t>(cfg.get_num_layers() - 1);
    for (uint64_t c = 0; c < 50000; ++c) {
        auto req = gen.generate_request(c);
        if (req) {
            ASSERT_GE(req->start.x, 0);
            ASSERT_GE(req->start.y, 0);
            ASSERT_LT(req->start.x, static_cast<int32_t>(gs));
            ASSERT_LT(req->start.y, static_cast<int32_t>(gs));
            ASSERT_GE(req->end.x, 0);
            ASSERT_GE(req->end.y, 0);
            ASSERT_LT(req->end.x, static_cast<int32_t>(gs));
            ASSERT_LT(req->end.y, static_cast<int32_t>(gs));
            ASSERT_EQ(req->end.z, top);
        }
    }
    return true;
}

bool test_reqgen_ids_monotonically_increasing() {
    auto cfg = make_config(3, 1, 0, 11);
    std::mt19937 rng(11);
    tsvra::RequestGenerator gen(cfg, rng);
    gen.initialize_hotspots();
    uint64_t last_id = 0;
    bool first = true;
    for (uint64_t c = 0; c < 50000; ++c) {
        auto req = gen.generate_request(c);
        if (req) {
            if (!first) ASSERT_GT(req->id, last_id);
            last_id = req->id;
            first = false;
        }
    }
    return true;
}

bool test_reqgen_hotspot_weights_positive() {
    auto cfg = make_config(3, 2, 0, 13);
    std::mt19937 rng(13);
    tsvra::RequestGenerator gen(cfg, rng);
    gen.initialize_hotspots();
    uint32_t nr = gen.get_num_regions();
    for (uint32_t ry = 0; ry < nr; ++ry)
        for (uint32_t rx = 0; rx < nr; ++rx)
            ASSERT_GT(gen.get_hotspot_weight(rx, ry), 0.0);
    return true;
}
// ============================================================
// Statistics tests
// ============================================================
bool test_stats_initial_state() {
    auto cfg = make_config();
    tsvra::Statistics stats(cfg);
    ASSERT_EQ(stats.get_total_requests(), 0u);
    ASSERT_EQ(stats.get_completed_requests(), 0u);
    ASSERT_EQ(stats.get_failed_requests(), 0u);
    ASSERT_EQ(stats.get_in_flight(), 0u);
    ASSERT_EQ(stats.get_redundant_tsv_usages(), 0u);
    return true;
}

bool test_stats_record_generated() {
    auto cfg = make_config();
    tsvra::Statistics stats(cfg);
    stats.record_request_generated();
    stats.record_request_generated();
    ASSERT_EQ(stats.get_total_requests(), 2u);
    return true;
}

bool test_stats_record_completed() {
    auto cfg = make_config();
    tsvra::Statistics stats(cfg);
    tsvra::Request req;
    req.id = 1;
    req.start = {0,0,0};
    req.end   = {0,0,1};
    req.generate_time = 10;
    req.complete_time = 60;
    req.completed = true;
    req.path = {{0,0,0},{0,0,1}};
    req.status = tsvra::RequestStatus::COMPLETED;
    stats.record_request_generated();
    stats.record_request_completed(req);
    ASSERT_EQ(stats.get_completed_requests(), 1u);
    ASSERT_EQ(stats.get_total_latency(), 50u);
    ASSERT_EQ(stats.get_max_latency(), 50u);
    ASSERT_EQ(stats.get_min_latency(), 50u);
    return true;
}

bool test_stats_record_failed() {
    auto cfg = make_config();
    tsvra::Statistics stats(cfg);
    tsvra::Request req;
    req.id = 2;
    req.status = tsvra::RequestStatus::FAILED;
    stats.record_request_generated();
    stats.record_request_failed(req);
    ASSERT_EQ(stats.get_failed_requests(), 1u);
    return true;
}

bool test_stats_average_latency() {
    auto cfg = make_config();
    tsvra::Statistics stats(cfg);
    auto make_req = [](uint64_t id, uint64_t gen, uint64_t done) {
        tsvra::Request r;
        r.id = id;
        r.generate_time = gen;
        r.complete_time = done;
        r.completed = true;
        r.status = tsvra::RequestStatus::COMPLETED;
        return r;
    };
    stats.record_request_generated();
    stats.record_request_generated();
    stats.record_request_completed(make_req(1, 0, 100));
    stats.record_request_completed(make_req(2, 0, 200));
    double avg = stats.get_average_latency();
    ASSERT_GT(avg, 140.0);
    ASSERT_LT(avg, 160.0);
    return true;
}

bool test_stats_redundant_tsv_usage() {
    auto cfg = make_config();
    tsvra::Statistics stats(cfg);
    stats.record_redundant_tsv_usage();
    stats.record_redundant_tsv_usage();
    stats.record_redundant_tsv_usage();
    ASSERT_EQ(stats.get_redundant_tsv_usages(), 3u);
    return true;
}

bool test_stats_in_flight() {
    auto cfg = make_config();
    tsvra::Statistics stats(cfg);
    stats.record_in_flight(17);
    ASSERT_EQ(stats.get_in_flight(), 17u);
    return true;
}

bool test_stats_bypass_diagnostic_setters() {
    auto cfg = make_config();
    tsvra::Statistics stats(cfg);
    stats.set_failed_vertical_encounters(5);
    stats.set_spare_found(3);
    stats.set_spare_unavailable(2);
    ASSERT_EQ(stats.get_failed_vertical_encounters(), 5u);
    ASSERT_EQ(stats.get_spare_found(), 3u);
    ASSERT_EQ(stats.get_spare_unavailable(), 2u);
    return true;
}
// ============================================================
// End-to-end / Simulator integration tests
// ============================================================
bool test_e2e_requests_end_at_top_layer() {
    auto cfg = make_config(3, 1, 3000, 42,
        tsvra::RedundancyLayout::SHARED_SPARE,
        tsvra::FailureMode::INITIAL_ONLY, 1e-5);
    tsvra::Simulator sim(cfg);
    sim.initialize();
    sim.run();
    const auto& stats = sim.get_statistics();
    for (const auto& req : stats.get_completed_requests_list()) {
        ASSERT_EQ(req.end.z, 2);  // top layer = num_layers-1 = 2
        ASSERT_FALSE(req.path.empty());
        ASSERT_EQ(req.path.back().z, 2);
    }
    return true;
}

bool test_e2e_paths_are_connected() {
    auto cfg = make_config(3, 1, 3000, 7,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    tsvra::Simulator sim(cfg);
    sim.initialize();
    sim.run();
    for (const auto& req : sim.get_statistics().get_completed_requests_list()) {
        for (size_t i = 1; i < req.path.size(); ++i) {
            int dx = std::abs(req.path[i].x - req.path[i-1].x);
            int dy = std::abs(req.path[i].y - req.path[i-1].y);
            int dz = std::abs(req.path[i].z - req.path[i-1].z);
            ASSERT_EQ(dx + dy + dz, 1);
        }
    }
    return true;
}

bool test_e2e_latency_positive() {
    auto cfg = make_config(3, 1, 3000, 13,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    tsvra::Simulator sim(cfg);
    sim.initialize();
    sim.run();
    const auto& stats = sim.get_statistics();
    if (stats.get_completed_requests() > 0) {
        ASSERT_GT(stats.get_average_latency(), 0.0);
        ASSERT_GE(stats.get_max_latency(), stats.get_min_latency());
    }
    return true;
}

bool test_e2e_drain_phase_no_in_flight() {
    // After drain, in_flight should be 0 under normal termination
    auto cfg = make_config(3, 1, 2000, 17,
        tsvra::RedundancyLayout::NONE,
        tsvra::FailureMode::INITIAL_ONLY, 0.0);
    tsvra::Simulator sim(cfg);
    sim.initialize();
    sim.run();
    ASSERT_EQ(sim.get_statistics().get_in_flight(), 0u);
    return true;
}

bool test_e2e_total_accounting() {
    // completed + failed == total (no requests lost after drain)
    auto cfg = make_config(3, 1, 3000, 21,
        tsvra::RedundancyLayout::SHARED_SPARE,
        tsvra::FailureMode::INITIAL_ONLY, 1e-4);
    tsvra::Simulator sim(cfg);
    sim.initialize();
    sim.run();
    const auto& stats = sim.get_statistics();
    uint64_t accounted = stats.get_completed_requests() + stats.get_failed_requests();
    ASSERT_EQ(accounted, stats.get_total_requests());
    return true;
}

bool test_e2e_runtime_failure_mode() {
    auto cfg = make_config(3, 1, 3000, 33,
        tsvra::RedundancyLayout::SHARED_SPARE,
        tsvra::FailureMode::RUNTIME_ONLY, 1e-4);
    tsvra::Simulator sim(cfg);
    sim.initialize();
    sim.run();
    const auto& stats = sim.get_statistics();
    uint64_t accounted = stats.get_completed_requests() + stats.get_failed_requests();
    ASSERT_EQ(accounted, stats.get_total_requests());
    ASSERT_EQ(stats.get_in_flight(), 0u);
    return true;
}

bool test_e2e_combined_failure_mode() {
    auto cfg = make_config(3, 1, 2000, 55,
        tsvra::RedundancyLayout::SHARED_SPARE,
        tsvra::FailureMode::COMBINED, 5e-5);
    tsvra::Simulator sim(cfg);
    sim.initialize();
    sim.run();
    const auto& stats = sim.get_statistics();
    uint64_t accounted = stats.get_completed_requests() + stats.get_failed_requests();
    ASSERT_EQ(accounted, stats.get_total_requests());
    ASSERT_EQ(stats.get_in_flight(), 0u);
    return true;
}

bool test_e2e_legacy_corner4_layout() {
    auto cfg = make_config(3, 1, 2000, 77,
        tsvra::RedundancyLayout::LEGACY_CORNER4,
        tsvra::FailureMode::INITIAL_ONLY, 1e-5);
    tsvra::Simulator sim(cfg);
    sim.initialize();
    sim.run();
    const auto& stats = sim.get_statistics();
    uint64_t accounted = stats.get_completed_requests() + stats.get_failed_requests();
    ASSERT_EQ(accounted, stats.get_total_requests());
    ASSERT_EQ(stats.get_in_flight(), 0u);
    return true;
}

bool test_e2e_deterministic_with_fixed_seed() {
    // Two runs with same seed must produce identical results
    auto run = [](uint32_t seed) {
        auto cfg = make_config(3, 1, 2000, seed,
            tsvra::RedundancyLayout::SHARED_SPARE,
            tsvra::FailureMode::INITIAL_ONLY, 1e-5);
        tsvra::Simulator sim(cfg);
        sim.initialize();
        sim.run();
        return sim.get_statistics().get_completed_requests();
    };
    uint64_t r1 = run(42);
    uint64_t r2 = run(42);
    ASSERT_EQ(r1, r2);
    return true;
}
// ============================================================
// main
// ============================================================
int main() {
    std::cout << "=== TSVRA Unit Tests ===\n\n";

    // TSV
    std::cout << "--- TSV ---\n";
    run_test("tsv_default_state",          test_tsv_default_state);
    run_test("tsv_redundant_flag",         test_tsv_redundant_flag);
    run_test("tsv_fail_unavailable",       test_tsv_fail_unavailable);
    run_test("tsv_occupy_until",           test_tsv_occupy_until);
    run_test("tsv_usage_count",            test_tsv_usage_count);

    // Config
    std::cout << "\n--- Config ---\n";
    run_test("config_defaults",            test_config_defaults);
    run_test("config_grid_size_formula",   test_config_grid_size_formula);
    run_test("config_setters_roundtrip",   test_config_setters_roundtrip);

    // Grid
    std::cout << "\n--- Grid ---\n";
    run_test("grid_dimensions",                       test_grid_dimensions);
    run_test("grid_valid_position",                   test_grid_valid_position);
    run_test("grid_get_tsv",                          test_grid_get_tsv);
    run_test("grid_no_failures_by_default",           test_grid_no_failures_by_default);
    run_test("grid_redundant_count_shared_spare",     test_grid_redundant_tsv_count_shared_spare);
    run_test("grid_redundant_count_none",             test_grid_redundant_tsv_count_none);
    run_test("grid_find_bypass_spare_shared",         test_grid_find_bypass_spare_shared);

    // Router
    std::cout << "\n--- Router ---\n";
    run_test("router_simple_vertical",       test_router_simple_vertical);
    run_test("router_path_connectivity",     test_router_path_connectivity);
    run_test("router_no_route_all_failed",   test_router_no_route_when_all_failed);
    run_test("router_delay_calculation",     test_router_delay_calculation);
    run_test("router_manhattan_heuristic",   test_router_manhattan_heuristic);
    run_test("router_horizontal_distance_limit", test_router_horizontal_distance_limit);

    // RequestGenerator
    std::cout << "\n--- RequestGenerator ---\n";
    run_test("reqgen_no_top_layer_start",           test_reqgen_no_top_layer_start);
    run_test("reqgen_all_start_layers_covered",     test_reqgen_all_start_layers_covered);
    run_test("reqgen_positions_in_bounds",          test_reqgen_positions_in_bounds);
    run_test("reqgen_ids_monotonically_increasing", test_reqgen_ids_monotonically_increasing);
    run_test("reqgen_hotspot_weights_positive",     test_reqgen_hotspot_weights_positive);

    // Statistics
    std::cout << "\n--- Statistics ---\n";
    run_test("stats_initial_state",            test_stats_initial_state);
    run_test("stats_record_generated",         test_stats_record_generated);
    run_test("stats_record_completed",         test_stats_record_completed);
    run_test("stats_record_failed",            test_stats_record_failed);
    run_test("stats_average_latency",          test_stats_average_latency);
    run_test("stats_redundant_tsv_usage",      test_stats_redundant_tsv_usage);
    run_test("stats_in_flight",                test_stats_in_flight);
    run_test("stats_bypass_diagnostic_setters",test_stats_bypass_diagnostic_setters);

    // End-to-end / Simulator
    std::cout << "\n--- End-to-End (Simulator) ---\n";
    run_test("e2e_requests_end_at_top_layer",  test_e2e_requests_end_at_top_layer);
    run_test("e2e_paths_are_connected",        test_e2e_paths_are_connected);
    run_test("e2e_latency_positive",           test_e2e_latency_positive);
    run_test("e2e_drain_phase_no_in_flight",   test_e2e_drain_phase_no_in_flight);
    run_test("e2e_total_accounting",           test_e2e_total_accounting);
    run_test("e2e_runtime_failure_mode",       test_e2e_runtime_failure_mode);
    run_test("e2e_combined_failure_mode",      test_e2e_combined_failure_mode);
    run_test("e2e_legacy_corner4_layout",      test_e2e_legacy_corner4_layout);
    run_test("e2e_deterministic_fixed_seed",   test_e2e_deterministic_with_fixed_seed);

    std::cout << "\n=== Results: " << tests_passed << "/" << tests_run
              << " passed, " << tests_failed << " failed ===\n";
    return tests_failed > 0 ? 1 : 0;
}





