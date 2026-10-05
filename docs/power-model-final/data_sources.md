# TSVRA Power Training Data Sources

## 1. Nature of the Data

This dataset is **synthetic data calibrated from physics and the literature** for architecture-level TSVRA power modeling. It is not a database of chip measurements. It is fixed at 45 nm, 1.0 V, and 1 GHz. Published parameters set the power scale, and TSVRA network, traffic, and fault parameters then generate 12,000 training samples.

These notes describe the retained August 2026 dataset, rather than the current simulator's [runtime power model](../power-model.md). The original label-generation script was not supplied; the training script consumes the existing CSV. The spreadsheet, dataset, trained model, and saved evaluation outputs are preserved as supplied. The ±20% label-bound columns are deterministic margins, not statistical confidence intervals.

## 2. Router Power Source

The router power baseline comes from MIT's DSENT paper:

> C. Sun et al., "DSENT: A Tool Connecting Emerging Photonics with Electronics for Opto-Electronic Networks-on-Chip," NOCS 2012.

- Paper: https://projects.csail.mit.edu/wiki/pub/LSPgroup/PublicationList/dsent_nocs12.pdf
- Case used: 45 nm, 64 bit, 1 GHz, injection rate 0.16.
- Published case power: buffer 7.55 mW, crossbar 2.06 mW, control 0.83 mW, clock 0.63 mW, total router power about 11.2 mW.
- The original notes describe scaling this baseline by effective injection rate and flit width. A single loaded operating point does not identify idle power and event slopes independently; the 0.76 mW remainder after subtracting buffer/crossbar/control power is not an established idle or leakage measurement. Current runtime coefficients instead come from independently captured DSENT bulk reference outputs.

## 3. TSV Parameter Source

TSV capacitance and geometry cases are attributed to Kim and Lim's SLIP 2010 paper:

> D. H. Kim and S. K. Lim, "Through-Silicon-Via-aware Delay and Power Prediction Model for Buffered Interconnects in 3D ICs."

- Paper: https://gtcad.gatech.edu/www/papers/p25-kim.pdf
- Capacitance cases were extracted for different TSV widths, heights, and liner thicknesses.
- Representative total capacitances used here are 10, 34, 67.5, 134.3, and 263.6 fF.
- TSV resistance is sampled from 80–120 Ω around the paper's case of about 100 Ω, to represent manufacturing and structural variation.

## 4. TSVRA Scenario Parameters

The following variables were not copied from an external power database. They are experimental factors defined by the original dataset study:

- Number of network routers, stacked layers, and flit width
- Traffic model, injection rate, bit toggle rate, and hotspot weight
- Average horizontal and vertical hops, and horizontal link length
- Initial fault rate, cluster intensity, reroute rate, and redundant vertical-link fraction
- TSV width, height, liner thickness, capacitance, and resistance

These parameters are sampled over a reasonable experimental range so the data covers normal operation, global faults, and clustered faults.

## 5. How Power Labels Are Generated

Interconnect switching energy uses:

```text
E_transition = 0.5 × C × V²
E_flit = E_transition × flit_width × toggle_rate
```

The original notes state that injection rate, router count, horizontal and vertical hops, TSV parameters, and reroute/redundant-transfer overhead are combined to produce:

- Horizontal link energy
- Vertical TSV energy
- Power of a single router
- Dynamic power of network transport
- The final label `network_total_power_mW`

These descriptions are insufficient to regenerate the exact labels without the original generation code. Retraining the supplied regressor reproduces a prediction workflow over the retained data, rather than recreating the data or validating its labels against measurements.

## 6. Scope

The data is suitable for training a power-prediction model, comparing fault strategies, and analyzing trends at the project-completion stage. Results should be described as "architecture-level estimates calibrated from the literature and physical formulas." They must not be described as tape-out measurement power, and they should not be extrapolated directly to another process, voltage, or frequency.

After Vivado, development-board, or chip measurements are available, keep the existing input fields, add `measured_power_mW`, and retrain the model or calibrate its residual.
