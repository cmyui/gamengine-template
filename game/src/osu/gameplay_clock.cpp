#include "gameplay_clock.h"

#include <engine/core/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace osu {

// Default lead-in before the first hit object (ms).
// osu! uses max(2000, AudioLeadIn) before the first object.
static constexpr double DEFAULT_LEAD_IN_MS = 2000.0;

void GameplayClock::set_audio_lead_in(double lead_in_ms) {
    audio_lead_in_ms_ = lead_in_ms;
}

void GameplayClock::set_first_hit_object_time(double time_ms) {
    first_hit_object_time_ms_ = time_ms;
}

void GameplayClock::set_global_offset(double offset_ms) {
    global_offset_ms_ = offset_ms;
}

void GameplayClock::set_local_offset(double offset_ms) {
    local_offset_ms_ = offset_ms;
}

void GameplayClock::start(const std::string& audio_file_path) {
    audio_file_path_ = audio_file_path;

    // Determine gameplay start time.
    // osu! starts the clock at: min(0, first_object - max(2000, audio_lead_in))
    double lead_in = std::max(DEFAULT_LEAD_IN_MS, audio_lead_in_ms_);
    gameplay_start_time_ms_ = std::min(0.0, first_hit_object_time_ms_ - lead_in);

    // Audio playback begins when the gameplay clock reaches time 0.
    // Before that, we're in "lead-in" (negative time, no audio).
    audio_offset_in_gameplay_ms_ = 0.0;

    // Start the gameplay clock at the lead-in start time.
    current_time_ms_ = gameplay_start_time_ms_;
    wall_time_since_start_ms_ = 0.0;
    audio_process_started_ = false;
    running_ = true;

    GAME_LOG_INFO("GameplayClock: start_time={:.0f}ms, first_object={:.0f}ms, lead_in={:.0f}ms",
        gameplay_start_time_ms_, first_hit_object_time_ms_, lead_in);
}

void GameplayClock::stop() {
    running_ = false;

    if (!audio_pid_.empty()) {
        std::string cmd = "kill " + audio_pid_ + " 2>/dev/null";
        std::system(cmd.c_str());
        audio_pid_.clear();
    }
    audio_process_started_ = false;
}

void GameplayClock::update(double real_dt_seconds) {
    if (!running_) {
        return;
    }

    double real_dt_ms = real_dt_seconds * 1000.0;

    // Frame stability: clamp large dt spikes (e.g. window drag, debugger).
    if (real_dt_ms > MAX_FRAME_MS) {
        real_dt_ms = MAX_FRAME_MS;
    }

    wall_time_since_start_ms_ += real_dt_ms;

    // Phase 1: Before audio starts (negative gameplay time → time 0).
    // Advance the clock using wall time.
    if (!audio_process_started_) {
        current_time_ms_ += real_dt_ms;

        // Start audio when gameplay clock crosses time 0.
        if (current_time_ms_ >= audio_offset_in_gameplay_ms_) {
            auto abs_path = std::filesystem::absolute(audio_file_path_).string();
            std::string cmd = "afplay \"" + abs_path + "\" &\necho $!";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buf[32];
                if (fgets(buf, sizeof(buf), pipe)) {
                    audio_pid_ = buf;
                    if (!audio_pid_.empty() && audio_pid_.back() == '\n') {
                        audio_pid_.pop_back();
                    }
                }
                pclose(pipe);
            }

            audio_process_started_ = true;
            audio_start_wall_ms_ = wall_time_since_start_ms_;

            // Snap to exactly the audio offset so we don't accumulate lead-in error.
            current_time_ms_ = audio_offset_in_gameplay_ms_;

            GAME_LOG_INFO("GameplayClock: audio started at gameplay time {:.0f}ms (pid {})",
                current_time_ms_, audio_pid_);
        }
        return;
    }

    // Phase 2: Audio is playing. Track audio position.
    //
    // Since afplay doesn't expose its position, we use wall-clock time
    // since the audio was started as a proxy for audio position.
    double audio_elapsed_ms = wall_time_since_start_ms_ - audio_start_wall_ms_;
    double audio_position = audio_offset_in_gameplay_ms_ + audio_elapsed_ms;

    // Apply user offsets: positive offset means "audio is perceived late,
    // so shift gameplay time earlier" → subtract from audio position.
    double total_offset = global_offset_ms_ + local_offset_ms_;
    double adjusted_audio_position = audio_position - total_offset;

    // Interpolate: smoothly converge to adjusted audio position.
    current_time_ms_ = interpolate(current_time_ms_, adjusted_audio_position, real_dt_ms);
}

double GameplayClock::interpolate(double interpolated, double audio_pos, double dt_ms) const {
    // Advance interpolated time by dt first (raw frame advance).
    interpolated += dt_ms;

    double error = std::abs(interpolated - audio_pos);

    // If error is too large, snap directly (seek, pause, lag spike recovery).
    if (error > MAX_ALLOWABLE_ERROR_MS) {
        return audio_pos;
    }

    // Exponential drift recovery toward audio position.
    // Half-life of 50ms: error halves every 50ms of real time.
    //   decay_factor = 0.5 ^ (dt / half_life)
    //   result = target + (current - target) * decay_factor
    double decay = std::pow(0.5, dt_ms / DRIFT_HALF_LIFE_MS);
    return audio_pos + (interpolated - audio_pos) * decay;
}

double GameplayClock::query_audio_position() const {
    // afplay doesn't expose position. Use wall-clock proxy.
    if (!audio_process_started_) {
        return 0.0;
    }
    return wall_time_since_start_ms_ - audio_start_wall_ms_;
}

} // namespace osu
