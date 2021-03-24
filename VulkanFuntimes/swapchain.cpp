#include "swapchain.hpp"

#include "util.hpp"
#include "driver.hpp"
#include "rendering.hpp"
#include "GLFW/glfw3.h"

vk::SwapchainKHR gSwapchain;
uint32_t gSwapchainCurrentImage;
uint32_t gFrameIndex = 0;
std::vector<vk::Image> gSwapchainImages;
std::vector<vk::ImageView> gSwapchainImageViews;
vk::Extent2D gSwapchainExtent;
vk::Viewport gViewport;
vk::Rect2D gScissor;

vk::Extent2D windowExtent() {
  int width, height;
  glfwGetFramebufferSize(gWindow, &width, &height);
  return vk::Extent2D(width, height);
}

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
      {/*flags=*/{}, gSurface, requestedImages, kPresentFormat,
       vk::ColorSpaceKHR::eSrgbNonlinear, gSwapchainExtent,
       /*imageArrayLayers=*/1, vk::ImageUsageFlagBits::eColorAttachment,
       vk::SharingMode::eExclusive, /*queueFamilyIndices=*/{},
       vk::SurfaceTransformFlagBitsKHR::eIdentity,
       vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eFifo,
       /*clipped=*/true});

  gSwapchainImages = gDevice.getSwapchainImagesKHR(gSwapchain);
  gSwapchainImageViews.clear();
  for (vk::Image image : gSwapchainImages) {
    gSwapchainImageViews.push_back(gDevice.createImageView(
        {/*flags=*/{}, image, vk::ImageViewType::e2D, kPresentFormat,
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

vk::Semaphore Swapchain::getFirstImage() {
  vk::Semaphore imageAvailableSemaphore =
      gImageAvailableSemaphores[gFrameIndex];
  vk::ResultValue<uint32_t> imageIndex_or = gDevice.acquireNextImageKHR(
      gSwapchain, UINT64_MAX, imageAvailableSemaphore,
      /*fence=*/nullptr);
  checkResizeOrThrowFail("acquireNextImageKHR", imageIndex_or.result);

  gSwapchainCurrentImage = imageIndex_or.value;
  return imageAvailableSemaphore;
}
vk::Semaphore Swapchain::getNextImage(vk::Semaphore renderFinishedSemaphore) {
  gFrameIndex = (gFrameIndex + 1) % kMaxImagesInFlight;
  vk::Semaphore imageAvailableSemaphore =
      gImageAvailableSemaphores[gFrameIndex];
  vk::ResultValue<uint32_t> imageIndex_or = gDevice.acquireNextImageKHR(
      gSwapchain, UINT64_MAX, imageAvailableSemaphore,
      /*fence=*/nullptr);
  checkResizeOrThrowFail("acquireNextImageKHR", imageIndex_or.result);

  checkResizeOrThrowFail("presentKHR", gGraphicsQueue.presentKHR(
                                           {renderFinishedSemaphore, gSwapchain,
                                            gSwapchainCurrentImage}));

  gSwapchainCurrentImage = imageIndex_or.value;
  return imageAvailableSemaphore;
}

Swapchain::~Swapchain() {
  for (vk::ImageView imageView : gSwapchainImageViews)
    gDevice.destroy(imageView);
  gSwapchainImages.clear();
  gSwapchainImageViews.clear();
  gDevice.destroy(gSwapchain);
  gSwapchain = nullptr;
}

std::array<vk::Fence, kMaxImagesInFlight> gFrameFences;
std::array<vk::Fence, kMaxImagesInFlight> gInFlightFences;
std::array<vk::Semaphore, kMaxImagesInFlight> gImageAvailableSemaphores;
std::array<vk::Semaphore, kMaxImagesInFlight> gRenderFinishedSemaphores;

Semaphores::Semaphores() {
  for (size_t i = 0; i < kMaxImagesInFlight; ++i) {
    gFrameFences[i] = gDevice.createFence({});
    gInFlightFences[i] = nullptr;
    gRenderFinishedSemaphores[i] = gDevice.createSemaphore({});
    gImageAvailableSemaphores[i] = gDevice.createSemaphore({});
  }
}
Semaphores::~Semaphores() {
  for (vk::Fence fence : gFrameFences) gDevice.destroy(fence);
  for (vk::Semaphore sem : gRenderFinishedSemaphores) gDevice.destroy(sem);
  for (vk::Semaphore sem : gImageAvailableSemaphores) gDevice.destroy(sem);
  gFrameFences = {};
  gInFlightFences = {};
  gRenderFinishedSemaphores = {};
  gImageAvailableSemaphores = {};
}

std::vector<vk::Framebuffer> gFramebuffers;

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

std::vector<vk::CommandBuffer> gBeginPass;
vk::CommandBuffer gEndPass;

PassCommandBuffers::PassCommandBuffers() {
  commandPool_ =
      gDevice.createCommandPool({/*flags=*/{}, gGraphicsQueueFamilyIndex});

  uint32_t nBuffers = (uint32_t)gFramebuffers.size();
  gBeginPass = gDevice.allocateCommandBuffers(
      {commandPool_, vk::CommandBufferLevel::ePrimary, nBuffers});

  for (uint32_t i = 0; i < nBuffers; ++i) {
    vk::CommandBuffer buf = gBeginPass[i];
    buf.begin(vk::CommandBufferBeginInfo());
    vk::ClearValue clearColor(
        vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 1}));
    buf.beginRenderPass(
        {gRenderPass, gFramebuffers[i], /*renderArea=*/
         vk::Rect2D(/*offset=*/{0, 0}, gSwapchainExtent), clearColor},
        vk::SubpassContents::eInline);
    buf.end();
  }

  gEndPass = gDevice.allocateCommandBuffers(
      {commandPool_, vk::CommandBufferLevel::ePrimary, 1})[0];
  gEndPass.begin(vk::CommandBufferBeginInfo());
  gEndPass.endRenderPass();
  gEndPass.end();
}

PassCommandBuffers::~PassCommandBuffers() { gDevice.destroy(commandPool_); }
