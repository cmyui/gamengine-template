#include <engine/core/entry_point.h>
#include "game_application.h"

namespace engine {
std::unique_ptr<Application> create_application() {
    return std::make_unique<GameApplication>();
}
}

ENGINE_ENTRY_POINT()
