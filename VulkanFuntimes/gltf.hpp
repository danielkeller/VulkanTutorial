#ifndef gltf_hpp
#define gltf_hpp

#include "gltf.pb.h"
#include <vulkan/vulkan.hpp>
#include <filesystem>
#include "glm/vec4.hpp"

struct Uniform {
  glm::vec4 baseColorFactor_ = glm::vec4(0);
};

struct Pixels {
  Pixels(int w, int h, unsigned char* d) : width_(w), height_(h), data_(d) {}
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
  uint32_t pipelinesCount() const { return data_.pipelines_size(); }
  vk::PipelineVertexInputStateCreateInfo pipelineInfo(uint32_t i) const;

  vk::DeviceSize bufferSize() const;
  void readBuffers(char* output) const;
  vk::DeviceSize uniformsSize() const;
  void readUniforms(char* output) const;
  uint32_t meshCount() const { return data_.meshes_size(); }
  uint32_t meshUniformOffset(uint32_t mesh) const;
  uint32_t materialCount() const { return data_.materials_size(); }
  uint32_t materialUniformOffset(uint32_t material) const;
  
  Pixels getDiffuseImage() const;
};

#endif /* gltf_hpp */
