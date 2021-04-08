#include "mikktspace.hpp"

#include <cmath>
#include <iostream>
#include "glm/geometric.hpp"

using glm::vec2;
using glm::vec3;
using glm::vec4;

void makeTangents(uint32_t nIndices, uint16_t* indices,
                  BufferRef<const vec3> positions,
                  BufferRef<const vec3> normals,
                  BufferRef<const vec2> texCoords, BufferRef<vec4> tangents) {
  uint32_t inconsistentUvs = 0;
  for (uint32_t l = 0; l < nIndices; ++l) tangents[indices[l]] = vec4(0);
  for (uint32_t l = 0; l < nIndices; ++l) {
    uint32_t i = indices[l];
    uint32_t j = indices[(l + 1) % 3 + l / 3 * 3];
    uint32_t k = indices[(l + 2) % 3 + l / 3 * 3];
    vec3 n = normals[i];
    vec3 v1 = positions[j] - positions[i], v2 = positions[k] - positions[i];
    vec2 t1 = texCoords[j] - texCoords[i], t2 = texCoords[k] - texCoords[i];

    // Is the texture flipped?
    float uv2xArea = t1.x * t2.y - t1.y * t2.x;
    if (std::abs(uv2xArea) < 0x1p-20)
      continue;  // Smaller than 1/2 pixel at 1024x1024
    float flip = uv2xArea > 0 ? 1 : -1;
    if (tangents[i].w != 0 && tangents[i].w != -flip) ++inconsistentUvs;
    tangents[i].w = -flip;

    // Project triangle onto tangent plane
    v1 -= n * dot(v1, n);
    v2 -= n * dot(v2, n);
    // Tangent is object space direction of texture coordinates
    vec3 s = normalize((t2.y * v1 - t1.y * v2) * flip);

    // Use angle between projected v1 and v2 as weight
    float angle = std::acos(dot(v1, v2) / (length(v1) * length(v2)));
    tangents[i] += vec4(s * angle, 0);
  }
  for (uint32_t l = 0; l < nIndices; ++l) {
    vec4& t = tangents[indices[l]];
    t = vec4(normalize(vec3(t.x, t.y, t.z)), t.w);
  }

  if (inconsistentUvs) std::cerr << inconsistentUvs << " inconsistent UVs\n";
}

void makeTangents(uint32_t nIndices, Index* indices, Vertex* vertices) {
  uint32_t inconsistentUvs = 0;
  for (uint32_t l = 0; l < nIndices; ++l) vertices[indices[l]].tangent = vec4(0);
  for (uint32_t l = 0; l < nIndices; ++l) {
    Vertex& i = vertices[indices[l]];
    Vertex& j = vertices[indices[(l + 1) % 3 + l / 3 * 3]];
    Vertex& k = vertices[indices[(l + 2) % 3 + l / 3 * 3]];
    vec3 n = i.normal;
    vec3 v1 = j.position - i.position, v2 = k.position - i.position;
    vec2 t1 = j.texcoord - i.texcoord, t2 = k.texcoord - i.texcoord;

    // Is the texture flipped?
    float uv2xArea = t1.x * t2.y - t1.y * t2.x;
    if (std::abs(uv2xArea) < 0x1p-20)
      continue;  // Smaller than 1/2 pixel at 1024x1024
    float flip = uv2xArea > 0 ? 1 : -1;
    if (i.tangent.w != 0 && i.tangent.w != -flip) ++inconsistentUvs;
    i.tangent.w = -flip;

    // Project triangle onto tangent plane
    v1 -= n * dot(v1, n);
    v2 -= n * dot(v2, n);
    // Tangent is object space direction of texture coordinates
    vec3 s = normalize((t2.y * v1 - t1.y * v2) * flip);

    // Use angle between projected v1 and v2 as weight
    float angle = std::acos(dot(v1, v2) / (length(v1) * length(v2)));
    i.tangent += vec4(s * angle, 0);
  }
  for (uint32_t l = 0; l < nIndices; ++l) {
    vec4& t = vertices[indices[l]].tangent;
    t = vec4(normalize(vec3(t.x, t.y, t.z)), t.w);
  }

  if (inconsistentUvs) std::cerr << inconsistentUvs << " inconsistent UVs\n";
}
