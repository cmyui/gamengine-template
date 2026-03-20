#include "script_exports.h"
#include <engine/scripting/script_registry.h>
#include "player_controller.h"

extern "C" SCRIPT_API void register_scripts(engine::ScriptRegistry& registry) {
    registry.register_script<PlayerController>("PlayerController");
}
