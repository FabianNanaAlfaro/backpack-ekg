#include <cassert>
#include <cmath>

#include "../firmware/backpack_ekg/BpmEstimator.h"

int main() {
    BpmEstimator estimator(0.6f, 0.4f);
    bool emitted = false;

    for (uint32_t time_ms = 0; time_ms <= 5000; time_ms += 10) {
        const uint32_t phase = time_ms % 1000;
        const float sample = (phase <= 20) ? 0.8f : 0.0f;
        if (estimator.update(sample, time_ms)) {
            emitted = true;
            assert(std::fabs(estimator.bpm() - 60.0f) < 0.001f);
        }
    }

    assert(emitted);
    assert(estimator.validIntervals() >= 3);
    return 0;
}

