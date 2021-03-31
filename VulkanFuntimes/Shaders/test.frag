#version 450

layout(binding = 2) uniform sampler2DArray texSampler;
layout(binding = 3) uniform Material {
  vec4 baseColorFactor;
  uint baseColorTexture, normalTexture;
}
material;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

void main() {
  vec3 light = vec3(-1, 1, 1);
  //  outColor = vec4(fragNormal, 1);
  vec3 baseColor =
      texture(texSampler, vec3(fragTexCoord, material.baseColorTexture)).rgb;
  vec3 normal =
      texture(texSampler, vec3(fragTexCoord, material.normalTexture)).rgb;
  //outColor = vec4(fragNormal,1);
  //outColor.rgb = baseColor * max(.1, dot(fragNormal, light));
  outColor.rgb = vec3(fragTexCoord.r);
  outColor.a = 1;
}
