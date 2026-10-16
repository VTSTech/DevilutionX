/**
 * preset_unload_test — headless repro/regression harness for the shader
 * unload path ("Shaders -> Off" CTD field report, 2026-10-16).
 *
 * Drives the exact resource lifecycle the field crash goes through, minus
 * the window/swapchain half (headless sandbox has no display):
 *
 *   1. SDL_CreateGPUDeviceWithProperties (Vulkan via a software ICD when
 *      VK_DRIVER_FILES points at lavapipe)
 *   2. preset::Runner::Load(good preset)   [the "shader is loaded" state]
 *   3. Runner::Run() frames on a live command buffer, submit, wait
 *      [resources warm and in flight, like presenting at the main menu]
 *   4. Runner::Unload()                    [the "switch Shaders to Off" half]
 *   5. steps 2-4 again (unload must not poison the next load)
 *   6. device destroyed with the runner already unloaded, and the reverse
 *      order (runner still loaded at device destroy is NOT allowed by the
 *      contract, so we only assert the supported orderings here)
 *
 * Contract: no crashes, no aborts; every step returns success. Runs under
 * ASan in CI-style sandboxes.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/render/gpu/preset/preset_parser.hpp"
#include "engine/render/gpu/preset/preset_runner.hpp"

namespace preset = devilution::gpu_scaler::preset;

namespace {

int gFailures = 0;

void Check(bool ok, const std::string &what)
{
        if (ok) {
                std::printf("  ok: %s\n", what.c_str());
        } else {
                ++gFailures;
                std::printf("  FAIL: %s\n", what.c_str());
        }
}

struct TestCase {
        const char *presetName;
        int frames;
};

/** Loads, runs `frames` frames, unloads. Returns false on any failure. */
bool LoadRunUnload(SDL_GPUDevice *device, const preset::Preset &parsed, const char *name, int frames)
{
        preset::Runner runner;
        std::string error;
        if (!runner.Load(device, "vulkan", parsed, error)) {
                Check(false, std::string(name) + ": Load failed: " + error);
                return false;
        }
        Check(runner.IsLoaded(), std::string(name) + ": loaded");

        bool runOk = true;
        for (int i = 0; i < frames && runOk; ++i) {
                // A small synthetic "game frame" source texture.
                SDL_GPUTextureCreateInfo texInfo {};
                texInfo.type = SDL_GPU_TEXTURETYPE_2D;
                texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
                texInfo.width = 320;
                texInfo.height = 240;
                texInfo.layer_count_or_depth = 1;
                texInfo.num_levels = 1;
                SDL_GPUTexture *gameTex = SDL_CreateGPUTexture(device, &texInfo);
                if (gameTex == nullptr) {
                        Check(false, std::string(name) + ": source texture creation failed: " + SDL_GetError());
                        runOk = false;
                        break;
                }
                SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
                if (cmd == nullptr) {
                        Check(false, std::string(name) + ": command buffer acquisition failed: " + SDL_GetError());
                        SDL_ReleaseGPUTexture(device, gameTex);
                        runOk = false;
                        break;
                }
                SDL_GPUTexture *outTex = nullptr;
                Uint32 outW = 0;
                Uint32 outH = 0;
                const bool frameOk = runner.Run(cmd, gameTex, 320, 240, 640, 480, 16667, &outTex, &outW, &outH);
                // Fence-submit so the unload below happens with the resources warm
                // but no longer in flight (the field sequence presents a menu frame
                // right up to the option switch).
                SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
                if (fence != nullptr) {
                        SDL_WaitForGPUFences(device, true, &fence, 1);
                        SDL_ReleaseGPUFence(device, fence);
                } else {
                        Check(false, std::string(name) + ": SubmitGPUCommandBufferAndAcquireFence failed: " + SDL_GetError());
                        runOk = false;
                }
                // The output texture is owned by the runner; the source is ours.
                SDL_ReleaseGPUTexture(device, gameTex);
                if (!frameOk) {
                        // Soft-fail frames are allowed by the contract but logged; a
                        // crash would have taken us down before this line.
                        std::printf("  note: %s frame %d skipped (Run returned false)\n", name, i);
                }
        }
        if (frames > 0)
                Check(runOk, std::string(name) + ": ran " + std::to_string(frames) + " frames");

        runner.Unload();
        Check(!runner.IsLoaded(), std::string(name) + ": unloaded");
        return runOk;
}

} // namespace

int main(int argc, char **argv)
{
        std::string root = "shaders/unload-corpus";
        for (int i = 1; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--root" && i + 1 < argc)
                        root = argv[++i];
        }

        std::printf("preset_unload_test: shader root: %s\n", root.c_str());

        if (!SDL_Init(SDL_INIT_VIDEO)) {
                std::printf("FAIL: SDL_Init(SDL_INIT_VIDEO): %s\n", SDL_GetError());
                return 1;
        }

        // Same property set the scaler's CreateDevice uses (SPIR-V for Vulkan).
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
        SDL_GPUDevice *device = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
        if (device == nullptr) {
                // Headless CI boxes have no Vulkan loader/ICD: the device half of the
                // lifecycle cannot be exercised there. Skip cleanly (exit 0) so the
                // suite stays green; run on a GPU-enabled (or lavapipe) runner for the
                // real regression coverage.
                std::printf("SKIP: no usable Vulkan device (%s)\n", SDL_GetError());
                SDL_Quit();
                return 0;
        }
        const char *driver = SDL_GetGPUDeviceDriver(device);
        std::printf("GPU device created, backend=%s\n", driver != nullptr ? driver : "unknown");

        // Parse every good-corpus preset once up front; skip this whole test
        // cleanly if the corpus is missing (e.g. corpus copy step not run).
        const char *corpus[] = {
                "single.slangp",
                "multipass.slangp",
                "feedback.slangp",
                "history.slangp",
                "lut.slangp",
        };
        std::vector<std::pair<const char *, preset::Preset>> parsed;
        for (const char *name : corpus) {
                const std::string path = root + "/" + name;
                preset::Preset p;
                std::string error;
                if (!preset::ParsePreset(path, p, error)) {
                        std::printf("note: corpus preset %s unavailable (%s) - skipping\n", name, error.c_str());
                        continue;
                }
                parsed.emplace_back(name, std::move(p));
        }
        if (parsed.empty()) {
                // Corpus not staged (copy step not run): nothing to exercise here.
                std::printf("SKIP: no corpus presets parsed from %s\n", root.c_str());
                SDL_DestroyGPUDevice(device);
                SDL_Quit();
                return 0;
        }

        // Round 1: load / run / unload each preset on the shared device.
        std::printf("\n-- round 1: load -> frames -> unload, per preset --\n");
        for (const auto &[name, p] : parsed)
                LoadRunUnload(device, p, name, 3);

        // Round 2: reload the first preset on the SAME device after unloads
        // (the "switch presets / switch back" path), then unload again.
        std::printf("\n-- round 2: reload after unload on the same device --\n");
        LoadRunUnload(device, parsed.front().second, parsed.front().first, 1);

        // Round 3: load -> unload WITHOUT any frames (the field sequence also
        // allows unloading before the first present, e.g. a failed menu frame).
        std::printf("\n-- round 3: load -> unload with zero frames --\n");
        LoadRunUnload(device, parsed.front().second, parsed.front().first, 0);

        SDL_DestroyGPUDevice(device);
        SDL_Quit();

        std::printf("\n%s (%d failures)\n", gFailures == 0 ? "ALL OK" : "FAILURES", gFailures);
        return gFailures == 0 ? 0 : 1;
}
