#include "gltf.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/message_differencer.h>
#include <filesystem>
#include <fstream>
#include "stb_image.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtx/string_cast.hpp"

#include "driver.hpp"

gltf::Format vulkanFormat(gltf::Type type, gltf::ComponentType component) {
  if (component == gltf::UNSIGNED_SHORT) {
    if (type == gltf::SCALAR) return gltf::FORMAT_R16_UINT;
    if (type == gltf::VEC2) return gltf::FORMAT_R16G16_UINT;
    if (type == gltf::VEC3) return gltf::FORMAT_R16G16B16_UINT;
    if (type == gltf::VEC4) return gltf::FORMAT_R16G16B16A16_UINT;
  }
  if (component == gltf::UNSIGNED_INT) {
    if (type == gltf::SCALAR) return gltf::FORMAT_R32_UINT;
    if (type == gltf::VEC2) return gltf::FORMAT_R32G32_UINT;
    if (type == gltf::VEC3) return gltf::FORMAT_R32G32B32_UINT;
    if (type == gltf::VEC4) return gltf::FORMAT_R32G32B32A32_UINT;
  }
  if (component == gltf::FLOAT) {
    if (type == gltf::SCALAR) return gltf::FORMAT_R32_SFLOAT;
    if (type == gltf::VEC2) return gltf::FORMAT_R32G32_SFLOAT;
    if (type == gltf::VEC3) return gltf::FORMAT_R32G32B32_SFLOAT;
    if (type == gltf::VEC4) return gltf::FORMAT_R32G32B32A32_SFLOAT;
  }
  throw std::runtime_error("Unsupported type and component type");
}
gltf::IndexType vulkanIndexType(gltf::ComponentType component) {
  if (component == gltf::UNSIGNED_SHORT) return gltf::INDEX_TYPE_UINT16;
  if (component == gltf::UNSIGNED_INT) return gltf::INDEX_TYPE_UINT32;
  throw std::runtime_error("Unsupported index type");
}

template <class T>
uint32_t setFieldInsert(google::protobuf::RepeatedPtrField<T>* field,
                        const T& val) {
  for (uint32_t i = 0; i < field->size(); ++i)
    if (google::protobuf::util::MessageDifferencer::Equals(field->at(i), val))
      return i;
  *field->Add() = val;
  return field->size() - 1;
}

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

  for (gltf::Mesh& mesh : *data_.mutable_meshes()) {
    for (gltf::Primitive& prim : *mesh.mutable_primitives()) {
      const auto& attr = prim.attributes();

      gltf::Pipeline pipeline;
      gltf::DrawCall* drawCall = mesh.add_draw_calls();

      auto* refl = attr.GetReflection();
      auto* desc = attr.GetDescriptor();
      for (int i = 0; i < desc->field_count(); ++i) {
        auto field = attr.GetDescriptor()->field(i);
        if (!refl->HasField(attr, field)) continue;

        const gltf::Accessor& accessor =
            data_.accessors(refl->GetUInt32(attr, field));
        const gltf::BufferView& bufferview =
            data_.buffer_views(accessor.buffer_view());

        gltf::PipelineAttribute attrData;
        attrData.set_format(
            vulkanFormat(accessor.type(), accessor.component_type()));
        attrData.set_location(field->number());
        uint32_t attrIndex =
            setFieldInsert(data_.mutable_attributes(), attrData);

        gltf::PipelineBinding binding;
        binding.add_attributes(attrIndex);
        binding.set_stride(bufferview.byte_stride());
        uint32_t bindIndex = setFieldInsert(data_.mutable_bindings(), binding);

        pipeline.add_bindings(bindIndex);

        gltf::DrawCall::DrawBinding* drawBind = drawCall->add_bindings();
        drawBind->set_buffer(bufferview.buffer());
        drawBind->set_offset(accessor.byte_offset() + bufferview.byte_offset());
      }

      uint32_t pipelineIndex =
          setFieldInsert(data_.mutable_pipelines(), pipeline);
      drawCall->set_pipeline(pipelineIndex);

      const gltf::Accessor& accessor = data_.accessors(prim.indices());
      const gltf::BufferView& bufferview =
          data_.buffer_views(accessor.buffer_view());
      drawCall->set_index_buffer(bufferview.buffer());
      drawCall->set_index_type(vulkanIndexType(accessor.component_type()));
      drawCall->set_index_offset(accessor.byte_offset() +
                                 bufferview.byte_offset());
      drawCall->set_index_count(accessor.count());
      drawCall->set_material(prim.material());
      if (bufferview.byte_stride() > 0)
        throw std::runtime_error("Vulkan indices cannot have strides");
    }
  }
}

vk::PipelineVertexInputStateCreateInfo Gltf::pipelineInfo(uint32_t p) const {
  bindings_.clear();
  attributes_.clear();

  uint32_t i = 0;
  for (uint32_t b : data_.pipelines(p).bindings()) {
    const gltf::PipelineBinding& binding = data_.bindings(b);
    bindings_.push_back({i, binding.stride(), vk::VertexInputRate::eVertex});
    for (uint32_t attr : binding.attributes()) {
      const gltf::PipelineAttribute& info = data_.attributes(attr);
      attributes_.push_back({info.location(), i,
                             static_cast<vk::Format>(info.format()),
                             info.offset()});
    }
    ++i;
  }
  return {/*flags=*/{}, bindings_, attributes_};
}

vk::DeviceSize Gltf::bufferSize() const {
  return data_.buffers(0).bytelength();
}

void Gltf::readBuffers(char* output) const {
  const gltf::Buffer& buf = data_.buffers(0);
  if (!buf.has_uri()) return;
  std::filesystem::path path = directory_ / buf.uri();
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open \"" + path.string() + "\"");
  file.read(output, buf.bytelength());
  output += buf.bytelength();
}

vk::DeviceSize Gltf::uniformsSize() const {
  return uniformSize(data_.meshes_size() * sizeof(glm::mat4)) +
         data_.materials_size() * sizeof(Uniform);
}

uint32_t Gltf::materialsUniformOffset() const {
  return static_cast<uint32_t>(
      uniformSize(data_.meshes_size() * sizeof(glm::mat4)));
}

void Gltf::readUniforms(char* output) const {
  std::fill_n(output, uniformsSize(), '\0');

  for (uint32_t root : data_.scenes(data_.scene()).nodes()) {
    std::vector<uint32_t> nodes = {root};
    while (data_.nodes(nodes.back()).children_size())
      nodes.push_back(data_.nodes(nodes.back()).children(0));
    while (true) {
      if (data_.nodes(nodes.back()).has_mesh()) {
        // Apply transforms left (last) to right (first)
        glm::mat4 result(1.);
        for (uint32_t node : nodes) {
          const gltf::Node data = data_.nodes(node);
          if (data.matrix_size() == 16)
            result *= glm::make_mat4(data.matrix().data());
          if (data.translation_size() == 3)
            result = glm::translate(result,
                                    glm::make_vec3(data.translation().data()));
          if (data.rotation_size() == 4)
            result *= glm::mat4(glm::make_quat(data.rotation().data()));
          if (data.scale_size() == 3)
            result = glm::scale(result, glm::make_vec3(data.scale().data()));
        }

        size_t offset = sizeof(glm::mat4) * data_.nodes(nodes.back()).mesh();
        std::copy_n((char*)&result, sizeof(glm::mat4), output + offset);
      }

      uint32_t prev = nodes.back();
      nodes.pop_back();
      if (nodes.empty()) break;  // End of tree

      auto siblings = data_.nodes(nodes.back()).children();
      auto it = std::find(siblings.begin(), siblings.end(), prev) + 1;
      if (it != siblings.end()) {
        nodes.push_back(*it);
        while (data_.nodes(nodes.back()).children_size())
          nodes.push_back(data_.nodes(nodes.back()).children(0));
      }
    }
  }

  uint32_t matIndex = 0;
  for (const gltf::Material& mat : data_.materials()) {
    Uniform u;
    if (mat.pbr_metallic_roughness().base_color_factor_size() == 4)
      u.baseColorFactor_ = glm::make_vec4(
          mat.pbr_metallic_roughness().base_color_factor().data());

    size_t offset = materialsUniformOffset() + sizeof(Uniform) * matIndex++;
    std::copy_n((char*)&u, sizeof u, output + offset);
  }
}

Pixels Gltf::getDiffuseImage() const {
  const gltf::Primitive& primitive = data_.meshes(0).primitives(0);
  const gltf::Material& material = data_.materials(primitive.material());
  if (!material.pbr_metallic_roughness().has_base_color_texture()) {
    char* data = (char*)malloc(4);
    *(uint32_t*)data = 0xFFFFFFFF;
    return Pixels(1, 1, (unsigned char*)data);
  }

  const gltf::Texture& texture = data_.textures(
      material.pbr_metallic_roughness().base_color_texture().index());
  const gltf::Image& image = data_.images(texture.source());
  std::filesystem::path path = directory_ / image.uri();

  int width, height, channels;
  unsigned char* data =
      stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
  if (!data)
    throw std::runtime_error(std::string("stbi_load: ") +
                             stbi_failure_reason());
  return Pixels(width, height, data);
}

Pixels::~Pixels() { stbi_image_free(data_); }
