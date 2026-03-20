#include "osu_game.h"

#include <engine/core/log.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace osu {

// Hit explosion linger duration after being hit (ms)
static constexpr float HIT_EXPLOSION_DURATION = 200.0f;
// Fade-out duration for missed objects (ms)
static constexpr float MISS_FADEOUT_DURATION = 200.0f;
// Border thickness relative to circle radius
static constexpr float BORDER_THICKNESS_RATIO = 0.12f;
// Approach circle ring thickness relative to circle radius
static constexpr float APPROACH_RING_THICKNESS_RATIO = 0.08f;
// How long judgement text stays on screen (ms)
static constexpr float JUDGEMENT_DISPLAY_DURATION = 600.0f;
// Spinner center position in osu pixels
static constexpr glm::vec2 SPINNER_CENTER{256.0f, 192.0f};
static constexpr float SPINNER_RADIUS = 150.0f;

// Score values
static constexpr int32_t SCORE_300 = 300;
static constexpr int32_t SCORE_100 = 100;
static constexpr int32_t SCORE_50 = 50;

float GameState::accuracy() const {
    int32_t total = count_300 + count_100 + count_50 + count_miss;
    if (total == 0) return 1.0f;
    float numerator = static_cast<float>(count_300 * 300 + count_100 * 100 +
                                         count_50 * 50);
    float denominator = static_cast<float>(total * 300);
    return numerator / denominator;
}

void OsuGame::init(float window_width, float window_height) {
    window_width_ = window_width;
    window_height_ = window_height;
    renderer_.init(window_width, window_height);
}

void OsuGame::load_beatmap(Beatmap beatmap) {
    beatmap_ = std::move(beatmap);

    int32_t object_count =
        static_cast<int32_t>(beatmap_.hit_objects.size());
    object_hit_.assign(object_count, false);
    object_hit_time_.assign(object_count, 0.0f);
    object_judgement_.assign(object_count, HitResult::Judgement::Miss);
    object_combo_colors_.resize(object_count, 0);

    state_ = GameState{};
    next_hit_index_ = 0;
    current_combo_color_ = 0;

    // Compute hit windows from OD
    float od = beatmap_.difficulty.overall_difficulty;
    hit_window_300_ = 80.0f - 6.0f * od;
    hit_window_100_ = 140.0f - 8.0f * od;
    hit_window_50_ = 200.0f - 10.0f * od;

    // Compute combo colors per object
    // Default combo color if beatmap has none
    if (beatmap_.combo_colors.empty()) {
        beatmap_.combo_colors.push_back(
            ComboColor{glm::vec3(1.0f, 0.6f, 0.2f)});
        beatmap_.combo_colors.push_back(
            ComboColor{glm::vec3(0.2f, 0.8f, 1.0f)});
        beatmap_.combo_colors.push_back(
            ComboColor{glm::vec3(0.4f, 1.0f, 0.4f)});
        beatmap_.combo_colors.push_back(
            ComboColor{glm::vec3(1.0f, 0.4f, 0.7f)});
    }

    int32_t color_index = 0;
    int32_t num_colors = static_cast<int32_t>(beatmap_.combo_colors.size());

    for (int32_t i = 0; i < object_count; ++i) {
        const auto& obj = beatmap_.hit_objects[i];
        if (obj.new_combo) {
            int32_t skip = 1 + obj.combo_color_offset;
            color_index = (color_index + skip) % num_colors;
        }
        object_combo_colors_[i] = color_index;
    }

    GAME_LOG_INFO("Loaded beatmap: {} - {} [{}] ({} objects, OD={:.1f}, AR={:.1f})",
                  beatmap_.metadata.artist, beatmap_.metadata.title,
                  beatmap_.metadata.version, object_count,
                  beatmap_.difficulty.overall_difficulty,
                  beatmap_.difficulty.approach_rate);
}

void OsuGame::start(float audio_start_time_ms) {
    current_time_ = audio_start_time_ms;
    next_hit_index_ = 0;
}

void OsuGame::update(float current_time_ms, float window_width,
                     float window_height) {
    current_time_ = current_time_ms;
    window_width_ = window_width;
    window_height_ = window_height;
    process_misses(current_time_ms);
}

void OsuGame::render() {
    renderer_.begin(window_width_, window_height_);

    float approach_time = beatmap_.approach_time();

    // Collect visible object indices
    std::vector<int> visible_indices;
    for (int i = 0; i < static_cast<int>(beatmap_.hit_objects.size()); ++i) {
        const auto& obj = beatmap_.hit_objects[i];

        // Determine the end of this object's visibility window
        float obj_end_time = static_cast<float>(obj.time);
        if (obj.type == HitObjectType::Slider) {
            obj_end_time += obj.slider_duration;
        } else if (obj.type == HitObjectType::Spinner) {
            obj_end_time = static_cast<float>(obj.end_time);
        }

        float disappear_time =
            obj_end_time + HIT_EXPLOSION_DURATION + MISS_FADEOUT_DURATION;
        float appear_time =
            static_cast<float>(obj.time) - approach_time;

        if (current_time_ >= appear_time && current_time_ <= disappear_time) {
            visible_indices.push_back(i);
        }
    }

    // Render in reverse order so earlier objects draw on top
    for (int i = static_cast<int>(visible_indices.size()) - 1; i >= 0; --i) {
        int idx = visible_indices[i];
        const auto& obj = beatmap_.hit_objects[idx];

        switch (obj.type) {
        case HitObjectType::Slider:
            render_slider(obj, idx, current_time_);
            break;
        case HitObjectType::Spinner:
            render_spinner(obj, idx, current_time_);
            break;
        case HitObjectType::Circle:
            render_hit_object(obj, idx, current_time_);
            break;
        }
    }

    renderer_.end();
}

void OsuGame::on_click(glm::vec2 screen_pos) {
    glm::vec2 osu_pos = renderer_.screen_to_osu(screen_pos);
    float circle_radius = beatmap_.circle_radius();

    // Search for the earliest unhit object within click range and timing window
    for (int32_t i = next_hit_index_;
         i < static_cast<int32_t>(beatmap_.hit_objects.size()); ++i) {
        if (object_hit_[i]) continue;

        const auto& obj = beatmap_.hit_objects[i];
        float time_error = std::abs(current_time_ - static_cast<float>(obj.time));

        // Outside the largest hit window -- stop scanning forward since
        // objects are sorted by time
        if (static_cast<float>(obj.time) - current_time_ > hit_window_50_) {
            break;
        }

        if (time_error > hit_window_50_) continue;

        // Check spatial distance
        glm::vec2 click_target = obj.position;
        if (obj.type == HitObjectType::Spinner) {
            click_target = SPINNER_CENTER;
        }

        float dist = glm::distance(osu_pos, click_target);
        if (dist > circle_radius) continue;

        HitResult::Judgement judgement = judge_hit(time_error);
        process_hit(i, current_time_, judgement);
        return;
    }
}

bool OsuGame::is_finished() const {
    if (beatmap_.hit_objects.empty()) return true;
    const auto& last = beatmap_.hit_objects.back();
    float last_end = static_cast<float>(last.time) + hit_window_50_ + 1000.0f;
    return current_time_ > last_end;
}

void OsuGame::process_hit(int32_t object_index, float hit_time_ms,
                           HitResult::Judgement judgement) {
    object_hit_[object_index] = true;
    object_hit_time_[object_index] = hit_time_ms;
    object_judgement_[object_index] = judgement;

    int32_t score_value = 0;
    switch (judgement) {
    case HitResult::Judgement::Great300:
        score_value = SCORE_300;
        state_.count_300++;
        state_.combo++;
        break;
    case HitResult::Judgement::Good100:
        score_value = SCORE_100;
        state_.count_100++;
        state_.combo++;
        break;
    case HitResult::Judgement::Meh50:
        score_value = SCORE_50;
        state_.count_50++;
        state_.combo++;
        break;
    case HitResult::Judgement::Miss:
        state_.count_miss++;
        state_.combo = 0;
        break;
    }

    // Score formula: base * (1 + combo multiplier)
    state_.score += score_value * (1 + state_.combo);
    state_.max_combo = std::max(state_.max_combo, state_.combo);

    // Advance next_hit_index past any already-judged objects
    while (next_hit_index_ <
               static_cast<int32_t>(beatmap_.hit_objects.size()) &&
           object_hit_[next_hit_index_]) {
        next_hit_index_++;
    }
}

void OsuGame::process_misses(float current_time_ms) {
    while (next_hit_index_ <
           static_cast<int32_t>(beatmap_.hit_objects.size())) {
        if (object_hit_[next_hit_index_]) {
            next_hit_index_++;
            continue;
        }

        const auto& obj = beatmap_.hit_objects[next_hit_index_];
        float deadline = static_cast<float>(obj.time) + hit_window_50_;

        if (current_time_ms <= deadline) break;

        // This object was missed
        object_hit_[next_hit_index_] = true;
        object_hit_time_[next_hit_index_] = current_time_ms;
        object_judgement_[next_hit_index_] = HitResult::Judgement::Miss;
        state_.count_miss++;
        state_.combo = 0;
        next_hit_index_++;
    }
}

void OsuGame::render_hit_object(const HitObject& obj, int index,
                                 float current_time_ms) {
    float approach_time = beatmap_.approach_time();
    float fade_in_time = beatmap_.fade_in_time();
    float circle_radius = beatmap_.circle_radius();
    float time_until_hit = static_cast<float>(obj.time) - current_time_ms;

    glm::vec4 combo_color = get_combo_color(index);

    // Object was hit: show brief explosion
    if (object_hit_[index]) {
        float since_hit = current_time_ms - object_hit_time_[index];
        if (since_hit > HIT_EXPLOSION_DURATION) return;
        if (object_judgement_[index] == HitResult::Judgement::Miss) {
            // Missed: fade out and shrink
            float t = since_hit / MISS_FADEOUT_DURATION;
            float alpha = std::max(0.0f, 1.0f - t);
            renderer_.draw_filled_circle(obj.position, circle_radius * 0.9f,
                                         combo_color, alpha * 0.5f);
            return;
        }
        // Hit: expand and fade
        float t = since_hit / HIT_EXPLOSION_DURATION;
        float alpha = std::max(0.0f, 1.0f - t);
        float expand = 1.0f + t * 0.5f;
        renderer_.draw_filled_circle(obj.position, circle_radius * expand,
                                     combo_color, alpha * 0.6f);
        return;
    }

    // Not yet visible
    if (time_until_hit > approach_time) return;

    // Approaching: compute visual properties
    float progress = 1.0f - time_until_hit / approach_time;
    float fade_ratio = fade_in_time / approach_time;
    float alpha = std::clamp(progress / fade_ratio, 0.0f, 1.0f);

    // After hit time (not yet judged as miss): start fading out
    if (time_until_hit < 0.0f) {
        float overtime = -time_until_hit;
        alpha = std::max(0.0f, 1.0f - overtime / MISS_FADEOUT_DURATION);
    }

    // Filled circle body
    renderer_.draw_filled_circle(obj.position, circle_radius, combo_color,
                                 alpha * 0.85f);

    // White border ring
    float border_thickness = circle_radius * BORDER_THICKNESS_RATIO;
    renderer_.draw_circle_ring(obj.position, circle_radius, border_thickness,
                               glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), alpha);

    // Approach circle (only before hit time)
    if (time_until_hit > 0.0f) {
        float approach_scale = 1.0f + 3.0f * (1.0f - progress);
        float approach_radius = circle_radius * approach_scale;
        float approach_thickness =
            approach_radius * APPROACH_RING_THICKNESS_RATIO;
        renderer_.draw_circle_ring(obj.position, approach_radius,
                                   approach_thickness, combo_color, alpha);
    }
}

void OsuGame::render_slider(const HitObject& obj, int index,
                             float current_time_ms) {
    float approach_time = beatmap_.approach_time();
    float fade_in_time = beatmap_.fade_in_time();
    float circle_radius = beatmap_.circle_radius();
    float time_until_hit = static_cast<float>(obj.time) - current_time_ms;

    glm::vec4 combo_color = get_combo_color(index);

    float total_duration = obj.slider_duration;
    float slider_end_time = static_cast<float>(obj.time) + total_duration;

    // Hit explosion after slider completes
    if (object_hit_[index] &&
        current_time_ms > object_hit_time_[index]) {
        float since_hit = current_time_ms - object_hit_time_[index];
        if (since_hit > HIT_EXPLOSION_DURATION) return;
        float t = since_hit / HIT_EXPLOSION_DURATION;
        float alpha = std::max(0.0f, 1.0f - t);
        renderer_.draw_filled_circle(obj.position, circle_radius * (1.0f + t * 0.5f),
                                     combo_color, alpha * 0.6f);
        return;
    }

    // Not yet visible
    if (time_until_hit > approach_time) return;

    float progress = 1.0f - time_until_hit / approach_time;
    float fade_ratio = fade_in_time / approach_time;
    float alpha = std::clamp(progress / fade_ratio, 0.0f, 1.0f);

    // After slider end: fade out
    if (current_time_ms > slider_end_time) {
        float overtime = current_time_ms - slider_end_time;
        alpha = std::max(0.0f, 1.0f - overtime / MISS_FADEOUT_DURATION);
    }

    // Draw slider body
    if (!obj.slider_path.calculated_path.empty()) {
        glm::vec4 body_color = combo_color * glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
        body_color.a = 0.7f;
        renderer_.draw_slider_body(obj.slider_path.calculated_path,
                                   circle_radius, body_color, alpha);
    }

    // Draw start circle (same as regular hit circle)
    renderer_.draw_filled_circle(obj.position, circle_radius, combo_color,
                                 alpha * 0.85f);
    float border_thickness = circle_radius * BORDER_THICKNESS_RATIO;
    renderer_.draw_circle_ring(obj.position, circle_radius, border_thickness,
                               glm::vec4(1.0f), alpha);

    // Draw end circle
    if (!obj.slider_path.calculated_path.empty()) {
        glm::vec2 end_pos = obj.slider_path.calculated_path.back();
        renderer_.draw_filled_circle(end_pos, circle_radius, combo_color,
                                     alpha * 0.6f);
        renderer_.draw_circle_ring(end_pos, circle_radius, border_thickness,
                                   glm::vec4(1.0f), alpha * 0.6f);
    }

    // Approach circle (before hit time)
    if (time_until_hit > 0.0f) {
        float approach_scale = 1.0f + 3.0f * (1.0f - progress);
        float approach_radius = circle_radius * approach_scale;
        float approach_thickness =
            approach_radius * APPROACH_RING_THICKNESS_RATIO;
        renderer_.draw_circle_ring(obj.position, approach_radius,
                                   approach_thickness, combo_color, alpha);
    }

    // Slider ball (active during slider)
    bool slider_active = current_time_ms >= static_cast<float>(obj.time) &&
                         current_time_ms <= slider_end_time;
    if (slider_active && !obj.slider_path.calculated_path.empty()) {
        float t = slider_progress_at(obj, current_time_ms);
        glm::vec2 ball_pos = slider_position_at(obj, t);
        renderer_.draw_filled_circle(ball_pos, circle_radius * 1.1f,
                                     glm::vec4(1.0f), 0.9f);
        renderer_.draw_filled_circle(ball_pos, circle_radius * 0.9f,
                                     combo_color, 0.95f);
    }
}

void OsuGame::render_spinner(const HitObject& obj, int index,
                              float current_time_ms) {
    float time_until_hit = static_cast<float>(obj.time) - current_time_ms;
    float approach_time = beatmap_.approach_time();
    glm::vec4 combo_color = get_combo_color(index);

    if (time_until_hit > approach_time) return;

    float end_time = static_cast<float>(obj.end_time);
    float alpha = 1.0f;

    if (time_until_hit > 0.0f) {
        float progress = 1.0f - time_until_hit / approach_time;
        alpha = std::clamp(progress * 2.0f, 0.0f, 1.0f);
    } else if (current_time_ms > end_time) {
        float overtime = current_time_ms - end_time;
        alpha = std::max(0.0f, 1.0f - overtime / MISS_FADEOUT_DURATION);
    }

    // Outer ring
    renderer_.draw_circle_ring(SPINNER_CENTER, SPINNER_RADIUS, 4.0f,
                               glm::vec4(1.0f), alpha * 0.7f);

    // Inner ring that shrinks as spinner progresses
    bool active = current_time_ms >= static_cast<float>(obj.time) &&
                  current_time_ms <= end_time;
    if (active) {
        float spinner_duration = end_time - static_cast<float>(obj.time);
        float elapsed = current_time_ms - static_cast<float>(obj.time);
        float t = std::clamp(elapsed / spinner_duration, 0.0f, 1.0f);
        float inner_radius = SPINNER_RADIUS * (1.0f - t * 0.7f);
        renderer_.draw_circle_ring(SPINNER_CENTER, inner_radius, 3.0f,
                                   combo_color, alpha);
    }

    // Center dot
    renderer_.draw_filled_circle(SPINNER_CENTER, 8.0f,
                                 glm::vec4(1.0f), alpha);
}

void OsuGame::render_imgui() {
    render_hud();
    render_judgements();
}

void OsuGame::render_judgements() {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    for (int i = 0; i < static_cast<int>(beatmap_.hit_objects.size()); ++i) {
        if (!object_hit_[i]) continue;

        float since_hit = current_time_ - object_hit_time_[i];
        if (since_hit < 0.0f || since_hit > JUDGEMENT_DISPLAY_DURATION) continue;

        float t = since_hit / JUDGEMENT_DISPLAY_DURATION;
        float alpha = std::max(0.0f, 1.0f - t);
        float rise = t * 30.0f; // float upward

        const auto& obj = beatmap_.hit_objects[i];
        glm::vec2 screen_pos = renderer_.osu_to_screen(obj.position);
        screen_pos.y -= rise;

        const char* text = nullptr;
        ImU32 color = 0;

        switch (object_judgement_[i]) {
        case HitResult::Judgement::Great300:
            text = "300";
            color = IM_COL32(100, 200, 255, static_cast<int>(alpha * 255));
            break;
        case HitResult::Judgement::Good100:
            text = "100";
            color = IM_COL32(100, 255, 100, static_cast<int>(alpha * 255));
            break;
        case HitResult::Judgement::Meh50:
            text = "50";
            color = IM_COL32(255, 200, 50, static_cast<int>(alpha * 255));
            break;
        case HitResult::Judgement::Miss:
            text = "MISS";
            color = IM_COL32(255, 50, 50, static_cast<int>(alpha * 255));
            break;
        }

        if (text) {
            ImVec2 text_size = ImGui::CalcTextSize(text);
            ImVec2 pos(screen_pos.x - text_size.x * 0.5f,
                       screen_pos.y - text_size.y * 0.5f);
            // Scale text by 1.5x
            float font_size = ImGui::GetFontSize() * 1.5f;
            draw_list->AddText(ImGui::GetFont(), font_size, pos, color, text);
        }
    }
}

void OsuGame::render_hud() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
    ImGui::SetNextWindowSize(ImVec2(350.0f, 130.0f));
    ImGui::Begin("##OsuHUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("Score: %d", state_.score);
    ImGui::Text("Combo: %dx", state_.combo);
    ImGui::Text("Accuracy: %.2f%%", state_.accuracy() * 100.0f);
    ImGui::Text("300: %d  100: %d  50: %d  Miss: %d", state_.count_300,
                state_.count_100, state_.count_50, state_.count_miss);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("%s - %s [%s]", beatmap_.metadata.artist.c_str(),
                beatmap_.metadata.title.c_str(),
                beatmap_.metadata.version.c_str());
    ImGui::PopStyleColor();

    ImGui::End();
}

HitResult::Judgement OsuGame::judge_hit(float time_error_ms) const {
    if (time_error_ms <= hit_window_300_)
        return HitResult::Judgement::Great300;
    if (time_error_ms <= hit_window_100_)
        return HitResult::Judgement::Good100;
    if (time_error_ms <= hit_window_50_)
        return HitResult::Judgement::Meh50;
    return HitResult::Judgement::Miss;
}

glm::vec4 OsuGame::get_combo_color(int index) const {
    int32_t color_index = object_combo_colors_[index];
    const auto& cc = beatmap_.combo_colors[color_index];
    return glm::vec4(cc.color, 1.0f);
}

float OsuGame::slider_progress_at(const HitObject& obj,
                                   float time_ms) const {
    if (obj.slider_duration <= 0.0f) return 0.0f;
    float total_duration = obj.slider_duration;
    float elapsed = time_ms - static_cast<float>(obj.time);
    elapsed = std::clamp(elapsed, 0.0f, total_duration);

    // Which repeat are we in?
    float single_pass = obj.slider_duration;
    int repeat_index = static_cast<int>(elapsed / single_pass);
    repeat_index = std::min(repeat_index, obj.repeat_count - 1);

    float within_pass = elapsed - static_cast<float>(repeat_index) * single_pass;
    float t = std::clamp(within_pass / single_pass, 0.0f, 1.0f);

    // Odd repeats go backward
    if (repeat_index % 2 == 1) {
        t = 1.0f - t;
    }

    return t;
}

glm::vec2 OsuGame::slider_position_at(const HitObject& obj, float t) const {
    const auto& path = obj.slider_path.calculated_path;
    if (path.empty()) return obj.position;
    if (path.size() == 1) return path[0];

    // Compute cumulative distances
    float total_length = 0.0f;
    for (size_t i = 1; i < path.size(); ++i) {
        total_length += glm::distance(path[i - 1], path[i]);
    }

    float target_dist = t * total_length;
    float accumulated = 0.0f;

    for (size_t i = 1; i < path.size(); ++i) {
        float seg_len = glm::distance(path[i - 1], path[i]);
        if (accumulated + seg_len >= target_dist) {
            float seg_t =
                (seg_len > 0.001f)
                    ? (target_dist - accumulated) / seg_len
                    : 0.0f;
            return glm::mix(path[i - 1], path[i], seg_t);
        }
        accumulated += seg_len;
    }

    return path.back();
}

} // namespace osu
