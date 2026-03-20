#pragma once

#include <cstdint>
#include <string>

namespace osu {

// Mirrors osu!'s clock hierarchy: audio source → interpolation → offset correction.
// Drives all gameplay timing so hit objects are judged relative to audio position.
class GameplayClock {
public:
    // Configure before start().
    void set_audio_lead_in(double lead_in_ms);
    void set_first_hit_object_time(double time_ms);
    void set_global_offset(double offset_ms);
    void set_local_offset(double offset_ms);

    // Start audio playback and the clock. audio_file_path is an absolute path.
    void start(const std::string& audio_file_path);
    void stop();

    // Call once per frame with the real elapsed dt (seconds).
    void update(double real_dt_seconds);

    // The authoritative gameplay time in ms. All hit object comparisons use this.
    double current_time() const { return current_time_ms_; }

    bool is_running() const { return running_; }

private:
    // Interpolation: smoothly tracks audio position with exponential drift recovery.
    double interpolate(double interpolated, double audio_pos, double dt_ms) const;

    // Query audio position from the OS audio subsystem (macOS: afplay offset proxy).
    double query_audio_position() const;

    // --- State ---
    bool running_ = false;
    double current_time_ms_ = 0.0;

    // The wall-clock time (ms) when we started the audio.
    double audio_start_wall_ms_ = 0.0;
    // Accumulated wall-clock time since start (ms). Used as audio position proxy.
    double wall_time_since_start_ms_ = 0.0;

    // When the gameplay clock logically starts (often negative for lead-in).
    double gameplay_start_time_ms_ = 0.0;
    // When audio file playback actually begins relative to gameplay clock.
    double audio_offset_in_gameplay_ms_ = 0.0;

    // User offsets (applied to audio position before comparison with hit objects).
    double global_offset_ms_ = 0.0;
    double local_offset_ms_ = 0.0;

    // Lead-in from beatmap [General].
    double audio_lead_in_ms_ = 0.0;
    double first_hit_object_time_ms_ = 0.0;

    // Audio process id for cleanup.
    std::string audio_pid_;
    std::string audio_file_path_;
    bool audio_process_started_ = false;

    // Frame stability: cap per-frame advance to ~16.67ms to avoid jumps.
    static constexpr double MAX_FRAME_MS = 1000.0 / 60.0 * 1.2; // ~20ms

    // Interpolation half-life (ms). Error halves every this many ms.
    static constexpr double DRIFT_HALF_LIFE_MS = 50.0;
    // Max allowable error before snapping to audio position.
    static constexpr double MAX_ALLOWABLE_ERROR_MS = 1000.0 / 60.0 * 2.0; // ~33ms
};

} // namespace osu
