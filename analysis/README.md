# Offline reference implementation

[`bpm_estimator.py`](bpm_estimator.py) mirrors the portable C++ estimator used by the firmware. It is intentionally dependency-free so the threshold logic can be inspected and tested without the board library, an ECG database or a clinical package.

Run it directly:

```text
python analysis/bpm_estimator.py
```

The command prints a short synthetic 60 BPM demonstration. The tests use synthetic pulses generated in memory; no project recording is included.

