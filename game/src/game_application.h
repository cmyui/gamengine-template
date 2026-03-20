#pragma once

#include <engine/core/application.h>
#include <engine/core/layer.h>

#include <memory>
#include <string>

namespace osu { class OsuGame; }

class OsuLayer : public engine::Layer {
public:
    OsuLayer(const std::string& osz_path, int difficulty_index);
    ~OsuLayer() override;

    void on_attach() override;
    void on_update(float dt) override;
    void on_render() override;
    void on_imgui_render() override;
    void on_event(engine::Event& event) override;

private:
    std::unique_ptr<osu::OsuGame> game_;
    std::string osz_path_;
    int difficulty_index_;
    float elapsed_time_ms_ = 0.0f;
    bool started_ = false;
};

class GameApplication : public engine::Application {
public:
    GameApplication();
    ~GameApplication() override;

protected:
    void on_init() override;
    void on_shutdown() override;
};
