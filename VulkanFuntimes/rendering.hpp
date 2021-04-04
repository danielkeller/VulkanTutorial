#ifndef rendering_hpp
#define rendering_hpp

#include "vulkan/vulkan.hpp"
#include "drawdata.hpp"

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
