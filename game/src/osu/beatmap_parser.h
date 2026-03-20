#pragma once

#include "beatmap.h"

#include <optional>
#include <string>

namespace osu {

class BeatmapParser {
public:
    static std::optional<Beatmap> parse(const std::string& filepath);

private:
    static void parse_general(const std::string& line, Beatmap& beatmap);
    static void parse_metadata(const std::string& line, Beatmap& beatmap);
    static void parse_difficulty(const std::string& line, Beatmap& beatmap);
    static void parse_events(const std::string& line, Beatmap& beatmap);
    static void parse_timing_point(const std::string& line, Beatmap& beatmap);
    static void parse_hit_object(const std::string& line, Beatmap& beatmap);
    static void parse_colors(const std::string& line, Beatmap& beatmap);
    static void compute_slider_properties(Beatmap& beatmap);
};

} // namespace osu
