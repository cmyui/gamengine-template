#include "game_application.h"

#include "osu/beatmap.h"
#include "osu/gameplay_clock.h"
#include "osu/osu_game.h"
#include "osu/osz_loader.h"

#include <engine/core/input.h>
#include <engine/core/log.h>
#include <engine/core/mouse_codes.h>
#include <engine/events/event.h>
#include <engine/events/key_event.h>
#include <engine/events/mouse_event.h>

// --- OsuLayer ---

OsuLayer::OsuLayer(const std::string& osz_path, int difficulty_index)
    : Layer("OsuLayer"),
      osz_path_(osz_path),
      difficulty_index_(difficulty_index) {}

OsuLayer::~OsuLayer() {
    if (clock_) {
        clock_->stop();
    }
}

void OsuLayer::on_attach() {
    game_ = std::make_unique<osu::OsuGame>();
    clock_ = std::make_unique<osu::GameplayClock>();

    auto& window = engine::Application::get().get_window();
    float w = static_cast<float>(window.get_width());
    float h = static_cast<float>(window.get_height());
    game_->init(w, h);

    auto contents = osu::OszLoader::load(osz_path_);
    if (!contents || contents->beatmaps.empty()) {
        GAME_LOG_ERROR("Failed to load osz: {}", osz_path_);
        return;
    }

    int idx = difficulty_index_;
    if (idx < 0 || idx >= static_cast<int>(contents->beatmaps.size())) {
        idx = 0;
    }

    GAME_LOG_INFO("Loaded {} difficulties", contents->beatmaps.size());
    for (size_t i = 0; i < contents->beatmaps.size(); i++) {
        GAME_LOG_INFO("  [{}] {} - {} [{}] ({} objects)",
            i,
            contents->beatmaps[i].metadata.artist,
            contents->beatmaps[i].metadata.title,
            contents->beatmaps[i].metadata.version,
            contents->beatmaps[i].hit_objects.size());
    }

    GAME_LOG_INFO("Playing difficulty index {}: [{}]", idx,
        contents->beatmaps[idx].metadata.version);

    auto& beatmap = contents->beatmaps[idx];

    // Configure clock from beatmap metadata.
    clock_->set_audio_lead_in(
        static_cast<double>(beatmap.general.audio_lead_in));

    double first_object_time = beatmap.hit_objects.empty()
        ? 0.0
        : static_cast<double>(beatmap.hit_objects.front().time);
    clock_->set_first_hit_object_time(first_object_time);

    // Load beatmap into game (moves the beatmap).
    game_->load_beatmap(std::move(beatmap));
    game_->start(0.0f);

    // Start the clock. Audio will begin when clock reaches time 0.
    clock_->start(contents->audio_path);
}

void OsuLayer::on_update(float dt) {
    if (!game_ || !clock_) {
        return;
    }

    clock_->update(static_cast<double>(dt));

    auto& window = engine::Application::get().get_window();
    float w = static_cast<float>(window.get_width());
    float h = static_cast<float>(window.get_height());

    game_->update(static_cast<float>(clock_->current_time()), w, h);
}

void OsuLayer::on_render() {
    if (!game_) {
        return;
    }
    game_->render();
}

void OsuLayer::on_imgui_render() {
    if (!game_) {
        return;
    }
    game_->render_imgui();
}

void OsuLayer::on_event(engine::Event& event) {
    if (!game_) {
        return;
    }

    if (event.get_type() == engine::EventType::KeyPressed) {
        auto& key_event = static_cast<engine::KeyPressedEvent&>(event);
        if (key_event.is_repeat()) {
            return;
        }

        auto key = key_event.get_key();
        if (key == engine::KeyCode::Z || key == engine::KeyCode::X ||
            key == engine::KeyCode::B || key == engine::KeyCode::N) {
            auto [mx, my] = engine::Input::get_mouse_position();
            game_->on_click(glm::vec2(mx, my));
            event.handled = true;
        }
    }
}

// --- GameApplication ---

GameApplication::GameApplication()
    : Application(engine::WindowProps("osu!", 1280, 720)) {}

GameApplication::~GameApplication() = default;

void GameApplication::on_init() {
    push_layer(std::make_unique<OsuLayer>(
        "assets/beatmaps/protoflicker.osz",
        1  // Master difficulty
    ));
}

void GameApplication::on_shutdown() {}
