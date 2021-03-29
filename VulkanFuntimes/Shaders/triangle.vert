#version 450

layout(push_constant) uniform Push {
  layout(offset=16) mat4 camera;
  uint model;
} push;
struct Model {
  mat4 mat;
};
layout(binding = 0) uniform Models { Model ix[1]; }models;

layout(location = 1) in vec3 inPosition;
layout(location = 2) in vec3 inNormal;
// layout(location = 4) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
void main() {
  gl_Position =
      push.camera * models.ix[push.model].mat * vec4(inPosition, 1.0);
  fragNormal = inNormal;
  // fragTexCoord = inTexCoord;
}
