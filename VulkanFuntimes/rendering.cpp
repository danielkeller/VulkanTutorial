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
  vk::PipelineVertexInputStateCreateInfo vertexInput = model.pipelineInfo(0);
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      /*flags=*/{}, /*topology=*/vk::PrimitiveTopology::eTriangleList};
  vk::PipelineRasterizationStateCreateInfo rasterization(
      /*flags=*/{}, /*depthClampEnable=*/false,
      /*rasterizerDiscardEnable=*/false, vk::PolygonMode::eFill,
      vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise,
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
      {/*binding=*/0, vk::DescriptorType::eUniformBuffer,
       /*descriptorCount=*/1, vk::ShaderStageFlagBits::eVertex,
       /*immutableSamplers=*/nullptr},
      {/*binding=*/1, vk::DescriptorType::eUniformBufferDynamic,
       /*descriptorCount=*/1, vk::ShaderStageFlagBits::eVertex,
       /*immutableSamplers=*/nullptr},
      {/*binding=*/2, vk::DescriptorType::eCombinedImageSampler,
       vk::ShaderStageFlagBits::eFragment,
       /*immutableSamplers=*/sampler_},
      {/*binding=*/3, vk::DescriptorType::eCombinedImageSampler,
       vk::ShaderStageFlagBits::eFragment,
       /*immutableSamplers=*/sampler_},
      {/*binding=*/4, vk::DescriptorType::eUniformBufferDynamic,
       /*descriptorCount=*/1, vk::ShaderStageFlagBits::eFragment,
       /*immutableSamplers=*/nullptr},
  };
  descriptorSetLayout_ =
      gDevice.createDescriptorSetLayout({/*flags=*/{}, bindings});

  layout_ = gDevice.createPipelineLayout(
      {/*flags=*/{}, descriptorSetLayout_, /*pushConstants=*/{}});

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

void TransferCommandBuffer::copy(vk::Buffer from, vk::Buffer to,
                                 vk::DeviceSize size,
                                 vk::PipelineStageFlags dstStage,
                                 vk::AccessFlags dstAccess) {
  cmd_.copyBuffer(from, to, vk::BufferCopy(/*src=*/0, /*dst=*/0, size));

  vk::BufferMemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite, dstAccess,
                                  gGraphicsQueueFamilyIndex,
                                  gGraphicsQueueFamilyIndex, to,
                                  /*offset=*/0, size);
  cmd_.pipelineBarrier(
      /*srcStage=*/vk::PipelineStageFlagBits::eTransfer, dstStage,
      /*dependencyFlags=*/{}, {}, barrier, {});
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
  transfer.copy(staging_buffer_.buffer_, buffer_, size,
                vk::PipelineStageFlagBits::eVertexInput,
                vk::AccessFlagBits::eIndexRead |
                    vk::AccessFlagBits::eVertexAttributeRead);
}
VertexBuffers::~VertexBuffers() {
  gDevice.destroy(buffer_);
  gDevice.free(memory_);
}

Textures::Textures(const Gltf &model) {
  std::vector<Pixels> images = model.getImages();
  if (images.empty()) return;
  uint32_t layers = static_cast<uint32_t>(images.size());
  vk::DeviceSize size = images[0].size() * layers;
  vk::Extent3D extent = images[0].extent();

  MappedStagingBuffer mapped = staging_buffer_.map(size);
  char *pointer = mapped.pointer_;
  for (const Pixels &pixels : images)
    pointer = std::copy_n(pixels.data_.get(), pixels.size(), pointer);

  image_ = gDevice.createImage(
      {vk::ImageCreateFlagBits::eMutableFormat, vk::ImageType::e2D,
       vk::Format::eR8G8B8A8Srgb, extent,
       /*mipLevels=*/1, layers, vk::SampleCountFlagBits::e1,
       vk::ImageTiling::eOptimal,
       vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
       vk::SharingMode::eExclusive, /*queueFamilyIndices=*/{}});

  uint32_t memoryType = getMemoryFor(gDevice.getImageMemoryRequirements(image_),
                                     vk::MemoryPropertyFlagBits::eDeviceLocal);
  memory_ = gDevice.allocateMemory({size, memoryType});
  gDevice.bindImageMemory(image_, memory_, /*offset=*/0);

  TransferCommandBuffer transfer;
  vk::ImageSubresourceRange wholeImage(vk::ImageAspectFlagBits::eColor,
                                       /*baseMip=*/0, /*levelCount=*/1,
                                       /*baseLayer=*/0, layers);

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
                                              layers);
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

  imageView_ = gDevice.createImageView({/*flags=*/{}, image_,
                                        vk::ImageViewType::e2DArray,
                                        vk::Format::eR8G8B8A8Srgb,
                                        /*componentMapping=*/{}, wholeImage});
  imageViewData_ = gDevice.createImageView(
      {/*flags=*/{}, image_, vk::ImageViewType::e2DArray,
       vk::Format::eR8G8B8A8Unorm,
       /*componentMapping=*/{}, wholeImage});
}

Textures::~Textures() {
  gDevice.destroy(imageView_);
  gDevice.destroy(imageViewData_);
  gDevice.destroy(image_);
  gDevice.free(memory_);
}

struct Camera {
  glm::mat4 eye, proj;
};

DescriptorPool::DescriptorPool(vk::DescriptorSetLayout layout,
                               const Textures &textures, const Gltf &gltf) {
  vk::DeviceSize sceneSize = gltf.uniformsSize();
  MappedStagingBuffer mapped = staging_buffer_.map(sceneSize);
  gltf.readUniforms(mapped.pointer_);

  scene_ = gDevice.createBuffer({/*flags=*/{}, sceneSize,
                                 vk::BufferUsageFlagBits::eUniformBuffer |
                                     vk::BufferUsageFlagBits::eTransferDst,
                                 vk::SharingMode::eExclusive});
  memory_ = gDevice.allocateMemory(
      {sceneSize, getMemoryFor(gDevice.getBufferMemoryRequirements(scene_),
                               vk::MemoryPropertyFlagBits::eDeviceLocal)});
  gDevice.bindBufferMemory(scene_, memory_, /*offset=*/0);

  TransferCommandBuffer transfer;
  transfer.copy(staging_buffer_.buffer_, scene_, sceneSize,
                vk::PipelineStageFlagBits::eVertexShader,
                vk::AccessFlagBits::eUniformRead);

  vk::DeviceSize cameraSize = sizeof(Camera);
  camera_ = gDevice.createBuffer({/*flags=*/{}, cameraSize,
                                  vk::BufferUsageFlagBits::eUniformBuffer,
                                  vk::SharingMode::eExclusive});
  shared_memory_ = gDevice.allocateMemory(
      {cameraSize,
       getMemoryFor(gDevice.getBufferMemoryRequirements(camera_),
                    vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent)});
  gDevice.bindBufferMemory(camera_, shared_memory_, /*offset=*/0);
  mapping_ =
      (char *)gDevice.mapMemory(shared_memory_, /*offset=*/0, cameraSize);

  std::initializer_list<vk::DescriptorPoolSize> sizes = {
      {vk::DescriptorType::eUniformBuffer, /*count=*/1},
      {vk::DescriptorType::eUniformBufferDynamic, /*count=*/2},
      {vk::DescriptorType::eCombinedImageSampler, /*count=*/2}};
  pool_ = gDevice.createDescriptorPool({/*flags=*/{}, /*maxSets=*/1, sizes});

  set_ = gDevice.allocateDescriptorSets({pool_, layout})[0];

  vk::DeviceSize size = uniformSize<Camera>();
  vk::DescriptorBufferInfo cameraBuffer(camera_, /*offset=*/0, cameraSize);
  vk::WriteDescriptorSet writeCamera(set_, /*binding=*/0, /*arrayElement=*/0,
                                     vk::DescriptorType::eUniformBuffer, {},
                                     cameraBuffer,
                                     /*texelBufferView=*/{});

  vk::DescriptorBufferInfo modelBuffer(scene_, /*offset=*/0, size);
  vk::WriteDescriptorSet writeModel(set_, /*binding=*/1, /*arrayElement=*/0,
                                    vk::DescriptorType::eUniformBufferDynamic,
                                    {}, modelBuffer,
                                    /*texelBufferView=*/{});

  vk::DescriptorImageInfo imageInfo(/*sampler=*/nullptr, textures.imageView_,
                                    vk::ImageLayout::eShaderReadOnlyOptimal);
  vk::WriteDescriptorSet writeImage(set_, /*binding=*/2, /*arrayElement=*/0,
                                    vk::DescriptorType::eCombinedImageSampler,
                                    imageInfo, {},
                                    /*texelBufferView=*/{});

  vk::DescriptorImageInfo dataInfo(/*sampler=*/nullptr, textures.imageViewData_,
                                   vk::ImageLayout::eShaderReadOnlyOptimal);
  vk::WriteDescriptorSet writeData(set_, /*binding=*/3, /*arrayElement=*/0,
                                   vk::DescriptorType::eCombinedImageSampler,
                                   dataInfo, {},
                                   /*texelBufferView=*/{});

  size = uniformSize<Uniform>();
  vk::DescriptorBufferInfo materialBuffer(scene_, /*offset=*/0, size);
  vk::WriteDescriptorSet writeMaterial(
      set_, /*binding=*/4, /*arrayElement=*/0,
      vk::DescriptorType::eUniformBufferDynamic, {}, materialBuffer,
      /*texelBufferView=*/{});
  gDevice.updateDescriptorSets(
      {writeCamera, writeModel, writeImage, writeData, writeMaterial},
      /*copies=*/{});
}

Camera getCamera() {
  static auto start = std::chrono::high_resolution_clock::now();
  auto now = std::chrono::high_resolution_clock::now();
  std::chrono::duration<float> spinTime =
      (now - start) % std::chrono::seconds(8);
  Camera result;
  result.proj = glm::perspective(
      /*fovy=*/glm::radians(45.f),
      gSwapchainExtent.width / (float)gSwapchainExtent.height,
      /*znear=*/0.1f, /*zfar=*/100.f);
  result.proj[1][1] *= -1;
  glm::vec4 eye =
  glm::vec4(2.f, 1.f, 20.f, 1.f);// *
//      glm::rotate(glm::mat4(1.f), spinTime.count() * glm::radians(45.f),
//                  glm::vec3(0.f, 1.f, 0.f));
  result.eye = glm::lookAt(
      /*eye=*/glm::vec3(eye.x, eye.y, eye.z),
      /*center=*/glm::vec3(0.f, 0.f, 0.f),
      /*camera-y=*/glm::vec3(0.f, 1.f, 0.f));
  return result;
}

void DescriptorPool::updateCamera() {
  Camera camera = getCamera();
  std::copy_n((char *)&camera, sizeof(Camera), mapping_);
}

DescriptorPool::~DescriptorPool() {
  gDevice.unmapMemory(shared_memory_);
  gDevice.destroy(camera_);
  gDevice.free(shared_memory_);
  gDevice.destroy(scene_);
  gDevice.free(memory_);
  gDevice.destroy(pool_);
}

std::vector<vk::CommandPool> gCommandPools;
CommandPool::CommandPool() {
  for (int i = 0; i < gSwapchainImageCount; ++i)
    gCommandPools.push_back(
        gDevice.createCommandPool({vk::CommandPoolCreateFlagBits::eTransient,
                                   gGraphicsQueueFamilyIndex}));
}
CommandPool::~CommandPool() {
  for (vk::CommandPool pool : gCommandPools) gDevice.destroy(pool);
  gCommandPools.clear();
}

CommandBuffer::CommandBuffer(const Pipeline &pipeline,
                             const DescriptorPool &descriptorPool,
                             const VertexBuffers &vertices, const Gltf &gltf) {
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
  for (uint32_t mesh = 0; mesh < gltf.meshCount(); ++mesh) {
    for (const auto &drawCall : gltf.data_.meshes(mesh).draw_calls()) {
      buf_.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, pipeline.layout_,
          /*firstSet=*/0, descriptorPool.set_,
          {gltf.meshUniformOffset(mesh),
           gltf.materialUniformOffset(drawCall.material())});

      for (uint32_t binding = 0; binding < drawCall.bindings_size(); ++binding)
        buf_.bindVertexBuffers(binding, vertices.buffer_,
                               drawCall.bindings(binding).offset());
      buf_.bindIndexBuffer(vertices.buffer_,
                           static_cast<vk::DeviceSize>(drawCall.index_offset()),
                           static_cast<vk::IndexType>(drawCall.index_type()));
      buf_.drawIndexed(drawCall.index_count(), /*instanceCount=*/1,
                       /*firstIndex=*/0,
                       /*vertexOffset=*/0,
                       /*firstInstance=*/0);
    }
  }
  buf_.endRenderPass();
  buf_.end();
}
