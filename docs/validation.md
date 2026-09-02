# Validation boundary

The table below is a concise transcription of the simulator verification points reported in the associated IEEE paper. It is included for traceability, not as a fresh benchmark produced by this repository.

| Reference simulator BPM | Reported mean measured BPM | Reported absolute error BPM |
| ---: | ---: | ---: |
| 30 | 30.00 | 0.00 |
| 60 | 60.00 | 0.00 |
| 100 | 100.00 | 0.00 |
| 150 | 149.93 | 0.07 |
| 180 | 180.23 | 0.23 |
| 210 | 209.33 | 0.67 |
| 320 | 319.12 | 0.33 |

The paper reports a mean absolute error of **0.19 BPM** and mean relative error of **0.09%** under steady-state simulator conditions. It also states the intended scope clearly: screening-oriented acquisition and heart-rate estimation, not a diagnostic 12-lead system and not a substitute for long-term ambulatory monitoring.

## What this repository tests

- Correct threshold direction (`upper > lower`).
- One event per hysteresis cycle.
- BPM calculation from timestamp intervals.
- Rejection of intervals outside the configured operating range.
- Seven-value smoothing behavior.
- Public-release boundary checks.

## What it does not test

- The electrical response of a particular AD8232/Xspace assembly.
- Human-subject recordings.
- Clinical sensitivity, specificity or diagnostic interpretation.
- The paper’s reported simulator table as a newly reproduced experiment.

See the paper linked in the root README for the complete experimental context and limitations.

