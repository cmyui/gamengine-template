#pragma once
#include <engine/core/application.h>
#include <memory>

namespace engine { class Scene; class ScriptEngine; }

class GameApplication : public engine::Application {
public:
    GameApplication();
    ~GameApplication() override;
protected:
    void on_init() override;
    void on_shutdown() override;
private:
    std::unique_ptr<engine::Scene> scene_;
    std::unique_ptr<engine::ScriptEngine> script_engine_;
};
