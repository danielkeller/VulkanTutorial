#include "rendering.hpp"

#include <fstream>
#include <chrono>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "stb_image.h"

#include "util.hpp"
#include "driver.hpp"
#include "swapchain.hpp"

vk::CommandPool gTransferCommandPool;

TransferCommandPool::TransferCommandPool() {
  gTransferCommandPool = gDevice.createCommandPool(
      {vk::CommandPoolCreateFlagBits::eTransient, gGraphicsQueueFamilyIndex});
}
TransferCommandPool::~TransferCommandPool() {
  gDevice.destroy(gTransferCommandPool);
  gTransferCommandPool = nullptr;
}
TransferCommandBuffer::TransferCommandBuffer() {
  cmd_ = gDevice.allocateCommandBuffers(
      {gTransferCommandPool, vk::CommandBufferLevel::ePrimary, 1})[0];
  cmd_.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
}

TransferCommandBuffer::~TransferCommandBuffer() {
  cmd_.end();
  vk::SubmitInfo submit;
  submit.setCommandBuffers(cmd_);
  gGraphicsQueue.submit(submit);
}

vk::CommandPool gCommandPool;

CommandPool::CommandPool() {
  gCommandPool =
      gDevice.createCommandPool({/*flags=*/{}, gGraphicsQueueFamilyIndex});
}
CommandPool::~CommandPool() { gDevice.destroy(gCommandPool); }

vk::ShaderModule readShader(const std::string &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open file \"" + filename + "\"");
  size_t fileSize = (size_t)file.tellg();
  std::vector<uint32_t> buffer(fileSize / 4);
  file.seekg(0);
  file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
  return gDevice.createShaderModule({vk::ShaderModuleCreateFlags(), buffer});
}

vk::RenderPass gRenderPass;

RenderPass::RenderPass() {
  std::initializer_list<vk::AttachmentDescription> attachments = {
      {/*flags=*/{}, kPresentFormat, vk::SampleCountFlagBits::e1,
       vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
       /*stencil*/ vk::AttachmentLoadOp::eDontCare,
       vk::AttachmentStoreOp::eDontCare,
       /*initialLayout=*/vk::ImageLayout::eUndefined,
       /*finalLayout=*/vk::ImageLayout::ePresentSrcKHR},
      {/*flags=*/{}, kDepthStencilFormat, vk::SampleCountFlagBits::e1,
       vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
       /*stencil*/ vk::AttachmentLoadOp::eClear,
       vk::AttachmentStoreOp::eDontCare,
       /*initialLayout=*/vk::ImageLayout::eUndefined,
       /*finalLayout=*/vk::ImageLayout::eDepthStencilAttachmentOptimal}};

  vk::AttachmentReference colorRef(
      /*attachment=*/0, vk::ImageLayout::eColorAttachmentOptimal);
  vk::AttachmentReference depthStencilRef(
      /*attachment=*/1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
  // Attachment ref index corresponds to layout number in shader
  vk::SubpassDescription subpass(
      /*flags=*/{}, vk::PipelineBindPoint::eGraphics,
      /*inputAttachments=*/{}, colorRef, /*resolveAttachments=*/{},
      &depthStencilRef);
  // Don't write to the image until the presenter is done with it
  std::initializer_list<vk::SubpassDependency> dependencies = {
      {/*src=*/VK_SUBPASS_EXTERNAL, /*dstSubpass=*/0,
       /*src=*/vk::PipelineStageFlagBits::eColorAttachmentOutput,
       /*dst=*/vk::PipelineStageFlagBits::eColorAttachmentOutput,
       /*src=*/vk::AccessFlags(),
       /*dst=*/vk::AccessFlagBits::eColorAttachmentWrite},
      // Don't touch the depth buffer until the previous frame is done with it
      {/*src=*/VK_SUBPASS_EXTERNAL, /*dstSubpass=*/0,
       vk::PipelineStageFlagBits::eLateFragmentTests,
       vk::PipelineStageFlagBits::eEarlyFragmentTests,
       vk::AccessFlagBits::eDepthStencilAttachmentRead |
           vk::AccessFlagBits::eDepthStencilAttachmentWrite,
       vk::AccessFlagBits::eDepthStencilAttachmentRead |
           vk::AccessFlagBits::eDepthStencilAttachmentWrite}};
  gRenderPass = gDevice.createRenderPass(
      {/*flags=*/{}, attachments, subpass, dependencies});
}
RenderPass::~RenderPass() { gDevice.destroy(gRenderPass); }

template <class V>
struct Description {
  static const vk::VertexInputBindingDescription binding;
  static const std::initializer_list<vk::VertexInputAttributeDescription>
      attribute;
};

struct Vertex {
  glm::vec3 pos;
  glm::vec2 uv;
};
template <>
const vk::VertexInputBindingDescription Description<Vertex>::binding{
    /*binding=*/0, /*stride=*/sizeof(Vertex), vk::VertexInputRate::eVertex};
template <>
const std::initializer_list<vk::VertexInputAttributeDescription>
    Description<Vertex>::attribute{
        {/*location=*/0, /*binding=*/0, vk::Format::eR32G32B32Sfloat,
         offsetof(Vertex, pos)},
        {/*location=*/1, /*binding=*/0, vk::Format::eR32G32Sfloat,
         offsetof(Vertex, uv)}};

const std::vector<Vertex> vertices = {{{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
                                      {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
                                      {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}},
                                      {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
                                      {{0.5f, -0.5f, -0.5f}, {1.0f, 0.5f}},
                                      {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f}},
                                      {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f}},
                                      {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}}};

const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};

MappedStagingBuffer StagingBuffer::map(vk::DeviceSize size) {
  buffer_ = gDevice.createBuffer({/*flags=*/{}, size,
                                  vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::SharingMode::eExclusive});
  uint32_t stagingMemoryType =
      getMemoryFor(gDevice.getBufferMemoryRequirements(buffer_),
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  memory_ = gDevice.allocateMemory({size, stagingMemoryType});
  gDevice.bindBufferMemory(buffer_, memory_, /*offset=*/0);

  void *mapped = gDevice.mapMemory(memory_, /*offset=*/0, size);
  return MappedStagingBuffer{memory_, (char *)mapped};
}
MappedStagingBuffer::~MappedStagingBuffer() { gDevice.unmapMemory(memory_); }
StagingBuffer::~StagingBuffer() {
  gDevice.destroy(buffer_);
  gDevice.free(memory_);
}

VertexBuffers::VertexBuffers() {
  count_ = (uint32_t)indices.size();
  vertex_offset_ = 0;
  vk::DeviceSize vertexSize = index_offset_ = sizeof(Vertex) * vertices.size();
  vk::DeviceSize indexSize = sizeof(uint16_t) * indices.size();
  vk::DeviceSize size = vertexSize + indexSize;

  MappedStagingBuffer mapped = staging_buffer_.map(size);
  std::copy_n((char *)&vertices[0], vertexSize, mapped.pointer_);
  std::copy_n((char *)&indices[0], indexSize, mapped.pointer_ + index_offset_);

  buffer_ = gDevice.createBuffer({/*flags=*/{}, size,
                                  vk::BufferUsageFlagBits::eIndexBuffer |
                                      vk::BufferUsageFlagBits::eVertexBuffer |
                                      vk::BufferUsageFlagBits::eTransferDst,
                                  vk::SharingMode::eExclusive});

  uint32_t memoryType =
      getMemoryFor(gDevice.getBufferMemoryRequirements(buffer_),
                   vk::MemoryPropertyFlagBits::eDeviceLocal);
  memory_ = gDevice.allocateMemory({size, memoryType});
  gDevice.bindBufferMemory(buffer_, memory_, /*offset=*/0);

  TransferCommandBuffer transfer;
  transfer.cmd_.copyBuffer(staging_buffer_.buffer_, buffer_,
                           vk::BufferCopy(/*src=*/0, /*dst=*/0, size));
}
VertexBuffers::~VertexBuffers() {
  gDevice.destroy(buffer_);
  gDevice.free(memory_);
}

vk::ImageView gTextureImageView;

Textures::Textures() {
  int width, height, channels;
  std::unique_ptr<stbi_uc, void (*)(void *)> cpixels(
      stbi_load("Textures/test.jpg", &width, &height, &channels,
                STBI_rgb_alpha),
      stbi_image_free);
  if (!cpixels)
    throw std::runtime_error(std::string("stbi_load: ") +
                             stbi_failure_reason());

  vk::DeviceSize size = width * height * 4;
  MappedStagingBuffer mapped = staging_buffer_.map(size);
  std::copy_n(cpixels.get(), size, mapped.pointer_);

  vk::Extent3D extent(width, height, 1);

  image_ = gDevice.createImage(
      {/*flags=*/{}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Srgb, extent,
       /*mipLevels=*/1, /*arrayLayers=*/1, vk::SampleCountFlagBits::e1,
       vk::ImageTiling::eOptimal,
       vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
       vk::SharingMode::eExclusive, /*queueFamilyIndices=*/{}});

  uint32_t memoryType = getMemoryFor(gDevice.getImageMemoryRequirements(image_),
                                     vk::MemoryPropertyFlagBits::eDeviceLocal);
  memory_ = gDevice.allocateMemory({size, memoryType});
  gDevice.bindImageMemory(image_, memory_, /*offset=*/0);

  TransferCommandBuffer transfer;
  vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor,
                                       /*baseMip=*/0,
                                       /*levelCount=*/1, /*baseLayer=*/0,
                                       /*layerCount=*/1);

  vk::ImageMemoryBarrier toTransferDst(
      /*srcAccess=*/{}, /*dstAccess=*/vk::AccessFlagBits::eTransferWrite,
      /*oldLayout=*/vk::ImageLayout::eUndefined,
      /*newLayout=*/vk::ImageLayout::eTransferDstOptimal,
      VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image_, wholeImage);
  transfer.cmd_.pipelineBarrier(
      /*srcStage=*/vk::PipelineStageFlagBits::eTopOfPipe,
      /*dstStage=*/vk::PipelineStageFlagBits::eTransfer,
      /*dependencyFlags=*/{}, {}, {}, toTransferDst);

  vk::ImageSubresourceLayers wholeImageLayers(vk::ImageAspectFlagBits::eColor,
                                              /*mipLevel=*/0, /*baseLayer=*/0,
                                              /*layerCount=*/1);
  vk::BufferImageCopy copy(/*offset=*/0, /*bufferRowLength=*/0,
                           /*bufferImageHeight=*/0, wholeImageLayers,
                           vk::Offset3D(0, 0, 0), extent);
  transfer.cmd_.copyBufferToImage(staging_buffer_.buffer_, image_,
                                  vk::ImageLayout::eTransferDstOptimal, copy);

  vk::ImageMemoryBarrier toShader(
      /*srcAccess=*/vk::AccessFlagBits::eTransferWrite,
      /*dstAccess=*/vk::AccessFlagBits::eShaderRead,
      /*oldLayout=*/vk::ImageLayout::eTransferDstOptimal,
      /*newLayout=*/vk::ImageLayout::eShaderReadOnlyOptimal,
      VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image_, wholeImage);
  transfer.cmd_.pipelineBarrier(
      /*srcStage=*/vk::PipelineStageFlagBits::eTransfer,
      /*dstStage=*/vk::PipelineStageFlagBits::eFragmentShader,
      /*dependencyFlags=*/{}, {}, {}, toShader);

  gTextureImageView = gDevice.createImageView(
      {/*flags=*/{}, image_, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Srgb,
       /*componentMapping=*/{}, wholeImage});
}

Textures::~Textures() {
  gDevice.destroy(gTextureImageView);
  gDevice.destroy(image_);
  gDevice.free(memory_);
}

Pipeline::Pipeline() {
  // Dynamic viewport
  vk::Viewport viewport;
  vk::Rect2D scissor;
  vk::PipelineViewportStateCreateInfo viewportState(
      /*flags=*/{}, viewport, scissor);
  auto dynamicStates = {vk::DynamicState::eViewport,
                        vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState(/*flags=*/{}, dynamicStates);

  // Fixed function stuff
  vk::PipelineVertexInputStateCreateInfo vertexInput{
      /*flags=*/{}, Description<Vertex>::binding,
      Description<Vertex>::attribute};
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      /*flags=*/{}, /*topology=*/vk::PrimitiveTopology::eTriangleList};
  vk::PipelineRasterizationStateCreateInfo rasterization(
      /*flags=*/{}, /*depthClampEnable=*/false,
      /*rasterizerDiscardEnable=*/false, vk::PolygonMode::eFill,
      vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise,
      /*depthBiasEnable=*/false, {}, {}, {}, /*lineWidth=*/1);
  vk::PipelineDepthStencilStateCreateInfo depthStencil(
      /*flags=*/{}, /*depthTestEnable=*/true, /*depthWriteEnable=*/true,
      vk::CompareOp::eLess);
  vk::PipelineMultisampleStateCreateInfo multisample;
  vk::PipelineColorBlendAttachmentState colorBlend1;
  colorBlend1.colorWriteMask = ~vk::ColorComponentFlags();  // All
  vk::PipelineColorBlendStateCreateInfo colorBlend(
      /*flags=*/{}, /*logicOpEnable=*/false, /*logicOp=*/{}, colorBlend1);

  vk::SamplerCreateInfo samplerCreate(/*flags=*/{}, vk::Filter::eLinear,
                                      vk::Filter::eLinear,
                                      vk::SamplerMipmapMode::eLinear);
  samplerCreate.setAnisotropyEnable(true);
  samplerCreate.setMaxAnisotropy(
      gPhysicalDeviceProperties.limits.maxSamplerAnisotropy);
  sampler_ = gDevice.createSampler(samplerCreate);

  std::initializer_list<vk::DescriptorSetLayoutBinding> bindings = {
      {/*binding=*/0, vk::DescriptorType::eUniformBuffer, /*descriptorCount=*/1,
       vk::ShaderStageFlagBits::eVertex, /*immutableSamplers=*/nullptr},
      {/*binding=*/1, vk::DescriptorType::eCombinedImageSampler,
       vk::ShaderStageFlagBits::eFragment,
       /*immutableSamplers=*/sampler_}};
  descriptorSetLayout_ =
      gDevice.createDescriptorSetLayout({/*flags=*/{}, bindings});

  layout_ = gDevice.createPipelineLayout({/*flags=*/{}, descriptorSetLayout_,
                                          /*pushConstantRanges=*/{}});

  vk::ShaderModule vert = readShader("triangle.vert");
  vk::ShaderModule frag = readShader("test.frag");

  std::initializer_list<vk::PipelineShaderStageCreateInfo> stages = {
      {/*flags=*/{}, vk::ShaderStageFlagBits::eVertex, vert, /*pName=*/"main"},
      {/*flags=*/{}, vk::ShaderStageFlagBits::eFragment, frag,
       /*pName=*/"main"}};

  vk::ResultValue<std::vector<vk::Pipeline>> pipelines_or =
      gDevice.createGraphicsPipelines(
          /*pipelineCache=*/nullptr,
          {{/*flags=*/{}, stages, &vertexInput, &inputAssembly,
            /*tesselation=*/{}, &viewportState, &rasterization, &multisample,
            &depthStencil, &colorBlend, &dynamicState, layout_, gRenderPass,
            /*subpass=*/0, /*basePipeline=*/{}}});

  gDevice.destroy(vert);
  gDevice.destroy(frag);

  throwFail("vkCreateGraphicsPipelines", pipelines_or.result);
  if (pipelines_or.value.empty())
    throw std::runtime_error("No pipeline returned???");
  pipeline_ = pipelines_or.value[0];
}
Pipeline::~Pipeline() {
  gDevice.destroy(pipeline_);
  gDevice.destroy(layout_);
  gDevice.destroy(sampler_);
  gDevice.destroy(descriptorSetLayout_);
}

template <class T>
vk::DeviceSize uniformSize() {
  static_assert(sizeof(T), "Empty uniform object");
  vk::DeviceSize align =
      gPhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
  return ((sizeof(T) + align - 1) / align) * align;
}

struct MVP {
  glm::mat4 model, view, projection;
};
vk::Buffer gUniformBuffer;

UniformBuffers::UniformBuffers() {
  vk::DeviceSize size = uniformSize<MVP>() * gSwapchainImageCount;
  gUniformBuffer = gDevice.createBuffer(
      {/*flags=*/{}, size, vk::BufferUsageFlagBits::eUniformBuffer,
       vk::SharingMode::eExclusive});
  uint32_t stagingMemoryType =
      getMemoryFor(gDevice.getBufferMemoryRequirements(gUniformBuffer),
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  memory_ = gDevice.allocateMemory({size, stagingMemoryType});
  gDevice.bindBufferMemory(gUniformBuffer, memory_, /*offset=*/0);
  mapping_ = (char *)gDevice.mapMemory(memory_, /*offset=*/0, size);
}

void UniformBuffers::update() {
  static auto start = std::chrono::high_resolution_clock::now();
  auto now = std::chrono::high_resolution_clock::now();
  std::chrono::duration<float> spinTime =
      (now - start) % std::chrono::seconds(4);
  MVP mvp;
  mvp.model = glm::rotate(glm::mat4(1.f), spinTime.count() * glm::radians(90.f),
                          glm::vec3(0.f, 0.f, 1.f));
  mvp.view = glm::lookAt(/*eye=*/glm::vec3(2.f, 2.f, 2.f),
                         /*center=*/glm::vec3(0.f, 0.f, 0.f),
                         /*camera-y=*/glm::vec3(0.f, 0.f, -1.f));
  mvp.projection =
      glm::perspective(/*fovy=*/glm::radians(45.f),
                       gSwapchainExtent.width / (float)gSwapchainExtent.height,
                       /*znear=*/0.1f, /*zfar=*/10.f);

  size_t offset = gSwapchainCurrentImage * uniformSize<MVP>();
  std::copy_n((char *)&mvp, uniformSize<MVP>(), mapping_ + offset);
  // eHostCoherent handles flushes and the later queue submit creates a memory
  // barrier
}

UniformBuffers::~UniformBuffers() {
  gDevice.unmapMemory(memory_);
  gDevice.destroy(gUniformBuffer);
  gDevice.free(memory_);
}

DescriptorPool::DescriptorPool(vk::DescriptorSetLayout layout) {
  std::initializer_list<vk::DescriptorPoolSize> sizes = {
      {vk::DescriptorType::eUniformBuffer,
       /*count=*/gSwapchainImageCount},
      {vk::DescriptorType::eCombinedImageSampler,
       /*count=*/gSwapchainImageCount}};
  pool_ = gDevice.createDescriptorPool(
      {/*flags=*/{}, /*maxSets=*/gSwapchainImageCount, sizes});

  std::vector<vk::DescriptorSetLayout> layouts(gSwapchainImageCount, layout);
  descriptorSets_ = gDevice.allocateDescriptorSets({pool_, layouts});

  for (size_t i = 0; i < gSwapchainImageCount; ++i) {
    vk::DeviceSize stride = uniformSize<MVP>();
    vk::DescriptorBufferInfo bufferInfo(gUniformBuffer, stride * i, stride);
    vk::WriteDescriptorSet writeBuffer(
        descriptorSets_[i], /*binding=*/0, /*arrayElement=*/0,
        vk::DescriptorType::eUniformBuffer, {}, bufferInfo,
        /*texelBufferView=*/{});

    vk::DescriptorImageInfo imageInfo(/*sampler=*/nullptr, gTextureImageView,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::WriteDescriptorSet writeImage(
        descriptorSets_[i], /*binding=*/1, /*arrayElement=*/0,
        vk::DescriptorType::eCombinedImageSampler, imageInfo, {},
        /*texelBufferView=*/{});
    gDevice.updateDescriptorSets({writeBuffer, writeImage}, {});
  }
}

DescriptorPool::~DescriptorPool() { gDevice.destroy(pool_); }

CommandBuffers::CommandBuffers(const Pipeline &pipeline,
                               const DescriptorPool &descriptorPool,
                               const VertexBuffers &vertices) {
  uint32_t nBuffers = (uint32_t)gFramebuffers.size();
  commandBuffers_ = gDevice.allocateCommandBuffers(
      {gCommandPool, vk::CommandBufferLevel::ePrimary, nBuffers});

  for (uint32_t i = 0; i < nBuffers; ++i) {
    vk::CommandBuffer buf = commandBuffers_[i];
    buf.begin(vk::CommandBufferBeginInfo());
    buf.setViewport(/*index=*/0, gViewport);
    buf.setScissor(/*index=*/0, gScissor);
    std::initializer_list<vk::ClearValue> clearValues = {
        vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 1}),
        vk::ClearDepthStencilValue(1.f, 0)};
    buf.beginRenderPass(
        {gRenderPass, gFramebuffers[i], /*renderArea=*/
         vk::Rect2D(/*offset=*/{0, 0}, gSwapchainExtent), clearValues},
        vk::SubpassContents::eInline);
    buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline_);
    buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.layout_,
                           /*firstSet=*/0, descriptorPool.descriptorSets_[i],
                           /*dynamicOffsets=*/{});
    buf.bindVertexBuffers(/*bindingOffset=*/0, vertices.buffer_,
                          vertices.vertex_offset_);
    buf.bindIndexBuffer(vertices.buffer_, vertices.index_offset_,
                        vk::IndexType::eUint16);
    buf.drawIndexed(vertices.count_, /*instanceCount=*/1, /*firstIndex=*/0,
                    /*vertexOffset=*/0,
                    /*firstInstance=*/0);
    buf.endRenderPass();
    buf.end();
  }
}
