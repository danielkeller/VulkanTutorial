#ifndef swapchain_hpp
#define swapchain_hpp

#include "vulkan/vulkan.hpp"

constexpr vk::Format kPresentFormat = vk::Format::eB8G8R8A8Srgb;
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
  Swapchain();
  ~Swapchain();
  uint32_t frameNum_ = 0;
  vk::Semaphore getFirstImage();
  vk::Semaphore getNextImage(vk::Semaphore renderFinishedSemaphore);
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

#endif /* swapchain_hpp */
