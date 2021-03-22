#version 450

layout(binding = 0) uniform MVP { mat4 model, view, projection; } mvp;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;
void main() {
  gl_Position =
      mvp.projection * mvp.view * mvp.model * vec4(inPosition, 0.0, 1.0);
  fragColor = inColor;
}
