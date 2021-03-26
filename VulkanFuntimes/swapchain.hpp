#ifndef swapchain_hpp
#define swapchain_hpp

#include "vulkan/vulkan.hpp"

constexpr vk::Format kPresentFormat = vk::Format::eB8G8R8A8Srgb;
constexpr vk::Format kDepthStencilFormat = vk::Format::eD32SfloatS8Uint;
constexpr uint32_t kMaxImagesInFlight = 2;

extern vk::SwapchainKHR gSwapchain;
extern uint32_t gSwapchainImageCount;
extern uint32_t gSwapchainCurrentImage;
extern std::vector<vk::Image> gSwapchainImages;
extern std::vector<vk::ImageView> gSwapchainImageViews;
extern vk::Extent2D gSwapchainExtent;
extern vk::Viewport gViewport;
extern vk::Rect2D gScissor;

struct Swapchain {
  Swapchain() { resizeToWindow(); }
  ~Swapchain() { destroy(); }
  uint32_t frameNum_ = 0;
  void resizeToWindow();
  void destroy();
  vk::Semaphore getFirstImage();
  vk::Semaphore getNextImage(vk::Semaphore renderFinishedSemaphore);
};

struct DepthStencil {
  vk::DeviceMemory memory_;
  vk::Image image_;
  DepthStencil();
  ~DepthStencil();
};

extern std::vector<vk::Fence> gFrameFences;
extern std::vector<vk::Fence> gInFlightFences;
extern std::vector<vk::Semaphore> gImageAvailableSemaphores;
extern std::vector<vk::Semaphore> gRenderFinishedSemaphores;

struct Semaphores {
  Semaphores();
  ~Semaphores();
};

extern std::vector<vk::Framebuffer> gFramebuffers;

struct Framebuffers {
  Framebuffers();
  ~Framebuffers();
};

extern vk::RenderPass gRenderPass;
struct RenderPass {
  RenderPass();
  ~RenderPass();
};

#endif /* swapchain_hpp */
