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
  // Wait for the transfers to finish
  gGraphicsQueue.waitIdle();
  gDevice.destroy(gTransferCommandPool);
  gTransferCommandPool = nullptr;
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

vk::RenderPass gRenderPass;

RenderPass::RenderPass() {
  vk::AttachmentDescription colorAttachment(
      /*flags=*/{}, kPresentFormat, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
      /*stencil*/ vk::AttachmentLoadOp::eDontCare,
      vk::AttachmentStoreOp::eDontCare,
      /*initialLayout=*/vk::ImageLayout::eUndefined,
      /*finalLayout=*/vk::ImageLayout::ePresentSrcKHR);
  vk::AttachmentReference colorRef(
      /*attachment=*/0, vk::ImageLayout::eColorAttachmentOptimal);
  // Attachment ref index corresponds to layout number in shader
  vk::SubpassDescription subpass(
      /*flags=*/{}, vk::PipelineBindPoint::eGraphics,
      /*inputAttachments=*/{},
      /*colorAttachments=*/colorRef);
  // Don't write to the image until the presenter is done with it
  vk::SubpassDependency colorWriteDependency(
      /*src=*/VK_SUBPASS_EXTERNAL, /*dstSubpass=*/0,
      /*src=*/vk::PipelineStageFlagBits::eColorAttachmentOutput,
      /*dst=*/vk::PipelineStageFlagBits::eColorAttachmentOutput,
      /*src=*/vk::AccessFlags(),
      /*dst=*/vk::AccessFlagBits::eColorAttachmentWrite);
  gRenderPass = gDevice.createRenderPass(
      {/*flags=*/{}, colorAttachment, subpass, colorWriteDependency});
}
RenderPass::~RenderPass() { gDevice.destroy(gRenderPass); }

template <class V>
struct Description {
  static const vk::VertexInputBindingDescription binding;
  static const std::initializer_list<vk::VertexInputAttributeDescription>
      attribute;
};

struct Vertex {
  glm::vec2 pos;
  glm::vec3 color;
};
template <>
const vk::VertexInputBindingDescription Description<Vertex>::binding{
    /*binding=*/0, /*stride=*/sizeof(Vertex), vk::VertexInputRate::eVertex};
template <>
const std::initializer_list<vk::VertexInputAttributeDescription>
    Description<Vertex>::attribute{
        {/*location=*/0, /*binding=*/0, vk::Format::eR32G32Sfloat,
         offsetof(Vertex, pos)},
        {/*location=*/1, /*binding=*/0, vk::Format::eR32G32B32Sfloat,
         offsetof(Vertex, color)}};

const std::vector<Vertex> vertices = {{{0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                                      {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
                                      {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
                                      {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}}};

const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

uint32_t getMemoryForBuffer(vk::Buffer buffer,
                            vk::MemoryPropertyFlags memFlagRequirements) {
  vk::MemoryRequirements memoryRequirements =
      gDevice.getBufferMemoryRequirements(buffer);

  vk::PhysicalDeviceMemoryProperties memProperties =
      gPhysicalDevice.getMemoryProperties();

  for (uint32_t i = 0; i < memProperties.memoryTypes.size(); ++i) {
    if (memoryRequirements.memoryTypeBits & (1 << i) &&
        memProperties.memoryTypes[i].propertyFlags & memFlagRequirements)
      return i;
  }
  throw std::runtime_error("No memory type found for buffer");
}

void copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) {
  vk::CommandBuffer cmd = gDevice.allocateCommandBuffers(
      {gTransferCommandPool, vk::CommandBufferLevel::ePrimary, 1})[0];
  cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  cmd.copyBuffer(src, dst, vk::BufferCopy(/*src=*/0, /*dst=*/0, size));
  cmd.end();
  vk::SubmitInfo submit;
  submit.setCommandBuffers(cmd);
  gGraphicsQueue.submit(submit);
}

VertexBuffers::VertexBuffers() {
  count_ = (uint32_t)indices.size();
  vertex_offset_ = 0;
  vk::DeviceSize vertexSize = index_offset_ = sizeof(Vertex) * vertices.size();
  vk::DeviceSize indexSize = sizeof(uint16_t) * indices.size();
  vk::DeviceSize size = vertexSize + indexSize;

  staging_buffer_ = gDevice.createBuffer({/*flags=*/{}, size,
                                          vk::BufferUsageFlagBits::eTransferSrc,
                                          vk::SharingMode::eExclusive});
  uint32_t stagingMemoryType = getMemoryForBuffer(
      staging_buffer_, vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
  staging_memory_ = gDevice.allocateMemory({size, stagingMemoryType});
  gDevice.bindBufferMemory(staging_buffer_, staging_memory_, /*offset=*/0);

  void *mapped = gDevice.mapMemory(staging_memory_, /*offset=*/0, size);
  std::copy_n((char *)&vertices[0], vertexSize, (char *)mapped);
  std::copy_n((char *)&indices[0], indexSize, (char *)mapped + index_offset_);
  gDevice.unmapMemory(staging_memory_);

  buffer_ = gDevice.createBuffer({/*flags=*/{}, size,
                                  vk::BufferUsageFlagBits::eIndexBuffer |
                                      vk::BufferUsageFlagBits::eVertexBuffer |
                                      vk::BufferUsageFlagBits::eTransferDst,
                                  vk::SharingMode::eExclusive});

  uint32_t memoryType =
      getMemoryForBuffer(buffer_, vk::MemoryPropertyFlagBits::eDeviceLocal);
  memory_ = gDevice.allocateMemory({size, memoryType});
  gDevice.bindBufferMemory(buffer_, memory_, /*offset=*/0);
  copyBuffer(staging_buffer_, buffer_, size);
}
VertexBuffers::~VertexBuffers() {
  gDevice.destroy(staging_buffer_);
  gDevice.free(staging_memory_);
  gDevice.destroy(buffer_);
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
      /*depthBiasEnable=*/false, {}, {}, {},
      /*lineWidth=*/1);
  vk::PipelineMultisampleStateCreateInfo multisample;
  vk::PipelineColorBlendAttachmentState colorBlend1;
  colorBlend1.colorWriteMask = ~vk::ColorComponentFlags();  // All
  vk::PipelineColorBlendStateCreateInfo colorBlend(
      /*flags=*/{}, /*logicOpEnable=*/false, /*logicOp=*/{}, colorBlend1);

  vk::DescriptorSetLayoutBinding binding(
      /*binding=*/0, vk::DescriptorType::eUniformBuffer, /*descriptorCount=*/1,
      vk::ShaderStageFlagBits::eVertex, /*immutableSamplers=*/nullptr);
  descriptorSetLayout_ =
      gDevice.createDescriptorSetLayout({/*flags=*/{}, binding});

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
            /*depthStencil=*/{}, &colorBlend, &dynamicState, layout_,
            gRenderPass, /*subpass=*/0, /*basePipeline=*/{}}});

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
  vk::DeviceSize size = uniformSize<MVP>() * kMaxImagesInFlight;
  gUniformBuffer = gDevice.createBuffer(
      {/*flags=*/{}, size, vk::BufferUsageFlagBits::eUniformBuffer,
       vk::SharingMode::eExclusive});
  uint32_t stagingMemoryType = getMemoryForBuffer(
      gUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible |
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

  size_t offset = gFrameIndex * uniformSize<MVP>();
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
  vk::DescriptorPoolSize size(vk::DescriptorType::eUniformBuffer,
                              /*count=*/kMaxImagesInFlight);
  pool_ = gDevice.createDescriptorPool(
      {/*flags=*/{}, /*maxSets=*/kMaxImagesInFlight, size});

  std::vector<vk::DescriptorSetLayout> layouts(kMaxImagesInFlight, layout);
  descriptorSets_ = gDevice.allocateDescriptorSets({pool_, layouts});

  for (size_t i = 0; i < kMaxImagesInFlight; ++i) {
    vk::DeviceSize stride = uniformSize<MVP>();
    vk::DescriptorBufferInfo bufferInfo(gUniformBuffer, stride * i, stride);
    vk::WriteDescriptorSet write(
        descriptorSets_[i], /*binding=*/0, /*arrayElement=*/0,
        vk::DescriptorType::eUniformBuffer, /*imageInfo=*/{}, bufferInfo,
        /*texelBufferView=*/{});
    gDevice.updateDescriptorSets(write, {});
  }
}

DescriptorPool::~DescriptorPool() { gDevice.destroy(pool_); }

CommandBuffers::CommandBuffers(const Pipeline &pipeline,
                               const DescriptorPool &descriptorPool,
                               const VertexBuffers &vertices) {
  commandPool_ =
      gDevice.createCommandPool({/*flags=*/{}, gGraphicsQueueFamilyIndex});

  commandBuffers_ = gDevice.allocateCommandBuffers(
      {commandPool_, vk::CommandBufferLevel::ePrimary, kMaxImagesInFlight});

  for (uint32_t i = 0; i < kMaxImagesInFlight; ++i) {
    vk::CommandBuffer buf = commandBuffers_[i];
    buf.begin(vk::CommandBufferBeginInfo());
    buf.setViewport(/*index=*/0, gViewport);
    buf.setScissor(/*index=*/0, gScissor);
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
    buf.end();
  }
}

CommandBuffers::~CommandBuffers() { gDevice.destroy(commandPool_); }
