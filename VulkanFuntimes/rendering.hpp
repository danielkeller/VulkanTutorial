#ifndef rendering_hpp
#define rendering_hpp

#include "vulkan/vulkan.hpp"
#include "gltf.hpp"

struct Pipeline {
  vk::DescriptorSetLayout descriptorSetLayout_;
  vk::PipelineLayout layout_;
  vk::Sampler sampler_;
  vk::Pipeline pipeline_;
  Pipeline(const Gltf& model);
  ~Pipeline();
};

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
  UniformBuffers(const Gltf& gltf);
  ~UniformBuffers();
};

struct DescriptorPool {
  vk::DescriptorPool pool_;
  vk::DescriptorSet set_;
  DescriptorPool(vk::DescriptorSetLayout layout, const Textures& textures, const Gltf &gltf);
  ~DescriptorPool();
};

extern std::vector<vk::CommandPool> gCommandPools;
struct CommandPool {
  CommandPool();
  ~CommandPool();
};

struct CommandBuffer {
  vk::CommandBuffer buf_;
  CommandBuffer(const Pipeline& pipeline, const DescriptorPool& descriptorPool,
                const VertexBuffers& vertices,
                const Gltf& gltf);
};

#endif /* rendering_hpp */
