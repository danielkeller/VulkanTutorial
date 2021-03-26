#ifndef rendering_hpp
#define rendering_hpp

#include "vulkan/vulkan.hpp"
#include "gltf.hpp"

extern vk::CommandPool gTransferCommandPool;
struct TransferCommandPool {
  TransferCommandPool();
  ~TransferCommandPool();
};
struct TransferCommandBuffer {
  TransferCommandBuffer();
  ~TransferCommandBuffer();
  vk::CommandBuffer cmd_;
};

extern vk::CommandPool gCommandPool;
struct CommandPool {
  CommandPool();
  ~CommandPool();
};

struct Pipeline {
  vk::DescriptorSetLayout descriptorSetLayout_;
  vk::PipelineLayout layout_;
  vk::Sampler sampler_;
  vk::Pipeline pipeline_;
  Pipeline(const Gltf& model);
  ~Pipeline();
};

struct MappedStagingBuffer;
struct StagingBuffer {
  MappedStagingBuffer map(vk::DeviceSize size);
  ~StagingBuffer();
  vk::Buffer buffer_;
  vk::DeviceMemory memory_;
};
struct MappedStagingBuffer {
  ~MappedStagingBuffer();
  vk::DeviceMemory memory_;
  char* pointer_;
};

struct VertexBuffers {
  vk::Buffer buffer_;
  vk::DeviceMemory memory_;
  StagingBuffer staging_buffer_;
  vk::DeviceSize index_offset_;
  uint32_t count_;
  vk::IndexType index_type_;
  std::vector<vk::DeviceSize> bind_offsets_;
  VertexBuffers(const Gltf& model);
  ~VertexBuffers();
};

struct Textures {
  vk::ImageView imageView_;
  vk::Image image_;
  vk::DeviceMemory memory_;
  StagingBuffer staging_buffer_;
  Textures(const Gltf& model);
  ~Textures();
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
  DescriptorPool(vk::DescriptorSetLayout layout, const Textures& textures);
  ~DescriptorPool();
};

struct CommandBuffers {
  std::vector<vk::CommandBuffer> commandBuffers_;
  CommandBuffers(const Pipeline& pipeline, const DescriptorPool& descriptorPool,
                 const VertexBuffers& vertices);
  // Buffers are freed by the pool
};

#endif /* rendering_hpp */
