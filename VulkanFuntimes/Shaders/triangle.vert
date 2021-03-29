#version 450

layout(binding = 0) uniform Camera { mat4 camera; } camera;
layout(binding = 1) uniform Model { mat4 model; } model;

layout(location = 1) in vec3 inPosition;
layout(location = 2) in vec3 inNormal;
// layout(location = 4) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
void main() {
  gl_Position =
      camera.camera * model.model * vec4(inPosition, 1.0);
  fragNormal = inNormal;
  //fragTexCoord = inTexCoord;
}
