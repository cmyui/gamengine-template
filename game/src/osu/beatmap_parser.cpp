#include "beatmap_parser.h"

#include <engine/core/log.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace osu {

namespace {

std::string trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream stream(str);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::pair<std::string, std::string> split_key_value(const std::string& line) {
    auto colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        return {"", ""};
    }
    std::string key = trim(line.substr(0, colon_pos));
    std::string value = trim(line.substr(colon_pos + 1));
    return {key, value};
}

// ---- Slider path computation helpers ----

float path_length(const std::vector<glm::vec2>& points) {
    float length = 0.0f;
    for (size_t i = 1; i < points.size(); ++i) {
        length += glm::distance(points[i - 1], points[i]);
    }
    return length;
}

bool is_flat_enough(const std::vector<glm::vec2>& points, float tolerance) {
    if (points.size() <= 2) {
        return true;
    }
    float direct = glm::distance(points.front(), points.back());
    float along = path_length(points);
    return (along - direct) <= tolerance;
}

glm::vec2 bezier_at(const std::vector<glm::vec2>& points, float t) {
    std::vector<glm::vec2> tmp = points;
    int n = static_cast<int>(tmp.size());
    for (int r = 1; r < n; ++r) {
        for (int i = 0; i < n - r; ++i) {
            tmp[i] = tmp[i] * (1.0f - t) + tmp[i + 1] * t;
        }
    }
    return tmp[0];
}

void subdivide_bezier(const std::vector<glm::vec2>& points,
                      std::vector<glm::vec2>& output, float tolerance) {
    if (is_flat_enough(points, tolerance)) {
        if (output.empty() ||
            glm::distance(output.back(), points.front()) > 0.001f) {
            output.push_back(points.front());
        }
        output.push_back(points.back());
        return;
    }

    size_t n = points.size();
    std::vector<glm::vec2> left;
    std::vector<glm::vec2> right;
    left.reserve(n);
    right.reserve(n);

    std::vector<glm::vec2> tmp = points;
    left.push_back(tmp[0]);

    for (size_t r = 1; r < n; ++r) {
        for (size_t i = 0; i < n - r; ++i) {
            tmp[i] = (tmp[i] + tmp[i + 1]) * 0.5f;
        }
        left.push_back(tmp[0]);
        right.push_back(tmp[n - r - 1]);
    }

    std::reverse(right.begin(), right.end());

    subdivide_bezier(left, output, tolerance);
    subdivide_bezier(right, output, tolerance);
}

glm::vec2 circumscribed_center(glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    float ax = a.x, ay = a.y;
    float bx = b.x, by = b.y;
    float cx = c.x, cy = c.y;

    float d = 2.0f * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    float ux =
        ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) +
         (cx * cx + cy * cy) * (ay - by)) /
        d;
    float uy =
        ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) +
         (cx * cx + cy * cy) * (bx - ax)) /
        d;

    return {ux, uy};
}

bool are_collinear(glm::vec2 a, glm::vec2 b, glm::vec2 c, float epsilon) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return std::abs(cross) < epsilon;
}

std::vector<glm::vec2>
compute_perfect_curve(const std::vector<glm::vec2>& points) {
    if (points.size() != 3 || are_collinear(points[0], points[1], points[2], 1.0f)) {
        // Fall back to bezier
        std::vector<glm::vec2> result;
        subdivide_bezier(points, result, 0.25f);
        return result;
    }

    glm::vec2 center = circumscribed_center(points[0], points[1], points[2]);
    float radius = glm::distance(center, points[0]);

    float angle_start = std::atan2(points[0].y - center.y, points[0].x - center.x);
    float angle_mid = std::atan2(points[1].y - center.y, points[1].x - center.x);
    float angle_end = std::atan2(points[2].y - center.y, points[2].x - center.x);

    // Determine winding direction
    float cross = (points[1].x - points[0].x) * (points[2].y - points[0].y) -
                  (points[1].y - points[0].y) * (points[2].x - points[0].x);
    bool clockwise = cross > 0.0f;

    float total_angle = angle_end - angle_start;
    if (clockwise && total_angle > 0.0f) {
        total_angle -= 2.0f * static_cast<float>(M_PI);
    } else if (!clockwise && total_angle < 0.0f) {
        total_angle += 2.0f * static_cast<float>(M_PI);
    }

    // Verify mid-point is between start and end
    float mid_rel = angle_mid - angle_start;
    if (clockwise && mid_rel > 0.0f) {
        mid_rel -= 2.0f * static_cast<float>(M_PI);
    } else if (!clockwise && mid_rel < 0.0f) {
        mid_rel += 2.0f * static_cast<float>(M_PI);
    }

    float arc_length = std::abs(total_angle) * radius;
    int num_points = std::max(2, static_cast<int>(arc_length / 4.0f));

    std::vector<glm::vec2> result;
    result.reserve(num_points);
    for (int i = 0; i < num_points; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(num_points - 1);
        float angle = angle_start + total_angle * t;
        result.push_back({center.x + radius * std::cos(angle),
                          center.y + radius * std::sin(angle)});
    }

    return result;
}

std::vector<glm::vec2>
compute_catmull(const std::vector<glm::vec2>& points) {
    static constexpr int POINTS_PER_SEGMENT = 50;

    std::vector<glm::vec2> result;
    if (points.size() < 2) {
        return points;
    }

    for (size_t i = 0; i + 1 < points.size(); ++i) {
        glm::vec2 p0 = (i > 0) ? points[i - 1] : points[i];
        glm::vec2 p1 = points[i];
        glm::vec2 p2 = points[i + 1];
        glm::vec2 p3 = (i + 2 < points.size()) ? points[i + 2] : points[i + 1];

        for (int j = 0; j < POINTS_PER_SEGMENT; ++j) {
            float t = static_cast<float>(j) / static_cast<float>(POINTS_PER_SEGMENT);
            float t2 = t * t;
            float t3 = t2 * t;

            glm::vec2 point =
                0.5f *
                ((2.0f * p1) + (-p0 + p2) * t +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);

            if (result.empty() ||
                glm::distance(result.back(), point) > 0.001f) {
                result.push_back(point);
            }
        }
    }

    // Add the final point
    if (!points.empty()) {
        result.push_back(points.back());
    }

    return result;
}

std::vector<glm::vec2> compute_linear(const std::vector<glm::vec2>& points) {
    return points;
}

std::vector<glm::vec2>
compute_slider_path_points(PathType type,
                           const std::vector<glm::vec2>& points) {
    switch (type) {
    case PathType::Linear:
        return compute_linear(points);
    case PathType::PerfectCurve:
        return compute_perfect_curve(points);
    case PathType::Bezier: {
        // In osu, repeated control points split the bezier into segments
        std::vector<glm::vec2> result;
        std::vector<glm::vec2> segment;

        for (size_t i = 0; i < points.size(); ++i) {
            segment.push_back(points[i]);
            bool is_segment_end =
                (i + 1 < points.size() &&
                 glm::distance(points[i], points[i + 1]) < 0.001f);
            bool is_last = (i + 1 == points.size());

            if ((is_segment_end || is_last) && segment.size() >= 2) {
                subdivide_bezier(segment, result, 0.25f);
                segment.clear();
                // The duplicate point will be picked up as start of next
                // segment
            }
        }

        // Handle remaining segment
        if (segment.size() >= 2) {
            subdivide_bezier(segment, result, 0.25f);
        }

        return result;
    }
    case PathType::Catmull:
        return compute_catmull(points);
    }
    return points;
}

std::vector<glm::vec2> resample_path(const std::vector<glm::vec2>& points,
                                     float target_length) {
    if (points.size() < 2) {
        return points;
    }

    std::vector<glm::vec2> result;
    result.push_back(points[0]);

    // Compute cumulative distances
    std::vector<float> cumulative;
    cumulative.reserve(points.size());
    cumulative.push_back(0.0f);
    for (size_t i = 1; i < points.size(); ++i) {
        cumulative.push_back(cumulative.back() +
                             glm::distance(points[i - 1], points[i]));
    }

    float total = cumulative.back();
    float actual_length = std::min(total, target_length);

    // Resample at 1 osu-pixel intervals
    float step = 1.0f;
    float dist = step;

    size_t segment_idx = 0;
    while (dist < actual_length) {
        while (segment_idx + 1 < cumulative.size() &&
               cumulative[segment_idx + 1] < dist) {
            ++segment_idx;
        }

        if (segment_idx + 1 >= cumulative.size()) {
            break;
        }

        float seg_start = cumulative[segment_idx];
        float seg_end = cumulative[segment_idx + 1];
        float seg_len = seg_end - seg_start;

        float t = (seg_len > 0.001f) ? (dist - seg_start) / seg_len : 0.0f;
        glm::vec2 p = points[segment_idx] * (1.0f - t) +
                      points[segment_idx + 1] * t;
        result.push_back(p);

        dist += step;
    }

    // Add the endpoint at exactly the target distance
    if (actual_length <= total) {
        size_t idx = 0;
        while (idx + 1 < cumulative.size() &&
               cumulative[idx + 1] < actual_length) {
            ++idx;
        }
        if (idx + 1 < cumulative.size()) {
            float seg_start = cumulative[idx];
            float seg_end = cumulative[idx + 1];
            float seg_len = seg_end - seg_start;
            float t =
                (seg_len > 0.001f) ? (actual_length - seg_start) / seg_len : 0.0f;
            glm::vec2 end_point =
                points[idx] * (1.0f - t) + points[idx + 1] * t;
            result.push_back(end_point);
        }
    }

    return result;
}

} // namespace

std::optional<Beatmap> BeatmapParser::parse(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        GAME_LOG_ERROR("Failed to open beatmap file: {}", filepath);
        return std::nullopt;
    }

    Beatmap beatmap;
    std::string current_section;
    std::string line;

    // Verify the osu file format header
    if (std::getline(file, line)) {
        line = trim(line);
        if (line.find("osu file format") == std::string::npos) {
            GAME_LOG_ERROR("Not a valid osu beatmap file: {}", filepath);
            return std::nullopt;
        }
    }

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '/') {
            continue;
        }

        // Section header
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        if (current_section == "General") {
            parse_general(line, beatmap);
        } else if (current_section == "Metadata") {
            parse_metadata(line, beatmap);
        } else if (current_section == "Difficulty") {
            parse_difficulty(line, beatmap);
        } else if (current_section == "Events") {
            parse_events(line, beatmap);
        } else if (current_section == "TimingPoints") {
            parse_timing_point(line, beatmap);
        } else if (current_section == "HitObjects") {
            parse_hit_object(line, beatmap);
        } else if (current_section == "Colours") {
            parse_colors(line, beatmap);
        }
    }

    compute_slider_properties(beatmap);

    GAME_LOG_INFO("Parsed beatmap: {} - {} [{}] ({} objects, {} timing points)",
                  beatmap.metadata.artist, beatmap.metadata.title,
                  beatmap.metadata.version, beatmap.hit_objects.size(),
                  beatmap.timing_points.size());

    return beatmap;
}

void BeatmapParser::parse_general(const std::string& line, Beatmap& beatmap) {
    auto [key, value] = split_key_value(line);
    if (key == "AudioFilename") {
        beatmap.general.audio_filename = value;
    } else if (key == "AudioLeadIn") {
        beatmap.general.audio_lead_in = std::stoi(value);
    } else if (key == "Mode") {
        beatmap.general.mode = std::stoi(value);
    } else if (key == "StackLeniency") {
        beatmap.general.stack_leniency = std::stof(value);
    }
}

void BeatmapParser::parse_metadata(const std::string& line, Beatmap& beatmap) {
    auto [key, value] = split_key_value(line);
    if (key == "Title") {
        beatmap.metadata.title = value;
    } else if (key == "Artist") {
        beatmap.metadata.artist = value;
    } else if (key == "Creator") {
        beatmap.metadata.creator = value;
    } else if (key == "Version") {
        beatmap.metadata.version = value;
    }
}

void BeatmapParser::parse_difficulty(const std::string& line,
                                     Beatmap& beatmap) {
    auto [key, value] = split_key_value(line);
    if (key == "HPDrainRate") {
        beatmap.difficulty.hp_drain = std::stof(value);
    } else if (key == "CircleSize") {
        beatmap.difficulty.circle_size = std::stof(value);
    } else if (key == "OverallDifficulty") {
        beatmap.difficulty.overall_difficulty = std::stof(value);
    } else if (key == "ApproachRate") {
        beatmap.difficulty.approach_rate = std::stof(value);
    } else if (key == "SliderMultiplier") {
        beatmap.difficulty.slider_multiplier = std::stof(value);
    } else if (key == "SliderTickRate") {
        beatmap.difficulty.slider_tick_rate = std::stof(value);
    }
}

void BeatmapParser::parse_events(const std::string& line, Beatmap& beatmap) {
    // Background image: 0,0,"filename.jpg",0,0
    auto tokens = split(line, ',');
    if (tokens.size() >= 3 && trim(tokens[0]) == "0" &&
        trim(tokens[1]) == "0") {
        std::string filename = trim(tokens[2]);
        // Strip surrounding quotes
        if (filename.size() >= 2 && filename.front() == '"' &&
            filename.back() == '"') {
            filename = filename.substr(1, filename.size() - 2);
        }
        beatmap.background_filename = filename;
    }
}

void BeatmapParser::parse_timing_point(const std::string& line,
                                       Beatmap& beatmap) {
    auto tokens = split(line, ',');
    if (tokens.size() < 2) {
        return;
    }

    TimingPoint tp;
    tp.time = std::stod(trim(tokens[0]));
    tp.beat_length = std::stod(trim(tokens[1]));

    if (tokens.size() > 2) {
        tp.meter = std::stoi(trim(tokens[2]));
    }

    // Field index 6 is the uninherited flag (1 = timing change, 0 = inherited)
    if (tokens.size() > 6) {
        tp.is_timing_change = (std::stoi(trim(tokens[6])) == 1);
    } else {
        // Old format: positive beat_length = uninherited
        tp.is_timing_change = (tp.beat_length > 0.0);
    }

    beatmap.timing_points.push_back(tp);
}

void BeatmapParser::parse_hit_object(const std::string& line,
                                     Beatmap& beatmap) {
    auto tokens = split(line, ',');
    if (tokens.size() < 4) {
        return;
    }

    HitObject obj;
    obj.position.x = std::stof(trim(tokens[0]));
    obj.position.y = std::stof(trim(tokens[1]));
    obj.time = std::stoi(trim(tokens[2]));

    int32_t type_flags = std::stoi(trim(tokens[3]));

    obj.new_combo = (type_flags >> 2) & 1;
    obj.combo_color_offset = (type_flags >> 4) & 7;

    if (type_flags & (1 << 0)) {
        obj.type = HitObjectType::Circle;
    } else if (type_flags & (1 << 1)) {
        obj.type = HitObjectType::Slider;

        if (tokens.size() > 5) {
            // Parse path: "B|x:y|x:y" or "L|x:y" etc.
            std::string path_str = trim(tokens[5]);
            auto path_parts = split(path_str, '|');

            if (!path_parts.empty()) {
                char type_char = path_parts[0][0];
                switch (type_char) {
                case 'L':
                    obj.slider_path.type = PathType::Linear;
                    break;
                case 'P':
                    obj.slider_path.type = PathType::PerfectCurve;
                    break;
                case 'B':
                    obj.slider_path.type = PathType::Bezier;
                    break;
                case 'C':
                    obj.slider_path.type = PathType::Catmull;
                    break;
                default:
                    obj.slider_path.type = PathType::Linear;
                    break;
                }

                // Start position is the first control point
                obj.slider_path.control_points.push_back(
                    PathControlPoint{obj.position});

                for (size_t i = 1; i < path_parts.size(); ++i) {
                    auto coords = split(path_parts[i], ':');
                    if (coords.size() >= 2) {
                        glm::vec2 pos(std::stof(trim(coords[0])),
                                      std::stof(trim(coords[1])));
                        obj.slider_path.control_points.push_back(
                            PathControlPoint{pos});
                    }
                }
            }
        }

        if (tokens.size() > 6) {
            obj.repeat_count = std::stoi(trim(tokens[6]));
        }

        if (tokens.size() > 7) {
            obj.slider_path.expected_distance = std::stof(trim(tokens[7]));
        }
    } else if (type_flags & (1 << 3)) {
        obj.type = HitObjectType::Spinner;
        if (tokens.size() > 5) {
            obj.end_time = std::stoi(trim(tokens[5]));
        }
    } else {
        // Unknown type, skip
        return;
    }

    beatmap.hit_objects.push_back(std::move(obj));
}

void BeatmapParser::parse_colors(const std::string& line, Beatmap& beatmap) {
    auto [key, value] = split_key_value(line);
    // Keys look like "Combo1", "Combo2", etc.
    if (key.find("Combo") != 0) {
        return;
    }

    auto parts = split(value, ',');
    if (parts.size() < 3) {
        return;
    }

    ComboColor color;
    color.color.r = std::stof(trim(parts[0])) / 255.0f;
    color.color.g = std::stof(trim(parts[1])) / 255.0f;
    color.color.b = std::stof(trim(parts[2])) / 255.0f;
    beatmap.combo_colors.push_back(color);
}

void BeatmapParser::compute_slider_properties(Beatmap& beatmap) {
    for (auto& obj : beatmap.hit_objects) {
        if (obj.type != HitObjectType::Slider) {
            continue;
        }

        // Build the list of raw control point positions
        std::vector<glm::vec2> points;
        points.reserve(obj.slider_path.control_points.size());
        for (const auto& cp : obj.slider_path.control_points) {
            points.push_back(cp.position);
        }

        if (points.size() < 2) {
            continue;
        }

        // Compute the path polyline
        auto raw_path =
            compute_slider_path_points(obj.slider_path.type, points);

        // Resample and truncate to expected distance
        if (obj.slider_path.expected_distance > 0.0f && !raw_path.empty()) {
            obj.slider_path.calculated_path =
                resample_path(raw_path, obj.slider_path.expected_distance);
        } else {
            obj.slider_path.calculated_path = raw_path;
        }

        obj.slider_path.calculated_length =
            path_length(obj.slider_path.calculated_path);

        // Compute slider duration
        // Find the active uninherited timing point
        double beat_length = 500.0; // default 120 BPM
        double sv_multiplier = 1.0;

        for (const auto& tp : beatmap.timing_points) {
            if (tp.time > static_cast<double>(obj.time) + 1.0) {
                break;
            }
            if (tp.is_timing_change) {
                beat_length = tp.beat_length;
                sv_multiplier = 1.0; // reset SV when a new timing section starts
            } else {
                sv_multiplier = tp.sv_multiplier();
            }
        }

        double base_velocity =
            static_cast<double>(beatmap.difficulty.slider_multiplier) * 100.0;
        double pixels_per_ms = base_velocity * sv_multiplier / beat_length;

        if (pixels_per_ms > 0.0) {
            double duration_per_span =
                static_cast<double>(obj.slider_path.expected_distance) /
                pixels_per_ms;
            obj.slider_duration = static_cast<float>(
                duration_per_span * static_cast<double>(obj.repeat_count));
        }
    }
}

} // namespace osu
