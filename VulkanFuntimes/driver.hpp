#ifndef driver_hpp
#define driver_hpp

#include "vulkan/vulkan.hpp"
#include "GLFW/glfw3.h"
#include <chrono>

extern GLFWwindow *gWindow;
extern bool gWindowSizeChanged;
struct Window {
  Window();
  ~Window();
};

extern vk::Instance gInstance;
extern vk::PhysicalDevice gPhysicalDevice;
extern vk::PhysicalDeviceProperties gPhysicalDeviceProperties;
struct Instance {
  Instance();
  ~Instance();
};

extern vk::SurfaceKHR gSurface;
struct Surface {
  Surface();
  ~Surface();
};

extern vk::Device gDevice;
extern uint32_t gGraphicsQueueFamilyIndex;
extern vk::Queue gGraphicsQueue;
struct Device {
  Device();
  ~Device();
};

uint32_t getMemoryFor(vk::MemoryRequirements memoryRequirements,
                      vk::MemoryPropertyFlags memFlagRequirements);

extern uint64_t gFrame;
struct FpsCount {
  static constexpr uint64_t kInterval = 200;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_ =
      std::chrono::high_resolution_clock::now();
  void count();
};

#endif /* driver_hpp */
