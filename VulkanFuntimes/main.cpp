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
  Pipeline pipeline1;
  VertexBuffers vertexBuffers = uploadData();
  FpsCount fpsCount;

  while (!glfwWindowShouldClose(gWindow)) {
    Swapchain swapchain;
    Semaphores semaphores;
    Framebuffers framebuffers;
    UniformBuffers uniformBuffers;
    DescriptorPool descriptorPool1(pipeline1.descriptorSetLayout_);
    CommandPool commandPool;
    CommandBuffers commandBuffers1(pipeline1, descriptorPool1, vertexBuffers);
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

      if (gInFlightFences[gSwapchainCurrentImage]) {
        throwFail("waitForFences",
                  gDevice.waitForFences(gInFlightFences[gSwapchainCurrentImage],
                                        /*waitAll=*/false,
                                        /*timeout=*/UINT64_MAX));
        gDevice.resetFences(gInFlightFences[gSwapchainCurrentImage]);
      }

      vk::PipelineStageFlags waitDestStage(
          vk::PipelineStageFlagBits::eColorAttachmentOutput);
      vk::CommandBuffer commandBuffer(
          commandBuffers1.commandBuffers_[gSwapchainCurrentImage]);
      vk::Semaphore renderFinishedSemaphore =
          gRenderFinishedSemaphores[gSwapchainCurrentImage];
      vk::SubmitInfo submit(/*wait=*/imageAvailableSemaphore, waitDestStage,
                            commandBuffer,
                            /*signal=*/renderFinishedSemaphore);

      gInFlightFences[gSwapchainCurrentImage] =
          gFrameFences[gSwapchainCurrentImage];
      gGraphicsQueue.submit({submit}, gInFlightFences[gSwapchainCurrentImage]);

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
