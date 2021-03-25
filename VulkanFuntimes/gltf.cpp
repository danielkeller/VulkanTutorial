#include "gltf.hpp"

#include <google/protobuf/util/json_util.h>
#include <filesystem>
#include <fstream>

Gltf::Gltf(std::filesystem::path path) {
  directory_ = path.remove_filename();
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open file \"" + path.string()
                             + "\"");
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
    throw std::runtime_error(parseStatus.message().as_string());
}

