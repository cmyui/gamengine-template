#pragma once

#include "beatmap.h"

#include <optional>
#include <string>
#include <vector>

namespace osu {

struct OszContents {
    std::vector<Beatmap> beatmaps;
    std::string audio_path;
    std::string background_path;
    std::string extract_directory;
};

class OszLoader {
public:
    static std::optional<OszContents> load(const std::string& osz_path);
};

} // namespace osu
