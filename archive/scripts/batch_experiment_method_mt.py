#!/usr/bin/env python3
"""Multithreaded batch runner for the final table in ExperimentMethod.pdf."""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import subprocess
import threading
import time
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple
from tqdm.auto import tqdm


DEFAULT_FAILURE_RATES = [
    0.05,
    0.10,
    0.15,
    0.20,
    0.25,
    0.30,
    0.35,
    0.40,
    0.45,
    0.50,
    0.60,
    0.70,
]
DEFAULT_LAYERS = [2, 3, 4, 5]
DEFAULT_MAX_HORIZONTAL_DISTANCE = 25
SEED_POOL = [42 + index for index in range(64)]
PROGRESS_RE = re.compile(r"\((\d+)/(\d+)\)")
REQUEST_COUNT_RE = re.compile(r"请求:\s*(\d+)|requests?:\s*(\d+)", re.IGNORECASE)


@dataclass(frozen=True)
class RunTask:
    failure_rate: float
    layers: int
    repeat_index: int
    seed: int

    @property
    def failure_rate_pct(self) -> int:
        return int(round(self.failure_rate * 100))

    @property
    def run_id(self) -> str:
        return (
            f"f{self.failure_rate_pct:02d}"
            f"_l{self.layers}"
            f"_r{self.repeat_index:02d}"
            f"_s{self.seed}"
        )


@dataclass
class TaskProgress:
    total_cycles: int
    current_cycle: int = 0
    started: bool = False
    finished: bool = False
    request_count: Optional[int] = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run the ExperimentMethod.pdf parameter sweep in parallel and "
            "write only the final table under output/."
        )
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=Path("bin/tsvra"),
        help="Path to the TSVRA executable.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("output"),
        help="Directory for final TSV/Markdown outputs.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=min(4, os.cpu_count() or 1),
        help="Maximum number of concurrent simulator processes.",
    )
    parser.add_argument(
        "--cycles",
        type=int,
        default=5000,
        help="Simulation cycles per run.",
    )
    parser.add_argument(
        "--grid-factor",
        type=int,
        default=4,
        help="Grid factor passed to the simulator.",
    )
    parser.add_argument(
        "--failure-mode",
        default="a",
        choices=["a", "b", "c"],
        help="Failure mode passed to the simulator.",
    )
    parser.add_argument(
        "--redundancy",
        default="shared",
        choices=["shared", "corner4", "none"],
        help="Redundancy layout passed to the simulator.",
    )
    parser.add_argument(
        "--failure-model",
        default="uniform",
        choices=["uniform", "clustered"],
        help="Failure model passed to the simulator.",
    )
    parser.add_argument(
        "--cluster-strength",
        type=float,
        default=0.8,
        help="Cluster strength for clustered failure model.",
    )
    parser.add_argument(
        "--cluster-radius",
        type=int,
        default=4,
        help="Cluster radius for clustered failure model.",
    )
    parser.add_argument(
        "--layers",
        nargs="+",
        type=int,
        default=DEFAULT_LAYERS,
        help="Layer counts to test.",
    )
    parser.add_argument(
        "--failure-rates",
        nargs="+",
        type=float,
        default=DEFAULT_FAILURE_RATES,
        help="Failure rates to test, written as decimals such as 0.05.",
    )
    parser.add_argument(
        "--max-horizontal-distance",
        type=int,
        default=DEFAULT_MAX_HORIZONTAL_DISTANCE,
        help=(
            "Per-request horizontal distance cap. "
            f"Default: {DEFAULT_MAX_HORIZONTAL_DISTANCE}; use 0 for unlimited behavior."
        ),
    )
    return parser.parse_args()


def repeat_count_for_failure_rate(failure_rate: float) -> int:
    failure_rate_pct = int(round(failure_rate * 100))
    if failure_rate_pct >= 30:
        return 10
    if failure_rate_pct >= 20:
        return 20
    return 50


def build_tasks(layers: Iterable[int], failure_rates: Iterable[float]) -> List[RunTask]:
    tasks: List[RunTask] = []
    for layer_count in layers:
        for failure_rate in failure_rates:
            repeats = repeat_count_for_failure_rate(failure_rate)
            if repeats > len(SEED_POOL):
                raise ValueError(
                    f"Not enough seeds for failure_rate={failure_rate}: {repeats}"
                )
            for repeat_index in range(1, repeats + 1):
                tasks.append(
                    RunTask(
                        failure_rate=failure_rate,
                        layers=layer_count,
                        repeat_index=repeat_index,
                        seed=SEED_POOL[repeat_index - 1],
                    )
                )
    return tasks


def parse_metric_csv(path: Path) -> Dict[str, str]:
    metrics: Dict[str, str] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        next(reader, None)
        for row in reader:
            if len(row) >= 2:
                metrics[row[0].strip()] = row[1].strip()
    return metrics


def to_int(metrics: Dict[str, str], key: str) -> int:
    return int(float(metrics[key]))


def to_float(metrics: Dict[str, str], key: str) -> float:
    return float(metrics[key])


def metric_int(metrics: Dict[str, str], key: str, default: int = 0) -> int:
    value = metrics.get(key)
    if value is None or value == "":
        return default
    return int(float(value))


def metric_float(metrics: Dict[str, str], key: str, default: float = 0.0) -> float:
    value = metrics.get(key)
    if value is None or value == "":
        return default
    return float(value)


def update_task_progress_from_line(
    progress_state: TaskProgress,
    progress_lock: threading.Lock,
    line: str,
) -> None:
    progress_match = PROGRESS_RE.search(line)
    if progress_match is not None:
        current_cycle = int(progress_match.group(1))
        total_cycles = int(progress_match.group(2))
        request_match = REQUEST_COUNT_RE.search(line)
        request_count = None
        if request_match is not None:
            request_count = int(request_match.group(1) or request_match.group(2))
        with progress_lock:
            progress_state.started = True
            progress_state.current_cycle = current_cycle
            progress_state.total_cycles = total_cycles
            if request_count is not None:
                progress_state.request_count = request_count
        return

    if "开始仿真" in line or "Starting simulation" in line:
        with progress_lock:
            progress_state.started = True


def run_single_task(
    binary: Path,
    tmp_dir: Path,
    cycles: int,
    grid_factor: int,
    failure_mode: str,
    redundancy: str,
    failure_model: str,
    cluster_strength: float,
    cluster_radius: int,
    max_horizontal_distance: int,
    task: RunTask,
    progress_state: TaskProgress,
    progress_lock: threading.Lock,
) -> Dict[str, object]:
    prefix = tmp_dir / task.run_id
    cmd = [
        str(binary),
        "--layers",
        str(task.layers),
        "--grid-factor",
        str(grid_factor),
        "--failure-mode",
        failure_mode,
        "--failure-rate",
        str(task.failure_rate),
        "--cycles",
        str(cycles),
        "--seed",
        str(task.seed),
        "--redundancy",
        redundancy,
        "--failure-model",
        failure_model,
        "--output",
        str(prefix),
    ]
    if max_horizontal_distance > 0:
        cmd.extend(["--max-horizontal-distance", str(max_horizontal_distance)])
    if failure_model == "clustered":
        cmd.extend(
            [
                "--cluster-strength",
                str(cluster_strength),
                "--cluster-radius",
                str(cluster_radius),
            ]
        )
    if os.name == "posix" and shutil.which("stdbuf") is not None:
        cmd = ["stdbuf", "-oL", "-eL", *cmd]

    combined_output: List[str] = []
    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    try:
        assert process.stdout is not None
        for raw_line in process.stdout:
            line = raw_line.strip()
            if line:
                combined_output.append(line)
                update_task_progress_from_line(progress_state, progress_lock, line)
    finally:
        if process.stdout is not None:
            process.stdout.close()

    return_code = process.wait()
    if return_code != 0:
        raise RuntimeError(
            f"{task.run_id} failed with code {return_code}: "
            + "\n".join(combined_output[-20:])
        )

    summary_path = Path(f"{prefix}_summary.csv")
    requests_path = Path(f"{prefix}_requests.csv")
    if not summary_path.exists() or not requests_path.exists():
        raise FileNotFoundError(f"Missing output CSVs for {task.run_id}")

    metrics = parse_metric_csv(summary_path)
    completed_requests = to_int(metrics, "Completed Requests")
    average_horizontal_distance = metric_float(metrics, "Average Horizontal Distance")
    total_requests = to_int(metrics, "Total Requests")
    correct_rate = (
        completed_requests * 100.0 / total_requests if total_requests else 0.0
    )

    summary_row = {
        "total_failure_rate_pct": task.failure_rate_pct,
        "layers": task.layers,
        "average_horizontal_distance": average_horizontal_distance,
        "correct_rate": correct_rate,
    }

    summary_path.unlink(missing_ok=True)
    requests_path.unlink(missing_ok=True)

    with progress_lock:
        progress_state.started = True
        progress_state.current_cycle = progress_state.total_cycles
        progress_state.finished = True

    return {
        "task": task,
        "summary_row": summary_row,
    }


def init_aggregate(expected_runs: int) -> Dict[str, float]:
    return {
        "expected_runs": expected_runs,
        "runs": 0,
        "horizontal_distance_sum": 0.0,
        "correct_rate_sum": 0.0,
    }


def update_aggregate(
    aggregate: Dict[Tuple[int, int], Dict[str, float]],
    task: RunTask,
    summary_row: Dict[str, object],
    repeats_by_rate: Dict[float, int],
) -> None:
    key = (task.failure_rate_pct, task.layers)
    slot = aggregate.setdefault(key, init_aggregate(repeats_by_rate[task.failure_rate]))
    slot["runs"] += 1
    slot["horizontal_distance_sum"] += float(summary_row["average_horizontal_distance"])
    slot["correct_rate_sum"] += float(summary_row["correct_rate"])


def finalize_pdf_row(
    failure_rate_pct: int,
    layers: int,
    slot: Dict[str, float],
) -> Dict[str, str]:
    runs = int(slot["runs"])
    average_horizontal_distance = (
        slot["horizontal_distance_sum"] / runs if runs else 0.0
    )
    correct_rate = slot["correct_rate_sum"] / runs if runs else 0.0
    return {
        "总故障率": f"{failure_rate_pct}%",
        "层数": str(layers),
        "Horizontal Distance": f"{average_horizontal_distance:.6f}",
        "正确率": f"{correct_rate:.6f}%",
    }


def write_tsv_table(
    path: Path,
    rows: List[Dict[str, str]],
    fieldnames: List[str],
) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def write_markdown_table(
    path: Path,
    rows: List[Dict[str, str]],
    fieldnames: List[str],
) -> None:
    with path.open("w", encoding="utf-8") as handle:
        handle.write("| " + " | ".join(fieldnames) + " |\n")
        handle.write("| " + " | ".join(["---"] * len(fieldnames)) + " |\n")
        for row in rows:
            handle.write("| " + " | ".join(row[name] for name in fieldnames) + " |\n")


def get_first_unfinished_task(
    tasks: List[RunTask],
    progress_by_run_id: Dict[str, TaskProgress],
    progress_lock: threading.Lock,
) -> Optional[RunTask]:
    with progress_lock:
        for task in tasks:
            if not progress_by_run_id[task.run_id].finished:
                return task
    return None


def snapshot_task_progress(
    progress_by_run_id: Dict[str, TaskProgress],
    run_id: str,
    progress_lock: threading.Lock,
) -> TaskProgress:
    with progress_lock:
        state = progress_by_run_id[run_id]
        return TaskProgress(
            total_cycles=state.total_cycles,
            current_cycle=state.current_cycle,
            started=state.started,
            finished=state.finished,
            request_count=state.request_count,
        )


def progress_postfix(task_state: TaskProgress) -> str:
    if not task_state.started:
        return "waiting"
    if task_state.finished:
        return "done"
    if task_state.current_cycle >= task_state.total_cycles:
        if task_state.request_count is not None:
            return f"draining req={task_state.request_count}"
        return "draining"
    if task_state.request_count is not None:
        return f"req={task_state.request_count}"
    return "running"


def main() -> int:
    args = parse_args()

    binary = args.binary.resolve()
    if not binary.exists():
        raise FileNotFoundError(f"Binary not found: {binary}")

    output_dir = args.output_dir.resolve()
    tmp_dir = output_dir / "_tmp_runs"
    output_dir.mkdir(parents=True, exist_ok=True)
    tmp_dir.mkdir(parents=True, exist_ok=True)
    for stale_file in tmp_dir.iterdir():
        if stale_file.is_file():
            stale_file.unlink()

    tasks = build_tasks(args.layers, args.failure_rates)
    total_task_count = len(tasks)
    repeats_by_rate = {
        rate: repeat_count_for_failure_rate(rate) for rate in args.failure_rates
    }
    progress_lock = threading.Lock()
    progress_by_run_id = {
        task.run_id: TaskProgress(total_cycles=args.cycles)
        for task in tasks
    }

    legacy_table_csv = output_dir / "experiment_method_summary.csv"
    table_tsv = output_dir / "experiment_method_summary.tsv"
    table_md = output_dir / "experiment_method_summary.md"
    table_fieldnames = [
        "总故障率",
        "层数",
        "Horizontal Distance",
        "正确率",
    ]

    aggregate: Dict[Tuple[int, int], Dict[str, float]] = {}

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        future_to_task = {
            executor.submit(
                run_single_task,
                binary,
                tmp_dir,
                args.cycles,
                args.grid_factor,
                args.failure_mode,
                args.redundancy,
                args.failure_model,
                args.cluster_strength,
                args.cluster_radius,
                args.max_horizontal_distance,
                task,
                progress_by_run_id[task.run_id],
                progress_lock,
            ): task
            for task in tasks
        }
        pending_futures = set(future_to_task)
        rendered_run_id: Optional[str] = None

        with tqdm(
            total=args.cycles,
            desc="waiting",
            unit="cycle",
            dynamic_ncols=True,
            leave=True,
            position=0,
        ) as progress, tqdm(
            total=total_task_count,
            desc="Experiment sweep",
            unit="run",
            dynamic_ncols=True,
            leave=True,
            position=1,
        ) as sweep_progress:
            while pending_futures:
                display_task = get_first_unfinished_task(
                    tasks,
                    progress_by_run_id,
                    progress_lock,
                )
                if display_task is not None:
                    task_state = snapshot_task_progress(
                        progress_by_run_id,
                        display_task.run_id,
                        progress_lock,
                    )
                    if rendered_run_id != display_task.run_id:
                        progress.reset(total=task_state.total_cycles)
                        progress.set_description(display_task.run_id)
                        rendered_run_id = display_task.run_id

                    progress.n = min(task_state.current_cycle, task_state.total_cycles)
                    progress.set_postfix_str(progress_postfix(task_state))
                    progress.refresh()

                done_futures, pending_futures = wait(
                    pending_futures,
                    timeout=0.2,
                    return_when=FIRST_COMPLETED,
                )

                for future in done_futures:
                    task = future_to_task[future]
                    result = future.result()
                    summary_row = result["summary_row"]

                    update_aggregate(
                        aggregate,
                        task,
                        summary_row,
                        repeats_by_rate,
                    )
                    sweep_progress.set_postfix_str(task.run_id)
                    sweep_progress.update(1)
                time.sleep(0.05)

    written_rows = []
    failure_rate_pcts = [int(round(rate * 100)) for rate in args.failure_rates]
    for layers in args.layers:
        for failure_rate_pct in failure_rate_pcts:
            key = (failure_rate_pct, layers)
            if key not in aggregate:
                continue
            written_rows.append(finalize_pdf_row(failure_rate_pct, layers, aggregate[key]))

    legacy_table_csv.unlink(missing_ok=True)
    write_tsv_table(table_tsv, written_rows, table_fieldnames)
    write_markdown_table(table_md, written_rows, table_fieldnames)
    tmp_dir.rmdir()
    print(f"Saved: {table_tsv}")
    print(f"Saved: {table_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
