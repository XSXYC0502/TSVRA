"""Artificial formula/interval/audit fixtures; no experiment sweep is executed."""
import csv
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

import analyze_experiments as a


def parameters(name="U", f=.3, layers=2):
    return {"num_layers": layers, "grid_factor": 4, "failure_mode": "a", "initial_failure_rate": f,
            "runtime_failure_rate": 0, "failure_model": "uniform" if name == "U" else "hotspot", "redundancy": "shared",
            "simulation_cycles": 100, "request_probability": .02, "power_config": "power_model.ini",
            "vertical_delay": 5, "horizontal_delay": 500, "router_delay": 15, "lambda1": 0, "lambda2": 0,
            "max_path_length": 0, "max_horizontal_distance": 0, "reliability_min": 0,
            "hotspot_count": 2 if name == "H12" else 1, "hotspot_radius": 3, "hotspot_multiplier": 5,
            "hotspot_centers": {"U": [], "H1": [[5,5,0]], "H2": [[10,10,0]], "H12": [[5,5,0],[10,10,0]]}[name]}


def fixture(prefix, zero_completion=False, zero_traffic=False):
    p = parameters()
    effective = {**p, "grid_size": 16, "random_seed": 101, "output_prefix": str(prefix)}
    power = {"frequency_hz": 1e9, "voltage_v": 1, "total_toggle_probability": .5, "router_idle_mw": 1,
             "router_buffer_event_pj": 1, "router_crossbar_event_pj": 2, "router_control_event_pj": 3,
             "horizontal_link_length_um": 1000, "horizontal_capacitance_ff_per_um": .1, "tsv_capacitance_ff": 10,
             "flit_bits": 64, "physical_signal_tsvs": 64, "redundant_vertical_capacitance_multiplier": 2,
             "router_event_energy_pj": 6, "horizontal_hop_energy_pj": 1.6, "vertical_hop_energy_pj": .16,
             "redundant_vertical_hop_energy_pj": .32}
    config = {"simulation": effective, "actual_seed": 101, "actual_hotspot_centers": [], "power_parameters": power}
    Path(f"{prefix}_config.json").write_text(json.dumps(config))
    values = {"Total Requests": 0 if zero_traffic else 3, "Completed Requests": 0 if zero_completion or zero_traffic else 2,
              "Failed Requests": 0 if zero_traffic else 3 if zero_completion else 1,
              "In Flight At Horizon": 0, "In Flight At End": 0, "Actual Seed": 101,
              "Router Count": 512, "Elapsed Cycles": 100, "Elapsed Seconds": 1e-7,
              "Router Events": 7, "Horizontal Hops": 4, "Vertical Hops": 3, "Redundant Vertical Hops": 1,
              "Router Static Power (mW)": 512, "Router Dynamic Power (mW)": .42,
              "Horizontal Power (mW)": .064, "Vertical Power (mW)": .0064, "Total Power (mW)": 512.4904,
              "Total Energy (pJ)": 51249.04, "Router Energy (pJ)": 51242, "Horizontal Energy (pJ)": 6.4,
              "Vertical Energy (pJ)": .64, "Failed TSVs": 20, "Failed Redundant TSVs": 2,
              "Redundant TSV Usages": 1,
              "Total Horizontal Distance": 4, "Average Horizontal Distance": 4/3 if not zero_traffic else 0}
    if zero_traffic:
        values.update({"Router Events":0,"Horizontal Hops":0,"Vertical Hops":0,"Redundant Vertical Hops":0,
                       "Redundant TSV Usages":0,"Total Horizontal Distance":0,"Router Dynamic Power (mW)":0,
                       "Horizontal Power (mW)":0,"Vertical Power (mW)":0,"Total Power (mW)":512,
                       "Total Energy (pJ)":51200,"Router Energy (pJ)":51200,"Horizontal Energy (pJ)":0,"Vertical Energy (pJ)":0})
    if not zero_traffic:
        values["Success Rate (%)"] = 0 if zero_completion else 200/3
    values["Energy Per Completed Flit (pJ)"] = "" if zero_completion or zero_traffic else 51249.04/2
    if not zero_completion and not zero_traffic:
        values.update({"Average Latency": 15, "Min Latency": 10, "Max Latency": 20})
    with Path(f"{prefix}_summary.csv").open("w", newline="") as h:
        w = csv.writer(h); w.writerow(["Metric", "Value"]); w.writerows(values.items())
    trace = {a.runner.METRICS[k]: v for k,v in values.items() if k in a.runner.METRICS}
    trace["completed_flits"] = trace.pop("completed_requests")
    with Path(f"{prefix}_power.csv").open("w", newline="") as h:
        w = csv.DictWriter(h, fieldnames=list(trace)); w.writeheader(); w.writerow(trace)
    rows = []
    for i in range(0 if zero_traffic else 3):
        state = "Failed" if zero_completion or i == 2 else "Completed"
        rows.append({"Request ID": i, "Start X": 1, "Start Y": 1, "Start Z": 0, "End X": 2, "End Y": 2, "End Z": 1,
                     "Generate Time": i, "Complete Time": "N/A" if state == "Failed" else i + (i+1)*10,
                     "Latency": "N/A" if state == "Failed" else (i+1)*10, "Horizontal Distance": 2 if i < 2 else 0, "Status": state})
    fields = ["Request ID", "Start X", "Start Y", "Start Z", "End X", "End Y", "End Z", "Generate Time", "Complete Time", "Latency", "Horizontal Distance", "Status"]
    with Path(f"{prefix}_requests.csv").open("w", newline="") as h:
        w = csv.DictWriter(h, fieldnames=fields); w.writeheader(); w.writerows(rows)
    task = {"run_id": "r", "configuration": "U", "seed": 101, "parameters": p}
    return task, {"run_id": "r", "status": "ok", "actual_seed": 101}, rows


class AnalysisTests(unittest.TestCase):
    def test_whole_seed_resampling_reproducible(self):
        w = a.bootstrap_weights(np.random.default_rng(71), 3, 10000)
        np.testing.assert_array_equal(w, a.bootstrap_weights(np.random.default_rng(71), 3, 10000))
        np.testing.assert_array_equal(w.sum(axis=1), np.full(10000, 3))
        self.assertTrue(np.all(w == w.astype(int)))

    def test_intervals_null_sd_and_degeneracy(self):
        w = a.bootstrap_weights(np.random.default_rng(1), 2, 10000)
        s = a.summarize_values([10,30], w)
        self.assertEqual(s["mean"], 20)
        self.assertAlmostEqual(s["sd"], np.sqrt(200))
        self.assertEqual((s["ci95_low"], s["ci95_high"]), (10,30))
        empty = a.summarize_values([None,None], w)
        self.assertIsNone(empty["mean"])
        self.assertIsNone(empty["ci95_low"])
        self.assertEqual(empty["bootstrap_defined_resamples"], 0)
        constant = a.summarize_values([100,100], w)
        self.assertEqual(constant["degenerate_interval"], 1)
        partially_defined = a.summarize_values([20,None], w)
        self.assertEqual(partially_defined["mean"], 20)
        self.assertEqual(partially_defined["defined_runs"], 1)
        self.assertGreater(partially_defined["bootstrap_defined_resamples"], 7000)
        self.assertLess(partially_defined["bootstrap_defined_resamples"], 8000)

    def test_equal_seed_and_pooled_ratios_are_different(self):
        tasks = [{"seed":101,"parameters":parameters()},{"seed":102,"parameters":parameters()}]
        runs=[]
        for seed,gen,comp,latency,energy in ((101,10,10,10,100),(102,100,20,100,800)):
            r = {m:0 for m in a.METRICS}
            r.update({"analysis_status":"valid", "actual_seed":seed, "total_requests":gen, "completed_requests":comp,
                      "success_rate_pct":100*comp/gen, "average_latency_cycles":latency,
                      "total_completed_latency_cycles":comp*latency, "total_energy_pj":energy,
                      "energy_per_completed_flit_pj":energy/comp})
            runs.append(r)
        summary,_=a.summarize_cell(tasks,runs,np.random.default_rng(42),10000)
        self.assertEqual(summary["success_rate_pct_mean"],60)
        self.assertAlmostEqual(summary["pooled_success_rate_pct"],100*30/110)
        self.assertEqual(summary["average_latency_cycles_mean"],55)
        self.assertEqual(summary["pooled_average_latency_cycles"],70)
        self.assertEqual(summary["energy_per_completed_flit_pj_mean"],25)
        self.assertEqual(summary["pooled_energy_per_completed_flit_pj"],30)
        self.assertEqual(summary["pooled_energy_per_completed_flit_pj_numerator"],900)
        self.assertEqual(summary["pooled_energy_per_completed_flit_pj_denominator"],30)

    def test_all_planned_map_means_max_gaussian_and_locality(self):
        for layers in (2,3,4,5):
            for f in (.05,.1,.15,.2,.25,.3,.35,.4,.45,.5,.6,.7):
                for name in a.SCENARIOS:
                    prob,weight=a.probability_map(parameters(name,f,layers))
                    self.assertAlmostEqual(prob.mean(),f,places=12)
                    self.assertTrue(np.all((prob>=0)&(prob<=1)))
                    self.assertTrue(np.all(weight[1:]==1))
        _,h1=a.probability_map(parameters("H1",.7,4))
        _,h2=a.probability_map(parameters("H2",.7,4))
        p,h12=a.probability_map(parameters("H12",.7,4))
        np.testing.assert_array_equal(h12,np.maximum(h1,h2))
        self.assertGreater(np.sum(p==1),0)
        np.testing.assert_array_equal(a.probability_map(parameters("H12",.7,4))[0],p)

    def test_raw_accounting_latency_population_power(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix=Path(directory)/"run"
            task,row,_=fixture(prefix)
            record,requests,_=a.audit_run(task,row,prefix)
            self.assertEqual(record["analysis_status"],"valid")
            self.assertEqual(record["average_latency_cycles"],15)
            self.assertEqual(record["median_latency_cycles"],15)
            self.assertEqual(record["p95_latency_cycles"],20)
            self.assertEqual(record["logical_channel_count"],256)
            self.assertEqual(record["spare_channel_count"],25)
            self.assertAlmostEqual(record["observed_failed_fraction"],20/256)
            self.assertIsNone(requests[2]["completion_time"])
            self.assertIsNone(requests[2]["latency_cycles"])

    def test_no_completion_and_no_traffic_stay_undefined(self):
        for no_traffic in (False,True):
            with tempfile.TemporaryDirectory() as directory:
                prefix=Path(directory)/"run"
                task,row,_=fixture(prefix,zero_completion=True,zero_traffic=no_traffic)
                record,_,_=a.audit_run(task,row,prefix)
                self.assertIsNone(record["average_latency_cycles"])
                self.assertIsNone(record["energy_per_completed_flit_pj"])
                self.assertEqual(record["success_rate_pct"],None if no_traffic else 0)
                self.assertEqual(record["total_mw"],512 if no_traffic else 512.4904)

    def test_duplicate_terminal_ids_and_false_latency_rejected(self):
        for field,value in (("Request ID",0),("Latency",19)):
            with tempfile.TemporaryDirectory() as directory:
                prefix=Path(directory)/"run"
                task,row,requests=fixture(prefix)
                requests[1][field]=value
                with Path(f"{prefix}_requests.csv").open("w",newline="") as h:
                    w=csv.DictWriter(h,fieldnames=list(requests[0]));w.writeheader();w.writerows(requests)
                with self.assertRaises(ValueError):
                    a.audit_run(task,row,prefix)

    def test_unfinished_wrong_seed_and_power_rejected(self):
        for metric,value in (("In Flight At End",1),("Actual Seed",102),("Total Power (mW)",500)):
            with tempfile.TemporaryDirectory() as directory:
                prefix=Path(directory)/"run"
                task,row,_=fixture(prefix)
                path=Path(f"{prefix}_summary.csv")
                with path.open(newline="") as h:
                    rows=list(csv.reader(h))
                rows=[[k,str(value) if k==metric else v] for k,v in rows]
                with path.open("w",newline="") as h:
                    csv.writer(h).writerows(rows)
                with self.assertRaises(ValueError):
                    a.audit_run(task,row,prefix)

    def test_paired_contrasts_joint_seeds_and_traffic_mismatch(self):
        runs=[]
        for seed in (101,102):
            for index,name in enumerate(a.SCENARIOS):
                r={m:float(index) for m in a.METRICS}
                r.update({**a.cell_metadata(parameters(name)), "analysis_status":"valid", "run_id":f"{name}_{seed}",
                          "actual_seed":seed, "traffic_sha256":"same", "effective_parameters":parameters(name),
                          "power_parameters":{}, "total_requests":10,"completed_requests":10,
                          "total_completed_latency_cycles":index*10,"total_energy_pj":100})
                runs.append(r)
        contrasts,audit=a.matched_contrasts(runs,np.random.default_rng(42),10000)
        self.assertEqual(len(contrasts),3)
        self.assertEqual(len(audit),6)
        for c in contrasts:
            self.assertEqual(c["matched_seeds"],[101,102])
            self.assertEqual(c["joint_scenarios"],list(a.SCENARIOS))
        self.assertEqual(contrasts[0]["average_latency_cycles_mean"],1)
        runs[-1]["traffic_sha256"]="different"
        contrasts,audit=a.matched_contrasts(runs,np.random.default_rng(42),10000)
        self.assertEqual(contrasts[0]["matched_seeds"],[101])
        self.assertEqual(sum(not r["traffic_equal"] for r in audit),1)

    def test_missing_jobs_visible_and_null_in_export(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            spec={"defaults":parameters(),"seeds":[101],"configurations":[{"name":"U"}]}
            (path/"batch.json").write_text(json.dumps(spec))
            metadata=a.analyze(path,path/"analysis",make_plots=False)
            self.assertEqual(metadata["errors"],1)
            summary=json.loads((path/"analysis/summary.json").read_text())[0]
            self.assertEqual(summary["missing_runs"],1)
            self.assertIsNone(summary["average_latency_cycles_mean"])
            self.assertIsNone(summary["pooled_success_rate_pct"])
            self.assertEqual(summary["planned_seeds"],[101])

    def test_full_power_paths_and_requested_seeds_are_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix=Path(directory)/"run"
            task,row,_=fixture(prefix)
            task["parameters"]["power_config"]="different/path/power_model.ini"
            with self.assertRaisesRegex(ValueError,"resolved path"):
                a.audit_run(task,row,prefix)
            task["parameters"]["power_config"]="power_model.ini"
            row["requested_seed"]=102
            with self.assertRaisesRegex(ValueError,"requested_seed"):
                a.audit_run(task,row,prefix)

    def test_fault_rate_changes_preserve_traffic_without_cross_layer_pairing(self):
        runs=[]
        for layer,f,digest in ((2,.05,"a"),(2,.7,"changed"),(3,.7,"different_by_layer")):
            p=parameters(f=f,layers=layer)
            runs.append({**a.cell_metadata(p),"analysis_status":"valid","run_id":f"{layer}_{f}","actual_seed":101,
                         "effective_parameters":p,"power_parameters":{},"traffic_sha256":digest})
        audits=a.audit_fixed_seed_traffic(runs)
        self.assertEqual(len(audits),1)
        self.assertFalse(audits[0]["traffic_equal"])
        self.assertEqual(audits[0]["num_layers"],2)

    def test_horizontal_distance_average_and_spare_usage_corruption_rejected(self):
        cases=(("Total Horizontal Distance",9,"horizontal distances"),
               ("Average Horizontal Distance",9,"Average horizontal distance"),
               ("Redundant TSV Usages",2,"spare launch"))
        for metric,value,message in cases:
            with self.subTest(metric=metric),tempfile.TemporaryDirectory() as directory:
                prefix=Path(directory)/"run"
                task,row,_=fixture(prefix)
                path=Path(f"{prefix}_summary.csv")
                with path.open(newline="") as h:
                    rows=list(csv.reader(h))
                rows=[[k,str(value) if k==metric else v] for k,v in rows]
                with path.open("w",newline="") as h:
                    csv.writer(h).writerows(rows)
                with self.assertRaisesRegex(ValueError,message):
                    a.audit_run(task,row,prefix)
        with tempfile.TemporaryDirectory() as directory:
            prefix=Path(directory)/"run"
            task,row,requests=fixture(prefix)
            requests[0]["Horizontal Distance"]=3
            with Path(f"{prefix}_requests.csv").open("w",newline="") as h:
                w=csv.DictWriter(h,fieldnames=list(requests[0]));w.writeheader();w.writerows(requests)
            with self.assertRaisesRegex(ValueError,"horizontal distances"):
                a.audit_run(task,row,prefix)

    def test_no_completions_rejects_numeric_min_and_max_latency(self):
        for metric in ("Min Latency","Max Latency"):
            with self.subTest(metric=metric),tempfile.TemporaryDirectory() as directory:
                prefix=Path(directory)/"run"
                task,row,_=fixture(prefix,zero_completion=True)
                with Path(f"{prefix}_summary.csv").open("a",newline="") as h:
                    csv.writer(h).writerow([metric,0])
                with self.assertRaisesRegex(ValueError,"Undefined min/max"):
                    a.audit_run(task,row,prefix)

    def test_zero_traffic_rejects_launch_counters(self):
        for changes in ({"Router Events":1,"Horizontal Hops":1},
                        {"Router Events":1,"Vertical Hops":1},
                        {"Router Events":1,"Vertical Hops":1,"Redundant Vertical Hops":1,"Redundant TSV Usages":1}):
            with self.subTest(changes=changes),tempfile.TemporaryDirectory() as directory:
                prefix=Path(directory)/"run"
                task,row,_=fixture(prefix,zero_traffic=True)
                path=Path(f"{prefix}_summary.csv")
                with path.open(newline="") as h:
                    rows=list(csv.reader(h))
                rows=[[k,str(changes[k]) if k in changes else v] for k,v in rows]
                with path.open("w",newline="") as h:
                    csv.writer(h).writerows(rows)
                with self.assertRaisesRegex(ValueError,"Zero generated traffic"):
                    a.audit_run(task,row,prefix)

    def test_request_id_and_workload_endpoint_corruption_rejected(self):
        for field,value,message in (("Request ID",-1,"nonnegative"),("Start Z",1,"workload"),("End Z",0,"workload")):
            with self.subTest(field=field),tempfile.TemporaryDirectory() as directory:
                prefix=Path(directory)/"run"
                task,row,requests=fixture(prefix)
                requests[0][field]=value
                with Path(f"{prefix}_requests.csv").open("w",newline="") as h:
                    w=csv.DictWriter(h,fieldnames=list(requests[0]));w.writeheader();w.writerows(requests)
                with self.assertRaisesRegex(ValueError,message):
                    a.audit_run(task,row,prefix)


if __name__=="__main__":
    unittest.main()
