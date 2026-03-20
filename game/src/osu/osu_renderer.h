#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace engine {
class Shader;
}

namespace osu {

struct Beatmap;

class OsuRenderer {
public:
    void init(float window_width, float window_height);
    void shutdown();

    void begin(float window_width, float window_height);
    void end();

    void draw_filled_circle(glm::vec2 center, float radius, glm::vec4 color,
                            float alpha = 1.0f);
    void draw_circle_ring(glm::vec2 center, float radius, float thickness,
                          glm::vec4 color, float alpha = 1.0f);
    void draw_slider_body(const std::vector<glm::vec2>& path, float radius,
                          glm::vec4 color, float alpha = 1.0f);
    void draw_line(glm::vec2 from, glm::vec2 to, float width, glm::vec4 color);

    glm::vec2 osu_to_screen(glm::vec2 osu_pos) const;
    glm::vec2 screen_to_osu(glm::vec2 screen_pos) const;
    float osu_to_screen_scale() const;

private:
    void create_circle_mesh();
    void create_quad_mesh();
    void update_playfield_transform(float window_width, float window_height);

    std::shared_ptr<engine::Shader> circle_shader_;
    uint32_t circle_vao_ = 0;
    uint32_t circle_vbo_ = 0;
    int circle_vertex_count_ = 0;

    std::shared_ptr<engine::Shader> quad_shader_;
    uint32_t quad_vao_ = 0;
    uint32_t quad_vbo_ = 0;

    glm::mat4 projection_{1.0f};
    glm::vec2 playfield_offset_{0.0f};
    float playfield_scale_ = 1.0f;
    float window_width_ = 0.0f;
    float window_height_ = 0.0f;
};

} // namespace osu
