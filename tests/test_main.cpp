#include "config.hpp"
#include "request_generator.hpp"
#include "simulator.hpp"
#include <iostream>
#include <set>
#include <cstdlib>

// 简易测试框架
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

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "  FAIL: " << #a << " == " << #b \
                      << " (" << (a) << " != " << (b) << ") (line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

static void run_test(const char* name, bool (*test_fn)()) {
    tests_run++;
    std::cout << "TEST " << name << " ... ";
    if (test_fn()) {
        tests_passed++;
        std::cout << "PASS\n";
    } else {
        tests_failed++;
        std::cout << "FAIL\n";
    }
}

// 测试1: 请求起始层不包含最顶层
bool test_request_generator_no_top_layer() {
    tsvra::Config config;
    config.set_num_layers(4);
    config.set_grid_factor(1);
    config.set_random_seed(42);

    std::mt19937 rng(42);
    tsvra::RequestGenerator gen(config, rng);
    gen.initialize_hotspots();

    uint32_t num_layers = config.get_num_layers();
    int32_t top_layer = static_cast<int32_t>(num_layers - 1);

    for (uint64_t cycle = 0; cycle < 100000; ++cycle) {
        auto req = gen.generate_request(cycle);
        if (req) {
            ASSERT_TRUE(req->start.z < top_layer);
            ASSERT_EQ(req->end.z, top_layer);
        }
    }
    return true;
}

// 测试2: 所有合法起始层都被覆盖
bool test_request_generator_all_layers_covered() {
    tsvra::Config config;
    config.set_num_layers(4);
    config.set_grid_factor(1);
    config.set_random_seed(42);

    std::mt19937 rng(42);
    tsvra::RequestGenerator gen(config, rng);
    gen.initialize_hotspots();

    std::set<int32_t> seen_layers;

    for (uint64_t cycle = 0; cycle < 500000; ++cycle) {
        auto req = gen.generate_request(cycle);
        if (req) {
            seen_layers.insert(req->start.z);
        }
    }

    // 应该覆盖层 0, 1, 2（不含最顶层 3）
    ASSERT_EQ(seen_layers.size(), static_cast<size_t>(3));
    ASSERT_TRUE(seen_layers.count(0) > 0);
    ASSERT_TRUE(seen_layers.count(1) > 0);
    ASSERT_TRUE(seen_layers.count(2) > 0);
    ASSERT_TRUE(seen_layers.count(3) == 0);

    return true;
}

// 测试3: 完成的请求都终止在最顶层
bool test_completed_requests_end_at_top() {
    tsvra::Config config;
    config.set_num_layers(2);
    config.set_grid_factor(1);
    config.set_random_seed(42);
    config.set_simulation_cycles(5000);
    config.set_failure_mode(tsvra::FailureMode::INITIAL_ONLY);
    config.set_failure_rate(0.0);
    config.set_json_stream(false);

    tsvra::Simulator sim(config);
    sim.initialize();
    sim.run();

    const auto& stats = sim.get_statistics();
    const auto& completed = stats.get_completed_requests_list();

    ASSERT_TRUE(completed.size() > 0);

    for (const auto& req : completed) {
        ASSERT_EQ(req.end.z, 1);
        ASSERT_EQ(req.start.z, 0);
        ASSERT_TRUE(req.completed);
    }

    return true;
}

// 测试4: 端到端向上传输验证
bool test_e2e_upward_traversal() {
    tsvra::Config config;
    config.set_num_layers(3);
    config.set_grid_factor(1);
    config.set_random_seed(42);
    config.set_simulation_cycles(10000);
    config.set_failure_mode(tsvra::FailureMode::INITIAL_ONLY);
    config.set_failure_rate(0.0);
    config.set_json_stream(false);

    tsvra::Simulator sim(config);
    sim.initialize();
    sim.run();

    const auto& stats = sim.get_statistics();
    const auto& completed = stats.get_completed_requests_list();

    ASSERT_TRUE(completed.size() > 0);

    for (const auto& req : completed) {
        // 起始层在顶层以下
        ASSERT_TRUE(req.start.z < 2);
        // 终止在最顶层
        ASSERT_EQ(req.end.z, 2);
        // 路径非空
        ASSERT_TRUE(!req.path.empty());
        // 路径起点和终点与请求一致
        ASSERT_EQ(req.path.front().z, req.start.z);
        ASSERT_EQ(req.path.back().z, req.end.z);

        // 路径中至少包含一次垂直跳（z坐标变化）
        bool has_vertical_hop = false;
        for (size_t i = 1; i < req.path.size(); ++i) {
            if (req.path[i].z != req.path[i - 1].z) {
                has_vertical_hop = true;
                break;
            }
        }
        ASSERT_TRUE(has_vertical_hop);
    }

    return true;
}

int main() {
    std::cout << "=== TSVRA Unit Tests ===\n\n";

    run_test("request_generator_no_top_layer", test_request_generator_no_top_layer);
    run_test("request_generator_all_layers_covered", test_request_generator_all_layers_covered);
    run_test("completed_requests_end_at_top", test_completed_requests_end_at_top);
    run_test("e2e_upward_traversal", test_e2e_upward_traversal);

    std::cout << "\n=== Results: " << tests_passed << "/" << tests_run
              << " passed, " << tests_failed << " failed ===\n";

    return tests_failed > 0 ? 1 : 0;
}
