#include "rendering.hpp"

#include <fstream>
#include <chrono>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"

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

VertexBuffers::VertexBuffers(const Gltf &model) {
  vk::DeviceSize size = model.bufferSize();
  MappedStagingBuffer mapped = staging_buffer_.map(size);
  model.readBuffers(mapped.pointer_);

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

  index_type_ = model.indexType();
  index_offset_ = model.indexOffset();
  count_ = model.primitiveCount();
  bind_offsets_ = model.bindOffsets();
}
VertexBuffers::~VertexBuffers() {
  gDevice.destroy(buffer_);
  gDevice.free(memory_);
}

Textures::Textures(const Gltf &model) {
  Pixels pixels = model.getDiffuseImage();
  vk::DeviceSize size = pixels.size();
  vk::Extent3D extent = pixels.extent();

  MappedStagingBuffer mapped = staging_buffer_.map(size);
  std::copy_n(pixels.data_, size, mapped.pointer_);

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

  imageView_ = gDevice.createImageView(
      {/*flags=*/{}, image_, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Srgb,
       /*componentMapping=*/{}, wholeImage});
}

Textures::~Textures() {
  gDevice.destroy(imageView_);
  gDevice.destroy(image_);
  gDevice.free(memory_);
}

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

Pipeline::Pipeline(const Gltf &model) {
  // Dynamic viewport
  vk::Viewport viewport;
  vk::Rect2D scissor;
  vk::PipelineViewportStateCreateInfo viewportState(
      /*flags=*/{}, viewport, scissor);
  auto dynamicStates = {vk::DynamicState::eViewport,
                        vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState(/*flags=*/{}, dynamicStates);

  // Fixed function stuff
  vk::PipelineVertexInputStateCreateInfo vertexInput = model.vertexInput();
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
  vk::PushConstantRange pushConstants(vk::ShaderStageFlagBits::eVertex,
                                      /*offset=*/0, sizeof(glm::mat4));

  layout_ = gDevice.createPipelineLayout(
      {/*flags=*/{}, descriptorSetLayout_, pushConstants});

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

struct Model {
  glm::mat4 model;
};
vk::Buffer gUniformBuffer;

UniformBuffers::UniformBuffers() {
  vk::DeviceSize size = uniformSize<Model>();
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

  Model model;
  model.model = glm::mat4(1.);
  std::copy_n((char *)&model, uniformSize<Model>(), mapping_);
  // eHostCoherent handles flushes and the later queue submit creates a memory
  // barrier
}

UniformBuffers::~UniformBuffers() {
  gDevice.unmapMemory(memory_);
  gDevice.destroy(gUniformBuffer);
  gDevice.free(memory_);
}

DescriptorPool::DescriptorPool(vk::DescriptorSetLayout layout,
                               const Textures &textures) {
  std::initializer_list<vk::DescriptorPoolSize> sizes = {
      {vk::DescriptorType::eUniformBuffer, /*count=*/1},
      {vk::DescriptorType::eCombinedImageSampler, /*count=*/1}};
  pool_ = gDevice.createDescriptorPool({/*flags=*/{}, /*maxSets=*/1, sizes});

  descriptorSet_ = gDevice.allocateDescriptorSets({pool_, layout})[0];

  vk::DeviceSize size = uniformSize<Model>();
  vk::DescriptorBufferInfo bufferInfo(gUniformBuffer, /*offset=*/0, size);
  vk::WriteDescriptorSet writeBuffer(descriptorSet_, /*binding=*/0,
                                     /*arrayElement=*/0,
                                     vk::DescriptorType::eUniformBuffer, {},
                                     bufferInfo, /*texelBufferView=*/{});

  vk::DescriptorImageInfo imageInfo(/*sampler=*/nullptr, textures.imageView_,
                                    vk::ImageLayout::eShaderReadOnlyOptimal);
  vk::WriteDescriptorSet writeImage(
      descriptorSet_, /*binding=*/1, /*arrayElement=*/0,
      vk::DescriptorType::eCombinedImageSampler, imageInfo, {},
      /*texelBufferView=*/{});
  gDevice.updateDescriptorSets({writeBuffer, writeImage}, {});
}

DescriptorPool::~DescriptorPool() { gDevice.destroy(pool_); }

std::vector<vk::CommandPool> gCommandPools;
CommandPool::CommandPool() {
  for (int i = 0; i < gSwapchainImageCount; ++i)
    gCommandPools.push_back(
        gDevice.createCommandPool({vk::CommandPoolCreateFlagBits::eTransient,
                                   gGraphicsQueueFamilyIndex}));
}
CommandPool::~CommandPool() {
  for (vk::CommandPool pool : gCommandPools) gDevice.destroy(pool);
}

glm::mat4 getCamera() {
  static auto start = std::chrono::high_resolution_clock::now();
  auto now = std::chrono::high_resolution_clock::now();
  std::chrono::duration<float> spinTime =
      (now - start) % std::chrono::seconds(4);
  return glm::perspective(
             /*fovy=*/glm::radians(45.f),
             gSwapchainExtent.width / (float)gSwapchainExtent.height,
             /*znear=*/0.1f, /*zfar=*/100.f) *
         glm::lookAt(/*eye=*/glm::vec3(30.f, 30.f, 30.f),
                     /*center=*/glm::vec3(0.f, 5.f, 0.f),
                     /*camera-y=*/glm::vec3(0.f, -1.f, 0.f)) *
         glm::rotate(glm::mat4(1.f), spinTime.count() * glm::radians(90.f),
                     glm::vec3(0.f, 1.f, 0.f));
}

CommandBuffer::CommandBuffer(const Pipeline &pipeline,
                             const DescriptorPool &descriptorPool,
                             const VertexBuffers &vertices) {
  vk::CommandPool pool = gCommandPools[gSwapchainCurrentImage];
  if (gFrame % 100 == 0) gDevice.resetCommandPool(pool);
  buf_ = gDevice.allocateCommandBuffers(
      {pool, vk::CommandBufferLevel::ePrimary, 1})[0];

  buf_.begin(vk::CommandBufferBeginInfo());
  buf_.setViewport(/*index=*/0, gViewport);
  buf_.setScissor(/*index=*/0, gScissor);
  std::initializer_list<vk::ClearValue> clearValues = {
      vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 1}),
      vk::ClearDepthStencilValue(1.f, 0)};
  buf_.beginRenderPass(
      {gRenderPass, gFramebuffers[gSwapchainCurrentImage], /*renderArea=*/
       vk::Rect2D(/*offset=*/{0, 0}, gSwapchainExtent), clearValues},
      vk::SubpassContents::eInline);
  buf_.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline_);
  buf_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.layout_,
                          /*firstSet=*/0, descriptorPool.descriptorSet_,
                          /*dynamicOffsets=*/{});
  glm::mat4 camera = getCamera();
  buf_.pushConstants(pipeline.layout_, vk::ShaderStageFlagBits::eVertex,
                     /*offset=*/0, sizeof camera, (void *)&camera);
  for (uint32_t binding = 0; binding < vertices.bind_offsets_.size(); ++binding)
    buf_.bindVertexBuffers(binding, vertices.buffer_,
                           vertices.bind_offsets_[binding]);
  buf_.bindIndexBuffer(vertices.buffer_, vertices.index_offset_,
                       vertices.index_type_);
  buf_.drawIndexed(vertices.count_, /*instanceCount=*/1, /*firstIndex=*/0,
                   /*vertexOffset=*/0,
                   /*firstInstance=*/0);
  buf_.endRenderPass();
  buf_.end();
}
