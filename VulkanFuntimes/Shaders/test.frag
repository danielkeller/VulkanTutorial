#version 450

layout(binding = 2) uniform sampler2DArray texSampler;
layout(binding = 3) uniform sampler2DArray dataSampler;
layout(binding = 4) uniform Material {
  vec4 baseColorFactor;
  uint baseColorTexture, normalTexture;
}
material;

layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragTangent;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 light = normalize(vec3(1, 1, 1));
  //  outColor = vec4(fragNormal, 1);
  vec3 baseColor =
      texture(texSampler, vec3(fragTexCoord, material.baseColorTexture)).rgb;

  vec3 binormal = fragTangent.w * cross(fragNormal, fragTangent.xyz);

  vec3 normal =
      texture(dataSampler, vec3(fragTexCoord, material.normalTexture)).rgb*2-1;
  vec3 worldNormal =
      normal.x * fragTangent.xyz + normal.y * binormal + normal.z * fragNormal;
   outColor = vec4(worldNormal.xyz, 1);
   outColor.rgb = baseColor * max(.1, dot(worldNormal, light));
//  outColor.rgb = vec3(1) * fragTangent.w;
//  outColor.rgb = worldNormal;
  outColor.a = 1;
}
