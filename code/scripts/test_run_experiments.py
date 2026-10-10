"""Batch safety and exported-accounting fixtures; no experiment sweep is run."""

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

import run_experiments as runner


def write_csv(path, fields, rows):
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def fixture(prefix):
    power = {"frequency_hz": 1e9, "voltage_v": 1, "total_toggle_probability": .5,
             "flit_bits": 64, "physical_signal_tsvs": 64, "router_idle_mw": .1,
             "router_buffer_event_pj": 2, "router_crossbar_event_pj": 3, "router_control_event_pj": 1,
             "horizontal_link_length_um": 100, "horizontal_capacitance_ff_per_um": .2,
             "tsv_capacitance_ff": 40, "redundant_vertical_capacitance_multiplier": 2,
             "parameter_version": "fixture", "calibration_status": "fixture", "technology": "fixture",
             "calibration_source_revision": "fixture", "router_source": "fixture"}
    simulation = {"num_layers": 2, "grid_factor": 4, "grid_size": 16, "simulation_cycles": 10,
                  "random_seed": 7, "redundancy": "shared", "failure_mode": "a",
                  "initial_failure_rate": .1, "runtime_failure_rate": 0,
                  "hotspot_centers": [], "power_config": str(runner.PROJECT / "code/config/power_model.ini")}
    configuration = {"actual_seed": 7, "simulation": simulation, "power_parameters": power,
                     "actual_hotspot_centers": []}
    values = {"actual_seed": 7, "total_requests": 2, "completed_requests": 1, "failed_requests": 1,
              "in_flight_at_end": 0, "in_flight_at_horizon": 1, "router_count": 512,
              "elapsed_cycles": 20, "elapsed_seconds": 2e-8, "router_events": 5,
              "horizontal_hops": 3, "vertical_hops": 2, "redundant_vertical_hops": 1,
              "router_static_mw": 51.2, "router_dynamic_mw": 1.5,
              "horizontal_mw": .048, "vertical_mw": .096, "total_mw": 52.844,
              "total_energy_pj": 1056.88, "router_energy_pj": 1054, "horizontal_energy_pj": .96,
              "vertical_energy_pj": 1.92, "success_rate_pct": 50, "terminal_delivery_rate_pct": 50,
              "route_failure_rate_pct": 50, "energy_per_completed_flit_pj": 1056.88,
              "average_latency_cycles": 8, "min_latency_cycles": 8, "max_latency_cycles": 8,
              "average_horizontal_distance": 1.5, "total_horizontal_distance": 3,
              "failed_tsvs": 5, "failed_redundant_tsvs": 1}
    summary_path = Path(f"{prefix}_summary.csv")
    with summary_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["Metric", "Value"])
        writer.writerows((label, values[key]) for label, key in runner.METRICS.items() if key in values)
    Path(f"{prefix}_config.json").write_text(json.dumps(configuration))
    trace = {key: values[key] for key in ("elapsed_cycles", "elapsed_seconds", "router_events",
             "horizontal_hops", "vertical_hops", "redundant_vertical_hops", "router_static_mw",
             "router_dynamic_mw", "horizontal_mw", "vertical_mw", "total_mw", "total_energy_pj")}
    write_csv(Path(f"{prefix}_power.csv"), list(trace), [trace])
    requests = [{"Request ID": 0, "Start X": 0, "Start Y": 0, "Start Z": 0,
                 "End X": 1, "End Y": 1, "End Z": 1, "Generate Time": 1, "Complete Time": 9,
                 "Latency": 8, "Horizontal Distance": 2, "Status": "Completed"},
                {"Request ID": 1, "Start X": 2, "Start Y": 2, "Start Z": 0,
                 "End X": 3, "End Y": 3, "End Z": 1, "Generate Time": 2, "Complete Time": "N/A",
                 "Latency": "N/A", "Horizontal Distance": 1, "Status": "Failed"}]
    write_csv(Path(f"{prefix}_requests.csv"), list(requests[0]), requests)
    return configuration, requests


def change_metric(prefix, label, value):
    path = Path(f"{prefix}_summary.csv")
    with path.open(newline="") as handle:
        rows = list(csv.reader(handle))
    rows = [[name, str(value) if name == label else existing] for name, existing in rows]
    with path.open("w", newline="") as handle:
        csv.writer(handle).writerows(rows)


class MatrixTests(unittest.TestCase):
    def test_exact_study_matrices(self):
        for stage, expected_count, expected_cells in (("pilot", 156, 52), ("main", 3960, 196)):
            spec = json.loads((runner.PROJECT / f"code/config/experiments.{stage}.json").read_text())
            tasks = runner.build_tasks(spec)
            self.assertEqual(len(tasks), expected_count)
            self.assertEqual(len({task.run_id for task in tasks}), expected_count)
            self.assertEqual(len({task.variant for task in tasks}), expected_cells)
            for task in tasks:
                parameters = task.parameters
                self.assertEqual((parameters["grid_factor"], parameters["simulation_cycles"],
                                  parameters["request_probability"]), (4, 10000, .02))
                self.assertEqual((parameters["failure_mode"], parameters["runtime_failure_rate"],
                                  parameters["redundancy"]), ("a", 0, "shared"))
                self.assertEqual((parameters["vertical_delay"], parameters["horizontal_delay"],
                                  parameters["router_delay"]), (5, 500, 15))
                self.assertTrue(all(parameters[key] == 0 for key in ("lambda1", "lambda2", "max_path_length",
                                                                     "max_horizontal_distance", "reliability_min")))
                scenario = task.configuration.split("_")[0]
                self.assertEqual(parameters["hotspot_centers"], {"U": [], "H1": [[5, 5, 0]],
                    "H2": [[10, 10, 0]], "H12": [[5, 5, 0], [10, 10, 0]]}[scenario])
                self.assertEqual((parameters["hotspot_radius"], parameters["hotspot_multiplier"]), (3, 5))
                if stage == "pilot":
                    self.assertIn(task.seed, (901, 902, 903))
                else:
                    rate = parameters["initial_failure_rate"]
                    maximum = 130 if rate <= .2 else 120 if rate <= .4 else 110
                    self.assertTrue(101 <= task.seed <= maximum)
                command = runner.command_for(Path("/simulator"), Path("/result"), task)
                self.assertIn("--initial-failure-rate", command)
                self.assertIn("--runtime-failure-rate", command)
                self.assertNotIn("--failure-rate", command)
                if scenario != "U":
                    self.assertIn("--hotspot-centers", command)
            for variant in {task.variant for task in tasks}:
                seeds = [task.seed for task in tasks if task.variant == variant]
                self.assertEqual(len(seeds), len(set(seeds)))

    def test_bad_seeds_and_duplicate_cells(self):
        for seeds in ([0], [7, 7], [True]):
            with self.assertRaises(ValueError):
                runner.build_tasks({"seeds": seeds, "configurations": [{"name": "bad"}]})
        with self.assertRaises(ValueError):
            runner.build_tasks({"configurations": [{"name": "bad", "matrix": {"num_layers": [2, 2]}}]})

    def test_explicit_phase_flags_override_shorthand(self):
        task = runner.Task("test", "v1", 1, 7,
                           {"initial_failure_rate": .3, "runtime_failure_rate": 0, "failure_rate": .5}, "run")
        command = runner.command_for(Path("/sim"), Path("/out"), task)
        self.assertLess(command.index("--failure-rate"), command.index("--initial-failure-rate"))
        self.assertLess(command.index("--failure-rate"), command.index("--runtime-failure-rate"))


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.prefix = Path(self.temporary.name) / "fixture"
        self.configuration, self.requests = fixture(self.prefix)

    def tearDown(self):
        self.temporary.cleanup()

    def test_independent_accounting_and_power(self):
        result = runner.check_outputs(self.prefix)
        self.assertEqual(result["total_completed_latency_cycles"], 8)
        self.assertEqual(result["median_latency_cycles"], 8)
        self.assertEqual(result["p95_latency_cycles"], 8)
        self.assertEqual(result["drain_cycles"], 10)
        self.assertAlmostEqual(result["router_static_energy_pj"], 1024)

    def test_partial_duplicate_and_timestamp_records_rejected(self):
        change_metric(self.prefix, "In Flight At End", 1)
        with self.assertRaisesRegex(ValueError, "Partial"):
            runner.check_outputs(self.prefix)
        change_metric(self.prefix, "In Flight At End", 0)
        self.requests[1]["Request ID"] = 0
        write_csv(Path(f"{self.prefix}_requests.csv"), list(self.requests[0]), self.requests)
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            runner.check_outputs(self.prefix)
        self.requests[1]["Request ID"] = 1
        self.requests[0]["Latency"] = 7
        write_csv(Path(f"{self.prefix}_requests.csv"), list(self.requests[0]), self.requests)
        with self.assertRaisesRegex(ValueError, "timestamps"):
            runner.check_outputs(self.prefix)

    def test_malformed_summary_and_independent_energy_rejected(self):
        with Path(f"{self.prefix}_summary.csv").open("a") as handle:
            handle.write("Actual Seed,7\n")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            runner.check_outputs(self.prefix)
        fixture(self.prefix)
        # Change only the component, leaving the trace total intact: cross-export checks alone miss this.
        change_metric(self.prefix, "Router Energy (pJ)", 1055)
        with self.assertRaisesRegex(ValueError, "independently recomputed"):
            runner.check_outputs(self.prefix)

    def test_actual_seed_must_match_requested(self):
        task = runner.Task("test", "v1", 1, 9, {}, "run")
        with self.assertRaisesRegex(ValueError, "requested seed"):
            runner.check_outputs(self.prefix, task)

    def test_header_and_generated_accounting_required(self):
        Path(f"{self.prefix}_requests.csv").write_text("")
        with self.assertRaisesRegex(ValueError, "header"):
            runner.check_outputs(self.prefix)
        fixture(self.prefix)
        change_metric(self.prefix, "Total Requests", 3)
        with self.assertRaisesRegex(ValueError, "Generated requests"):
            runner.check_outputs(self.prefix)

    def test_no_completion_metrics_remain_undefined(self):
        self.requests[0].update(Status="Failed", **{"Complete Time": "N/A", "Latency": "N/A"})
        write_csv(Path(f"{self.prefix}_requests.csv"), list(self.requests[0]), self.requests)
        for label, value in (("Completed Requests", 0), ("Failed Requests", 2), ("Success Rate (%)", 0),
                             ("Terminal Delivery Rate (%)", 0), ("Route Failure Rate (%)", 100),
                             ("Energy Per Completed Flit (pJ)", ""), ("Average Latency", ""),
                             ("Min Latency", ""), ("Max Latency", "")):
            change_metric(self.prefix, label, value)
        result = runner.check_outputs(self.prefix)
        self.assertEqual(result["success_rate_pct"], 0)
        self.assertIsNone(result["average_latency_cycles"])
        self.assertIsNone(result["energy_per_completed_flit_pj"])
        self.assertGreater(result["total_energy_pj"], 0)


class ProcessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.task = runner.Task("fixture", "v1", 1, 7, {}, "test_run")

    def tearDown(self):
        self.temporary.cleanup()

    def executable(self, source):
        path = self.directory / "fake_simulator"
        path.write_text(f"#!{sys.executable}\n{source}\n")
        path.chmod(0o755)
        return path

    def test_failed_and_timed_out_processes_retained(self):
        binary = self.executable("import sys; print('fixture crash'); sys.exit(3)")
        row = runner.run_task(binary, self.directory, self.task, 2, runner.RunControl())
        self.assertEqual((row["status"], row["return_code"]), ("failed", 3))
        self.assertTrue(Path(f"{row['output_prefix']}_result.json").is_file())
        self.assertIn("fixture crash", Path(f"{row['output_prefix']}.log").read_text())
        task = runner.Task("fixture", "v1", 1, 7, {}, "timeout_run")
        binary = self.executable("import time; print('fixture timeout', flush=True); time.sleep(2)")
        row = runner.run_task(binary, self.directory, task, .05, runner.RunControl())
        self.assertEqual(row["status"], "timeout")
        self.assertLess(row["wall_seconds"], 1)
        self.assertIn("fixture timeout", Path(f"{row['output_prefix']}.log").read_text())
        if hasattr(runner.os, "wait4"):
            self.assertGreater(row["peak_rss_kib"], 0)

    def test_zero_exit_without_exports_is_failed(self):
        row = runner.run_task(self.executable("print('no exports')"), self.directory,
                              self.task, 2, runner.RunControl())
        self.assertEqual(row["status"], "failed")
        self.assertTrue(row["error"])

    def test_collisions_never_replace_evidence(self):
        path = self.directory / "test_run_result.json"
        path.write_text("keep me")
        row = runner.run_task(Path("/no/binary"), self.directory, self.task, 2, runner.RunControl())
        self.assertEqual(row["status"], "failed")
        self.assertEqual(path.read_text(), "keep me")
        with self.assertRaisesRegex(ValueError, "not empty"):
            runner.reserve_output(self.directory)

    def test_prestart_cancellation_is_retained(self):
        control = runner.RunControl()
        control.cancel()
        row = runner.run_task(Path("/no/binary"), self.directory, self.task, 2, control)
        self.assertEqual(row["status"], "cancelled")


if __name__ == "__main__":
    unittest.main()
