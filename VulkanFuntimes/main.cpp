#include <iostream>

#include "driver.hpp"
#include "swapchain.hpp"
#include "rendering.hpp"
#include "util.hpp"
#include "gltf.hpp"

void mainApp() {
  std::ios_base::sync_with_stdio(false);
  Window window;
  Instance instance;
  Surface surface;
  Device device;
  Swapchain swapchain;
  RenderPass renderPass;
  FpsCount fpsCount;

  Gltf gltffile("models/DamagedHelmet/DamagedHelmet.gltf");

  // Gltf gltffile("models/viking_room/scene.gltf");

  Pipeline pipeline1(gltffile);

  TransferCommandPool transferCommandPool;
  VertexBuffers vertexBuffers1(gltffile);
  Textures textures1(gltffile);
  DescriptorPool descriptorPool1(pipeline1.descriptorSetLayout_, textures1,
                                 gltffile);

  while (!glfwWindowShouldClose(gWindow)) {
    swapchain.resizeToWindow();
    Semaphores semaphores;
    DepthStencil depthStencil;
    Framebuffers framebuffers;
    CommandPool commandPool;
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

      descriptorPool1.updateCamera();

      CommandBuffer commandBuffer1(pipeline1, descriptorPool1, vertexBuffers1,
                                   gltffile);

      vk::Semaphore renderFinishedSemaphore =
          gRenderFinishedSemaphores[gSwapchainCurrentImage];
      vk::PipelineStageFlags waitDestStage(
          vk::PipelineStageFlagBits::eColorAttachmentOutput);
      vk::SubmitInfo submit(/*wait=*/imageAvailableSemaphore, waitDestStage,
                            commandBuffer1.buf_,
                            /*signal=*/renderFinishedSemaphore);

      gInFlightFences[gSwapchainCurrentImage] =
          gFrameFences[gSwapchainCurrentImage];
      gGraphicsQueue.submit({submit}, gInFlightFences[gSwapchainCurrentImage]);

      imageAvailableSemaphore = swapchain.getNextImage(renderFinishedSemaphore);

      glfwPollEvents();
      fpsCount.count();
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
