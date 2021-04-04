#include "drawdata.hpp"

#include "driver.hpp"

TransferManager *gTransferManager;
TransferManager::TransferManager() {
  transferCommandPool_ = gDevice.createCommandPool(
      {vk::CommandPoolCreateFlagBits::eTransient, gGraphicsQueueFamilyIndex});
  gTransferManager = this;
}

Transfer TransferManager::newTransfer(vk::DeviceSize size) {
  Transfer ret;
  ret.buffer_ = gDevice.createBuffer({/*flags=*/{}, size,
                                      vk::BufferUsageFlagBits::eTransferSrc,
                                      vk::SharingMode::eExclusive});

  uint32_t stagingMemoryType =
      getMemoryFor(gDevice.getBufferMemoryRequirements(ret.buffer_),
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  ret.memory_ = gDevice.allocateMemory({size, stagingMemoryType});
  gDevice.bindBufferMemory(ret.buffer_, ret.memory_, /*offset=*/0);
  ret.pointer_ = (char *)gDevice.mapMemory(ret.memory_, /*offset=*/0, size);

  ret.cmd_ = gDevice.allocateCommandBuffers(
      {transferCommandPool_, vk::CommandBufferLevel::ePrimary, 1})[0];
  ret.cmd_.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  ret.done_ = gDevice.createFence({});

  currentBuffers_.push_back(ret);
  return ret;
}

void Transfer::copy(vk::Buffer from, vk::Buffer to, vk::DeviceSize size,
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

Transfer::~Transfer() {
  cmd_.end();
  vk::SubmitInfo submit;
  submit.setCommandBuffers(cmd_);
  gGraphicsQueue.submit(submit, done_);
  gDevice.unmapMemory(memory_);
}

void TransferManager::collectGarbage() {
  for (auto it = currentBuffers_.begin(); it != currentBuffers_.end();) {
    if (gDevice.getFenceStatus(it->done_) == vk::Result::eSuccess) {
      gDevice.destroy(it->done_);
      gDevice.destroy(it->buffer_);
      gDevice.free(it->memory_);
      it = currentBuffers_.erase(it);
    } else
      ++it;
  }
  if (currentBuffers_.empty()) gDevice.resetCommandPool(transferCommandPool_);
}

TransferManager::~TransferManager() {
  gDevice.destroy(transferCommandPool_);
  gTransferManager = nullptr;
}

VertexBuffers::VertexBuffers(const Gltf &model) {
  vk::DeviceSize size = model.bufferSize();
  Transfer transfer = gTransferManager->newTransfer(size);
  model.readBuffers(transfer.pointer_);

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

  transfer.copy(transfer.buffer_, buffer_, size,
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

  Transfer transfer = gTransferManager->newTransfer(size);
  char *pointer = transfer.pointer_;
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
  transfer.cmd_.copyBufferToImage(transfer.buffer_, image_,
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

DescriptorPool::DescriptorPool(vk::DescriptorSetLayout layout,
                               const Textures &textures, const Gltf &gltf) {
  vk::DeviceSize sceneSize = gltf.uniformsSize();
  Transfer transfer = gTransferManager->newTransfer(sceneSize);
  gltf.readUniforms(transfer.pointer_);

  scene_ = gDevice.createBuffer({/*flags=*/{}, sceneSize,
                                 vk::BufferUsageFlagBits::eUniformBuffer |
                                     vk::BufferUsageFlagBits::eTransferDst,
                                 vk::SharingMode::eExclusive});
  memory_ = gDevice.allocateMemory(
      {sceneSize, getMemoryFor(gDevice.getBufferMemoryRequirements(scene_),
                               vk::MemoryPropertyFlagBits::eDeviceLocal)});
  gDevice.bindBufferMemory(scene_, memory_, /*offset=*/0);

  transfer.copy(transfer.buffer_, scene_, sceneSize,
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
