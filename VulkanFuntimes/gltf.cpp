#include "gltf.hpp"

#include <google/protobuf/util/json_util.h>
#include <filesystem>
#include <fstream>
#include "stb_image.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtx/string_cast.hpp"

Gltf::Gltf(std::filesystem::path path) {
  directory_ = path;
  directory_.remove_filename();

  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open \"" + path.string() + "\"");
  size_t fileSize = (size_t)file.tellg();
  std::string buffer(fileSize, '\0');
  file.seekg(0);
  file.read(buffer.data(), fileSize);

  google::protobuf::util::JsonParseOptions options;
  options.ignore_unknown_fields = true;
  options.case_insensitive_enum_parsing = true;
  auto parseStatus =
      google::protobuf::util::JsonStringToMessage(buffer, &data_, options);
  if (!parseStatus.ok())
    throw std::runtime_error("failed to parse \"" + path.string() +
                             "\": " + parseStatus.message().as_string());
}

vk::Format vulkanFormat(gltf::Type type, gltf::ComponentType component) {
  if (type == gltf::SCALAR) {
    if (component == gltf::UNSIGNED_INT) return vk::Format::eR32Uint;
    if (component == gltf::FLOAT) return vk::Format::eR32Sfloat;
  }
  if (type == gltf::VEC2) {
    if (component == gltf::UNSIGNED_INT) return vk::Format::eR32G32Uint;
    if (component == gltf::FLOAT) return vk::Format::eR32G32Sfloat;
  }
  if (type == gltf::VEC3) {
    if (component == gltf::UNSIGNED_INT) return vk::Format::eR32G32B32Uint;
    if (component == gltf::FLOAT) return vk::Format::eR32G32B32Sfloat;
  }
  if (type == gltf::VEC4) {
    if (component == gltf::UNSIGNED_INT) return vk::Format::eR32G32B32A32Uint;
    if (component == gltf::FLOAT) return vk::Format::eR32G32B32A32Sfloat;
  }
  throw std::runtime_error("Unsupported type and component type");
}

vk::VertexInputAttributeDescription attributeFor(const gltf::Accessor& accessor,
                                                 uint32_t location) {
  return {location, accessor.bufferview(),
          vulkanFormat(accessor.type(), accessor.componenttype()),
          accessor.byteoffset()};
}

vk::PipelineVertexInputStateCreateInfo Gltf::vertexInput() const {
  bindings_.clear();
  for (uint32_t i = 0; i < data_.bufferviews().size(); ++i) {
    if (data_.bufferviews(i).target() != gltf::ARRAY_BUFFER) continue;
    bindings_.push_back({/*binding=*/i, data_.bufferviews(i).bytestride()});
  }

  attributes_.clear();
  const gltf::Primitive::Attributes& attributes =
      data_.meshes(0).primitives(0).attributes();
  if (attributes.has_position())
    attributes_.push_back(
        attributeFor(data_.accessors(attributes.position()), 0));
  if (attributes.has_texcoord_0())
    attributes_.push_back(
        attributeFor(data_.accessors(attributes.texcoord_0()), 1));

  return {/*flags=*/{}, bindings_, attributes_};
}

std::vector<vk::DeviceSize> Gltf::bindOffsets() const {
  std::vector<vk::DeviceSize> result;
  for (const gltf::BufferView& bufferView : data_.bufferviews()) {
    if (bufferView.target() != gltf::ARRAY_BUFFER)
      result.push_back(0);
    else
      result.push_back(bufferView.byteoffset());
  }
  return result;
}

vk::DeviceSize Gltf::bufferSize() const {
  vk::DeviceSize result = 0;
  for (const gltf::Buffer& buf : data_.buffers()) result += buf.bytelength();
  return result;
}

void Gltf::readBuffers(char* output) const {
  for (const gltf::Buffer& buf : data_.buffers()) {
    if (!buf.has_uri()) continue;
    std::filesystem::path path = directory_ / buf.uri();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
      throw std::runtime_error("failed to open \"" + path.string() + "\"");
    file.read(output, buf.bytelength());
    output += buf.bytelength();
  }
}

void Gltf::readUniforms(char* output) const {
  // Find the one mesh
  std::vector<uint32_t> nodes;
  nodes.push_back(data_.scenes(data_.scene()).nodes(0));
  while (data_.nodes(nodes.back()).children_size())
    nodes.push_back(data_.nodes(nodes.back()).children(0));
  while (!data_.nodes(nodes.back()).has_mesh()) {
    uint32_t prev = nodes.back();
    nodes.pop_back();
    if (nodes.empty()) return;  // No mesh

    auto siblings = data_.nodes(nodes.back()).children();
    auto it = std::find(siblings.begin(), siblings.end(), prev) + 1;
    if (it != siblings.end()) {
      nodes.push_back(*it);
      while (data_.nodes(nodes.back()).children_size())
        nodes.push_back(data_.nodes(nodes.back()).children(0));
    }
  }

  // Apply transforms left (last) to right (first)
  glm::mat4 result(1.);
  for (uint32_t node : nodes) {
    const gltf::Node data = data_.nodes(node);
    if (data.matrix_size() == 16)
      result *= glm::make_mat4(data.matrix().data());
    if (data.translation_size() == 3)
      result =
          glm::translate(result, glm::make_vec3(data.translation().data()));
    if (data.rotation_size() == 4)
      result *= glm::mat4(glm::make_quat(data.rotation().data()));
    if (data.scale_size() == 3)
      result = glm::scale(result, glm::make_vec3(data.scale().data()));
  }

  std::copy_n((char*)&result, sizeof(glm::mat4), output);
}

vk::IndexType Gltf::indexType() const {
  const gltf::Accessor& accessor =
      data_.accessors(data_.meshes(0).primitives(0).indices());
  if (accessor.componenttype() == gltf::UNSIGNED_INT)
    return vk::IndexType::eUint32;
  throw std::runtime_error("Unsupported index type");
}

vk::DeviceSize Gltf::indexOffset() const {
  const gltf::Accessor& accessor =
      data_.accessors(data_.meshes(0).primitives(0).indices());
  const gltf::BufferView& bufferView = data_.bufferviews(accessor.bufferview());
  return bufferView.byteoffset();
}
uint32_t Gltf::primitiveCount() const {
  const gltf::Accessor& accessor =
      data_.accessors(data_.meshes(0).primitives(0).indices());
  return accessor.count();
}

Pixels Gltf::getDiffuseImage() const {
  const gltf::Primitive& primitive = data_.meshes(0).primitives(0);
  const gltf::Material& material = data_.materials(primitive.material());
  const gltf::Texture& texture = data_.textures(
      material.pbrmetallicroughness().basecolortexture().index());
  const gltf::Image& image = data_.images(texture.source());
  std::filesystem::path path = directory_ / image.uri();

  Pixels result;
  int width, height, channels;
  result.data_ =
      stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
  result.width_ = width;
  result.height_ = height;
  if (!result.data_)
    throw std::runtime_error(std::string("stbi_load: ") +
                             stbi_failure_reason());
  return result;
}

Pixels::~Pixels() { stbi_image_free(data_); }
