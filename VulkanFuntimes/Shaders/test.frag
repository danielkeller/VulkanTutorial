#version 450

layout(binding = 2) uniform sampler2DArray texSampler;
layout(binding = 3) uniform sampler2DArray dataSampler;
layout(binding = 4) uniform Material {
  vec4 baseColorFactor;
  uint baseColorTexture, normalTexture, metallicRoughnessTexture;
}
material;

layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in vec3 fragView;

layout(location = 0) out vec4 outColor;

const float dielectricSpecular = 0.04;
const float PI = 3.1415926535897932384626433832795;

vec4 tex(uint index) { return texture(texSampler, vec3(fragTexCoord, index)); }
vec4 data(uint index) {
  return texture(dataSampler, vec3(fragTexCoord, index));
}

void main() {
  vec4 baseColor = material.baseColorFactor * tex(material.baseColorTexture);
  vec3 tnormal = data(material.normalTexture).rgb * 2 - 1;
  float metallic = data(material.metallicRoughnessTexture).b;
  float roughness = data(material.metallicRoughnessTexture).g;

  vec3 binormal = fragTangent.w * cross(fragNormal, fragTangent.xyz);
  vec3 normal = normalize(tnormal.x * fragTangent.xyz + tnormal.y * binormal +
                          tnormal.z * fragNormal);

  normal = normalize(fragNormal);
  vec3 light = normalize(vec3(1, 1, 1));
  vec3 view = normalize(fragView);
  vec3 H = normalize(light + view);

  float VdotN = max(dot(view, normal), 0.0);
  float LdotN = max(dot(light, normal), 0.0);
  float NdotH = max(dot(normal, H), 0.0);
  float VdotH = max(dot(view, H), 1e-6);

  vec3 f0 =
      mix(vec3(dielectricSpecular), /*fresColor**/ baseColor.rgb, metallic);
  vec3 F = f0 + (1 - f0) * pow(1 - VdotH, 5);
  //  vec3 F = f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1 - VdotH, 5);

  vec3 color = baseColor.rgb * (1 - dielectricSpecular) * (1 - metallic);
  vec3 diffuse = (1 - F) * color / PI;

  float a = roughness * roughness;
  float a2 = a * a;
  float D = a2 / (PI * pow((a2 - 1) * NdotH * NdotH + 1, 2));

  float k = a * sqrt(2 / PI);
  float G = VdotN / (VdotN * (1 - k) + k) * LdotN / (LdotN * (1 - k) + k);

  vec3 specular = F * D * G / (4 * VdotN * LdotN + 1e-6);

  //  outColor.rgb = vec3(specular);
  outColor.rgb = (diffuse + specular) * LdotN;
  outColor.a = 1;
}
