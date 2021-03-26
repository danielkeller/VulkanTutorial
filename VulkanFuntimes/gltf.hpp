#ifndef gltf_hpp
#define gltf_hpp

#include "gltf.pb.h"
#include <vulkan/vulkan.hpp>
#include <filesystem>

struct Pixels {
  uint32_t width_, height_;
  unsigned char* data_;
  size_t size() const { return width_ * height_ * 4; }
  vk::Extent3D extent() const { return {width_, height_, 1}; }
  ~Pixels();
};

struct Gltf {
  Gltf(std::filesystem::path path);
  gltf::Gltf data_;
  std::filesystem::path directory_;

  mutable std::vector<vk::VertexInputBindingDescription> bindings_;
  mutable std::vector<vk::VertexInputAttributeDescription> attributes_;
  vk::PipelineVertexInputStateCreateInfo vertexInput() const;
  vk::DeviceSize bufferSize() const;
  void readBuffers(char* output) const;
  vk::IndexType indexType() const;
  vk::DeviceSize indexOffset() const;
  uint32_t primitiveCount() const;
  std::vector<vk::DeviceSize> bindOffsets() const;
  Pixels getDiffuseImage() const;
};

#endif /* gltf_hpp */
