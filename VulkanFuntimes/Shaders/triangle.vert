#version 450

layout(push_constant) uniform Camera { mat4 camera; } camera;
layout(binding = 0) uniform Model { mat4 model; } model;

layout(location = 1) in vec3 inPosition;
layout(location = 4) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
void main() {
  gl_Position =
      camera.camera * model.model * vec4(inPosition, 1.0);
  fragTexCoord = inTexCoord;
}
