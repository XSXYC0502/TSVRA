# TSVRA documentation

Start with the repository [README](../README.md) for build and run commands. The documents below describe the current implementation; retained research materials are identified separately.

## Current implementation

| Document | Contents |
| --- | --- |
| [Problem setting](problem-setting.md) | Grid, redundancy, traffic, routing, scheduling, and interpretation of results |
| [Architecture](architecture.md) | Native modules, browser/server integration, and developer entry points |
| [Parameters and fixed assumptions](hardcoded-parameters.md) | Native, example-file, and web profiles; validation and performance limits; where to change values |
| [Failure model](failure-model.md) | Uniform/hotspot probabilities, runtime lifetimes, routing risk, and limitations |
| [Runtime power model](power-model.md) | Event/idle accounting, calibration evidence, units, and batch experiments |
| [Web visualization](web-visualization.md) | Native JSON stream, WebSocket messages, state, and playback controls |
| [Three.js notes](threejs-instancedmesh-notes.md) | Instanced rendering guidance |
| [Windows run guide](../HOW_TO_RUN_PROJECT_UNDER_WINDOWS.md) | MSVC/MinGW builds and Windows troubleshooting |
| [Roadmap](roadmap.md) | Implemented capabilities and proposed follow-up work |
| [Report draft](report-draft.md) | Implementation-based prose for a project report and guidance on supporting results |

## Retained research materials

- [Power-prediction package](power-model-final/README_final_plan.md): the August 2026 synthetic dataset, spreadsheet, trained regression model, training/prediction scripts, and evaluation outputs. It is a separate research workflow; the simulator uses the runtime power model described above.
- [Historical experiment reports](historical-results/README.md): earlier failure-rate/layer and horizontal-distance-cap tables, with their evidence limits explained.

The runtime calibration source revision, raw DSENT outputs, and fitting record remain in [`tools/power_calibration/`](../tools/power_calibration/). The retained package's `data/dataset_manifest.json` describes its dataset; the runtime calibration's `manifest.json` records calibration inputs. Neither is an inventory of this documentation directory.
