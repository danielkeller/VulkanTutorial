#include "gltf.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/message_differencer.h>
#include <filesystem>
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

void Gltf::readJSON(size_t length) {
  std::string buffer(length, '\0');
  file_.read(buffer.data(), length);

  google::protobuf::util::JsonParseOptions options;
  options.ignore_unknown_fields = true;
  options.case_insensitive_enum_parsing = true;
  auto parseStatus =
      google::protobuf::util::JsonStringToMessage(buffer, &data_, options);
  if (!parseStatus.ok())
    throw std::runtime_error("failed to parse: " +
                             parseStatus.message().as_string());
}

Gltf::Gltf(std::filesystem::path path) {
  directory_ = path;
  directory_.remove_filename();

  file_ = std::ifstream(path, std::ios::ate | std::ios::binary);
  if (!file_.is_open()) throw std::runtime_error("failed to open");
  size_t fileSize = (size_t)file_.tellg();
  file_.seekg(0);

  uint32_t magic;
  file_.read((char*)&magic, 4);
  if (magic == 0x46546C67) {
    // GLB file
    uint32_t version, size;
    file_.read((char*)&version, 4);
    file_.read((char*)&size, 4);
    if (version != 2)
      throw std::runtime_error("Wrong glTF version: " +
                               std::to_string(version));
    if (size > fileSize) throw std::runtime_error("File truncated");
    uint32_t dataLength, dataType;
    file_.read((char*)&dataLength, 4);
    file_.read((char*)&dataType, 4);
    if (dataType != 0x4E4F534A)
      throw std::runtime_error("Expected JSON segment");
    readJSON(dataLength);
    auto dataEnd = file_.tellg();
    if (dataEnd < fileSize) {
      uint32_t binLength, binType;
      file_.read((char*)&binLength, 4);
      file_.read((char*)&binType, 4);
      if (binType != 0x004E4942)
        throw std::runtime_error("Expected BIN segment");
      bufferStart_ = dataEnd + 8ll;
    }
    setupVulkanData();
  } else if (magic == 0x62706C67) {
    // GLPB file
    data_.ParseFromIstream(&file_);
    openBinFile();
  } else {
    // JSON file
    file_.seekg(0);
    readJSON(fileSize);
    openBinFile();
    setupVulkanData();
  }
}

void Gltf::openBinFile() {
  const gltf::Buffer& buf = data_.buffers(0);
  if (!buf.has_uri()) return;
  std::filesystem::path path = directory_ / buf.uri();
  file_ = std::ifstream(path, std::ios::binary);
  if (!file_.is_open())
    throw std::runtime_error("failed to open \"" + path.string() + "\"");
  bufferStart_ = 0;
}

void Gltf::setupVulkanData() {
  uint64_t outputPos = 0;

  for (gltf::Mesh& mesh : *data_.mutable_meshes()) {
    for (gltf::Primitive& prim : *mesh.mutable_primitives()) {
      auto* attr = prim.mutable_attributes();

      // Create Vulkan draw call info
      gltf::Pipeline pipeline;
      gltf::DrawCall* drawCall = mesh.add_draw_calls();

      const gltf::Accessor& accessor = data_.accessors(prim.indices());
      const gltf::BufferView& bufferview =
          data_.buffer_views(accessor.buffer_view());
      uint32_t indexSize = size(accessor.component_type());
      uint32_t stride = std::max(indexSize, bufferview.byte_stride());
      gltf::BufferRead read;
      read.set_buffer(0);
      read.set_buffer_offset(accessor.byte_offset() + bufferview.byte_offset());
      read.set_buffer_stride(stride);
      read.set_output_offset(outputPos);
      read.set_output_stride(indexSize);
      read.set_read_count(accessor.count());
      *data_.add_buffer_reads() = read;
      drawCall->set_index_buffer(0);
      drawCall->set_index_type(vulkanIndexType(accessor.component_type()));
      drawCall->set_index_offset(outputPos);
      drawCall->set_index_count(accessor.count());
      outputPos += indexSize * accessor.count();

      drawCall->set_material(prim.material());

      uint32_t attributesSize = 0;
      std::vector<gltf::BufferRead> toPack;
      std::vector<uint32_t> attributes;

      auto* refl = attr->GetReflection();
      auto* desc = attr->GetDescriptor();
      for (int i = 0; i < desc->field_count(); ++i) {
        auto field = attr->GetDescriptor()->field(i);
        if (!refl->HasField(*attr, field)) continue;

        const gltf::Accessor& accessor =
            data_.accessors(refl->GetUInt32(*attr, field));
        const gltf::BufferView& bufferview =
            data_.buffer_views(accessor.buffer_view());
        uint32_t attrSize = accessor.type() * size(accessor.component_type());
        uint32_t stride = std::max(attrSize, bufferview.byte_stride());

        gltf::BufferRead read;
        read.set_buffer(0);
        read.set_buffer_offset(accessor.byte_offset() +
                               bufferview.byte_offset());
        read.set_buffer_stride(stride);
        read.set_output_offset(outputPos + attributesSize);
        read.set_read_count(accessor.count());
        toPack.push_back(read);

        gltf::PipelineAttribute attrData;
        attrData.set_offset(attributesSize);
        attrData.set_format(
            vulkanFormat(accessor.type(), accessor.component_type()));
        attrData.set_location(
            static_cast<gltf::ShaderLocation>(field->number()));
        attributes.push_back(
            setFieldInsert(data_.mutable_attributes(), attrData));
        attributesSize += attrSize;
      }

      if (!attr->has_tangent() && attr->has_position() && attr->has_normal() &&
          attr->has_texcoord_0()) {
        drawCall->set_generate_tangents(true);
        gltf::PipelineAttribute attrData;
        attrData.set_offset(attributesSize);
        attrData.set_format(gltf::FORMAT_R32G32B32A32_SFLOAT);
        attrData.set_location(gltf::TANGENT);
        attributes.push_back(
            setFieldInsert(data_.mutable_attributes(), attrData));
        attributesSize += sizeof(glm::vec4);
      }

      gltf::PipelineBinding binding;
      std::sort(attributes.begin(), attributes.end());
      binding.mutable_attributes()->Add(attributes.begin(), attributes.end());
      binding.set_stride(attributesSize);
      uint32_t bindIndex = setFieldInsert(data_.mutable_bindings(), binding);

      pipeline.add_bindings(bindIndex);
      uint32_t pipelineIndex =
          setFieldInsert(data_.mutable_pipelines(), pipeline);
      drawCall->set_pipeline(pipelineIndex);

      gltf::DrawCall::DrawBinding* drawBind = drawCall->add_bindings();
      drawBind->set_buffer(0);
      drawBind->set_offset(outputPos);

      // Counts should all be the same
      outputPos += attributesSize * toPack[0].read_count();

      for (gltf::BufferRead& read : toPack) {
        read.set_output_stride(attributesSize);
        data_.mutable_buffer_reads()->Add(std::move(read));
      }
    }
  }

  std::sort(data_.mutable_buffer_reads()->begin(),
            data_.mutable_buffer_reads()->end(), [](auto a, auto b) {
              return a.buffer_offset() < b.buffer_offset();
            });

  data_.mutable_buffers(0)->set_alloc_length(outputPos);
}

void Gltf::save(std::filesystem::path path) {
  std::filesystem::path dir = path;
  dir.remove_filename();
  std::filesystem::create_directories(dir);

  std::filesystem::path binpath = path;
  binpath.replace_extension("bin");
  std::ofstream bin(binpath, std::ios::binary | std::ios::out);
  bin << file_.rdbuf();

  long long start = bin.tellp();
  for (gltf::Image& image : *data_.mutable_images()) {
    if (!image.has_uri()) continue;
    std::filesystem::path imagepath = directory_ / image.uri();
    std::ifstream imagedata(imagepath, std::ios::binary | std::ios::in);
    bin << imagedata.rdbuf();
    long long end = bin.tellp();
    image.clear_uri();
    image.set_buffer_view(data_.buffer_views_size());
    gltf::BufferView* bufferview = data_.add_buffer_views();
    bufferview->set_buffer(0);
    bufferview->set_byte_offset(start);
    bufferview->set_byte_length(end - start);
    start = end;
  }
  file_ = std::ifstream(binpath, std::ios::ate | std::ios::binary);
  directory_ = dir;
  data_.mutable_buffers(0)->set_uri(binpath.filename());
  bufferStart_ = 0;

  std::ofstream file(path, std::ios::binary | std::ios_base::out);
  constexpr uint32_t magic = 0x62706C67;
  file.write((char*)&magic, 4);
  if (!data_.SerializeToOstream(&file))
    throw std::runtime_error(strerror(errno));
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
  return data_.buffers(0).alloc_length();
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
  std::ifstream binfile;
  std::ifstream* file;
  if (buf.has_uri()) {
    std::filesystem::path path = directory_ / buf.uri();
    binfile = std::ifstream(path, std::ios::binary);
    if (!binfile.is_open())
      throw std::runtime_error("failed to open \"" + path.string() + "\"");
    file = &binfile;
  } else {
    file_.seekg(bufferStart_);
    file = &file_;
  }

  uint64_t current = 0;
  for (const gltf::BufferRead& read : data_.buffer_reads()) {
    uint64_t size = std::min(read.buffer_stride(), read.output_stride());
    for (uint64_t i = 0; i < read.read_count(); ++i) {
      uint64_t start = read.buffer_offset() + read.buffer_stride() * i;
      uint64_t outstart = read.output_offset() + read.output_stride() * i;
      if (current != start)  // Should be rare
        file->seekg(start - current, std::ios_base::cur);
      file->read(output + outstart, size);
      current = start + size;
    }
  }

  // Generate tangents if needed
  for (const gltf::Mesh& mesh : data_.meshes()) {
    for (const gltf::DrawCall& drawCall : mesh.draw_calls()) {
      if (!drawCall.generate_tangents()) continue;
      auto tangents =
          getBufferRef<glm::vec4>(data_, output, drawCall, gltf::TANGENT,
                                  gltf::FORMAT_R32G32B32A32_SFLOAT);
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

template <class T>
uint32_t texIndex(const gltf::Gltf& data, const T& info) {
  return data.textures(info.index()).source();
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
    const auto& pbr = mat.pbr_metallic_roughness();
    Uniform u;
    if (pbr.base_color_factor_size() == 4)
      u.baseColorFactor_ = glm::make_vec4(pbr.base_color_factor().data());
    if (pbr.has_base_color_texture())
      u.baseColorTexture = texIndex(data_, pbr.base_color_texture());
    if (pbr.has_metallic_roughness_texture())
      u.metallicRoughnessTexture =
          texIndex(data_, pbr.metallic_roughness_texture());
    if (mat.has_normal_texture())
      u.normalTexture = texIndex(data_, mat.normal_texture());

    size_t offset = materialUniformOffset(matIndex++);
    std::copy_n((char*)&u, sizeof u, output + offset);
  }
}

struct StbIoData {
  std::ifstream& file_;
  size_t current_;
  size_t end_;
  int read(char* data, int size) {
    int toRead = std::min(int(end_ - current_), size);
    current_ += toRead;
    file_.read(data, toRead);
    return toRead;
  }
  void skip(int n) {
    int toRead = std::min(int(end_ - current_), n);
    current_ += toRead;
    file_.seekg(toRead, std::ios_base::cur);
  }
  int eof() { return current_ == end_; }
};

constexpr stbi_io_callbacks io_callbacks = {
    [](void* user, char* data, int size) {
      return ((StbIoData*)user)->read(data, size);
    },
    [](void* user, int n) { ((StbIoData*)user)->skip(n); },
    [](void* user) { return ((StbIoData*)user)->eof(); }};

std::vector<Pixels> Gltf::getImages() const {
  std::vector<Pixels> result;
  for (const gltf::Image& image : data_.images()) {
    int width, height, channels;
    unsigned char* data;
    if (image.has_uri()) {
      std::filesystem::path path = directory_ / image.uri();
      //    std::filesystem::path path = "Textures/checker.png";
      data =
          stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
      if (!data)
        throw std::runtime_error(std::string("stbi_load: ") +
                                 stbi_failure_reason() + " " + path.string());
    } else if (image.has_buffer_view()) {
      const gltf::BufferView& bufferView =
          data_.buffer_views(image.buffer_view());
      size_t current = bufferStart_ + bufferView.byte_offset();
      size_t end = current + bufferView.byte_length();
      file_.seekg(current);
      StbIoData ioData = {file_, current, end};
      data = stbi_load_from_callbacks(&io_callbacks, &ioData, &width, &height,
                                      &channels, STBI_rgb_alpha);
      if (!data)
        throw std::runtime_error(std::string("stbi_load: ") +
                                 stbi_failure_reason());
    } else
      throw std::runtime_error("No image data");
    result.emplace_back(width, height, data);
  }
  return result;
}

Pixels::Pixels(int w, int h, unsigned char* d)
    : width_(w), height_(h), data_(d, stbi_image_free) {}
