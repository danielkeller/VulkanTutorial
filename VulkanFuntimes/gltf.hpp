#ifndef gltf_hpp
#define gltf_hpp

#include "gltf.pb.h"
#include <vulkan/vulkan.hpp>
#include <filesystem>
#include <fstream>
#include "glm/vec4.hpp"

struct Uniform {
  glm::vec4 baseColorFactor_ = glm::vec4(1);
  uint32_t baseColorTexture, normalTexture, metallicRoughnessTexture;
};

struct Pixels {
  Pixels(int w, int h, unsigned char* d);
  Pixels(Pixels&&) = default;
  uint32_t width_, height_;
  std::unique_ptr<unsigned char, void(*)(void*)> data_;
  size_t size() const { return width_ * height_ * 4; }
  vk::Extent3D extent() const { return {width_, height_, 1}; }
};

struct Gltf {
  Gltf(std::filesystem::path path);
  void save(std::filesystem::path path);

  vk::DeviceSize bufferSize() const;
  void readBuffers(char* output) const;
  vk::DeviceSize uniformsSize() const;
  void readUniforms(char* output) const;
  std::vector<Pixels> getImages() const;
  uint32_t meshCount() const { return data_.meshes_size(); }
  uint32_t meshUniformOffset(uint32_t mesh) const;
  uint32_t materialCount() const { return data_.materials_size(); }
  uint32_t materialUniformOffset(uint32_t material) const;
  
  gltf::Gltf data_;
  std::filesystem::path directory_;
  mutable std::ifstream file_;
  long long bufferStart_;
  
private:
  void readJSON(size_t length);
  void openBinFile();
  void setupVulkanData();
};

#endif /* gltf_hpp */
