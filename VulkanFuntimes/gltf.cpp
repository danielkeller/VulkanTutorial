#include "gltf.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/message_differencer.h>
#include <filesystem>
#include "stb_image.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/gtx/string_cast.hpp"

#include "driver.hpp"
#include "mikktspace.hpp"
#include "util.hpp"

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

void Gltf::setupVulkanData() {
  uint64_t offset = 0;
  for (gltf::Mesh& mesh : *data_.mutable_meshes()) {
    for (gltf::Primitive& prim : *mesh.mutable_primitives()) {
      if (!prim.attributes().has_position()) continue;
      const auto& inds = data_.accessors(prim.indices());
      const auto& verts = data_.accessors(prim.attributes().position());
      prim.set_index_offset(offset);
      offset += inds.count() * sizeof(Index);
      prim.set_vertex_offset(offset);
      offset += verts.count() * sizeof(Vertex);
    }
  }
  data_.mutable_buffers(0)->set_alloc_length(offset);
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

vk::DeviceSize Gltf::bufferSize() const {
  return data_.buffers(0).alloc_length();
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

  auto readAttr = [&](uint32_t acc, auto buf) -> uint32_t {
    const auto& accessor = data_.accessors(acc);
    const auto& bufferview = data_.buffer_views(accessor.buffer_view());
    uint64_t offs = bufferview.byte_offset() + accessor.byte_offset();
    if (current != offs) {
      file->seekg(offs - current, std::ios::cur);
      current = offs;
    }
    for (uint64_t i = 0; i < accessor.count(); ++i)
      file->read((char*)&(buf[i]), sizeof(buf[i]));
    current += accessor.count() * sizeof(buf[0]);
    return accessor.count();
  };

  for (const gltf::Mesh& mesh : data_.meshes()) {
    for (const gltf::Primitive& prim : mesh.primitives()) {
      const auto& attrs = prim.attributes();
      if (!attrs.has_position()) continue;

      Index* inds = (Index*)(output + prim.index_offset());
      Vertex* verts = (Vertex*)(output + prim.vertex_offset());
      uint32_t nInds = readAttr(prim.indices(), inds);
      readAttr(attrs.position(), BufferRef(verts, &Vertex::position));
      if (attrs.has_normal())
        readAttr(attrs.normal(), BufferRef(verts, &Vertex::normal));
      if (attrs.has_texcoord_0())
        readAttr(attrs.texcoord_0(), BufferRef(verts, &Vertex::texcoord));
      if (attrs.has_tangent())
        readAttr(attrs.tangent(), BufferRef(verts, &Vertex::tangent));
      else
        makeTangents(nInds, inds, verts);
    }
  }
}

void Gltf::save(std::filesystem::path path) {
  std::filesystem::path dir = path;
  dir.remove_filename();
  std::filesystem::create_directories(dir);

  std::filesystem::path binpath = path;
  binpath.replace_extension("bin");
  std::ofstream bin(binpath, std::ios::binary | std::ios::out);

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
