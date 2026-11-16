#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_UPSCALER_FRAMEGENERATION_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_UPSCALER_FRAMEGENERATION_H_

// DLSS Frame Generation through NVIDIA Streamline (KYTY_FRAMEGEN=<generated frames per rendered
// frame, 1..5>), after the Demon's Souls fork's frame-gen.cpp. Streamline's runtime
// (sl.interposer.dll and its plugins, NVIDIA's own files) is the user's: KYTY_STREAMLINE_DIR, a
// "streamline" folder next to the emulator, or the SDK unpacked under _Build/deps/streamline/sdk.
// It becomes the process' Vulkan loader, so the instance, the device and the swapchain are made
// through it and it shows the generated frames inside vkQueuePresentKHR.
//
// The inputs are the game's own: the depth and the motion vectors of the frame, which the
// renderer thread finds in the game's temporal upscaler (FindInputs) and takes right after that
// pass (CaptureInputs); at the guest flip they go with the presenter's frame (PrepareInputs) and
// the presenting thread tags them for Streamline (BeforePresent). A frame without them (a menu,
// a movie, a loading screen) is shown as it is.

#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/image/image.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Libs::Graphics {
class CommandBuffer;
class TextureCache;
struct PreparedBindings;
namespace ShaderRecompiler::IR {
struct ResourceSnapshot;
} // namespace ShaderRecompiler::IR

namespace FrameGen {

// What the game's view constants tell about a frame, as far as they are known. Matrices are
// row-major with row vectors (clip = view * view_to_clip), without jitter.
struct View {
	// The part of the depth and motion images the frame was rendered into (dynamic resolution).
	uint32_t width = 0, height = 0;
	// Motion vector * scale = the way to the pixel's previous position, in parts of the view.
	std::array<float, 2>  motion_scale {};
	std::array<float, 2>  jitter {}; // the projection's sample offset, in pixels
	std::array<float, 16> view_to_clip {};
	std::array<float, 16> clip_to_prev_clip {};
	std::array<float, 3>  position {}, right {}, up {}, forward {};
	float                 near_plane = 0.1f, vertical_fov = 1.0f;
	bool                  camera = false; // the matrices and the camera's frame are the game's
	bool                  cut    = false; // the game starts its history again in this frame
};

// A frame's depth (the game's format) and motion vectors (16_16_FLOAT), as DLSS takes them.
struct Images {
	VulkanImage   depth, motion;
	vk::ImageView depth_view = nullptr, motion_view = nullptr;
};

// One presented frame's inputs, owned by the presenter's frame.
struct FrameInputs {
	std::unique_ptr<Images> images;
	View                    view;
	uint64_t                guest_frame = 0;
	bool                    valid       = false;
};

// The NVIDIA driver makes the device Streamline asks for only while it finds address space below
// 1 TB (2^40). The emulator reserves the guest's, up to 0xfc00000000, and what it maps and
// allocates itself afterwards packs upward from there, so by the time the device is made little
// of the 16 GB up to 2^40 is left and the driver refuses (VK_ERROR_INITIALIZATION_FAILED). With
// KYTY_FRAMEGEN set they are kept aside from the process' first moment (HoldDriverAddressSpace,
// before anything maps memory) until right before vkCreateDevice (ReleaseDriverAddressSpace).
void HoldDriverAddressSpace();
void ReleaseDriverAddressSpace();
// Process start, before the window: loads Streamline when KYTY_FRAMEGEN is set.
void Initialize();
// After Initialize: the library SDL has to load as the Vulkan loader (Streamline's interposer),
// or null.
[[nodiscard]] const char* VulkanLibrary();
// After Initialize: Streamline is loaded (the device then enables what it relies on but does not
// add itself, VK_KHR_present_id for its VK_NV_low_latency2).
[[nodiscard]] bool Requested();
// Before the device is created: why Streamline says DLSS-G cannot run with this GPU, driver and
// Windows (e.g. hardware-accelerated GPU scheduling turned off); empty when it can, or when
// frame generation is off.
[[nodiscard]] std::string SupportProblem(vk::PhysicalDevice physical_device);
// After the device exists: checks that DLSS-G is available on it.
void OnDevice(vk::Instance instance, vk::PhysicalDevice physical_device, vk::Device device);
// Right after the dispatcher is loaded for the device (before the recording layer copies it):
// the command-buffer calls Streamline intercepts only for its command-list state tracking
// (disabled here) go straight to the driver.
void BypassCommandHooks(vk::Device device);
[[nodiscard]] bool Enabled();

// The presenting thread, around vkQueuePresentKHR of the image showing `inputs` (null: a frame
// without inputs). `repeated`: the frame was shown before (the system overlay changed over it):
// nothing is generated for it, and it does not count as a frame without inputs. `backbuffer` is
// the swapchain extent, `region` the part the game image covers.
void BeforePresent(const FrameInputs* inputs, bool repeated, vk::Extent2D backbuffer, vk::Rect2D region,
                   vk::Format format, uint32_t image_count);
void AfterPresent();
// The presenting thread: the swapchain is made again.
void SwapchainChanging();
// Any thread: a frame with inputs was never shown: the next one resets DLSS-G's history.
void NoteDroppedFrame();
// The frames DLSS-G showed per presented frame (1 without generation), as last queried.
[[nodiscard]] uint32_t PresentedFrames();

// Renderer thread, a compute dispatch with its bindings resolved, before it is recorded: notes it
// when it is the first pass of the game's temporal upscaler, which has the frame's depth and
// writes its motion vectors.
void FindInputs(uint64_t shader_hash, const ShaderRecompiler::IR::ResourceSnapshot& resources,
                const PreparedBindings& bindings, std::span<const uint32_t, 3> groups);
// Renderer thread, right after a dispatch is recorded: when FindInputs noted it, copies its
// depth and decodes its motion vectors (the game may use the images' memory for something else
// later in the frame).
void CaptureInputs(GraphicContext& graphics, TextureCache& cache, CommandBuffer& command);
// Renderer thread, at the guest flip: what was captured since the last flip goes to `inputs`.
void PrepareInputs(FrameInputs& inputs);
void DestroyInputs(GraphicContext& graphics, FrameInputs& inputs);

} // namespace FrameGen
} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_UPSCALER_FRAMEGENERATION_H_
