#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace osu {

static constexpr float PLAYFIELD_WIDTH = 512.0f;
static constexpr float PLAYFIELD_HEIGHT = 384.0f;
static constexpr float BASE_OBJECT_RADIUS = 64.0f;

enum class PathType { Linear, PerfectCurve, Bezier, Catmull };

struct PathControlPoint {
    glm::vec2 position;
};

struct SliderPath {
    PathType type = PathType::Linear;
    std::vector<PathControlPoint> control_points;
    float expected_distance = 0.0f;
    std::vector<glm::vec2> calculated_path;
    float calculated_length = 0.0f;
};

enum class HitObjectType { Circle, Slider, Spinner };

struct HitObject {
    HitObjectType type;
    glm::vec2 position;
    int32_t time;
    int32_t new_combo;
    int32_t combo_color_offset;

    SliderPath slider_path;
    int32_t repeat_count = 0;
    float slider_duration = 0.0f;

    int32_t end_time = 0;
};

struct TimingPoint {
    double time;
    double beat_length;
    bool is_timing_change;
    int32_t meter = 4;

    double bpm() const { return 60000.0 / beat_length; }
    double sv_multiplier() const {
        return is_timing_change ? 1.0 : (-100.0 / beat_length);
    }
};

struct BeatmapDifficulty {
    float hp_drain = 5.0f;
    float circle_size = 5.0f;
    float overall_difficulty = 5.0f;
    float approach_rate = 5.0f;
    float slider_multiplier = 1.4f;
    float slider_tick_rate = 1.0f;
};

struct BeatmapMetadata {
    std::string title;
    std::string artist;
    std::string creator;
    std::string version;
};

struct BeatmapGeneral {
    std::string audio_filename;
    int32_t audio_lead_in = 0;
    int32_t mode = 0;
    float stack_leniency = 0.7f;
};

struct ComboColor {
    glm::vec3 color;
};

struct Beatmap {
    BeatmapGeneral general;
    BeatmapMetadata metadata;
    BeatmapDifficulty difficulty;
    std::string background_filename;
    std::vector<TimingPoint> timing_points;
    std::vector<HitObject> hit_objects;
    std::vector<ComboColor> combo_colors;

    float circle_radius() const;
    float approach_time() const;
    float fade_in_time() const;
};

} // namespace osu
