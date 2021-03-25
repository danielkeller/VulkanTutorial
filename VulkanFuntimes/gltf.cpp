//
//  gltf.cpp
//  VulkanFuntimes
//
//  Created by Daniel Keller on 3/25/21.
//

#include "gltf.hpp"
#include <google/protobuf/util/json_util.h>

gltf::File load(std::string path) {
  gltf::File result;
  google::protobuf::util::JsonStringToMessage(R"({"foo":"bar"})", &result);
  return result;
}

