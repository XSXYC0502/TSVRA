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

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]).resolve()


def close(a, b):
    assert math.isclose(float(a), float(b), rel_tol=1e-12, abs_tol=1e-9), (a, b)


def native(prefix, extra=()):
    args = [str(BINARY), "--layers", "3", "--grid-factor", "1", "--redundancy", "none",
            "--cycles", "30", "--seed", "42", "--failure-rate", "0",
            "--request-probability", "0.25", *extra]
    subprocess.run([*args, "--output", str(prefix)], cwd=ROOT, capture_output=True,
                   text=True, check=True, timeout=30)
    with Path(str(prefix) + "_summary.csv").open() as file:
        summary = {row[0]: row[1] for row in list(csv.reader(file))[1:]}
    with Path(str(prefix) + "_requests.csv").open() as file:
        requests = list(csv.DictReader(file))
    with Path(str(prefix) + "_power.csv").open() as file:
        trace = list(csv.DictReader(file))
    config = json.loads(Path(str(prefix) + "_config.json").read_text())
    return args, summary, requests, trace, config


with tempfile.TemporaryDirectory(prefix="tsvra-integration-") as folder:
    base = Path(folder)
    args, summary, requests, trace, config = native(base / "uniform")
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
    keys = ["Request ID", "Start X", "Start Y", "Start Z", "End X", "End Y", "End Z", "Generate Time"]
    traffic = lambda records: sorted(tuple(row[key] for key in keys) for row in records)
    assert traffic(requests) == traffic(hot_requests)
    assert hot_config["actual_hotspot_centers"]
    close(summary["Total Energy (pJ)"], hot_summary["Total Energy (pJ)"])

    # Failed route searches generate no artificial flit-hop energy.
    failed_args, failed, _, _, _ = native(base / "all-failed", ["--failure-rate", "1"])
    assert int(failed["Completed Requests"]) == 0
    assert int(failed["Router Events"]) == 0
    assert failed["Energy Per Completed Flit (pJ)"] == ""
    assert float(failed["Total Energy (pJ)"]) > 0
    failed_json = subprocess.run([*failed_args, "--json-stream"], cwd=ROOT, input="r", text=True,
                                 capture_output=True, check=True, timeout=30)
    assert json.loads(failed_json.stdout.splitlines()[-1])["summary"]["power"]["energyPerCompletedFlitPj"] is None

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

print("CLI/JSON parity, drain energy, RNG isolation, failed-hop energy and exact step/stop passed")
