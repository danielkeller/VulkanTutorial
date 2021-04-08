#ifndef rendering_hpp
#define rendering_hpp

#include "vulkan/vulkan.hpp"
#include "drawdata.hpp"

using Index = uint16_t;
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texcoord;
  glm::vec4 tangent;
};

struct Pipeline {
  vk::DescriptorSetLayout descriptorSetLayout_;
  vk::PipelineLayout layout_;
  vk::Sampler sampler_;
  vk::Pipeline pipeline_;
  Pipeline(const Gltf& model);
  ~Pipeline();
};

extern std::vector<vk::CommandPool> gCommandPools;
struct CommandPool {
  CommandPool();
  ~CommandPool();
};

struct CommandBuffer {
  vk::CommandBuffer buf_;
  CommandBuffer(const Pipeline& pipeline, const DescriptorPool& descriptorPool,
                const VertexBuffers& vertices, const Gltf& gltf);
};

#endif /* rendering_hpp */
