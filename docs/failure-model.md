# Failure distributions

The user selects a distribution independently of when failures occur (`a` initial, `b` runtime, `c` both).

| Distribution | Channel weight | Spatial behavior |
|---|---|---|
| `uniform` | 1 | Independent identical probabilities |
| `hotspot` | Fixed Gaussian excess above 1 | Higher probability around configured centers |

For channel i at lower-layer interface z and a center j on the same interface:

`w_i = 1 + (multiplier - 1) * max_j exp(-distance(i,j)^2 / (2*radius^2))`.

Interfaces without a center have weight 1. Auto centers are drawn once from their own seeded RNG; manual centers use `x,y,z` triples. The two probability maps independently solve `p_i = min(1, scale*w_i)` so their means over **all original channels**, including spares, equal the initial/runtime settings. Maps do not change as channels fail. Multiple centers use maximum influence rather than addition.

## Runtime probability and routing risk

Initial probability describes defects at initialization. Runtime probability p is conditional per cycle for a surviving channel. The first runtime trial is cycle 0. A channel's first failure cycle follows `Pr(T <= t) = 1-(1-p)^(t+1)`. The simulator samples each lifetime once and processes due events through a priority queue; this has the same distribution as repeated independent Bernoulli trials.

Vertical crossings in both directions use the channel at the lower z. If it fails during a reserved or launched crossing, rerouting starts at the last reached router; already launched energy is retained. Runtime failures remain active during drain.

For runtime risk, the route's log-survival cost adds `-log(1-p_i) * exposure_cycles` for each vertical crossing. Exposure runs from planning time through its predicted completion, including preceding hops and waits. `lambda2` weights this cost; `reliability_min` limits its sum. It is a planning estimate: later contention can change exposure, and repeated use of a channel is approximated as separate exposures. Initial-only routing uses the initial probability as a design-risk proxy despite observing the current health state. Horizontal hops carry no TSV failure risk. Risk/congestion penalties do not enter physical completion times.

The simulator retains its original one-cycle dispatch after new routing/rerouting. Arrival and runtime exposure estimates include that cycle. Direct `Router::route` callers can set the optional dispatch delay explicitly; its default is zero.

Traffic, hotspot placement, initial failures and runtime lifetimes use separate `std::mt19937` streams created with `std::seed_seq{actual_seed, stream_tag}`. Tags are `0x54524146`, `0x484f5453`, `0x494e4954`, `0x52554e54` respectively. Exact reproducibility is intended within the same C++ standard-library implementation; distribution implementations may differ across toolchains.

## Evidence and limits

[Ni et al., *A Region-Based Through-Silicon via Repair Method for Clustered Faults* (2017)](https://www.jstage.jst.go.jp/article/transele/E100.C/12/E100.C_1108/_article) describes spatially clustered manufacturing defects associated with wafer processing. This motivates offering spatial fault regions. The Gaussian shape and mean normalization here are explicit simulation choices, not a fitted manufacturing dataset or a result claimed by that paper.

Fixed regions enable reproducible redundancy comparisons. This model does not infer temperature, stress, wear or traffic-dependent failures. Traffic heatmaps show traffic weights/usage; the failure view shows a separate fixed probability map. All probabilities apply to a logical 64 bit channel, not directly to a single physical via.
