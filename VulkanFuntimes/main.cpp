#include <iostream>
#include <chrono>

#include "driver.hpp"
#include "swapchain.hpp"
#include "rendering.hpp"
#include "util.hpp"

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
      while (!glfwGetWindowAttrib(gWindow, GLFW_FOCUSED)) glfwPollEvents();

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
