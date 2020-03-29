#version 450

layout (location = 0) out vec3 fragColor;

vec2 vertices[3] = vec2[](
    vec2(0, -1.0),
    vec2(1.0, 1.0),
    vec2(-1.0, 1.0)
);

vec3 colors[3] = vec3[](
    vec3(0.3, 0.5, 0.1),
    vec3(0.95, 0.33, 0.64),
    vec3(0.55, 0.00, 0.55)
);


void main() {
    gl_Position = vec4(vertices[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}