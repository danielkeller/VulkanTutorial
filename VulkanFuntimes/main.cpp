#include <iostream>

#include "driver.hpp"
#include "swapchain.hpp"
#include "rendering.hpp"
#include "util.hpp"

void mainApp() {
  Window window;
  Instance instance;
  Surface surface;
  Device device;
  RenderPass renderPass;
  Pipeline pipeline1;
  FpsCount fpsCount;

  while (!glfwWindowShouldClose(gWindow)) {
    uint64_t swapchainFrame = 0;
    Swapchain swapchain;
    Semaphores semaphores;
    Framebuffers framebuffers;
    CommandPool commandPool;
    VertexBuffers vertexBuffers;
    CommandBuffers commandBuffers1(pipeline1.pipeline_, vertexBuffers);
    gWindowSizeChanged = false;
    std::cerr << "resize " << gSwapchainExtent.width << "x"
              << gSwapchainExtent.height << "\n";

    auto [imageIndex, imageAvailableSemaphore] = swapchain.getFirstImage();
    while (!glfwWindowShouldClose(gWindow) && !gWindowSizeChanged) {
      // Pause while the window is in the background
      while (!glfwGetWindowAttrib(gWindow, GLFW_FOCUSED)) {
        std::cerr << "pausing\n";
        glfwWaitEvents();
      }

      if (gInFlightFences[imageIndex]) {
        throwFail("waitForFences",
                  gDevice.waitForFences(gInFlightFences[imageIndex],
                                        /*waitAll=*/false,
                                        /*timeout=*/UINT64_MAX));
        gDevice.resetFences(gInFlightFences[imageIndex]);
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

      gInFlightFences[imageIndex] = gFrameFences[imageIndex];
      gGraphicsQueue.submit({submit}, gInFlightFences[imageIndex]);

      std::tie(imageIndex, imageAvailableSemaphore) =
          swapchain.getNextImage(renderFinishedSemaphore);

      ++swapchainFrame;
      fpsCount.count();
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
