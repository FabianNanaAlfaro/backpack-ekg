import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from analysis.bpm_estimator import BpmEstimator


class BpmEstimatorTests(unittest.TestCase):
    def test_hysteresis_emits_one_event_per_cycle(self) -> None:
        estimator = BpmEstimator(0.6, 0.4)
        events = []
        sequence = [0.0, 0.8, 0.9, 0.7, 0.5, 0.3, 0.8, 0.3]
        for timestamp, sample in enumerate(sequence, start=1000):
            value = estimator.update(sample, timestamp)
            if value is not None:
                events.append(value)
        self.assertEqual(events, [])
        self.assertEqual(estimator.valid_intervals, 0)

    def test_sixty_bpm_after_two_crossings(self) -> None:
        estimator = BpmEstimator(0.6, 0.4)
        events = []
        for timestamp in range(0, 5001, 10):
            phase = timestamp % 1000
            sample = 0.8 if phase in (0, 10, 20) else 0.0
            value = estimator.update(sample, timestamp)
            if value is not None:
                events.append(value)
        self.assertGreaterEqual(len(events), 3)
        self.assertTrue(all(abs(value - 60.0) < 1e-9 for value in events))

    def test_high_plateau_does_not_double_count(self) -> None:
        estimator = BpmEstimator(0.6, 0.4)
        samples = [(0, 0.0), (100, 0.8), (200, 0.8), (300, 0.8), (400, 0.3), (1100, 0.8)]
        events = []
        for timestamp, sample in samples:
            value = estimator.update(sample, timestamp)
            if value is not None:
                events.append(value)
        self.assertEqual(len(events), 1)
        self.assertAlmostEqual(events[0], 60.0)

    def test_intermediate_noise_does_not_rearm_detector(self) -> None:
        estimator = BpmEstimator(0.6, 0.4)
        samples = [(0, 0.0), (100, 0.8), (200, 0.55), (300, 0.5), (400, 0.45), (900, 0.8)]
        events = []
        for timestamp, sample in samples:
            value = estimator.update(sample, timestamp)
            if value is not None:
                events.append(value)
        self.assertEqual(events, [])

        value = estimator.update(0.3, 1000)
        self.assertIsNone(value)
        value = estimator.update(0.8, 1100)
        self.assertAlmostEqual(value, 60.0)

    def test_out_of_range_interval_is_rejected(self) -> None:
        estimator = BpmEstimator(0.6, 0.4)
        for timestamp, sample in [(0, 0.0), (100, 0.8), (200, 0.3), (250, 0.8)]:
            estimator.update(sample, timestamp)
        self.assertEqual(estimator.valid_intervals, 0)

    def test_reset_clears_history(self) -> None:
        estimator = BpmEstimator(0.6, 0.4)
        for timestamp, sample in [(0, 0.8), (10, 0.3), (1010, 0.8)]:
            estimator.update(sample, timestamp)
        self.assertEqual(estimator.valid_intervals, 1)
        estimator.reset()
        self.assertEqual(estimator.valid_intervals, 0)
        self.assertIsNone(estimator.bpm)

    def test_invalid_threshold_order_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            BpmEstimator(0.4, 0.6)


if __name__ == "__main__":
    unittest.main()
