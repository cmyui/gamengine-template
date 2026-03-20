#include "osu_renderer.h"

#include "beatmap.h"

#include <engine/core/log.h>
#include <engine/renderer/shader.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osu {

static const char* CIRCLE_VERTEX_SRC = R"glsl(
#version 410 core
layout(location = 0) in vec2 a_position;

uniform mat4 u_projection;
uniform vec2 u_center;
uniform float u_radius;

out vec2 v_local_pos;

void main() {
    v_local_pos = a_position;
    vec2 world_pos = u_center + a_position * u_radius;
    gl_Position = u_projection * vec4(world_pos, 0.0, 1.0);
}
)glsl";

static const char* CIRCLE_FRAGMENT_SRC = R"glsl(
#version 410 core
in vec2 v_local_pos;

uniform vec4 u_color;
uniform float u_alpha;
uniform float u_inner_radius;
uniform float u_outer_radius;

out vec4 frag_color;

void main() {
    float dist = length(v_local_pos);
    if (dist > 1.0) discard;

    float aa = fwidth(dist);
    float outer_alpha = 1.0 - smoothstep(u_outer_radius - aa, u_outer_radius, dist);
    float inner_alpha = smoothstep(u_inner_radius - aa, u_inner_radius, dist);
    float ring_alpha = outer_alpha * inner_alpha;

    frag_color = vec4(u_color.rgb, u_color.a * u_alpha * ring_alpha);
}
)glsl";

static const char* QUAD_VERTEX_SRC = R"glsl(
#version 410 core
layout(location = 0) in vec2 a_position;

uniform mat4 u_projection;
uniform mat4 u_model;

void main() {
    gl_Position = u_projection * u_model * vec4(a_position, 0.0, 1.0);
}
)glsl";

static const char* QUAD_FRAGMENT_SRC = R"glsl(
#version 410 core

uniform vec4 u_color;

out vec4 frag_color;

void main() {
    frag_color = u_color;
}
)glsl";

static constexpr int CIRCLE_SEGMENTS = 64;

void OsuRenderer::init(float window_width, float window_height) {
    circle_shader_ = std::make_shared<engine::Shader>(
        CIRCLE_VERTEX_SRC, CIRCLE_FRAGMENT_SRC);
    quad_shader_ = std::make_shared<engine::Shader>(
        QUAD_VERTEX_SRC, QUAD_FRAGMENT_SRC);

    create_circle_mesh();
    create_quad_mesh();
    update_playfield_transform(window_width, window_height);

    GAME_LOG_INFO("OsuRenderer initialized ({}x{})", window_width, window_height);
}

void OsuRenderer::shutdown() {
    if (circle_vao_) {
        glDeleteVertexArrays(1, &circle_vao_);
        glDeleteBuffers(1, &circle_vbo_);
        circle_vao_ = 0;
        circle_vbo_ = 0;
    }
    if (quad_vao_) {
        glDeleteVertexArrays(1, &quad_vao_);
        glDeleteBuffers(1, &quad_vbo_);
        quad_vao_ = 0;
        quad_vbo_ = 0;
    }
    circle_shader_.reset();
    quad_shader_.reset();
}

void OsuRenderer::begin(float window_width, float window_height) {
    update_playfield_transform(window_width, window_height);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
}

void OsuRenderer::end() {
    // Intentionally empty; state restored by caller if needed.
}

void OsuRenderer::draw_filled_circle(glm::vec2 center, float radius,
                                      glm::vec4 color, float alpha) {
    glm::vec2 screen_center = osu_to_screen(center);
    float screen_radius = radius * playfield_scale_;

    circle_shader_->bind();
    circle_shader_->set_mat4("u_projection", projection_);
    circle_shader_->set_vec2("u_center", screen_center);
    circle_shader_->set_float("u_radius", screen_radius);
    circle_shader_->set_vec4("u_color", color);
    circle_shader_->set_float("u_alpha", alpha);
    circle_shader_->set_float("u_inner_radius", 0.0f);
    circle_shader_->set_float("u_outer_radius", 1.0f);

    glBindVertexArray(circle_vao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, circle_vertex_count_);
    glBindVertexArray(0);
}

void OsuRenderer::draw_circle_ring(glm::vec2 center, float radius,
                                    float thickness, glm::vec4 color,
                                    float alpha) {
    glm::vec2 screen_center = osu_to_screen(center);
    float screen_radius = radius * playfield_scale_;

    float inner = std::max(0.0f, 1.0f - thickness / radius);

    circle_shader_->bind();
    circle_shader_->set_mat4("u_projection", projection_);
    circle_shader_->set_vec2("u_center", screen_center);
    circle_shader_->set_float("u_radius", screen_radius);
    circle_shader_->set_vec4("u_color", color);
    circle_shader_->set_float("u_alpha", alpha);
    circle_shader_->set_float("u_inner_radius", inner);
    circle_shader_->set_float("u_outer_radius", 1.0f);

    glBindVertexArray(circle_vao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, circle_vertex_count_);
    glBindVertexArray(0);
}

void OsuRenderer::draw_slider_body(const std::vector<glm::vec2>& path,
                                    float radius, glm::vec4 color,
                                    float alpha) {
    if (path.size() < 2) return;

    // Stamp circles along the path at intervals of ~2 osu pixels.
    // Use a separate framebuffer-free approach: draw with MAX blending
    // to avoid alpha buildup, then draw a final pass. For simplicity,
    // we draw overlapping circles and rely on depth/stencil-free
    // approach by clamping alpha.
    //
    // Simple approach: draw all circles at reduced alpha then overlay.
    // Even simpler: just stamp them. The overlapping alpha accumulation
    // is a known limitation of this capsule approximation.
    //
    // To mitigate alpha buildup, we draw the slider body circles into
    // the stencil buffer first, then draw a single quad.
    // ... Actually, the simplest correct approach for a basic renderer:
    // draw with GL_MAX blend equation so overlapping regions don't
    // accumulate alpha.

    GLenum prev_blend_src, prev_blend_dst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, reinterpret_cast<GLint*>(&prev_blend_src));
    glGetIntegerv(GL_BLEND_DST_ALPHA, reinterpret_cast<GLint*>(&prev_blend_dst));

    // Use MAX blending to prevent alpha accumulation from overlapping circles
    glBlendEquation(GL_MAX);

    float step_distance = 2.0f;
    float accumulated = 0.0f;

    // Draw circle at the first point
    draw_filled_circle(path[0], radius, color, alpha);

    for (size_t i = 1; i < path.size(); ++i) {
        glm::vec2 delta = path[i] - path[i - 1];
        float segment_length = glm::length(delta);
        if (segment_length < 0.001f) continue;

        glm::vec2 dir = delta / segment_length;
        accumulated += segment_length;

        while (accumulated >= step_distance) {
            accumulated -= step_distance;
            float t = 1.0f - accumulated / segment_length;
            glm::vec2 pos = path[i - 1] + dir * (segment_length * t);
            draw_filled_circle(pos, radius, color, alpha);
        }
    }

    // Always draw circle at the last point
    draw_filled_circle(path.back(), radius, color, alpha);

    // Restore normal blending
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void OsuRenderer::draw_line(glm::vec2 from, glm::vec2 to, float width,
                             glm::vec4 color) {
    glm::vec2 screen_from = osu_to_screen(from);
    glm::vec2 screen_to = osu_to_screen(to);
    float screen_width = width * playfield_scale_;

    glm::vec2 dir = screen_to - screen_from;
    float length = glm::length(dir);
    if (length < 0.001f) return;

    float angle = std::atan2(dir.y, dir.x);

    glm::mat4 model{1.0f};
    model = glm::translate(model, glm::vec3(screen_from, 0.0f));
    model = glm::rotate(model, angle, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(length, screen_width, 1.0f));
    // Offset so the quad spans [0,1] in x and [-0.5, 0.5] in y
    model = glm::translate(model, glm::vec3(0.0f, -0.5f, 0.0f));

    quad_shader_->bind();
    quad_shader_->set_mat4("u_projection", projection_);
    quad_shader_->set_mat4("u_model", model);
    quad_shader_->set_vec4("u_color", color);

    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

glm::vec2 OsuRenderer::osu_to_screen(glm::vec2 osu_pos) const {
    return osu_pos * playfield_scale_ + playfield_offset_;
}

glm::vec2 OsuRenderer::screen_to_osu(glm::vec2 screen_pos) const {
    return (screen_pos - playfield_offset_) / playfield_scale_;
}

float OsuRenderer::osu_to_screen_scale() const {
    return playfield_scale_;
}

void OsuRenderer::create_circle_mesh() {
    // Unit circle as a triangle fan: center + CIRCLE_SEGMENTS perimeter vertices + 1 closing vertex
    circle_vertex_count_ = CIRCLE_SEGMENTS + 2;
    std::vector<float> vertices;
    vertices.reserve(circle_vertex_count_ * 2);

    // Center vertex
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);

    for (int i = 0; i <= CIRCLE_SEGMENTS; ++i) {
        float angle = 2.0f * static_cast<float>(std::numbers::pi) *
                      static_cast<float>(i) / static_cast<float>(CIRCLE_SEGMENTS);
        vertices.push_back(std::cos(angle));
        vertices.push_back(std::sin(angle));
    }

    glGenVertexArrays(1, &circle_vao_);
    glGenBuffers(1, &circle_vbo_);

    glBindVertexArray(circle_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, circle_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
}

void OsuRenderer::create_quad_mesh() {
    // Unit quad: [0,0] to [1,1]
    float vertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    glGenVertexArrays(1, &quad_vao_);
    glGenBuffers(1, &quad_vbo_);

    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
}

void OsuRenderer::update_playfield_transform(float window_width,
                                              float window_height) {
    window_width_ = window_width;
    window_height_ = window_height;

    float scale_x = window_width / PLAYFIELD_WIDTH;
    float scale_y = window_height / PLAYFIELD_HEIGHT;
    playfield_scale_ = std::min(scale_x, scale_y) * 0.8f;

    playfield_offset_.x =
        (window_width - PLAYFIELD_WIDTH * playfield_scale_) / 2.0f;
    playfield_offset_.y =
        (window_height - PLAYFIELD_HEIGHT * playfield_scale_) / 2.0f;

    // Y=0 at top, Y=window_height at bottom
    projection_ =
        glm::ortho(0.0f, window_width, window_height, 0.0f, -1.0f, 1.0f);
}

} // namespace osu
