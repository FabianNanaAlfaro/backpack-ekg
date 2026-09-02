#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Portable two-threshold heart-rate estimator.
// It deliberately contains no Arduino or board-library dependency.
class BpmEstimator {
public:
  static const size_t kDefaultHistorySize = 7;

  explicit BpmEstimator(float upperThreshold = 2.18f,
                        float lowerThreshold = 2.14f,
                        float minBpm = 30.0f,
                        float maxBpm = 330.0f)
      : upperThreshold_(upperThreshold),
        lowerThreshold_(lowerThreshold),
        minBpm_(minBpm),
        maxBpm_(maxBpm),
        historyIndex_(0),
        historyCount_(0),
        armed_(true),
        hasLastCrossing_(false),
        lastCrossingMs_(0),
        lastIntervalMs_(0),
        lastRawBpm_(0.0f) {
    if (!thresholdsValid(upperThreshold_, lowerThreshold_)) {
      upperThreshold_ = 2.18f;
      lowerThreshold_ = 2.14f;
    }
    if (!bpmRangeValid(minBpm_, maxBpm_)) {
      minBpm_ = 30.0f;
      maxBpm_ = 330.0f;
    }
    reset();
  }

  void reset() {
    historyIndex_ = 0;
    historyCount_ = 0;
    armed_ = true;
    hasLastCrossing_ = false;
    lastCrossingMs_ = 0;
    lastIntervalMs_ = 0;
    lastRawBpm_ = 0.0f;
    for (size_t i = 0; i < kDefaultHistorySize; ++i) {
      history_[i] = 0.0f;
    }
  }

  // Returns false instead of silently inverting a caller's hysteresis band.
  bool setThresholds(float upperThreshold, float lowerThreshold) {
    if (!thresholdsValid(upperThreshold, lowerThreshold)) {
      return false;
    }
    upperThreshold_ = upperThreshold;
    lowerThreshold_ = lowerThreshold;
    return true;
  }

  bool setBpmRange(float minBpm, float maxBpm) {
    if (!bpmRangeValid(minBpm, maxBpm)) {
      return false;
    }
    minBpm_ = minBpm;
    maxBpm_ = maxBpm;
    return true;
  }

  // Feed one filtered sample and its acquisition timestamp.
  // Returns true only when a new, valid smoothed BPM is available.
  bool update(float sample, uint32_t timestampMs) {
    if (!isFinite(sample)) {
      return false;
    }

    if (!armed_) {
      if (sample <= lowerThreshold_) {
        armed_ = true;
      }
      return false;
    }

    if (sample < upperThreshold_) {
      return false;
    }

    // One rising threshold crossing = one candidate beat. The detector stays
    // latched until the signal reaches the lower threshold.
    armed_ = false;
    if (!hasLastCrossing_) {
      hasLastCrossing_ = true;
      lastCrossingMs_ = timestampMs;
      return false;
    }

    const uint32_t intervalMs = timestampMs - lastCrossingMs_;
    lastCrossingMs_ = timestampMs;
    if (intervalMs == 0) {
      return false;
    }

    const float rawBpm = 60000.0f / static_cast<float>(intervalMs);
    lastIntervalMs_ = intervalMs;
    lastRawBpm_ = rawBpm;
    if (rawBpm < minBpm_ || rawBpm > maxBpm_) {
      return false;
    }

    history_[historyIndex_] = rawBpm;
    historyIndex_ = (historyIndex_ + 1) % kDefaultHistorySize;
    if (historyCount_ < kDefaultHistorySize) {
      ++historyCount_;
    }
    return true;
  }

  float bpm() const {
    if (historyCount_ == 0) {
      return 0.0f;
    }
    float total = 0.0f;
    for (size_t i = 0; i < historyCount_; ++i) {
      total += history_[i];
    }
    return total / static_cast<float>(historyCount_);
  }

  float upperThreshold() const { return upperThreshold_; }
  float lowerThreshold() const { return lowerThreshold_; }
  float minBpm() const { return minBpm_; }
  float maxBpm() const { return maxBpm_; }
  uint32_t lastIntervalMs() const { return lastIntervalMs_; }
  float lastRawBpm() const { return lastRawBpm_; }
  size_t validIntervals() const { return historyCount_; }
  bool hasBpm() const { return historyCount_ > 0; }

private:
  static bool isFinite(float value) {
    return !isnan(value) && !isinf(value);
  }

  static bool thresholdsValid(float upperThreshold, float lowerThreshold) {
    return isFinite(upperThreshold) && isFinite(lowerThreshold) &&
           upperThreshold > lowerThreshold;
  }

  static bool bpmRangeValid(float minBpm, float maxBpm) {
    return isFinite(minBpm) && isFinite(maxBpm) && minBpm > 0.0f &&
           minBpm < maxBpm;
  }

  float upperThreshold_;
  float lowerThreshold_;
  float minBpm_;
  float maxBpm_;
  float history_[kDefaultHistorySize];
  size_t historyIndex_;
  size_t historyCount_;
  bool armed_;
  bool hasLastCrossing_;
  uint32_t lastCrossingMs_;
  uint32_t lastIntervalMs_;
  float lastRawBpm_;
};

