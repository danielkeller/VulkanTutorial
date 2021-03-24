#version 450

layout(binding = 0) uniform MVP { mat4 model, view, projection; }
mvp;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
void main() {
  gl_Position =
      mvp.projection * mvp.view * mvp.model * vec4(inPosition, 0.0, 1.0);
  fragTexCoord = inTexCoord;
}
