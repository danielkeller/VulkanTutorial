#include "driver.hpp"

#include <iostream>

GLFWwindow *gWindow = nullptr;
bool gWindowSizeChanged = false;

void windowSizeCallback(GLFWwindow *, int, int) { gWindowSizeChanged = true; }
Window::Window() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  gWindow = glfwCreateWindow(800, 600, "Hello World", nullptr, nullptr);
  glfwSetFramebufferSizeCallback(gWindow, windowSizeCallback);
}
Window::~Window() {
  glfwDestroyWindow(gWindow);
  glfwTerminate();
  gWindow = nullptr;
}

vk::Instance gInstance;
vk::PhysicalDevice gPhysicalDevice;
vk::PhysicalDeviceProperties gPhysicalDeviceProperties;

Instance::Instance() {
  vk::ApplicationInfo appInfo(/*pApplicationName=*/"Hello Triangle",
                              /*applicationVersion=*/VK_MAKE_VERSION(1, 0, 0),
                              /*pEngineName=*/"Dan's Awesome Engine",
                              /*engineVersion=*/VK_MAKE_VERSION(1, 0, 0),
                              /*apiVersion=*/VK_API_VERSION_1_0);
  uint32_t nGlfwExts;
  const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&nGlfwExts);
  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + nGlfwExts);
  extensions.push_back("VK_KHR_get_physical_device_properties2");

  std::initializer_list<const char *> enabledLayerNames = {
#if DEBUG
    "VK_LAYER_KHRONOS_validation"
#endif
  };

  vk::InstanceCreateInfo instInfo(vk::InstanceCreateFlags(), &appInfo,
                                  enabledLayerNames, extensions);
  gInstance = vk::createInstance(instInfo);

  vk::DeviceSize biggest_device = 0;
  for (const vk::PhysicalDevice &device :
       gInstance.enumeratePhysicalDevices()) {
    vk::DeviceSize device_size = 0;
    for (const auto &heap : device.getMemoryProperties().memoryHeaps)
      device_size += heap.size;
    if (device_size > biggest_device) {
      biggest_device = device_size;
      gPhysicalDevice = device;
    }
  }

  gPhysicalDeviceProperties = gPhysicalDevice.getProperties();
}
Instance::~Instance() {
  gInstance.destroy();
  gInstance = nullptr;
  gPhysicalDevice = nullptr;
}

vk::SurfaceKHR gSurface;

Surface::Surface() {
  VkSurfaceKHR surface;
  if (glfwCreateWindowSurface(gInstance, gWindow, nullptr, &surface) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create window surface");
  }
  gSurface = surface;
}
Surface::~Surface() { gInstance.destroy(gSurface); }

uint32_t graphicsQueueFamily() {
  std::vector<vk::QueueFamilyProperties> families =
      gPhysicalDevice.getQueueFamilyProperties();
  for (uint32_t i = 0; i < families.size(); ++i) {
    if (!(families[i].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
    if (!gPhysicalDevice.getSurfaceSupportKHR(i, gSurface)) continue;
    return i;
  }
  throw std::runtime_error("Device has no usable graphics queue family");
}

vk::Device gDevice;
uint32_t gGraphicsQueueFamilyIndex = 0;
vk::Queue gGraphicsQueue;

Device::Device() {
  std::initializer_list<float> priorities = {1.f};
  gGraphicsQueueFamilyIndex = graphicsQueueFamily();
  std::initializer_list<vk::DeviceQueueCreateInfo> queues = {
      {/*flags=*/{}, gGraphicsQueueFamilyIndex, priorities}};

  std::vector<const char *> extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_KHR_MAINTENANCE3_EXTENSION_NAME,
      VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
  };
  for (const auto &ext : gPhysicalDevice.enumerateDeviceExtensionProperties())
    if (ext.extensionName == std::string_view("VK_KHR_portability_subset"))
      extensions.push_back("VK_KHR_portability_subset");

  vk::PhysicalDeviceFeatures features;
  features.setSamplerAnisotropy(true);
  gDevice = gPhysicalDevice.createDevice({/*flags=*/{}, queues,
                                          /*pEnabledLayerNames=*/{}, extensions,
                                          &features});

  gGraphicsQueue = gDevice.getQueue(gGraphicsQueueFamilyIndex,
                                    /*queueIndex=*/0);
}
Device::~Device() {
  gDevice.destroy();
  gDevice = nullptr;
  gGraphicsQueue = nullptr;
}

uint32_t getMemoryFor(vk::MemoryRequirements memoryRequirements,
                      vk::MemoryPropertyFlags memFlagRequirements) {
  vk::PhysicalDeviceMemoryProperties memProperties =
      gPhysicalDevice.getMemoryProperties();

  for (uint32_t i = 0; i < memProperties.memoryTypes.size(); ++i) {
    if (memoryRequirements.memoryTypeBits & (1 << i) &&
        memProperties.memoryTypes[i].propertyFlags & memFlagRequirements)
      return i;
  }
  throw std::runtime_error("No memory type found for buffer");
}

uint64_t gFrame = 0;
void FpsCount::count() {
  if (!gFrame || gFrame % kInterval != 0) return;
  auto end = std::chrono::high_resolution_clock::now();
  auto timeus =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
  auto fps = (kInterval * 1000000) / timeus.count();
  std::cerr << fps << " FPS\n";
  start_ = end;
  ++gFrame;
}
