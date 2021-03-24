#include <iostream>

#include "driver.hpp"
#include "swapchain.hpp"
#include "rendering.hpp"
#include "util.hpp"

VertexBuffers uploadData() {
  TransferCommandPool transferCommandPool;
  return VertexBuffers();
}

void mainApp() {
  Window window;
  Instance instance;
  Surface surface;
  Device device;
  RenderPass renderPass;
  Semaphores semaphores;
  Pipeline pipeline1;
  UniformBuffers uniformBuffers;
  DescriptorPool descriptorPool1(pipeline1.descriptorSetLayout_);
  VertexBuffers vertexBuffers1 = uploadData();
  FpsCount fpsCount;

  while (!glfwWindowShouldClose(gWindow)) {
    Swapchain swapchain;
    Framebuffers framebuffers;
    PassCommandBuffers passCommandBuffers;
    CommandBuffers commandBuffers1(pipeline1, descriptorPool1, vertexBuffers1);
    gWindowSizeChanged = false;
    std::cerr << "resize " << gSwapchainExtent.width << "x"
              << gSwapchainExtent.height << "\n";

    vk::Semaphore imageAvailableSemaphore = swapchain.getFirstImage();
    while (!glfwWindowShouldClose(gWindow) && !gWindowSizeChanged) {
      // Pause while the window is in the background
      while (!glfwGetWindowAttrib(gWindow, GLFW_FOCUSED)) {
        std::cerr << "pausing\n";
        glfwWaitEvents();
      }

      if (gInFlightFences[gFrameIndex]) {
        throwFail("waitForFences",
                  gDevice.waitForFences(gInFlightFences[gFrameIndex],
                                        /*waitAll=*/false,
                                        /*timeout=*/UINT64_MAX));
        gDevice.resetFences(gInFlightFences[gFrameIndex]);
      }

      vk::PipelineStageFlags waitDestStage(
          vk::PipelineStageFlagBits::eColorAttachmentOutput);
      auto commandBuffers = {gBeginPass[gSwapchainCurrentImage],
                             commandBuffers1.commandBuffers_[gFrameIndex],
                             gEndPass};
      vk::Semaphore renderFinishedSemaphore =
          gRenderFinishedSemaphores[gFrameIndex];
      vk::SubmitInfo submit(/*wait=*/imageAvailableSemaphore, waitDestStage,
                            commandBuffers,
                            /*signal=*/renderFinishedSemaphore);

      gInFlightFences[gFrameIndex] = gFrameFences[gFrameIndex];
      gGraphicsQueue.submit({submit}, gInFlightFences[gFrameIndex]);

      imageAvailableSemaphore = swapchain.getNextImage(renderFinishedSemaphore);

      glfwPollEvents();
      fpsCount.count();
      uniformBuffers.update();
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
