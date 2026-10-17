// libslang-vk — Image helpers (M1): owning 2D images + pixel readback for
// hosts/tests without their own allocator (headless CI, golden images).
// M3: hosts the shared mip-chain / barrier / staging utilities consumed by
// the chain (pass targets, history ring, mip refill) and the LUT uploader.

#include <algorithm>
#include <cstring>
#include <utility>

#include "vk_internal.hpp"
#include "impls.hpp"

namespace slv {

namespace vk {

void BarrierMipLevel(VkCommandBuffer cmd, VkImage image, uint32_t level, uint32_t levelCount, VkImageLayout oldLayout,
    VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkAccessFlags srcAccess,
    VkAccessFlags dstAccess)
{
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, levelCount, 0, 1};
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);
}

// Refills a full mip chain from level 0 (F1 mip plan; linear blits between
// consecutive levels). Level 0 starts in SHADER_READ_ONLY (render pass
// finalLayout) or TRANSFER_DST (freshly blitted/copied); every level rests
// in SHADER_READ_ONLY afterwards. Shared by pass targets, the pass-0 input
// copy and mipmap LUT uploads (M3).
void RecordMipChain(VkCommandBuffer cmd, VkImage image, uint32_t levels, uint32_t width, uint32_t height,
    bool level0InTransferDst)
{
        if (levels <= 1)
                return;
        constexpr VkPipelineStageFlags kTransfer = VK_PIPELINE_STAGE_TRANSFER_BIT;
        constexpr VkPipelineStageFlags kAll = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

        if (level0InTransferDst)
                BarrierMipLevel(cmd, image, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kTransfer, kTransfer, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
        else
                BarrierMipLevel(cmd, image, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kAll, kTransfer, VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);

        int32_t mipW = static_cast<int32_t>(width);
        int32_t mipH = static_cast<int32_t>(height);
        for (uint32_t l = 1; l < levels; ++l) {
                BarrierMipLevel(cmd, image, l, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kAll, kTransfer, VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT);
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l - 1, 0, 1};
                blit.srcOffsets[1] = {std::max(1, mipW), std::max(1, mipH), 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
                blit.dstOffsets[1] = {std::max(1, mipW >> 1), std::max(1, mipH >> 1), 1};
                vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                // The source level is consumed: rest it in SHADER_READ_ONLY.
                BarrierMipLevel(cmd, image, l - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kTransfer, kAll, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_SHADER_READ_BIT);
                if (l + 1 < levels)
                        BarrierMipLevel(cmd, image, l, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kTransfer, kTransfer, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT);
                else
                        BarrierMipLevel(cmd, image, l, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kTransfer, kAll, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
                mipW = std::max(1, mipW >> 1);
                mipH = std::max(1, mipH >> 1);
        }
}

auto CreateStagingBuffer(Device &device, VkDeviceSize bytes, VkBuffer *outBuffer, VmaAllocation *outAllocation,
    void **outMapped) -> std::expected<void, Error>
{
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bytes;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo mappedInfo{};
        if (vmaCreateBuffer(device.impl().allocator, &bufferInfo, &allocInfo, outBuffer, outAllocation, &mappedInfo)
            != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "staging buffer allocation failed"));
        *outMapped = mappedInfo.pMappedData;
        return {};
}

} // namespace vk

struct Image::Impl {
        Device *device = nullptr;
        vk::VkImageWithView obj;
        ImageDesc desc;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

        ~Impl()
        {
                if (device == nullptr)
                        return;
                VkDevice dev = device->device();
                if (obj.view != VK_NULL_HANDLE)
                        vkDestroyImageView(dev, obj.view, nullptr);
                if (obj.image != VK_NULL_HANDLE)
                        vmaDestroyImage(device->impl().allocator, obj.image, obj.allocation);
        }
};

auto Image::create(Device &device, const CreateInfo &info) -> std::expected<Image, Error>
{
        if (info.desc.width == 0 || info.desc.height == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Image::create: zero extent"));
        const uint32_t texelSize = vk::FormatTexelSize(info.desc.format);
        if (texelSize == 0)
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                    "Image::create: unsupported VkFormat " + std::to_string(static_cast<int>(info.desc.format))));
        if (info.pixels != nullptr && info.pixelBytes != static_cast<size_t>(info.desc.width) * info.desc.height * texelSize)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                    "Image::create: pixelBytes mismatch (want width*height*texelSize)"));

        Image out;
        out.impl_ = std::make_unique<Impl>();
        Impl &impl = *out.impl_;
        impl.device = &device;
        impl.desc = info.desc;
        impl.layout = info.finalLayout;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = info.desc.format;
        imageInfo.extent = {info.desc.width, info.desc.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = info.usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(device.impl().allocator, &imageInfo, &allocInfo, &impl.obj.image, &impl.obj.allocation,
                nullptr)
            != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "image allocation failed"));

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = impl.obj.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = info.desc.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        SLV_VK(vkCreateImageView(device.device(), &viewInfo, nullptr, &impl.obj.view), Errc::vk_error);

        // Upload + transition through a one-shot command buffer.
        const bool upload = info.pixels != nullptr;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        void *stagingMapped = nullptr;
        if (upload) {
                auto stagingOk = vk::CreateStagingBuffer(
                    device, static_cast<VkDeviceSize>(info.pixelBytes), &stagingBuffer, &stagingAllocation,
                    &stagingMapped);
                if (!stagingOk)
                        return std::unexpected(stagingOk.error());
                std::memcpy(stagingMapped, info.pixels, info.pixelBytes);
        }

        auto recorded = device.impl().SubmitAndWait([&](VkCommandBuffer cmd) {
                // Classic (core-1.0) barriers: the M1 floor is Vulkan 1.1, so
                // sync2's vkCmdPipelineBarrier2 is not guaranteed. M2 introduces
                // capability-gated sync2/dynamic-rendering paths (plan §8).
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = upload ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : info.finalLayout;
                barrier.image = impl.obj.image;
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    upload ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                    nullptr, 1, &barrier);

                if (upload) {
                        VkBufferImageCopy copy{};
                        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        copy.imageExtent = {info.desc.width, info.desc.height, 1};
                        vkCmdCopyBufferToImage(cmd, stagingBuffer, impl.obj.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1, &copy);

                        VkImageMemoryBarrier toFinal = barrier;
                        toFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        toFinal.newLayout = info.finalLayout;
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                            0, nullptr, 0, nullptr, 1, &toFinal);
                }
        });
        if (upload)
                vmaDestroyBuffer(device.impl().allocator, stagingBuffer, stagingAllocation);
        if (!recorded)
                return std::unexpected(recorded.error());
        return out;
}

Image::~Image() = default;
Image::Image(Image &&) noexcept = default;
auto Image::operator=(Image &&) noexcept -> Image & = default;

auto Image::update(const void *pixels, size_t pixelBytes) -> std::expected<void, Error>
{
        if (impl_ == nullptr || impl_->device == nullptr)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Image::update: image is not initialized"));
        if (pixels == nullptr)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Image::update: null pixels"));
        const uint32_t texelSize = vk::FormatTexelSize(impl_->desc.format);
        if (texelSize == 0)
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                    "Image::update: unsupported VkFormat " + std::to_string(static_cast<int>(impl_->desc.format))));
        if (pixelBytes != static_cast<size_t>(impl_->desc.width) * impl_->desc.height * texelSize)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                    "Image::update: pixelBytes mismatch (want width*height*texelSize)"));

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        void *stagingMapped = nullptr;
        auto stagingOk = vk::CreateStagingBuffer(
            *impl_->device, static_cast<VkDeviceSize>(pixelBytes), &stagingBuffer, &stagingAllocation, &stagingMapped);
        if (!stagingOk)
                return std::unexpected(stagingOk.error());
        std::memcpy(stagingMapped, pixels, pixelBytes);

        const VkImageLayout current = impl_->layout;
        auto recorded = impl_->device->impl().SubmitAndWait([&](VkCommandBuffer cmd) {
                VkImageMemoryBarrier toDst{};
                toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toDst.oldLayout = current;
                toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toDst.image = impl_->obj.image;
                toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &toDst);

                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {impl_->desc.width, impl_->desc.height, 1};
                vkCmdCopyBufferToImage(
                    cmd, stagingBuffer, impl_->obj.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

                VkImageMemoryBarrier toRest = toDst;
                toRest.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toRest.newLayout = current;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &toRest);
        });
        vmaDestroyBuffer(impl_->device->impl().allocator, stagingBuffer, stagingAllocation);
        if (!recorded)
                return std::unexpected(recorded.error());
        return {};
}

auto Image::image() const -> VkImage { return impl_->obj.image; }
auto Image::view() const -> VkImageView { return impl_->obj.view; }
auto Image::desc() const -> const ImageDesc & { return impl_->desc; }
auto Image::layout() const -> VkImageLayout { return impl_->layout; }

auto Device::readPixels(VkImage image, VkFormat format, uint32_t width, uint32_t height, VkImageLayout currentLayout)
    -> std::expected<std::vector<uint8_t>, Error>
{
        const uint32_t texelSize = vk::FormatTexelSize(format);
        if (texelSize == 0)
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                    "readPixels: unsupported VkFormat " + std::to_string(static_cast<int>(format))));
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * texelSize;
        if (bytes == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "readPixels: zero extent"));

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bytes;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo mappedInfo{};
        if (vmaCreateBuffer(impl_->allocator, &bufferInfo, &allocInfo, &buffer, &allocation, &mappedInfo) != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "readback buffer allocation failed"));

        auto recorded = impl_->SubmitAndWait([&](VkCommandBuffer cmd) {
                // Classic barriers for the Vulkan 1.1 floor (see Image::create).
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.oldLayout = currentLayout;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.image = image;
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);

                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {width, height, 1};
                vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);

                VkImageMemoryBarrier restore = barrier;
                restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                restore.newLayout = currentLayout;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &restore);
        });

        std::vector<uint8_t> out;
        if (recorded) {
                out.resize(static_cast<size_t>(bytes));
                std::memcpy(out.data(), mappedInfo.pMappedData, static_cast<size_t>(bytes));
        }
        vmaDestroyBuffer(impl_->allocator, buffer, allocation);
        if (!recorded)
                return std::unexpected(recorded.error());
        return out;
}

} // namespace slv
