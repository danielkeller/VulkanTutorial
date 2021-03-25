#ifndef gltf_hpp
#define gltf_hpp

#include "gltf.pb.h"
#include <filesystem>

struct Gltf {
  Gltf(std::filesystem::path path);
  gltf::Gltf data_;
  std::filesystem::path directory_;
};

#endif /* gltf_hpp */
