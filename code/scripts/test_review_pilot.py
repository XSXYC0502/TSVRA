"""Operational review rejects incomplete or changed studies before authorizing main."""
import json
from pathlib import Path
import tempfile
import unittest

import review_pilot
from run_experiments import build_tasks, project_path


class PilotReviewTests(unittest.TestCase):
    def test_interpolation_preserves_measured_endpoints(self):
        self.assertEqual(review_pilot.interpolate([(0.05, 1), (0.3, 2), (0.7, 6)], 0.5), 4)
        self.assertEqual(review_pilot.interpolate([(0.05, 1), (0.3, 2)], 0), 1)

    def fixture(self, path):
        spec = json.loads(project_path("code/config/experiments.pilot.json").read_text())
        tasks = build_tasks(spec)
        declared = [{"run_id": t.run_id, "parameters": t.parameters, "seed": t.seed} for t in tasks]
        rows = [{"run_id": t.run_id, "parameters": t.parameters, "requested_seed": t.seed, "actual_seed": t.seed,
                 "status": "ok", "in_flight_at_end": 0, "total_requests": 200, "completed_requests": 200,
                 "failed_requests": 0, "elapsed_cycles": 15000, "wall_seconds": .03,
                 "average_latency_cycles": 5000, "success_rate_pct": 100, "total_mw": 8425,
                 "peak_rss_kib": 24000} for t in tasks]
        (path / "batch.json").write_text(json.dumps(spec))
        (path / "manifest.json").write_text(json.dumps({"tasks": declared}))
        (path / "results.json").write_text(json.dumps({"runs": rows}))
        return declared, rows

    def test_complete_approved_budget_and_changed_workload(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            self.fixture(path)
            main = project_path("code/config/experiments.main.json")
            result = review_pilot.review(path, main, 4)
            self.assertEqual(result["accepted_pilot_runs"], 156)
            self.assertEqual(result["main_runs"], 3960)
            changed = json.loads(main.read_text())
            changed["defaults"]["request_probability"] = .5
            altered = path / "changed-main.json"
            altered.write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError, "approved"):
                review_pilot.review(path, altered, 4)

    def test_consistent_but_truncated_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            declared, rows = self.fixture(path)
            (path / "manifest.json").write_text(json.dumps({"tasks": declared[:16]}))
            (path / "results.json").write_text(json.dumps({"runs": rows[:16]}))
            with self.assertRaisesRegex(ValueError, "manifest"):
                review_pilot.review(path, project_path("code/config/experiments.main.json"), 4)


if __name__ == "__main__":
    unittest.main()
