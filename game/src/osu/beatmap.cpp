#include "beatmap.h"

namespace osu {

float Beatmap::circle_radius() const {
    float cs = difficulty.circle_size;
    return BASE_OBJECT_RADIUS * (1.0f - 0.7f * (cs - 5.0f) / 5.0f) / 2.0f;
}

float Beatmap::approach_time() const {
    float ar = difficulty.approach_rate;
    if (ar > 5.0f) {
        return 1200.0f + (450.0f - 1200.0f) * (ar - 5.0f) / 5.0f;
    }
    if (ar < 5.0f) {
        return 1200.0f + (1800.0f - 1200.0f) * (5.0f - ar) / 5.0f;
    }
    return 1200.0f;
}

float Beatmap::fade_in_time() const {
    float ar = difficulty.approach_rate;
    if (ar > 5.0f) {
        return 800.0f + (300.0f - 800.0f) * (ar - 5.0f) / 5.0f;
    }
    if (ar < 5.0f) {
        return 800.0f + (1200.0f - 800.0f) * (5.0f - ar) / 5.0f;
    }
    return 800.0f;
}

} // namespace osu
