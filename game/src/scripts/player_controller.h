#pragma once
#include <engine/scripting/native_script.h>

class PlayerController : public engine::NativeScript {
public:
    void on_create() override;
    void on_update(float dt) override;
    void on_destroy() override;
private:
    float speed_ = 5.0f;
};
