#ifndef rendering_hpp
#define rendering_hpp

#include "vulkan/vulkan.hpp"

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
  vk::PipelineLayout layout_;
  vk::Pipeline pipeline_;
  Pipeline();
  ~Pipeline();
};

struct CommandBuffers {
  std::vector<vk::CommandBuffer> commandBuffers_;
  CommandBuffers(vk::Pipeline pipeline);
  // Buffers are freed by the pool
};

#endif /* rendering_hpp */
