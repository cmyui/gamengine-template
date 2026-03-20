#include "osz_loader.h"

#include "beatmap_parser.h"

#include <engine/core/log.h>

#include <cstdlib>
#include <filesystem>

namespace osu {

std::optional<OszContents> OszLoader::load(const std::string& osz_path) {
    namespace fs = std::filesystem;

    fs::path osz_file(osz_path);
    if (!fs::exists(osz_file)) {
        GAME_LOG_ERROR("OSZ file not found: {}", osz_path);
        return std::nullopt;
    }

    // Extract to a directory alongside the .osz file, named after its stem
    fs::path extract_dir = osz_file.parent_path() / osz_file.stem();

    std::error_code ec;
    fs::create_directories(extract_dir, ec);
    if (ec) {
        GAME_LOG_ERROR("Failed to create extract directory {}: {}",
                       extract_dir.string(), ec.message());
        return std::nullopt;
    }

    // Extract the .osz archive using unzip
    std::string command = "unzip -o \"" + osz_file.string() + "\" -d \"" +
                          extract_dir.string() + "\" > /dev/null 2>&1";
    int result = std::system(command.c_str());
    if (result != 0) {
        GAME_LOG_ERROR("Failed to extract OSZ file: {} (exit code {})",
                       osz_path, result);
        return std::nullopt;
    }

    OszContents contents;
    contents.extract_directory = extract_dir.string();

    // Find and parse all .osu files
    for (const auto& entry : fs::directory_iterator(extract_dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".osu") {
            continue;
        }

        auto beatmap = BeatmapParser::parse(entry.path().string());
        if (beatmap.has_value()) {
            contents.beatmaps.push_back(std::move(*beatmap));
        }
    }

    if (contents.beatmaps.empty()) {
        GAME_LOG_ERROR("No valid beatmaps found in OSZ: {}", osz_path);
        return std::nullopt;
    }

    // Derive audio and background paths from the first parsed beatmap
    const auto& first = contents.beatmaps[0];

    if (!first.general.audio_filename.empty()) {
        fs::path audio = extract_dir / first.general.audio_filename;
        if (fs::exists(audio)) {
            contents.audio_path = audio.string();
        } else {
            GAME_LOG_ERROR("Audio file not found: {}", audio.string());
        }
    }

    if (!first.background_filename.empty()) {
        fs::path bg = extract_dir / first.background_filename;
        if (fs::exists(bg)) {
            contents.background_path = bg.string();
        } else {
            GAME_LOG_ERROR("Background file not found: {}", bg.string());
        }
    }

    GAME_LOG_INFO("Loaded OSZ: {} ({} beatmaps)", osz_path,
                  contents.beatmaps.size());

    return contents;
}

} // namespace osu
