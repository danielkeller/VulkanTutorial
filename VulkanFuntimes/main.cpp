//
//  main.cpp
//  VulkanFuntimes
//
//  Created by Daniel Keller on 3/15/21.
//

#include <iostream>
#include <fstream>
#include <chrono>
#include "vulkan/vulkan.hpp"
#include "GLFW/glfw3.h"

void throwFail(const char *context, const vk::Result &result) {
  if (result != vk::Result::eSuccess)
    vk::throwResultException(result, "context");
}

constexpr vk::Format presentFormat = vk::Format::eB8G8R8A8Srgb;
constexpr uint32_t kMaxImagesInFlight = 2;

GLFWwindow *gWindow = nullptr;           // Owned by Window
bool gWindowSizeChanged = false;         // Owned by Window
vk::Instance gInstance;                  // Owned by Instance
vk::PhysicalDevice gPhysicalDevice;      // Owned by Instance
vk::SurfaceKHR gSurface;                 // Owned by Surface
vk::Device gDevice;                      // Owned by Device
uint32_t gGraphicsQueueFamilyIndex = 0;  // Owned by Device
vk::Queue gGraphicsQueue;                // Owned by Device

vk::SwapchainKHR gSwapchain;                           // Owned by Swapchain
uint32_t gSwapchainImageCount;                         // Owned by Swapchain
std::vector<vk::Image> gSwapchainImages;               // Owned by Swapchain
std::vector<vk::ImageView> gSwapchainImageViews;       // Owned by Swapchain
vk::Extent2D gSwapchainExtent;                         // Owned by Swapchain
vk::Viewport gViewport;                                // Owned by Swapchain
vk::Rect2D gScissor;                                   // Owned by Swapchain
std::vector<vk::Fence> gFrameFences;                   // Owned by Semaphores
vk::Fence gAcquireFence;                               // Owned by Semaphores
std::vector<vk::Semaphore> gImageAvailableSemaphores;  // Owned by Semaphores
std::vector<vk::Semaphore> gRenderFinishedSemaphores;  // Owned by Semaphores
std::vector<vk::Framebuffer> gFramebuffers;            // Owned by Framebuffers

vk::RenderPass gRenderPass;    // Owned by RenderPass
vk::CommandPool gCommandPool;  // Owned by CommandPool

void windowSizeCallback(GLFWwindow *, int, int) { gWindowSizeChanged = true; }

struct Window {
  Window() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    gWindow = glfwCreateWindow(800, 600, "Hello World", nullptr, nullptr);
    glfwSetFramebufferSizeCallback(gWindow, windowSizeCallback);
  }
  ~Window() {
    glfwDestroyWindow(gWindow);
    glfwTerminate();
    gWindow = nullptr;
  }
};

struct Instance {
  Instance();
  ~Instance();
};
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
      "VK_LAYER_KHRONOS_validation"};

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
}
Instance::~Instance() {
  gInstance.destroy();
  gInstance = nullptr;
  gPhysicalDevice = nullptr;
}

struct Surface {
  Surface();
  ~Surface();
};
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

struct Device {
  Device();
  ~Device();
};
Device::Device() {
  std::initializer_list<float> priorities = {1.f};
  gGraphicsQueueFamilyIndex = graphicsQueueFamily();
  std::initializer_list<vk::DeviceQueueCreateInfo> queues = {
      {/*flags=*/{}, gGraphicsQueueFamilyIndex, priorities}};

  std::vector<const char *> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  for (const auto &ext : gPhysicalDevice.enumerateDeviceExtensionProperties())
    if (ext.extensionName == std::string_view("VK_KHR_portability_subset"))
      extensions.push_back("VK_KHR_portability_subset");

  gDevice = gPhysicalDevice.createDevice({/*flags=*/{}, queues,
                                          /*pEnabledLayerNames=*/{},
                                          /*extensions=*/extensions});

  gGraphicsQueue = gDevice.getQueue(gGraphicsQueueFamilyIndex,
                                    /*queueIndex=*/0);
}
Device::~Device() {
  gDevice.destroy();
  gDevice = nullptr;
  gGraphicsQueue = nullptr;
}

struct CommandPool {
  CommandPool();
  ~CommandPool();
};
CommandPool::CommandPool() {
  gCommandPool =
      gDevice.createCommandPool({/*flags=*/{}, gGraphicsQueueFamilyIndex});
}
CommandPool::~CommandPool() { gDevice.destroy(gCommandPool); }

vk::Extent2D windowExtent() {
  int width, height;
  glfwGetFramebufferSize(gWindow, &width, &height);
  return vk::Extent2D(width, height);
}

vk::ShaderModule readShader(const std::string &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open file \"" + filename + "\"");
  size_t fileSize = (size_t)file.tellg();
  std::vector<uint32_t> buffer(fileSize / 4);
  file.seekg(0);
  file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
  return gDevice.createShaderModule({vk::ShaderModuleCreateFlags(), buffer});
}

struct RenderPass {
  RenderPass();
  ~RenderPass();
};
RenderPass::RenderPass() {
  vk::AttachmentDescription colorAttachment(
      /*flags=*/{}, presentFormat, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
      /*stencil*/ vk::AttachmentLoadOp::eDontCare,
      vk::AttachmentStoreOp::eDontCare,
      /*initialLayout=*/vk::ImageLayout::eUndefined,
      /*finalLayout=*/vk::ImageLayout::ePresentSrcKHR);
  vk::AttachmentReference colorRef(
      /*attachment=*/0, vk::ImageLayout::eColorAttachmentOptimal);
  // Attachment ref index corresponds to layout number in shader
  vk::SubpassDescription subpass(
      /*flags=*/{}, vk::PipelineBindPoint::eGraphics,
      /*inputAttachments=*/{},
      /*colorAttachments=*/colorRef);
  // Don't write to the image until the presenter is done with it
  vk::SubpassDependency colorWriteDependency(
      /*src=*/VK_SUBPASS_EXTERNAL, /*dstSubpass=*/0,
      /*src=*/vk::PipelineStageFlagBits::eColorAttachmentOutput,
      /*dst=*/vk::PipelineStageFlagBits::eColorAttachmentOutput,
      /*src=*/vk::AccessFlags(),
      /*dst=*/vk::AccessFlagBits::eColorAttachmentWrite);
  gRenderPass = gDevice.createRenderPass(
      {/*flags=*/{}, colorAttachment, subpass, colorWriteDependency});
}
RenderPass::~RenderPass() { gDevice.destroy(gRenderPass); }

struct Swapchain {
  Swapchain();
  ~Swapchain();
  uint32_t frameNum_ = 0;
  uint32_t currentImage_ = 0;
  std::tuple<uint32_t, vk::Semaphore> getFirstImage();
  std::tuple<uint32_t, vk::Semaphore> getNextImage(
      vk::Semaphore renderFinishedSemaphore);
};
Swapchain::Swapchain() {
  vk::SurfaceCapabilitiesKHR caps =
      gPhysicalDevice.getSurfaceCapabilitiesKHR(gSurface);
  // The min image count is the minimum number of images in the swapchain for
  // the application to be able to eventually aquire one of them
  uint32_t requestedImages = kMaxImagesInFlight + caps.minImageCount - 1;
  gSwapchainExtent = windowExtent();
  gViewport =
      vk::Viewport(0, 0, gSwapchainExtent.width, gSwapchainExtent.height,
                   /*minZ=*/0., /*maxZ=*/1.);
  gScissor = vk::Rect2D(vk::Offset2D(0, 0), gSwapchainExtent);

  gSwapchain = gDevice.createSwapchainKHR(
      {/*flags=*/{}, gSurface, requestedImages, presentFormat,
       vk::ColorSpaceKHR::eSrgbNonlinear, gSwapchainExtent,
       /*imageArrayLayers=*/1, vk::ImageUsageFlagBits::eColorAttachment,
       vk::SharingMode::eExclusive, /*queueFamilyIndices=*/{},
       vk::SurfaceTransformFlagBitsKHR::eIdentity,
       vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eFifo,
       /*clipped=*/true});

  gSwapchainImages = gDevice.getSwapchainImagesKHR(gSwapchain);
  gSwapchainImageCount = (uint32_t)gSwapchainImages.size();
  gSwapchainImageViews.clear();
  for (vk::Image image : gSwapchainImages) {
    gSwapchainImageViews.push_back(gDevice.createImageView(
        {/*flags=*/{}, image, vk::ImageViewType::e2D, presentFormat,
         vk::ComponentMapping(),
         vk::ImageSubresourceRange(
             vk::ImageAspectFlagBits::eColor, /*baseMipLevel=*/0,
             /*levelCount=*/1, /*baseArrayLayer=*/0, /*layerCount=*/1)}));
  }
}

void checkResizeOrThrowFail(const char *context, vk::Result result) {
  if (result == vk::Result::eSuboptimalKHR ||
      result == vk::Result::eErrorOutOfDateKHR)
    gWindowSizeChanged = true;
  else
    throwFail(context, result);
}

std::tuple<uint32_t, vk::Semaphore> Swapchain::getFirstImage() {
  vk::Semaphore imageAvailableSemaphore = gImageAvailableSemaphores[frameNum_];
  vk::ResultValue<uint32_t> imageIndex_or = gDevice.acquireNextImageKHR(
      gSwapchain, UINT64_MAX, imageAvailableSemaphore,
      /*fence=*/nullptr);
  checkResizeOrThrowFail("acquireNextImageKHR", imageIndex_or.result);

  return {currentImage_ = imageIndex_or.value, imageAvailableSemaphore};
}
std::tuple<uint32_t, vk::Semaphore> Swapchain::getNextImage(
    vk::Semaphore renderFinishedSemaphore) {
  frameNum_ = (frameNum_ + 1) % kMaxImagesInFlight;
  vk::Semaphore imageAvailableSemaphore = gImageAvailableSemaphores[frameNum_];
  vk::ResultValue<uint32_t> imageIndex_or = gDevice.acquireNextImageKHR(
      gSwapchain, UINT64_MAX, imageAvailableSemaphore,
      /*fence=*/nullptr);
  checkResizeOrThrowFail("acquireNextImageKHR", imageIndex_or.result);

  checkResizeOrThrowFail(
      "presentKHR", gGraphicsQueue.presentKHR(
                        {renderFinishedSemaphore, gSwapchain, currentImage_}));

  return {currentImage_ = imageIndex_or.value, imageAvailableSemaphore};
}

Swapchain::~Swapchain() {
  for (vk::ImageView imageView : gSwapchainImageViews)
    gDevice.destroy(imageView);
  gSwapchainImages.clear();
  gSwapchainImageViews.clear();
  gDevice.destroy(gSwapchain);
  gSwapchain = nullptr;
}

struct Semaphores {
  Semaphores();
  ~Semaphores();
};
Semaphores::Semaphores() {
  gAcquireFence = gDevice.createFence({});
  for (size_t i = 0; i < gSwapchainImageCount; ++i) {
    gFrameFences.push_back(gDevice.createFence({}));
    gRenderFinishedSemaphores.push_back(gDevice.createSemaphore({}));
    gImageAvailableSemaphores.push_back(gDevice.createSemaphore({}));
  }
}
Semaphores::~Semaphores() {
  gDevice.destroy(gAcquireFence);
  for (vk::Fence fence : gFrameFences) gDevice.destroy(fence);
  for (vk::Semaphore sem : gRenderFinishedSemaphores) gDevice.destroy(sem);
  for (vk::Semaphore sem : gImageAvailableSemaphores) gDevice.destroy(sem);
  gFrameFences.clear();
  gRenderFinishedSemaphores.clear();
  gImageAvailableSemaphores.clear();
}

struct Framebuffers {
  Framebuffers();
  ~Framebuffers();
};
Framebuffers::Framebuffers() {
  for (vk::ImageView imageView : gSwapchainImageViews) {
    gFramebuffers.push_back(gDevice.createFramebuffer(
        {/*flags=*/{}, gRenderPass, imageView, gSwapchainExtent.width,
         gSwapchainExtent.height, /*layers=*/1}));
  }
}
Framebuffers::~Framebuffers() {
  for (vk::Framebuffer fb : gFramebuffers) gDevice.destroy(fb);
  gFramebuffers.clear();
}

struct Pipeline {
  vk::PipelineLayout layout_;
  vk::Pipeline pipeline_;
  Pipeline();
  ~Pipeline();
};
Pipeline::Pipeline() {
  // Dynamic viewport
  vk::Viewport viewport;
  vk::Rect2D scissor;
  vk::PipelineViewportStateCreateInfo viewportState(
      /*flags=*/{}, viewport, scissor);
  auto dynamicStates = {vk::DynamicState::eViewport,
                        vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState(/*flags=*/{}, dynamicStates);

  // Fixed function stuff
  vk::PipelineVertexInputStateCreateInfo vertexInput{
      /*flags=*/{}, /*bindings=*/{}, /*attributes=*/{}};
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      /*flags=*/{}, /*topology=*/vk::PrimitiveTopology::eTriangleList};
  vk::PipelineRasterizationStateCreateInfo rasterization(
      /*flags=*/{}, /*depthClampEnable=*/false,
      /*rasterizerDiscardEnable=*/false, vk::PolygonMode::eFill,
      vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise,
      /*depthBiasEnable=*/false, {}, {}, {},
      /*lineWidth=*/1);
  vk::PipelineMultisampleStateCreateInfo multisample;
  vk::PipelineColorBlendAttachmentState colorBlend1;
  colorBlend1.colorWriteMask = ~vk::ColorComponentFlags();  // All
  vk::PipelineColorBlendStateCreateInfo colorBlend(
      /*flags=*/{}, /*logicOpEnable=*/false, /*logicOp=*/{}, colorBlend1);

  layout_ = gDevice.createPipelineLayout({vk::PipelineLayoutCreateFlags(),
                                          /*setLayouts=*/{},
                                          /*pushConstantRanges=*/{}});

  vk::ShaderModule vert = readShader("triangle.vert");
  vk::ShaderModule frag = readShader("test.frag");

  std::initializer_list<vk::PipelineShaderStageCreateInfo> stages = {
      {/*flags=*/{}, vk::ShaderStageFlagBits::eVertex, vert, /*pName=*/"main"},
      {/*flags=*/{}, vk::ShaderStageFlagBits::eFragment, frag,
       /*pName=*/"main"}};

  vk::ResultValue<std::vector<vk::Pipeline>> pipelines_or =
      gDevice.createGraphicsPipelines(
          /*pipelineCache=*/nullptr,
          {{/*flags=*/{}, stages, &vertexInput, &inputAssembly,
            /*tesselation=*/{}, &viewportState, &rasterization, &multisample,
            /*depthStencil=*/{}, &colorBlend, &dynamicState, layout_,
            gRenderPass, /*subpass=*/0, /*basePipeline=*/{}}});

  gDevice.destroy(vert);
  gDevice.destroy(frag);

  throwFail("vkCreateGraphicsPipelines", pipelines_or.result);
  if (pipelines_or.value.empty())
    throw std::runtime_error("No pipeline returned???");
  pipeline_ = pipelines_or.value[0];
}
Pipeline::~Pipeline() {
  gDevice.destroy(pipeline_);
  gDevice.destroy(layout_);
}

struct FpsCount {
  static constexpr uint64_t kInterval = 200;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_ =
      std::chrono::high_resolution_clock::now();
  void count(uint64_t frame);
};
void FpsCount::count(uint64_t frame) {
  if (!frame || frame % kInterval != 0) return;
  auto end = std::chrono::high_resolution_clock::now();
  auto timeus =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
  auto fps = (kInterval * 1000000) / timeus.count();
  std::cerr << fps << " FPS\n";
  start_ = end;
}

struct CommandBuffers {
  std::vector<vk::CommandBuffer> commandBuffers_;
  CommandBuffers(vk::Pipeline pipeline);
  // Buffers are freed by the pool
};
CommandBuffers::CommandBuffers(vk::Pipeline pipeline) {
  uint32_t nBuffers = (uint32_t)gFramebuffers.size();
  commandBuffers_ = gDevice.allocateCommandBuffers(
      {gCommandPool, vk::CommandBufferLevel::ePrimary, nBuffers});

  for (uint32_t i = 0; i < nBuffers; ++i) {
    vk::CommandBuffer buf = commandBuffers_[i];
    buf.begin(vk::CommandBufferBeginInfo());
    buf.setViewport(/*index=*/0, gViewport);
    buf.setScissor(/*index=*/0, gScissor);
    vk::ClearValue clearColor(
        vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 1}));
    buf.beginRenderPass(
        {gRenderPass, gFramebuffers[i], /*renderArea=*/
         vk::Rect2D(/*offset=*/{0, 0}, gSwapchainExtent), clearColor},
        vk::SubpassContents::eInline);
    buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    buf.draw(/*vertexCount=*/3, /*instanceCount=*/1, /*firstVertex=*/0,
             /* firstInstance=*/0);
    buf.endRenderPass();
    buf.end();
  }
}

void mainApp() {
  Window window;
  Instance instance;
  Surface surface;
  Device device;
  RenderPass renderPass;
  Pipeline pipeline1;
  FpsCount fpsCount;

  uint64_t frame = 0;

  while (!glfwWindowShouldClose(gWindow)) {
    uint64_t swapchainFrame = 0;
    Swapchain swapchain;
    Semaphores semaphores;
    Framebuffers framebuffers;
    CommandPool commandPool;
    CommandBuffers commandBuffers1(pipeline1.pipeline_);
    gWindowSizeChanged = false;
    std::cerr << "resize " << gSwapchainExtent.width << "x"
              << gSwapchainExtent.height << "\n";

    auto [imageIndex, imageAvailableSemaphore] = swapchain.getFirstImage();
    while (!glfwWindowShouldClose(gWindow) && !gWindowSizeChanged) {
      // Pause while the window is in the background
      while (!glfwGetWindowAttrib(gWindow, GLFW_FOCUSED))
        glfwPollEvents();
      
      if (swapchainFrame > kMaxImagesInFlight) {
        throwFail("waitForFences",
                  gDevice.waitForFences(gFrameFences[imageIndex],
                                        /*waitAll=*/false,
                                        /*timeout=*/UINT64_MAX));
        gDevice.resetFences(gFrameFences[imageIndex]);
      }

      vk::PipelineStageFlags waitDestStage(
          vk::PipelineStageFlagBits::eColorAttachmentOutput);
      vk::CommandBuffer commandBuffer(
          commandBuffers1.commandBuffers_[imageIndex]);
      vk::Semaphore renderFinishedSemaphore =
          gRenderFinishedSemaphores[imageIndex];
      vk::SubmitInfo submit(/*wait=*/imageAvailableSemaphore, waitDestStage,
                            commandBuffer,
                            /*signal=*/renderFinishedSemaphore);

      gGraphicsQueue.submit({submit}, gFrameFences[imageIndex]);

      std::tie(imageIndex, imageAvailableSemaphore) =
          swapchain.getNextImage(renderFinishedSemaphore);

      ++swapchainFrame;
      fpsCount.count(frame);
      ++frame;
      glfwPollEvents();
    }
    gGraphicsQueue.waitIdle();
  }
}

int main(int argc, const char *argv[]) {
  try {
    mainApp();
  } catch (const vk::Error &exc) {
    std::cerr << exc.what() << '\n';
    return 2;
  } catch (const std::runtime_error &exc) {
    std::cerr << exc.what() << '\n';
    return 1;
  }

  return 0;
}
