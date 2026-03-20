#type vertex
#version 410 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_tex_coords;

uniform mat4 u_view_projection;
uniform mat4 u_model;

out vec3 v_normal;
out vec2 v_tex_coords;
out vec3 v_frag_pos;

void main() {
    v_frag_pos = vec3(u_model * vec4(a_position, 1.0));
    v_normal = mat3(transpose(inverse(u_model))) * a_normal;
    v_tex_coords = a_tex_coords;
    gl_Position = u_view_projection * vec4(v_frag_pos, 1.0);
}

#type fragment
#version 410 core

in vec3 v_normal;
in vec2 v_tex_coords;
in vec3 v_frag_pos;

out vec4 frag_color;

struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
};

uniform Material u_material;
uniform sampler2D u_texture_diffuse;

void main() {
    vec3 color = texture(u_texture_diffuse, v_tex_coords).rgb * u_material.diffuse;
    frag_color = vec4(color, 1.0);
}
