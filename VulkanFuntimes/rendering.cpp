#include "rendering.hpp"

#include <fstream>
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"

#include "util.hpp"
#include "driver.hpp"
#include "swapchain.hpp"

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

const std::vector<Vertex> vertices = {{{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                                      {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
                                      {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}};

uint32_t getMemoryForBuffer(vk::Buffer buffer) {
  vk::MemoryRequirements memoryRequirements =
      gDevice.getBufferMemoryRequirements(buffer);
  vk::MemoryPropertyFlags memFlagRequirements =
      vk::MemoryPropertyFlagBits::eHostVisible |
      vk::MemoryPropertyFlagBits::eHostCoherent;

  vk::PhysicalDeviceMemoryProperties memProperties =
      gPhysicalDevice.getMemoryProperties();

  for (uint32_t i = 0; i < memProperties.memoryTypes.size(); ++i) {
    if (memoryRequirements.memoryTypeBits & (1 << i) &&
        memProperties.memoryTypes[i].propertyFlags & memFlagRequirements)
      return i;
  }
  throw std::runtime_error("No memory type found for buffer");
}

VertexBuffers::VertexBuffers() {
  vk::DeviceSize size = sizeof(Vertex) * vertices.size();
  buffer_ = gDevice.createBuffer({/*flags=*/{}, size,
                                  vk::BufferUsageFlagBits::eVertexBuffer,
                                  vk::SharingMode::eExclusive});

  uint32_t memoryType = getMemoryForBuffer(buffer_);
  memory_ = gDevice.allocateMemory({size, memoryType});
  gDevice.bindBufferMemory(buffer_, memory_, /*offset=*/0);
  void *mapped = gDevice.mapMemory(memory_, /*offset=*/0, size);
  std::copy_n((char *)&vertices[0], size, (char *)mapped);
  gDevice.unmapMemory(memory_);
  
  count_ = (uint32_t)vertices.size();
}
VertexBuffers::~VertexBuffers() {
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
    vk::DeviceSize bufferOffset = 0;
    buf.bindVertexBuffers(/*offset=*/0, vertices.buffer_, bufferOffset);
    buf.draw(/*vertexCount=*/3, /*instanceCount=*/1, /*firstVertex=*/0,
             /* firstInstance=*/0);
    buf.endRenderPass();
    buf.end();
  }
}
