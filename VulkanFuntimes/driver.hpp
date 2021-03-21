#ifndef driver_hpp
#define driver_hpp

#include "vulkan/vulkan.hpp"
#include "GLFW/glfw3.h"

extern GLFWwindow *gWindow;
extern bool gWindowSizeChanged;
struct Window {
  Window();
  ~Window();
};

extern vk::Instance gInstance;
extern vk::PhysicalDevice gPhysicalDevice;
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

#endif /* driver_hpp */
