#ifndef TSVRA_SIMULATOR_HPP
#define TSVRA_SIMULATOR_HPP

#include "config.hpp"
#include "grid.hpp"
#include "router.hpp"
#include "request_generator.hpp"
#include "statistics.hpp"
#include <random>
#include <queue>
#include <memory>
#include <string>
#include <sstream>

namespace tsvra {

// JSON streaming event structures
struct JsonNewRequest {
    uint64_t id;
    int32_t sx, sy, sz, ex, ey, ez;
    uint64_t time;
    std::vector<Point> path;
};

struct JsonCompleted {
    uint64_t id;
    uint64_t time;
    uint64_t latency;
};

struct JsonFailed {
    uint64_t id;
    uint64_t time;
    std::string reason;
};

struct JsonNewFailure {
    int32_t x, y, z;
    uint64_t cycle;
};

struct JsonReroute {
    uint64_t request_id;
    std::vector<Point> old_path;
    std::vector<Point> new_path;
    Point redundant_used;
};

// Simulator class
class Simulator {
public:
    explicit Simulator(const Config& config);

    // Initialize simulation environment
    void initialize();

    // Run simulation
    void run();

    // Get statistics
    const Statistics& get_statistics() const { return stats_; }

private:
    const Config& config_;
    std::mt19937 rng_;

    // Core components
    std::unique_ptr<Grid> grid_;
    std::unique_ptr<Router> router_;
    std::unique_ptr<RequestGenerator> req_gen_;
    Statistics stats_;

    // Pending request queue
    std::queue<Request> pending_requests_;

    // In-transit request list
    std::vector<Request> transmitting_requests_;

    // Current clock cycle
    uint64_t current_cycle_;

    // JSON streaming mode
    bool json_stream_mode_;
    bool paused_;
    int steps_remaining_;
    bool stop_requested_;

    // Per-cycle event accumulators
    std::vector<JsonNewRequest> cycle_new_requests_;
    std::vector<JsonCompleted> cycle_completed_;
    std::vector<JsonFailed> cycle_failed_;
    std::vector<JsonNewFailure> cycle_new_failures_;
    std::vector<JsonReroute> cycle_reroutes_;

    // Simulation steps
    void simulate_cycle();
    void generate_new_requests();
    void handle_runtime_failures();
    void process_pending_requests();
    void update_transmitting_requests();
    bool route_request(Request& req);
    bool reroute_request(Request& req);
    void update_tsv_occupancy(const std::vector<Point>& path, uint64_t complete_time);
    void increment_path_usage(const std::vector<Point>& path);
    void decrement_path_usage(const std::vector<Point>& path);

    // Helper functions
    void print_progress() const;

    // JSON streaming helpers
    void emit_init_json();
    void emit_cycle_json();
    void emit_done_json();
    void poll_stdin_commands();
    void reset_cycle_accumulators();
    static std::string point_to_json(const Point& p);
    static std::string path_to_json(const std::vector<Point>& path);
    static std::string escape_json_string(const std::string& s);
};

} // namespace tsvra

#endif // TSVRA_SIMULATOR_HPP
