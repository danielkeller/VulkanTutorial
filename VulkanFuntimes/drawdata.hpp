#ifndef drawdata_hpp
#define drawdata_hpp

#include "glm/mat4x4.hpp"

#include "vulkan/vulkan.hpp"
#include "gltf.hpp"

struct StagingBuffer {
  vk::Buffer buffer_;
  vk::DeviceMemory memory_;
  vk::Fence done_;
};
struct Transfer : public StagingBuffer {
  ~Transfer();
  void copy(vk::Buffer from, vk::Buffer to, vk::DeviceSize size,
            vk::PipelineStageFlags dstStage, vk::AccessFlags dstAccess);
  vk::CommandBuffer cmd_;
  char* pointer_;
};

struct TransferManager {
  TransferManager();
  ~TransferManager();
  vk::CommandPool transferCommandPool_;
  std::vector<StagingBuffer> currentBuffers_;
  Transfer newTransfer(vk::DeviceSize size);
  void collectGarbage();
};
extern TransferManager* gTransferManager;

struct VertexBuffers {
  vk::Buffer buffer_;
  vk::DeviceMemory memory_;
  VertexBuffers(const Gltf& model);
  ~VertexBuffers();
};

struct Textures {
  vk::ImageView imageView_;
  vk::ImageView imageViewData_;
  vk::Image image_;
  vk::DeviceMemory memory_;
  Textures(const Gltf& model);
  ~Textures();
};

struct Camera {
  glm::mat4 eye, proj;
};

struct DescriptorPool {
  vk::DescriptorPool pool_;
  vk::DescriptorSet set_;
  vk::DeviceMemory memory_;
  vk::Buffer scene_;
  vk::DeviceMemory shared_memory_;
  vk::Buffer camera_;
  char* mapping_;
  DescriptorPool(vk::DescriptorSetLayout layout, const Textures& textures,
                 const Gltf& gltf);
  void updateCamera();
  ~DescriptorPool();
};

#endif /* drawdata_hpp */
