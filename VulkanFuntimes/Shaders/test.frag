#version 450

layout(binding = 1) uniform sampler2D texSampler;
//layout(binding = 1) uniform Material {
//  vec4 baseColorFactor;
//} material;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

void main() {
  // outColor = texture(texSampler, fragTexCoord);
  outColor = vec4(fragNormal, 1);
}
