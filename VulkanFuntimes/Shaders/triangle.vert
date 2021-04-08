#version 450

layout(binding = 0) uniform Camera {
  mat4 eye;
  mat4 proj;
}
camera;
layout(binding = 1) uniform Model { mat4 model; }
model;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec3 fragView;

void main() {
  gl_Position = camera.proj * camera.eye * model.model * vec4(inPosition, 1);
  fragNormal = (model.model * vec4(inNormal, 0)).xyz;
  fragTangent = vec4((model.model * vec4(inTangent.xyz, 0)).xyz, inTangent.w);
  vec4 eyeWorld = vec4(camera.eye[3].xyz, 0);
  fragView = (model.model * vec4(inPosition, 1) - eyeWorld * camera.eye).xyz;
  fragTexCoord = inTexCoord;
}
