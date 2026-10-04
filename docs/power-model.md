# Explicit power model and offline calibration

The simulator estimates energy with an explicit idle term and counters for actual launched hops. It loads editable coefficients from `power_model.ini`; simulation and batch execution need no DSENT installation, training, NumPy, scikit-learn, or model prediction service. The default router coefficients were generated offline using the original MIT DSENT implementation. This is an architectural estimate with documented circuit and geometry assumptions.

## Architecture and time

- Each grid point represents one equivalent hardware router. The router population is `num_layers × (4 × grid_factor)²`, including points at boundaries and points serving spare channels. All use the same calibrated router reference.
- One `Request` represents one 64-bit flit. Each vertical channel represents **64 physical signal TSVs**, each with the stated effective capacitance. A spare vertical channel has the same width and default capacitance as a regular channel.
- The reference router has 6 input/output ports, 8 virtual channels per input port, and 2 buffers per virtual channel: **16 flit buffers per port**, rather than 16 buffers per virtual channel. These are the declared equivalent circuit assumptions; the simulator does not model those queues or an RTL implementation.
- The operating reference is **45 nm bulk CMOS, 1 V, 1 GHz, 340 K**, using DSENT's shipped `Bulk45LVT` technology. It is distinct from the 45 nm SOI validation point in the DSENT paper.
- Existing horizontal and vertical delay settings remain architectural cycle delays. The added router service is 15 cycles per launched hop by default. At the selected 1 GHz mapping, one simulation cycle maps to 1 ns. This mapping does not establish measured physical link delay, timing closure, or a measured 15 ns router latency.

Native execution speed and Python batch concurrency do not affect simulated seconds or modeled hardware power. Increasing the number of layers or grid points increases the router population and its idle energy.

## Energy and event definitions

Let `N_R` be the router population, `T = elapsed_cycles / frequency_hz`, and `N_H`, `N_V`, `N_S` be launched horizontal, all vertical, and spare vertical hops. Spare vertical hops are a subset of `N_V`. Each successful launch counts one router event, so `N_event = N_H + N_V`.

```text
E_router_idle [pJ] = N_R × router_idle_mw × T [s] × 10^9
E_router_event [pJ] = N_event × (buffer_event_pj + crossbar_event_pj + control_event_pj)
E_H [pJ] = N_H × horizontal_hop_energy_pj
E_V [pJ] = (N_V - N_S) × vertical_hop_energy_pj
         + N_S × redundant_vertical_hop_energy_pj
E_total = E_router_idle + E_router_event + E_H + E_V
P_component [mW] = E_component [pJ] / (T [s] × 10^9)
```

`router_static_mw` is the non-data-dependent router power, comprising the calibrated continuously running clock and DSENT leakage. The name does not imply that its clock component is transistor leakage. A router event represents one input buffer write plus read, one crossbar traversal, and one two-stage switch allocation. Their circuit energy is charged once at the hop launch; idle energy continues during its service and link delay.

Route search, candidate paths, queue checks, and blocked attempts do not launch a hop and therefore do not increment these counters. A hop launched by a request that later fails still consumed energy. Launched hops of in-flight requests and hops during drain also contribute. Reliability faults do not power-gate routers in this model.

Link energy uses the selected average bit switching assumption:

```text
E_link [pJ] = 0.5 × C_bit [fF] × 10^-3 × V² × 64 × p_toggle
C_H_bit [fF] = horizontal_link_length_um × horizontal_capacitance_ff_per_um
C_V_bit [fF] = tsv_capacitance_ff
```

`p_toggle` is the total probability of either a `0→1` or `1→0` transition. Its default is 0.5. The factor 0.5 already accounts for counting both directions; this convention prevents doubling capacitive energy. This is expected switching energy because requests do not contain 64-bit payload patterns. Dedicated link repeaters, drivers, receivers, ground/power TSVs, coupling patterns, clock distribution outside the router reference, and spare selection circuitry have no separate fitted term in the current model.

Snapshots retain cumulative counters and energy. Consecutive snapshot differences produce interval energy without resetting counters. The power trace contains the cumulative average `total_mw` and `interval_total_mw` for the period since its preceding sample; the dashboard uses the interval quantity for current activity. A time-zero snapshot has zero average powers by convention.

## Default coefficients and sources

| Term | Default | Basis |
| --- | ---: | --- |
| Router idle per router | 16.4557457 mW | Offline DSENT clock 0.6082657 mW + explicit DSENT leakage 15.84748 mW |
| Router buffer energy per event | 4.5489791667 pJ | Offline slope for one write and one read |
| Router crossbar energy per event | 1.2392827083 pJ | Offline unicast traversal slope |
| Router control energy per event | 0.5370015377 pJ | Offline two-stage switch allocation slope |
| Total router energy per event | 6.3252634127 pJ | Sum of the three event coefficients |
| Horizontal length | 1000 µm | Editable nominal geometry assumption |
| Horizontal capacitance | 0.150 fF/µm | DSENT paper Table I, 45 nm reference global wire |
| TSV square width, height | 2 µm, 20 µm | Kim and Lim geometry reference |
| Effective capacitance per signal TSV | 67.5 fF | Kim and Lim Table 2: 66.9 fF MOS + 0.6 fF coupling |
| Horizontal / vertical energy per hop | 2.4 / 1.08 pJ | Capacitive formula at 1 V and total toggle probability 0.5 |
| Spare vertical capacitance multiplier | 1.0 | Same channel geometry assumption |

The horizontal coefficient is taken from [Sun et al., DSENT, NOCS 2012, Table I](https://projects.csail.mit.edu/wiki/pub/LSPgroup/PublicationList/dsent_nocs12.pdf); it is a declared reference wire assumption rather than a fitted extraction from the shipped bulk technology file. That paper's Table II gives a SOI router loaded operating point: 11.2 mW total, 7.55 mW buffer, 2.06 mW crossbar, 0.83 mW control, and 0.63 mW clock at injection 0.16. A single loaded point cannot identify idle power or event slopes. In particular, the 0.76 mW remainder after subtracting those buffer/crossbar/control entries **does not establish leakage**. The defaults here are independently calibrated bulk reference results and do not reproduce that SOI operating point.

[Kim and Lim, “Through-Silicon-Via-aware Delay and Power Prediction Model for Buffered Interconnects in 3D ICs,” SLIP 2010, Tables 2–3](https://gtcad.gatech.edu/www/papers/p25-kim.pdf) use square TSVs and a 0.1 µm liner for the selected 2 µm width, 20 µm height case. `tsv_width_um` records the square width; it is not a circular diameter. The adopted 67.5 fF is an effective load assumption. Width and height are provenance fields: changing them does **not** automatically infer a new capacitance. Supply the corresponding effective `tsv_capacitance_ff` after geometry changes.

The calibration source is [MIT Graphite's original DSENT 0.9 source](https://github.com/mit-carbon/Graphite/tree/1b4bb98f5bfb96ca394b71e7ef3e9e500a1e3997/contrib/dsent/dsent-core), pinned at commit `1b4bb98f5bfb96ca394b71e7ef3e9e500a1e3997`. Its published README identifies version 0.9 dated 22 May 2012. The shipped technology file calls itself bulk CMOS, runs at 340 K, and differs from the paper's SOI reference. The large default leakage is the actual output of that reference model. It has not been replaced by a smaller assumed number.

## Reproduce offline calibration

The checked-in calibration inputs and evidence are:

- `tools/dsent_router.cfg`: full equivalent router configuration and stable output labels;
- `tools/power_calibration/manifest.json`: source revision, configuration/technology checksums, operating point, and per-sample checksums;
- `tools/power_calibration/sample_00.txt` through `sample_03.txt`: raw idle and activity outputs;
- `tools/power_calibration/fit.json`: explicit clock/leakage split, fitted coefficients, samples, reconstruction error, and limitations;
- `tools/calibrate_power.py`: offline collection and fitting using the Python standard library.

To regenerate the INI from the captured evidence without downloading or running DSENT:

```bash
python3 tools/calibrate_power.py --input-dir tools/power_calibration --output /tmp/reproduced_power.ini
```

To collect fresh reference outputs, obtain the pinned upstream checkout and build its DSENT core. The path below is only an example; it is outside the active simulator source.

```bash
git clone https://github.com/mit-carbon/Graphite.git /tmp/tsvra-dsent-source
git -C /tmp/tsvra-dsent-source checkout 1b4bb98f5bfb96ca394b71e7ef3e9e500a1e3997
make -C /tmp/tsvra-dsent-source/contrib/dsent/dsent-core -j4
python3 tools/calibrate_power.py \
  --dsent-root /tmp/tsvra-dsent-source/contrib/dsent/dsent-core \
  --evidence-dir /tmp/tsvra-power-calibration \
  --output /tmp/reproduced_power.ini
```

The collector requires the declared revision and unmodified tracked core sources. It runs injection rates 0, 0.08, 0.16, and 0.32 flits per port per cycle. For this 6-port router, transfer rate is `6 × injection × 10^9` events/s. The zero-activity point establishes the continuous clock plus the independently reported leakage. Each component uses the nonnegative least-squares slope through the origin:

```text
slope [J/event] = sum(event_rate × component_W) / sum(event_rate²)
coefficient [pJ/event] = slope × 10^12
```

The fitted total reproduces the four captured DSENT totals with maximum relative error `3.47 × 10^-7`, within their printed precision. This checks the arithmetic reduction of this reference; it is not independent silicon validation. DSENT 0.9 documents incomplete VC/credit bookkeeping and timing/circuit modeling limits in its README. The simulator's 15-cycle router service remains independently chosen.

`calibration_status = dsent_bulk45_reference` identifies these default router coefficients. Keep `parameter_version`, sources, and `calibration_status` accurate when replacing them. Declared custom values can use a status such as `provisional_custom_architecture`. Link geometry, reference wire capacitance, and average toggling remain assumptions even when the router status reports a DSENT calibration. Router event coefficients and idle power apply at their calibrated operating point; changing voltage or frequency does not automatically re-characterize them. Voltage directly affects the capacitive link formula, and frequency directly affects the cycle-to-second mapping. Recalibrate router coefficients for a different circuit operating point.

## Reporting and batch experiments

Normal simulation exports `<prefix>_summary.csv`, `<prefix>_requests.csv`, `<prefix>_power.csv`, and `<prefix>_config.json`. The configuration retains the actual seed, effective simulation settings, actual hotspot centers, and exact power parameters. Final elapsed time, events, energy, and average power include drain after the traffic horizon. `In Flight At Horizon` describes the earlier horizon even if those flits complete during drain.

Energy per completed flit is `all modeled energy / completed flits`; idle energy and unsuccessful traffic stay in its numerator. With zero completions it is undefined: JSON uses `null`, CSV uses an empty field, and reports show `—`. Average latency is defined only for completed flits. Power remains defined for a nonzero idle simulation.

Edit `experiments/example.json`, then run:

```bash
python3 experiments.py --dry-run
python3 experiments.py
```

The editable file defines `binary`, `output_dir`, `jobs`, `repetitions`, `seeds`, optional `timeout_seconds`, shared `defaults`, and `configurations`. Each named configuration supplies `parameters` and an optional `matrix`; all combinations of its matrix arrays are run. Configuration-level `repetitions` and `seeds` override the shared values. The first `repetitions` seeds are used, so supply enough seeds. Seed 0 requests a random seed, which is exported as `actual_seed`. Paths in settings are relative to the repository; absolute paths also work. `--binary`, `--output-dir`, and `--jobs` override those settings for one invocation.

Use simulation JSON names such as `num_layers`, `simulation_cycles`, `router_delay`, `failure_model`, `hotspot_count`, `hotspot_radius`, and `hotspot_multiplier`. Explicit centers can be integer triples, for example `"hotspot_centers": [[6,6,0]]`. `failure_rate` sets both initial and runtime probabilities; the specific `initial_failure_rate` and `runtime_failure_rate` controls override the corresponding value. `failure_mode` selects which stages use those probabilities. The example compares uniform and hotspot failures at two rates and two layer counts, with two seeds per point: 16 runs.

The runner uses concurrent simulator processes, prints completion and available cycle progress, retains every raw output/log, and refreshes `runs.csv`, `summary.csv`, and `summary.md`. It validates actual seed agreement, request counts, final trace totals, and router/link counter identities before accepting a run. Individual errors remain in the report and make the runner return a nonzero exit code. A fresh output directory is required when run filenames would collide; existing run evidence is preserved.

The summary includes the legacy horizontal-distance and success-rate comparison along with failure, hotspot, latency, event, and power results. Run means weight repetitions equally; pooled success and energy divide summed numerators by summed denominators. For the previous failure-rate sweep with different repeat counts, define separate named configurations for each rate range and set their respective repetitions and seed lists (for example 50 repeats below 0.20, 20 below 0.30, and 10 from 0.30 upward). No code changes or external Python packages are required.
