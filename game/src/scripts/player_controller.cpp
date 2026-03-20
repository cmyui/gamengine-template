#include "player_controller.h"

#include <engine/core/input.h>
#include <engine/core/log.h>
#include <engine/scene/components.h>
#include <engine/scene/scene.h>

void PlayerController::on_create() {
    GAME_LOG_INFO("PlayerController created");
}

void PlayerController::on_update(float dt) {
    auto& transform = entity.get_component<engine::TransformComponent>();

    if (engine::Input::is_key_pressed(engine::KeyCode::W)) {
        transform.position.z -= speed_ * dt;
    }
    if (engine::Input::is_key_pressed(engine::KeyCode::S)) {
        transform.position.z += speed_ * dt;
    }
    if (engine::Input::is_key_pressed(engine::KeyCode::A)) {
        transform.position.x -= speed_ * dt;
    }
    if (engine::Input::is_key_pressed(engine::KeyCode::D)) {
        transform.position.x += speed_ * dt;
    }
}

void PlayerController::on_destroy() {
    GAME_LOG_INFO("PlayerController destroyed");
}
