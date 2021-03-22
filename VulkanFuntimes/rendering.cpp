#include "rendering.hpp"

#include <fstream>
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"

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

  layout_ = gDevice.createPipelineLayout({vk::PipelineLayoutCreateFlags(),
                                          /*setLayouts=*/{},
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
}

CommandBuffers::CommandBuffers(vk::Pipeline pipeline,
                               const VertexBuffers &vertices) {
  uint32_t nBuffers = (uint32_t)gFramebuffers.size();
  commandBuffers_ = gDevice.allocateCommandBuffers(
      {gCommandPool, vk::CommandBufferLevel::ePrimary, nBuffers});

  for (uint32_t i = 0; i < nBuffers; ++i) {
    vk::CommandBuffer buf = commandBuffers_[i];
    buf.begin(vk::CommandBufferBeginInfo());
    buf.setViewport(/*index=*/0, gViewport);
    buf.setScissor(/*index=*/0, gScissor);
    vk::ClearValue clearColor(
        vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 1}));
    buf.beginRenderPass(
        {gRenderPass, gFramebuffers[i], /*renderArea=*/
         vk::Rect2D(/*offset=*/{0, 0}, gSwapchainExtent), clearColor},
        vk::SubpassContents::eInline);
    buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
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
