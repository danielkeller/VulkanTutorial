#ifndef swapchain_hpp
#define swapchain_hpp

#include "vulkan/vulkan.hpp"

constexpr vk::Format kPresentFormat = vk::Format::eB8G8R8A8Srgb;
constexpr uint32_t kMaxImagesInFlight = 2;

extern vk::SwapchainKHR gSwapchain;
extern uint32_t gSwapchainCurrentImage;
extern uint32_t gFrameIndex;
extern std::vector<vk::Image> gSwapchainImages;
extern std::vector<vk::ImageView> gSwapchainImageViews;
extern vk::Extent2D gSwapchainExtent;
extern vk::Viewport gViewport;
extern vk::Rect2D gScissor;

struct Swapchain {
  Swapchain();
  ~Swapchain();
  vk::Semaphore getFirstImage();
  vk::Semaphore getNextImage(vk::Semaphore renderFinishedSemaphore);
};

extern std::array<vk::Fence, kMaxImagesInFlight> gFrameFences;
extern std::array<vk::Fence, kMaxImagesInFlight> gInFlightFences;
extern std::array<vk::Semaphore, kMaxImagesInFlight> gImageAvailableSemaphores;
extern std::array<vk::Semaphore, kMaxImagesInFlight> gRenderFinishedSemaphores;

struct Semaphores {
  Semaphores();
  ~Semaphores();
};

extern std::vector<vk::Framebuffer> gFramebuffers;

struct Framebuffers {
  Framebuffers();
  ~Framebuffers();
};

extern std::vector<vk::CommandBuffer> gBeginPass;
extern vk::CommandBuffer gEndPass;
struct PassCommandBuffers {
  vk::CommandPool commandPool_;
  PassCommandBuffers();
  ~PassCommandBuffers();
};

#endif /* swapchain_hpp */
