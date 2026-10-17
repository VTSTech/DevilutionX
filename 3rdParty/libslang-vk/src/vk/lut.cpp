// libslang-vk — LUT texture upload (M3, plan §11): decodes image files to
// RGBA8 and uploads them as R8G8B8A8_UNORM with the LUT's own sampler
// options and a pre-generated full mip chain when the preset sets
// `name_mipmap = true` (SPEC 3.5).
//
// Divergences from the reference host (documented in PROVENANCE.md):
//   - decode via vendored stb_image.h (public domain) instead of SDL_image:
//     PNG/JPEG/BMP/TGA/PSD/GIF/PNM supported; .dds/.webp/.exr are not and
//     fail with an explicit error;
//   - 8-bit precision upload (no float LUT preservation), matching the
//     reference host's R8G8B8A8_UNORM path.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include "vk_internal.hpp"
#include "impls.hpp"

namespace slv {
namespace vk {

namespace {

// ROB-style cap on LUT file sizes: LUTs are small assets; anything beyond
// this is a planted file and fails with a named error instead of a giant
// allocation (the preset parser applies the same policy to .slangp files).
constexpr std::streamoff kMaxLutFileBytes = 256 * 1024 * 1024;

// Git-lfs pointer files show up when shader packs are fetched as archives
// without LFS smudging. SPEC 3.5 wants the explicit diagnostic instead of a
// bare "unsupported image format".
constexpr const char *kLfsPointerPrefix = "version https://git-lfs";

auto ReadWholeFile(const std::string &path, std::vector<uint8_t> &out, std::string &error) -> bool
{
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
                error = "cannot open LUT image file: " + path;
                return false;
        }
        stream.seekg(0, std::ios::end);
        const std::streamoff bytes = stream.tellg();
        stream.seekg(0, std::ios::beg);
        if (bytes < 0) {
                error = "cannot determine LUT image size: " + path;
                return false;
        }
        if (bytes > kMaxLutFileBytes) {
                error = "LUT image file too large (" + path + ": " + std::to_string(bytes)
                    + " bytes; limit is 256 MiB)";
                return false;
        }
        out.resize(static_cast<size_t>(bytes));
        if (bytes > 0)
                stream.read(reinterpret_cast<char *>(out.data()), bytes);
        if (!stream && !stream.eof()) {
                error = "cannot read LUT image file: " + path;
                return false;
        }
        return true;
}

} // namespace

auto LoadLutPixels(const std::string &absolutePath, std::vector<uint8_t> &outRgba, uint32_t &outWidth,
    uint32_t &outHeight, std::string &error) -> bool
{
        std::vector<uint8_t> file;
        if (!ReadWholeFile(absolutePath, file, error))
                return false;
        if (file.empty()) {
                error = "LUT image file is empty: " + absolutePath;
                return false;
        }
        if (file.size() > 23 && std::equal(kLfsPointerPrefix, kLfsPointerPrefix + 23, file.begin())) {
                error = "LUT image is a git-lfs POINTER file, not image data (fetch the pack with LFS enabled): "
                    + absolutePath;
                return false;
        }

        int width = 0;
        int height = 0;
        int components = 0;
        stbi_uc *decoded =
            stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &width, &height, &components, 4);
        if (decoded == nullptr) {
                error = std::string("stb_image: ") + (stbi_failure_reason() ? stbi_failure_reason() : "decode failed")
                    + " (" + absolutePath
                    + "; supported formats: png, jpg, bmp, tga, psd, gif, pnm — see PROVENANCE.md)";
                return false;
        }
        if (width <= 0 || height <= 0) {
                stbi_image_free(decoded);
                error = "LUT image has a zero extent: " + absolutePath;
                return false;
        }

        outRgba.assign(decoded, decoded + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        stbi_image_free(decoded);
        outWidth = static_cast<uint32_t>(width);
        outHeight = static_cast<uint32_t>(height);
        return true;
}

auto UploadLutTexture(Device &device, const TextureDef &def, const std::string &resolvedPath,
    Preset::Impl::LutGpu &out) -> std::expected<void, Error>
{
        std::vector<uint8_t> rgba;
        std::string decodeError;
        if (!LoadLutPixels(resolvedPath, rgba, out.width, out.height, decodeError))
                return std::unexpected(SLV_ERROR(Errc::preset_parse, "LUT texture '" + def.name + "': " + decodeError));

        // Full mip chain at upload when `name_mipmap = true` (SPEC 3.5): the
        // LUT is static, so the chain is generated once here, not per frame.
        out.mipLevels = def.mipmap ? MipLevelsForExtent(out.width, out.height) : 1;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = {out.width, out.height, 1};
        imageInfo.mipLevels = out.mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC: mip generation blits read lower levels; TRANSFER_DST:
        // the staging copy. SAMPLED: every bind.
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(device.impl().allocator, &imageInfo, &allocInfo, &out.img.image, &out.img.allocation,
                nullptr)
            != VK_SUCCESS)
                return std::unexpected(
                    SLV_ERROR(Errc::vk_out_of_memory, "LUT texture '" + def.name + "': image allocation failed"));

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = out.img.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, out.mipLevels, 0, 1};
        SLV_VK(vkCreateImageView(device.device(), &viewInfo, nullptr, &out.img.view), Errc::vk_error);

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        void *stagingMapped = nullptr;
        auto stagingOk = CreateStagingBuffer(device, static_cast<VkDeviceSize>(rgba.size()), &stagingBuffer,
            &stagingAllocation, &stagingMapped);
        if (!stagingOk)
                return std::unexpected(stagingOk.error());
        std::memcpy(stagingMapped, rgba.data(), rgba.size());

        auto recorded = device.impl().SubmitAndWait([&](VkCommandBuffer cmd) {
                // Level 0 -> TRANSFER_DST for the staging copy; levels >= 1 ->
                // SHADER_READ_ONLY, which is the state RecordMipChain expects
                // them in (same prep as the chain's MakePassTarget; a fresh
                // UNDEFINED image may transition any level once, VUID-01197).
                VkImageMemoryBarrier copyBarrier{};
                copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                copyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                copyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                copyBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                copyBarrier.image = out.img.image;
                copyBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                    nullptr, 0, nullptr, 1, &copyBarrier);
                if (out.mipLevels > 1) {
                        VkImageMemoryBarrier chainBarrier = copyBarrier;
                        chainBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        chainBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 1, out.mipLevels - 1, 0, 1};
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &chainBarrier);
                }

                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {out.width, out.height, 1};
                vkCmdCopyBufferToImage(
                    cmd, stagingBuffer, out.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

                // RecordMipChain rests every level in SHADER_READ_ONLY; for a
                // 1-mip LUT the copy transition does it directly.
                if (out.mipLevels > 1)
                        RecordMipChain(cmd, out.img.image, out.mipLevels, out.width, out.height,
                            /*level0InTransferDst=*/true);
                else
                        BarrierMipLevel(cmd, out.img.image, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
        });
        vmaDestroyBuffer(device.impl().allocator, stagingBuffer, stagingAllocation);
        if (!recorded)
                return std::unexpected(recorded.error());
        return {};
}

} // namespace vk
} // namespace slv
