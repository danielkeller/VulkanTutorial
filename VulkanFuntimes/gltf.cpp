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
#include "mikktspace.hpp"

uint32_t size(gltf::ComponentType component) {
  if (component == gltf::UNSIGNED_SHORT) return 2;
  if (component == gltf::UNSIGNED_INT) return 4;
  if (component == gltf::FLOAT) return 4;
  throw std::runtime_error("Unsupported component type");
}

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

  uint32_t tangentBufferView = data_.buffer_views_size();
  data_.add_buffer_views()->set_buffer(0);

  for (gltf::Mesh& mesh : *data_.mutable_meshes()) {
    for (gltf::Primitive& prim : *mesh.mutable_primitives()) {
      auto* attr = prim.mutable_attributes();

      if (!attr->has_tangent() && attr->has_position() && attr->has_normal() &&
          attr->has_texcoord_0()) {
        uint32_t vertexCount = data_.accessors(attr->position()).count();
        uint64_t tangentLength = vertexCount * sizeof(glm::vec4);
        uint64_t tangentOffset = data_.buffers(0).generated_byte_length();
        data_.mutable_buffers(0)->set_generated_byte_length(tangentOffset +
                                                            tangentLength);

        attr->set_tangent(data_.accessors_size());
        gltf::Accessor* accessor = data_.add_accessors();
        accessor->set_component_type(gltf::FLOAT);
        accessor->set_type(gltf::VEC4);
        accessor->set_buffer_view(tangentBufferView);
        accessor->set_byte_offset(data_.buffers(0).byte_length() +
                                  tangentOffset);
      }

      gltf::Pipeline pipeline;
      gltf::DrawCall* drawCall = mesh.add_draw_calls();

      auto* refl = attr->GetReflection();
      auto* desc = attr->GetDescriptor();
      for (int i = 0; i < desc->field_count(); ++i) {
        auto field = attr->GetDescriptor()->field(i);
        if (!refl->HasField(*attr, field)) continue;

        const gltf::Accessor& accessor =
            data_.accessors(refl->GetUInt32(*attr, field));
        const gltf::BufferView& bufferview =
            data_.buffer_views(accessor.buffer_view());

        gltf::PipelineAttribute attrData;
        attrData.set_format(
            vulkanFormat(accessor.type(), accessor.component_type()));
        attrData.set_location(
            static_cast<gltf::ShaderLocation>(field->number()));
        uint32_t attrIndex =
            setFieldInsert(data_.mutable_attributes(), attrData);

        gltf::PipelineBinding binding;
        binding.add_attributes(attrIndex);
        uint32_t minStride = accessor.type() * size(accessor.component_type());
        binding.set_stride(std::max(minStride, bufferview.byte_stride()));
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
      attributes_.push_back({static_cast<uint32_t>(info.location()), i,
                             static_cast<vk::Format>(info.format()),
                             info.offset()});
    }
    ++i;
  }
  return {/*flags=*/{}, bindings_, attributes_};
}

vk::DeviceSize Gltf::bufferSize() const {
  return data_.buffers(0).byte_length() +
         data_.buffers(0).generated_byte_length();
}

template <class T>
BufferRef<T> getBufferRef(const gltf::Gltf& data, char* ptr,
                          const gltf::DrawCall& drawCall,
                          gltf::ShaderLocation attribute, gltf::Format format) {
  const gltf::Pipeline& pipeline = data.pipelines(drawCall.pipeline());
  for (uint32_t i = 0; i < pipeline.bindings_size(); ++i) {
    const gltf::PipelineBinding& binding = data.bindings(pipeline.bindings(i));
    for (uint32_t attr : binding.attributes()) {
      const gltf::PipelineAttribute& pipeAttr = data.attributes(attr);
      if (pipeAttr.location() != attribute) continue;
      if (pipeAttr.format() != format)
        throw std::runtime_error(
            "Unexpected format " + gltf::Format_Name(pipeAttr.format()) +
            " for " + gltf::ShaderLocation_Name(attribute));

      return BufferRef<T>(
          ptr + pipeAttr.offset() + drawCall.bindings(i).offset(),
          binding.stride());
    }
  }
  throw std::runtime_error("Attrribute not found: " +
                           gltf::ShaderLocation_Name(attribute));
}

void Gltf::readBuffers(char* output) const {
  const gltf::Buffer& buf = data_.buffers(0);
  if (!buf.has_uri()) return;
  std::filesystem::path path = directory_ / buf.uri();
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open \"" + path.string() + "\"");
  file.read(output, buf.byte_length());

  // Generate tangents if needed
  for (const gltf::Mesh& mesh : data_.meshes()) {
    for (const gltf::DrawCall& drawCall : mesh.draw_calls()) {
      auto tangents =
          getBufferRef<glm::vec4>(data_, output, drawCall, gltf::TANGENT,
                                  gltf::FORMAT_R32G32B32A32_SFLOAT);
      // Is it already in the buffer?
      if (tangents.buffer_ - output < buf.byte_length()) continue;

      auto positions =
          getBufferRef<glm::vec3>(data_, output, drawCall, gltf::POSITION,
                                  gltf::FORMAT_R32G32B32_SFLOAT);
      auto normals = getBufferRef<glm::vec3>(
          data_, output, drawCall, gltf::NORMAL, gltf::FORMAT_R32G32B32_SFLOAT);
      auto texCoords =
          getBufferRef<glm::vec2>(data_, output, drawCall, gltf::TEXCOORD_0,
                                  gltf::FORMAT_R32G32_SFLOAT);
      if (drawCall.index_type() != gltf::INDEX_TYPE_UINT16)
        throw std::runtime_error("32 bit indices not supported");
      uint16_t* indices = (uint16_t*)(output + drawCall.index_offset());
      makeTangents(drawCall.index_count(), indices, positions, normals,
                   texCoords, tangents);
    }
  }
}

vk::DeviceSize Gltf::uniformsSize() const {
  return data_.meshes_size() * uniformSize<glm::mat4>() +
         data_.materials_size() * uniformSize<Uniform>();
}

uint32_t Gltf::meshUniformOffset(uint32_t mesh) const {
  return mesh * static_cast<uint32_t>(uniformSize<glm::mat4>());
}

uint32_t Gltf::materialUniformOffset(uint32_t material) const {
  return data_.meshes_size() * static_cast<uint32_t>(uniformSize<glm::mat4>()) +
         material * static_cast<uint32_t>(uniformSize<Uniform>());
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

        size_t offset = meshUniformOffset(data_.nodes(nodes.back()).mesh());
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
    if (mat.pbr_metallic_roughness().has_base_color_texture())
      u.baseColorTexture =
          data_
              .textures(
                  mat.pbr_metallic_roughness().base_color_texture().index())
              .source();
    if (mat.has_normal_texture())
      u.normalTexture = data_.textures(mat.normal_texture().index()).source();

    size_t offset = materialUniformOffset(matIndex++);
    std::copy_n((char*)&u, sizeof u, output + offset);
  }
}

std::vector<Pixels> Gltf::getImages() const {
  std::vector<Pixels> result;
  for (const gltf::Image& image : data_.images()) {
    std::filesystem::path path = directory_ / image.uri();
    //    std::filesystem::path path = "Textures/checker.png";

    int width, height, channels;
    unsigned char* data =
        stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data)
      throw std::runtime_error(std::string("stbi_load: ") +
                               stbi_failure_reason() + " " + path.string());
    result.emplace_back(width, height, data);
  }
  return result;
}

Pixels::Pixels(int w, int h, unsigned char* d)
    : width_(w), height_(h), data_(d, stbi_image_free) {}
