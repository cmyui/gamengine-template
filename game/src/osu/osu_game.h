#pragma once

#include "beatmap.h"
#include "osu_renderer.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace osu {

struct HitResult {
    enum class Judgement { Miss, Meh50, Good100, Great300 };
    Judgement judgement;
    int32_t hit_object_index;
};

struct GameState {
    int32_t score = 0;
    int32_t combo = 0;
    int32_t max_combo = 0;
    int32_t count_300 = 0;
    int32_t count_100 = 0;
    int32_t count_50 = 0;
    int32_t count_miss = 0;

    float accuracy() const;
};

class OsuGame {
public:
    void init(float window_width, float window_height);
    void load_beatmap(Beatmap beatmap);
    void start(float audio_start_time_ms);

    void update(float current_time_ms, float window_width, float window_height);
    void render();
    void render_imgui();

    void on_click(glm::vec2 screen_pos);

    const GameState& get_state() const { return state_; }
    const Beatmap& get_beatmap() const { return beatmap_; }
    bool is_finished() const;

private:
    void process_hit(int32_t object_index, float hit_time_ms,
                     HitResult::Judgement judgement);
    void process_misses(float current_time_ms);
    void render_hit_object(const HitObject& obj, int index,
                           float current_time_ms);
    void render_slider(const HitObject& obj, int index,
                       float current_time_ms);
    void render_spinner(const HitObject& obj, int index,
                        float current_time_ms);
    void render_hud();

    HitResult::Judgement judge_hit(float time_error_ms) const;
    glm::vec4 get_combo_color(int index) const;
    float slider_progress_at(const HitObject& obj, float time_ms) const;
    glm::vec2 slider_position_at(const HitObject& obj, float t) const;

    Beatmap beatmap_;
    OsuRenderer renderer_;
    GameState state_;

    float current_time_ = 0.0f;
    float window_width_ = 0.0f;
    float window_height_ = 0.0f;
    int32_t next_hit_index_ = 0;
    std::vector<bool> object_hit_;
    std::vector<float> object_hit_time_;
    std::vector<HitResult::Judgement> object_judgement_;

    int32_t current_combo_color_ = 0;
    std::vector<int32_t> object_combo_colors_;

    float hit_window_300_ = 0.0f;
    float hit_window_100_ = 0.0f;
    float hit_window_50_ = 0.0f;
};

} // namespace osu
