#ifndef rendering_hpp
#define rendering_hpp

#include "vulkan/vulkan.hpp"

extern vk::CommandPool gTransferCommandPool;
struct TransferCommandPool {
  TransferCommandPool();
  ~TransferCommandPool();
};

extern vk::CommandPool gCommandPool;
struct CommandPool {
  CommandPool();
  ~CommandPool();
};

extern vk::RenderPass gRenderPass;
struct RenderPass {
  RenderPass();
  ~RenderPass();
};

struct Pipeline {
  vk::DescriptorSetLayout descriptorSetLayout_;
  vk::PipelineLayout layout_;
  vk::Pipeline pipeline_;
  Pipeline();
  ~Pipeline();
};

struct VertexBuffers {
  vk::Buffer buffer_;
  vk::DeviceMemory memory_;
  vk::Buffer staging_buffer_;
  vk::DeviceMemory staging_memory_;
  vk::DeviceSize vertex_offset_;
  vk::DeviceSize index_offset_;
  uint32_t count_;
  VertexBuffers();
  ~VertexBuffers();
};

struct UniformBuffers {
  vk::DeviceMemory memory_;
  char* mapping_;
  UniformBuffers();
  void update();
  ~UniformBuffers();
};

struct DescriptorPool {
  vk::DescriptorPool pool_;
  std::vector<vk::DescriptorSet> descriptorSets_;
  DescriptorPool(vk::DescriptorSetLayout layout);
  ~DescriptorPool();
};

struct CommandBuffers {
  std::vector<vk::CommandBuffer> commandBuffers_;
  CommandBuffers(const Pipeline& pipeline, const DescriptorPool& descriptorPool,
                 const VertexBuffers& vertices);
  // Buffers are freed by the pool
};

#endif /* rendering_hpp */
