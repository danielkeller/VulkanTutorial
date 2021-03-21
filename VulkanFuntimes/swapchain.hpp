#ifndef swapchain_hpp
#define swapchain_hpp

#include "vulkan/vulkan.hpp"

constexpr vk::Format kPresentFormat = vk::Format::eB8G8R8A8Srgb;
constexpr uint32_t kMaxImagesInFlight = 2;

extern vk::SwapchainKHR gSwapchain;
extern uint32_t gSwapchainImageCount;
extern std::vector<vk::Image> gSwapchainImages;
extern std::vector<vk::ImageView> gSwapchainImageViews;
extern vk::Extent2D gSwapchainExtent;
extern vk::Viewport gViewport;
extern vk::Rect2D gScissor;

struct Swapchain {
  Swapchain();
  ~Swapchain();
  uint32_t frameNum_ = 0;
  uint32_t currentImage_ = 0;
  std::tuple<uint32_t, vk::Semaphore> getFirstImage();
  std::tuple<uint32_t, vk::Semaphore> getNextImage(
      vk::Semaphore renderFinishedSemaphore);
};

extern std::vector<vk::Fence> gFrameFences;
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

#endif /* swapchain_hpp */
