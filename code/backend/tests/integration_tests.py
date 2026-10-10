"""Check the executable's shared CLI/web accounting and interactive pacing."""

import csv
import json
import math
import queue
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BINARY = Path(sys.argv[1]).resolve()


def close(a, b):
    assert math.isclose(float(a), float(b), rel_tol=1e-12, abs_tol=1e-9), (a, b)


def native(prefix, extra=(), cwd=ROOT):
    args = [str(BINARY), "--layers", "3", "--grid-factor", "1", "--redundancy", "none",
            "--cycles", "30", "--seed", "42", "--failure-rate", "0",
            "--request-probability", "0.25", *extra]
    subprocess.run([*args, "--output", str(prefix)], cwd=cwd, capture_output=True,
                   text=True, check=True, timeout=30)
    with Path(str(prefix) + "_summary.csv").open() as file:
        summary = {row[0]: row[1] for row in list(csv.reader(file))[1:]}
    with Path(str(prefix) + "_requests.csv").open() as file:
        requests = list(csv.DictReader(file))
    with Path(str(prefix) + "_power.csv").open() as file:
        trace = list(csv.DictReader(file))
    config = json.loads(Path(str(prefix) + "_config.json").read_text())
    return args, summary, requests, trace, config


TRAFFIC_KEYS = ["Request ID", "Start X", "Start Y", "Start Z", "End X", "End Y", "End Z", "Generate Time"]


def traffic(records):
    return sorted(tuple(row[key] for key in TRAFFIC_KEYS) for row in records)


def experiment_close(actual, expected):
    assert math.isclose(float(actual), float(expected), rel_tol=1e-8, abs_tol=1e-8), (actual, expected)


def validate_raw_records(summary, requests, trace, config):
    """Reconstruct terminal, timing and component-energy identities independently."""
    generated = int(summary["Total Requests"])
    completed = [row for row in requests if row["Status"] == "Completed"]
    failed = [row for row in requests if row["Status"] == "Failed"]
    assert len(requests) == len({int(row["Request ID"]) for row in requests}) == generated
    assert len(completed) == int(summary["Completed Requests"])
    assert len(failed) == int(summary["Failed Requests"])
    assert generated == len(completed) + len(failed)
    assert int(summary["In Flight At End"]) == 0
    settings, parameters = config["simulation"], config["power_parameters"]
    assert config["actual_seed"] == int(summary["Actual Seed"]) == settings["random_seed"]
    size, layers = settings["grid_size"], settings["num_layers"]
    latencies = []
    for row in requests:
        assert 0 <= int(row["Generate Time"]) < settings["simulation_cycles"]
        for endpoint in ["Start", "End"]:
            assert 0 <= int(row[endpoint + " X"]) < size
            assert 0 <= int(row[endpoint + " Y"]) < size
        assert 0 <= int(row["Start Z"]) < layers - 1
        assert int(row["End Z"]) == layers - 1
        if row["Status"] == "Completed":
            latency = int(row["Complete Time"]) - int(row["Generate Time"])
            assert latency == int(row["Latency"]) > 0
            latencies.append(latency)
        else:
            assert row["Complete Time"] == row["Latency"] == "N/A"
    if completed:
        experiment_close(summary["Average Latency"], sum(latencies) / len(latencies))
        assert int(summary["Min Latency"]) == min(latencies)
        assert int(summary["Max Latency"]) == max(latencies)
    else:
        assert "Average Latency" not in summary
        assert summary["Energy Per Completed Flit (pJ)"] == ""
    if generated:
        experiment_close(summary["Success Rate (%)"], 100 * len(completed) / generated)
    else:
        assert "Success Rate (%)" not in summary
    if not failed:
        assert int(summary["In Flight At Horizon"]) == sum(
            int(row["Complete Time"]) >= settings["simulation_cycles"] for row in completed)
    assert int(summary["Total Horizontal Distance"]) == sum(int(row["Horizontal Distance"]) for row in requests)
    assert int(summary["Failed Redundant TSVs"]) <= int(summary["Failed TSVs"]) <= size * size * (layers - 1)

    # Recompute hop coefficients from primitive coefficients, not derived exports.
    event_energy = sum(parameters[key] for key in ["router_buffer_event_pj", "router_crossbar_event_pj", "router_control_event_pj"])
    factor = 0.5 * 1e-3 * parameters["voltage_v"] ** 2 * parameters["total_toggle_probability"]
    horizontal_energy = factor * parameters["horizontal_link_length_um"] * parameters["horizontal_capacitance_ff_per_um"] * parameters["flit_bits"]
    vertical_energy = factor * parameters["tsv_capacitance_ff"] * parameters["physical_signal_tsvs"]
    spare_energy = vertical_energy * parameters["redundant_vertical_capacitance_multiplier"]
    experiment_close(parameters["router_event_energy_pj"], event_energy)
    experiment_close(parameters["horizontal_hop_energy_pj"], horizontal_energy)
    experiment_close(parameters["vertical_hop_energy_pj"], vertical_energy)
    experiment_close(parameters["redundant_vertical_hop_energy_pj"], spare_energy)
    routers = size * size * layers
    assert int(summary["Router Count"]) == routers
    elapsed = int(summary["Elapsed Cycles"])
    seconds = elapsed / parameters["frequency_hz"]
    assert elapsed >= settings["simulation_cycles"]
    experiment_close(summary["Elapsed Seconds"], seconds)
    events, horizontal, vertical, spares = [int(summary[key]) for key in ["Router Events", "Horizontal Hops", "Vertical Hops", "Redundant Vertical Hops"]]
    assert events == horizontal + vertical and spares <= vertical
    assert spares == int(summary["Redundant TSV Usages"])
    idle = routers * parameters["router_idle_mw"]
    components = {
        "Router Energy (pJ)": idle * seconds * 1e9 + events * event_energy,
        "Horizontal Energy (pJ)": horizontal * horizontal_energy,
        "Vertical Energy (pJ)": (vertical - spares) * vertical_energy + spares * spare_energy,
    }
    for name, value in components.items():
        experiment_close(summary[name], value)
    total = sum(components.values())
    experiment_close(summary["Total Energy (pJ)"], total)
    experiment_close(summary["Total Power (mW)"], total / (seconds * 1e9))
    experiment_close(summary["Router Static Power (mW)"], idle)
    experiment_close(summary["Router Dynamic Power (mW)"], events * event_energy / (seconds * 1e9))
    experiment_close(summary["Horizontal Power (mW)"], components["Horizontal Energy (pJ)"] / (seconds * 1e9))
    experiment_close(summary["Vertical Power (mW)"], components["Vertical Energy (pJ)"] / (seconds * 1e9))
    if completed:
        experiment_close(summary["Energy Per Completed Flit (pJ)"], total / len(completed))

    assert trace and int(trace[-1]["elapsed_cycles"]) == elapsed
    final = trace[-1]
    for csv_key, trace_key in [("Router Events", "router_events"), ("Horizontal Hops", "horizontal_hops"),
            ("Vertical Hops", "vertical_hops"), ("Redundant Vertical Hops", "redundant_vertical_hops"),
            ("Total Energy (pJ)", "total_energy_pj"), ("Total Power (mW)", "total_mw")]:
        if csv_key in ["Router Events", "Horizontal Hops", "Vertical Hops", "Redundant Vertical Hops"]:
            assert int(summary[csv_key]) == int(final[trace_key])
        else:
            experiment_close(summary[csv_key], final[trace_key])
    previous_cycles, previous_events = 0, 0
    for row in trace:
        cycles, count = int(row["elapsed_cycles"]), int(row["router_events"])
        assert cycles > previous_cycles and count >= previous_events
        assert count == int(row["horizontal_hops"]) + int(row["vertical_hops"])
        denominator = cycles / parameters["frequency_hz"] * 1e9
        dynamic = count * event_energy
        links = int(row["horizontal_hops"]) * horizontal_energy
        links += (int(row["vertical_hops"]) - int(row["redundant_vertical_hops"])) * vertical_energy
        links += int(row["redundant_vertical_hops"]) * spare_energy
        experiment_close(row["router_static_mw"], idle)
        experiment_close(row["router_dynamic_mw"], dynamic / denominator)
        experiment_close(row["total_energy_pj"], idle * denominator + dynamic + links)
        previous_cycles, previous_events = cycles, count


def full_experiment_replay(base):
    scenarios = {"U": [], "H1": [{"x": 5, "y": 5, "z": 0}],
                 "H2": [{"x": 10, "y": 10, "z": 0}],
                 "H12": [{"x": 5, "y": 5, "z": 0}, {"x": 10, "y": 10, "z": 0}]}
    for layers in range(2, 6):
        reference_traffic = None
        for name, centers in scenarios.items():
            expected = {"num_layers": layers, "grid_factor": 4, "grid_size": 16,
                "redundancy": "shared", "failure_mode": "a", "failure_rate": 0,
                "initial_failure_rate": 0.30, "runtime_failure_rate": 0,
                "simulation_cycles": 300, "request_probability": 0.02, "random_seed": 101,
                "vertical_delay": 5, "horizontal_delay": 500, "router_delay": 15,
                "lambda1": 0, "lambda2": 0, "max_path_length": 0,
                "max_horizontal_distance": 0, "reliability_min": 0,
                "failure_model": "hotspot" if centers else "uniform", "hotspot_count": max(1, len(centers)),
                "hotspot_radius": 3, "hotspot_multiplier": 5, "hotspot_centers": centers,
                "power_config": str(ROOT / "code/config/power_model.ini")}
            extra = ["--layers", str(layers), "--grid-factor", "4", "--redundancy", "shared",
                "--cycles", "300", "--seed", "101", "--failure-mode", "a",
                "--initial-failure-rate", "0.30", "--runtime-failure-rate", "0",
                "--request-probability", "0.02", "--vertical-delay", "5", "--horizontal-delay", "500",
                "--router-delay", "15", "--lambda1", "0", "--lambda2", "0", "--max-path-length", "0",
                "--max-horizontal-distance", "0", "--reliability-min", "0", "--failure-model", expected["failure_model"],
                "--hotspot-count", str(expected["hotspot_count"]), "--hotspot-radius", "3", "--hotspot-multiplier", "5",
                "--power-config", expected["power_config"]]
            if centers:
                extra += ["--hotspot-centers", ";".join(",".join(str(center[axis]) for axis in ["x", "y", "z"]) for center in centers)]
            first = native(base / f"replay-{layers}-{name}-a", extra)
            second = native(base / f"replay-{layers}-{name}-b", extra)
            args, summary, requests, trace, config = first
            for key, value in expected.items():
                assert config["simulation"][key] == value, (layers, name, key)
            assert config["actual_seed"] == 101 and config["actual_hotspot_centers"] == centers
            validate_raw_records(summary, requests, trace, config)
            assert requests, "Replay must exercise generated traffic"
            # Every field is canonical except the intentionally different output prefix.
            canonical = lambda records: sorted(records, key=lambda row: int(row["Request ID"]))
            first_config = json.loads(json.dumps(config))
            second_config = json.loads(json.dumps(second[4]))
            first_config["simulation"].pop("output_prefix")
            second_config["simulation"].pop("output_prefix")
            assert first_config == second_config
            assert summary == second[1] and trace == second[3]
            assert canonical(requests) == canonical(second[2])
            if reference_traffic is None:
                reference_traffic = traffic(requests)
            assert traffic(requests) == reference_traffic, (layers, name, "fault-dependent traffic")

            # One streamed replay per layer checks the two-center manual profile.
            if name == "H12":
                stream = subprocess.run([*args, "--json-stream"], cwd=ROOT, input="r", text=True,
                                        capture_output=True, check=True, timeout=30)
                messages = [json.loads(line) for line in stream.stdout.splitlines()]
                initial, done = messages[0], messages[-1]["summary"]
                assert initial["type"] == "init" and messages[-1]["type"] == "done"
                for csv_key, json_key in [("grid_size", "gridSize"), ("num_layers", "numLayers"),
                    ("grid_factor", "gridFactor"), ("failure_mode", "failureMode"), ("failure_rate", "failureRate"),
                    ("initial_failure_rate", "initialFailureRate"), ("runtime_failure_rate", "runtimeFailureRate"),
                    ("vertical_delay", "verticalDelay"), ("horizontal_delay", "horizontalDelay"),
                    ("router_delay", "routerDelay"), ("request_probability", "requestProbability"),
                    ("simulation_cycles", "totalCycles")]:
                    assert initial["config"][json_key] == config["simulation"][csv_key]
                assert initial["config"]["actualSeed"] == done["actualSeed"] == config["actual_seed"]
                assert initial["config"]["redundancy"]["layout"] == config["simulation"]["redundancy"]
                assert initial["config"]["failureModel"]["centers"] == centers
                for csv_key, json_key in [("failure_model", "type"), ("hotspot_count", "hotspotCount"),
                    ("hotspot_radius", "hotspotRadius"), ("hotspot_multiplier", "hotspotMultiplier")]:
                    assert initial["config"]["failureModel"][json_key] == config["simulation"][csv_key]
                assert initial["powerParameters"] == config["power_parameters"]
                assert len(initial["grid"]) == 256 * layers
                assert len(initial["failureProbabilities"]) == 256 * (layers - 1)
                assert initial["config"]["redundancy"]["sparesPerLayer"] == 25
                experiment_close(sum(row["initial"] for row in initial["failureProbabilities"]) / (256 * (layers - 1)), 0.30)
                assert all(row["runtime"] == 0 for row in initial["failureProbabilities"])
                new = {row["id"]: row for message in messages[1:-1] for row in message["events"]["newRequests"]}
                complete_events = [row for message in messages[1:-1] for row in message["events"]["completed"]]
                failure_events = [row for message in messages[1:-1] for row in message["events"]["failed"]]
                terminal = [row["id"] for row in complete_events + failure_events]
                assert len(new) == len(terminal) == len(set(terminal)) == int(summary["Total Requests"])
                assert done["inFlightAtEnd"] == int(summary["In Flight At End"]) == 0
                assert done["inFlightAtHorizon"] == int(summary["In Flight At Horizon"])
                assert done["completed"] == int(summary["Completed Requests"])
                assert done["failed"] == int(summary["Failed Requests"])
                # Human-facing JSON rate/latency fields explicitly format to 2/1 decimals.
                assert abs(done["successRate"] - float(summary["Success Rate (%)"])) <= 0.005 + 1e-12
                assert abs(done["avgLatency"] - float(summary["Average Latency"])) <= 0.05 + 1e-12
                by_id = {int(row["Request ID"]): row for row in requests}
                for event in complete_events:
                    row = by_id[event["id"]]
                    assert row["Status"] == "Completed"
                    assert event["time"] == int(row["Complete Time"])
                    assert event["latency"] == int(row["Latency"])
                for event in failure_events:
                    assert by_id[event["id"]]["Status"] == "Failed"
                failed_channels = {(row["x"], row["y"], row["z"]) for row in initial["initialFailures"]}
                streamed_traffic = []
                for event in new.values():
                    streamed_traffic.append(tuple(str(event[key]) for key in ["id", "sx", "sy", "sz", "ex", "ey", "ez", "time"]))
                    for before, after in zip(event["path"], event["path"][1:]):
                        assert sum(abs(a - b) for a, b in zip(before, after)) == 1
                        assert 0 <= after[0] < 16 and 0 <= after[1] < 16 and 0 <= after[2] < layers
                        if before[2] != after[2]:
                            assert (before[0], before[1], min(before[2], after[2])) not in failed_channels
                assert sorted(streamed_traffic) == reference_traffic
                for csv_key, json_key in [("Router Events", "routerEvents"), ("Horizontal Hops", "horizontalHops"),
                    ("Vertical Hops", "verticalHops"), ("Redundant Vertical Hops", "redundantVerticalHops"),
                    ("Elapsed Cycles", "elapsedCycles"), ("Elapsed Seconds", "elapsedSeconds"),
                    ("Router Static Power (mW)", "routerStaticMw"), ("Router Dynamic Power (mW)", "routerDynamicMw"),
                    ("Horizontal Power (mW)", "horizontalMw"), ("Vertical Power (mW)", "verticalMw"),
                    ("Total Power (mW)", "totalMw"), ("Total Energy (pJ)", "totalEnergyPj"),
                    ("Router Energy (pJ)", "routerEnergyPj"), ("Horizontal Energy (pJ)", "horizontalEnergyPj"),
                    ("Vertical Energy (pJ)", "verticalEnergyPj")]:
                    if csv_key in ["Router Events", "Horizontal Hops", "Vertical Hops", "Redundant Vertical Hops", "Elapsed Cycles"]:
                        assert int(summary[csv_key]) == done["power"][json_key]
                    else:
                        experiment_close(summary[csv_key], done["power"][json_key])


def runtime_reroute_fixture(base):
    extra = ["--layers", "3", "--cycles", "1", "--seed", "6", "--request-probability", "1",
             "--failure-mode", "b", "--initial-failure-rate", "0", "--runtime-failure-rate", "0.004",
             "--horizontal-delay", "20"]
    args, summary, requests, trace, config = native(base / "reroute-unlimited", extra)
    validate_raw_records(summary, requests, trace, config)
    stream = subprocess.run([*args, "--json-stream"], cwd=ROOT, input="r", text=True,
                            capture_output=True, check=True, timeout=30)
    messages = [json.loads(line) for line in stream.stdout.splitlines()]
    reroutes = [(message["cycle"], event, message["stats"]["power"]) for message in messages[1:-1]
                for event in message["events"]["reroutes"]]
    assert len(reroutes) == 2
    first_cycle, first, first_power = reroutes[0]
    second_cycle, second, second_power = reroutes[1]
    assert first_cycle == 107 and second_cycle == 152
    # Three horizontal arrivals reached (3,2,0); its vertical edge failed
    # before launch. The second cancelled edge launched after a fourth arrival.
    assert first["oldPath"][:4] == first["newPath"][:4]
    assert first["newPath"][3] == [3, 2, 0]
    assert first_power["horizontalHops"] == 3 and first_power["verticalHops"] == 0
    assert second["oldPath"][:5] == second["newPath"][:5]
    assert second["newPath"][4] == [3, 1, 0]
    assert second_power["horizontalHops"] == 4 and second_power["verticalHops"] == 1
    failures = {(event["x"], event["y"], event["z"]): message["cycle"]
                for message in messages[1:-1] for event in message["events"]["newFailures"]}
    assert failures[(3, 2, 0)] < 1 + 3 * 35
    assert 143 <= failures[(3, 1, 0)] < 143 + 20
    assert requests[0]["Status"] == "Completed" and int(requests[0]["Horizontal Distance"]) == 9
    assert int(summary["Horizontal Hops"]) == 9 and int(summary["Vertical Hops"]) == 3
    assert int(summary["Router Events"]) == 12  # Includes the cancelled launched crossing.

    # The same reroute has four completed horizontal steps. A budget of five
    # must reject a suffix requiring more than one, without restarting the budget.
    _, limited, rows, limited_trace, limited_config = native(base / "reroute-constrained",
                                                            [*extra, "--max-horizontal-distance", "5"])
    validate_raw_records(limited, rows, limited_trace, limited_config)
    assert int(limited["Failed Requests"]) == 1 and int(limited["Completed Requests"]) == 0
    assert rows[0]["Status"] == "Failed" and int(rows[0]["Horizontal Distance"]) == 4
    assert int(limited["Elapsed Cycles"]) == 153
    assert int(limited["Router Events"]) == 5 and int(limited["Vertical Hops"]) == 1


with tempfile.TemporaryDirectory(prefix="tsvra-integration-") as folder:
    base = Path(folder)
    args, summary, requests, trace, config = native(base / "uniform")
    validate_raw_records(summary, requests, trace, config)
    # Explicit resource/output paths also work outside the repository cwd.
    _, outside_summary, outside_requests, _, _ = native(
        base / "outside-cwd", ["--power-config", str(ROOT / "code/config/power_model.ini")], cwd=base
    )
    assert outside_summary == summary
    assert outside_requests == requests
    stream = subprocess.run([*args, "--json-stream"], cwd=ROOT, input="r", text=True,
                            capture_output=True, check=True, timeout=30)
    messages = [json.loads(line) for line in stream.stdout.splitlines()]
    assert messages[0]["type"] == "init" and messages[-1]["type"] == "done"
    initial = messages[0]
    done = messages[-1]["summary"]
    power = done["power"]
    assert initial["config"]["verticalDelay"] == 5
    assert initial["config"]["horizontalDelay"] == 500
    assert initial["config"]["routerDelay"] == 15
    assert config["actual_seed"] == done["actualSeed"] == 42
    for csv_key, json_key in [("Total Requests", "totalRequests"),
                              ("Completed Requests", "completed"), ("Failed Requests", "failed")]:
        assert int(summary[csv_key]) == done[json_key]
    for csv_key, json_key in [("Elapsed Cycles", "elapsedCycles"), ("Elapsed Seconds", "elapsedSeconds"),
                              ("Router Events", "routerEvents"), ("Horizontal Hops", "horizontalHops"),
                              ("Vertical Hops", "verticalHops"), ("Total Power (mW)", "totalMw"),
                              ("Total Energy (pJ)", "totalEnergyPj"),
                              ("Energy Per Completed Flit (pJ)", "energyPerCompletedFlitPj")]:
        close(summary[csv_key], power[json_key])
    close(trace[-1]["total_energy_pj"], power["totalEnergyPj"])
    assert int(trace[-1]["elapsed_cycles"]) == power["elapsedCycles"] > 30
    assert power["routerEvents"] == power["horizontalHops"] + power["verticalHops"]
    assert power["horizontalHops"] == sum(int(row["Horizontal Distance"]) for row in requests)
    close(power["totalEnergyPj"], power["routerEnergyPj"] + power["horizontalEnergyPj"] + power["verticalEnergyPj"])
    for before, after in zip(messages[1:-1], messages[2:-1]):
        assert after["stats"]["power"]["totalEnergyPj"] >= before["stats"]["power"]["totalEnergyPj"]

    # Changing fault distribution cannot change the traffic draw sequence.
    _, hot_summary, hot_requests, _, hot_config = native(base / "hotspot", ["--failure-model", "hotspot"])
    assert traffic(requests) == traffic(hot_requests)
    assert hot_config["actual_hotspot_centers"]
    close(summary["Total Energy (pJ)"], hot_summary["Total Energy (pJ)"])

    # Failed route searches generate no artificial flit-hop energy.
    failed_args, failed, failed_rows, failed_trace, failed_config = native(base / "all-failed", ["--failure-rate", "1"])
    validate_raw_records(failed, failed_rows, failed_trace, failed_config)
    assert int(failed["Completed Requests"]) == 0
    assert int(failed["Router Events"]) == 0
    assert failed["Energy Per Completed Flit (pJ)"] == ""
    assert float(failed["Total Energy (pJ)"]) > 0
    failed_json = subprocess.run([*failed_args, "--json-stream"], cwd=ROOT, input="r", text=True,
                                 capture_output=True, check=True, timeout=30)
    assert json.loads(failed_json.stdout.splitlines()[-1])["summary"]["power"]["energyPerCompletedFlitPj"] is None

    _, zero, zero_rows, zero_trace, zero_config = native(base / "zero-traffic", ["--request-probability", "0"])
    validate_raw_records(zero, zero_rows, zero_trace, zero_config)
    assert int(zero["Total Requests"]) == int(zero["Router Events"]) == 0
    assert int(zero["Elapsed Cycles"]) == 30 and int(zero["In Flight At Horizon"]) == 0
    assert float(zero["Total Energy (pJ)"]) > 0

    full_experiment_replay(base)
    runtime_reroute_fixture(base)

    # A one-hop flit launches, then its channel fails before arrival. Its
    # consumed energy survives the route failure; the one-hop limit forbids detours.
    runtime_json = subprocess.run(
        [*args, "--layers", "2", "--cycles", "1", "--seed", "27",
         "--request-probability", "1", "--failure-mode", "b",
         "--initial-failure-rate", "0", "--runtime-failure-rate", "0.03",
         "--max-path-length", "1", "--json-stream"],
        cwd=ROOT, input="r", text=True, capture_output=True, check=True, timeout=30,
    )
    runtime_messages = [json.loads(line) for line in runtime_json.stdout.splitlines()]
    runtime_done = runtime_messages[-1]["summary"]
    runtime_power = runtime_done["power"]
    assert runtime_done["failed"] == 1 and runtime_done["completed"] == 0
    assert runtime_power["routerEvents"] == runtime_power["verticalHops"] == 1
    assert runtime_power["horizontalHops"] == 0
    close(runtime_power["verticalEnergyPj"], 1.08)
    close(runtime_power["routerDynamicMw"] * runtime_power["elapsedSeconds"] * 1e9,
          runtime_messages[0]["powerParameters"]["router_event_energy_pj"])
    launched_cycles = [message["stats"]["power"] for message in runtime_messages[1:-1]
                       if message["stats"]["power"]["routerEvents"]]
    assert launched_cycles
    assert all(power["verticalHops"] == 1 and power["verticalEnergyPj"] == 1.08 for power in launched_cycles)

    # A live controller grants exactly one cycle and retains launched energy on stop.
    process = subprocess.Popen([*args, "--cycles", "100", "--request-probability", "1", "--json-stream"],
                               cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True, bufsize=1)
    inbox = queue.Queue()

    def read_lines():
        for line in process.stdout:
            inbox.put(json.loads(line))

    threading.Thread(target=read_lines, daemon=True).start()
    try:
        assert inbox.get(timeout=5)["type"] == "init"
        process.stdin.write("n")
        process.stdin.flush()
        first = inbox.get(timeout=5)
        assert first["type"] == "cycle" and first["cycle"] == 0
        try:
            unexpected = inbox.get(timeout=0.05)
            raise AssertionError(("Extra cycle without credit", unexpected))
        except queue.Empty:
            pass
        process.stdin.write("n")
        process.stdin.flush()
        second = inbox.get(timeout=5)
        assert second["cycle"] == 1 and second["stats"]["power"]["routerEvents"] == 1
        process.stdin.write("s")
        process.stdin.flush()
        stopped = inbox.get(timeout=5)
        assert stopped["type"] == "done"
        assert stopped["summary"]["inFlightAtEnd"] == 2
        assert stopped["summary"]["power"] == second["stats"]["power"]
        assert process.wait(timeout=5) == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        process.stdin.close()
        process.stdout.close()

    # Resume supersedes queued step credits while stdin remains open.
    resume_process = subprocess.Popen([*args, "--cycles", "3", "--request-probability", "0", "--json-stream"],
                                      cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.DEVNULL, text=True, bufsize=1)
    resumed_messages = queue.Queue()

    def read_resume_lines():
        for line in resume_process.stdout:
            resumed_messages.put(json.loads(line))

    threading.Thread(target=read_resume_lines, daemon=True).start()
    try:
        assert resumed_messages.get(timeout=5)["type"] == "init"
        resume_process.stdin.write("nr")
        resume_process.stdin.flush()
        for expected_cycle in range(3):
            message = resumed_messages.get(timeout=5)
            assert message["type"] == "cycle" and message["cycle"] == expected_cycle
        assert resumed_messages.get(timeout=5)["type"] == "done"
        assert resume_process.wait(timeout=5) == 0
    finally:
        if resume_process.poll() is None:
            resume_process.kill()
            resume_process.wait()
        resume_process.stdin.close()
        resume_process.stdout.close()

    bad = subprocess.run([str(BINARY), "--router-delay", "-1"], cwd=ROOT, capture_output=True, timeout=5)
    assert bad.returncode != 0

print("Full shared g4 L2-L5 replay/manual-center traffic matching, raw terminal/timestamp/energy identities, "
      "runtime reroute distance retention, relocated resources, CLI/JSON parity and exact step/stop passed")
