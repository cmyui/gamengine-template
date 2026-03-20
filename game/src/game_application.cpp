#include "game_application.h"

#include <engine/core/log.h>
#include <engine/platform/platform_detection.h>
#include <engine/scene/components.h>
#include <engine/scene/scene.h>
#include <engine/scene/scene_serializer.h>
#include <engine/scripting/native_script.h>
#include <engine/scripting/script_engine.h>

#ifdef ENGINE_PLATFORM_WINDOWS
static constexpr const char* SCRIPTS_LIBRARY_PATH = "GameScripts.dll";
#elif defined(ENGINE_PLATFORM_MACOS)
static constexpr const char* SCRIPTS_LIBRARY_PATH = "libGameScripts.dylib";
#else
static constexpr const char* SCRIPTS_LIBRARY_PATH = "libGameScripts.so";
#endif

GameApplication::GameApplication()
    : Application(engine::WindowProps("Game", 1280, 720)) {}

GameApplication::~GameApplication() = default;

void GameApplication::on_init() {
    scene_ = std::make_unique<engine::Scene>();
    script_engine_ = std::make_unique<engine::ScriptEngine>();

    engine::SceneSerializer serializer(*scene_);
    serializer.deserialize("assets/scenes/main.scene.json");

    if (!script_engine_->load_library(SCRIPTS_LIBRARY_PATH)) {
        GAME_LOG_ERROR("Failed to load game scripts library");
        return;
    }

    auto& registry = scene_->get_registry();
    auto view = registry.view<engine::NativeScriptComponent>();
    for (auto entity_handle : view) {
        auto& nsc = view.get<engine::NativeScriptComponent>(entity_handle);
        auto instance = script_engine_->get_registry().create(nsc.script_name);
        if (instance) {
            instance->entity = engine::Entity(entity_handle, scene_.get());
            nsc.instance = instance.release();
            nsc.instance->on_create();
        } else {
            GAME_LOG_ERROR("Failed to create script: {}", nsc.script_name);
        }
    }
}

void GameApplication::on_shutdown() {
    auto& registry = scene_->get_registry();
    auto view = registry.view<engine::NativeScriptComponent>();
    for (auto entity_handle : view) {
        auto& nsc = view.get<engine::NativeScriptComponent>(entity_handle);
        if (nsc.instance) {
            nsc.instance->on_destroy();
            delete nsc.instance;
            nsc.instance = nullptr;
        }
    }

    script_engine_->unload_library();
    scene_.reset();
}
