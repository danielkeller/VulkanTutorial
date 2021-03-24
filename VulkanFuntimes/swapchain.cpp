#include "swapchain.hpp"

#include "util.hpp"
#include "driver.hpp"
#include "rendering.hpp"
#include "GLFW/glfw3.h"

vk::SwapchainKHR gSwapchain;
uint32_t gSwapchainImageCount;
uint32_t gSwapchainCurrentImage;
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

void Swapchain::resizeToWindow() {
  if (gSwapchainExtent == windowExtent()) return;
  destroy();
  
  gSwapchainExtent = windowExtent();
  gViewport =
      vk::Viewport(0, 0, gSwapchainExtent.width, gSwapchainExtent.height,
                   /*minZ=*/0., /*maxZ=*/1.);
  gScissor = vk::Rect2D(vk::Offset2D(0, 0), gSwapchainExtent);
  
  vk::SurfaceCapabilitiesKHR caps =
      gPhysicalDevice.getSurfaceCapabilitiesKHR(gSurface);
  // The min image count is the minimum number of images in the swapchain for
  // the application to be able to eventually aquire one of them
  uint32_t requestedImages = kMaxImagesInFlight + caps.minImageCount - 1;

  gSwapchain = gDevice.createSwapchainKHR(
      {/*flags=*/{}, gSurface, requestedImages, kPresentFormat,
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
  frameNum_ = (frameNum_ + 1) % kMaxImagesInFlight;
  vk::Semaphore imageAvailableSemaphore = gImageAvailableSemaphores[frameNum_];
  vk::ResultValue<uint32_t> imageIndex_or = gDevice.acquireNextImageKHR(
      gSwapchain, UINT64_MAX, imageAvailableSemaphore,
      /*fence=*/nullptr);
  checkResizeOrThrowFail("acquireNextImageKHR", imageIndex_or.result);

  gSwapchainCurrentImage = imageIndex_or.value;
  return imageAvailableSemaphore;
}
vk::Semaphore Swapchain::getNextImage(vk::Semaphore renderFinishedSemaphore) {
  frameNum_ = (frameNum_ + 1) % kMaxImagesInFlight;
  vk::Semaphore imageAvailableSemaphore = gImageAvailableSemaphores[frameNum_];
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

void Swapchain::destroy() {
  for (vk::ImageView imageView : gSwapchainImageViews)
    gDevice.destroy(imageView);
  gSwapchainImages.clear();
  gSwapchainImageViews.clear();
  gDevice.destroy(gSwapchain);
  gSwapchain = nullptr;
}

std::vector<vk::Fence> gFrameFences;
std::vector<vk::Fence> gInFlightFences;
std::vector<vk::Semaphore> gImageAvailableSemaphores;
std::vector<vk::Semaphore> gRenderFinishedSemaphores;

Semaphores::Semaphores() {
  for (size_t i = 0; i < gSwapchainImageCount; ++i) {
    gFrameFences.push_back(gDevice.createFence({}));
    gInFlightFences.push_back(nullptr);
    gRenderFinishedSemaphores.push_back(gDevice.createSemaphore({}));
    gImageAvailableSemaphores.push_back(gDevice.createSemaphore({}));
  }
}
Semaphores::~Semaphores() {
  for (vk::Fence fence : gFrameFences) gDevice.destroy(fence);
  for (vk::Semaphore sem : gRenderFinishedSemaphores) gDevice.destroy(sem);
  for (vk::Semaphore sem : gImageAvailableSemaphores) gDevice.destroy(sem);
  gFrameFences.clear();
  gInFlightFences.clear();
  gRenderFinishedSemaphores.clear();
  gImageAvailableSemaphores.clear();
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
