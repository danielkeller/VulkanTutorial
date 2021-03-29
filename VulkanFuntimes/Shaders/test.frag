#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(push_constant) uniform Push {
  uint material;
} push;
layout(binding = 1) uniform sampler2D texSampler;
struct Material {
  vec4 baseColorFactor;
};
layout(binding = 2) uniform Materials {
  Material ix[1];
} material;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

void main() {
  // outColor = texture(texSampler, fragTexCoord);
  outColor = material.ix[push.material].baseColorFactor *
    max(.2, dot(fragNormal, vec3(1,1,1)));
  outColor.a = 1;
}
