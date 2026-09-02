"""Dependency-free reference implementation of the Backpack EKG BPM logic."""

from __future__ import annotations

from dataclasses import dataclass, field
from math import isfinite
from typing import Optional


@dataclass
class BpmEstimator:
    """Two-threshold beat timing with seven-value smoothing.

    A beat is registered on the rising crossing of ``upper_threshold``.
    The detector is re-armed only after the signal falls to ``lower_threshold``.
    """

    upper_threshold: float = 2.18
    lower_threshold: float = 2.14
    min_bpm: float = 30.0
    max_bpm: float = 330.0
    history_size: int = 7
    _armed: bool = field(default=True, init=False)
    _last_crossing_ms: Optional[int] = field(default=None, init=False)
    _history: list[float] = field(default_factory=list, init=False)
    last_interval_ms: Optional[int] = field(default=None, init=False)
    last_raw_bpm: Optional[float] = field(default=None, init=False)

    def __post_init__(self) -> None:
        if self.history_size < 1:
            raise ValueError("history_size must be positive")
        self.set_thresholds(self.upper_threshold, self.lower_threshold)
        self.set_bpm_range(self.min_bpm, self.max_bpm)

    def set_thresholds(self, upper: float, lower: float) -> None:
        if not isfinite(upper) or not isfinite(lower) or upper <= lower:
            raise ValueError("upper threshold must be finite and greater than lower")
        self.upper_threshold = upper
        self.lower_threshold = lower

    def set_bpm_range(self, minimum: float, maximum: float) -> None:
        if not isfinite(minimum) or not isfinite(maximum) or minimum <= 0 or minimum >= maximum:
            raise ValueError("invalid BPM range")
        self.min_bpm = minimum
        self.max_bpm = maximum

    def reset(self) -> None:
        self._armed = True
        self._last_crossing_ms = None
        self._history.clear()
        self.last_interval_ms = None
        self.last_raw_bpm = None

    def update(self, sample: float, timestamp_ms: int) -> Optional[float]:
        """Consume one sample and return the smoothed BPM on a valid beat interval."""

        if not isfinite(sample):
            return None

        if not self._armed:
            if sample <= self.lower_threshold:
                self._armed = True
            return None

        if sample < self.upper_threshold:
            return None

        self._armed = False
        if self._last_crossing_ms is None:
            self._last_crossing_ms = timestamp_ms
            return None

        interval = timestamp_ms - self._last_crossing_ms
        self._last_crossing_ms = timestamp_ms
        if interval <= 0:
            return None

        raw_bpm = 60000.0 / interval
        self.last_interval_ms = interval
        self.last_raw_bpm = raw_bpm
        if not self.min_bpm <= raw_bpm <= self.max_bpm:
            return None

        self._history.append(raw_bpm)
        if len(self._history) > self.history_size:
            self._history.pop(0)
        return self.bpm

    @property
    def bpm(self) -> Optional[float]:
        """Return the average of the accepted history, or ``None`` initially."""

        if not self._history:
            return None
        return sum(self._history) / len(self._history)

    @property
    def valid_intervals(self) -> int:
        return len(self._history)


def demo() -> None:
    estimator = BpmEstimator(0.6, 0.4)
    outputs: list[float] = []
    for beat_ms in range(1000, 7001, 1000):
        for timestamp in range(beat_ms - 20, beat_ms + 31, 10):
            sample = 0.8 if timestamp < beat_ms + 10 else 0.0
            value = estimator.update(sample, timestamp)
            if value is not None:
                outputs.append(value)
    print("synthetic outputs:", ", ".join(f"{value:.2f}" for value in outputs))


if __name__ == "__main__":
    demo()

