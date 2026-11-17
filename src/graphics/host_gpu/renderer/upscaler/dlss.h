#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_UPSCALER_DLSS_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_UPSCALER_DLSS_H_

#include "graphics/guest_gpu/pssr.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace Libs::Graphics {
struct GraphicContext;
} // namespace Libs::Graphics

namespace Libs::Graphics::Dlss {

struct D3D11Resource;
struct D3D12Resource;
struct Feature;

// NGX's parameter object; the declaration order is its vtable.
class Parameter {
public:
	virtual void Set(const char* name, unsigned long long value)        = 0;
	virtual void Set(const char* name, float value)                     = 0;
	virtual void Set(const char* name, double value)                    = 0;
	virtual void Set(const char* name, unsigned int value)              = 0;
	virtual void Set(const char* name, int value)                       = 0;
	virtual void Set(const char* name, D3D11Resource* value)            = 0;
	virtual void Set(const char* name, D3D12Resource* value)            = 0;
	virtual void Set(const char* name, void* value)                     = 0;
	virtual int  Get(const char* name, unsigned long long* value) const = 0;
	virtual int  Get(const char* name, float* value) const              = 0;
	virtual int  Get(const char* name, double* value) const             = 0;
	virtual int  Get(const char* name, unsigned int* value) const       = 0;
	virtual int  Get(const char* name, int* value) const                = 0;
	virtual int  Get(const char* name, D3D11Resource** value) const     = 0;
	virtual int  Get(const char* name, D3D12Resource** value) const     = 0;
	virtual int  Get(const char* name, void** value) const              = 0;
	virtual void Reset()                                                = 0;

protected:
	~Parameter() = default;
};

struct Resource {
	VkImageView             view;
	VkImage                 image;
	VkImageSubresourceRange range;
	VkFormat                format;
	uint32_t                width;
	uint32_t                height;
	int32_t                 type; // 0 is an image view
	uint8_t                 read_write;
};

struct Inputs {
	Resource  color;
	Resource  depth;
	Resource  motion;
	Resource* exposure;
	Resource  output;
};

struct Creation {
	uint32_t output_width;
	uint32_t output_height;
	uint32_t flags;
	int32_t  mode;
	uint32_t min_width;
	uint32_t min_height;
	uint32_t max_width;
	uint32_t max_height;

	bool operator==(const Creation&) const = default;
};

inline constexpr uint32_t FIXED_FLAGS = Pssr::kReverseDepth | Pssr::kAutoExposure;

[[nodiscard]] bool                    Accepts(const Pssr::Request& request);
[[nodiscard]] bool                    Takes(const Creation& creation, const Pssr::Request& request,
                                            const Inputs& inputs);
// `any_size`: a render size no mode takes gets a feature of that one size (up to three times
// smaller than the output); a title whose size changed from frame to frame would make a feature
// each time.
[[nodiscard]] std::optional<Creation> Choose(Parameter& capabilities, const Pssr::Request& request,
                                             const Inputs& inputs, bool any_size = false);
void SetCreation(Parameter& parameters, const Creation& creation, uint32_t preset);
void SetEvaluation(Parameter& parameters, const Pssr::Request& request, Inputs& inputs, bool reset);

// Call before the instance.
void Load();
void AppendExtensions(std::vector<const char*>&                   out,
                      const std::vector<vk::ExtensionProperties>& available, bool device);
void Init(const GraphicContext& graphics, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
          PFN_vkGetDeviceProcAddr get_device_proc_addr);
// Call after every feature is released and before the device is destroyed.
void                     Shutdown();
[[nodiscard]] Parameter* Capabilities(); // null while DLSS is off

// Both record into command. A null feature or false means DLSS can't take the request.
[[nodiscard]] Feature* CreateFeature(vk::CommandBuffer command, const Creation& creation);
[[nodiscard]] bool     Evaluate(vk::CommandBuffer command, const Feature* feature,
                                const Pssr::Request& request, Inputs& inputs, bool reset);
void                   ReleaseFeature(Feature* feature);

// For the record of a lost device (GraphicContext::LogLostDevice): an upscaler (DLSS or FSR)
// recorded a picture for the submission `tick`; and how many it made and that last tick. A
// device lost in another submission was not lost in the upscaler's commands.
void                                        NoteEvaluation(uint64_t tick);
[[nodiscard]] std::pair<uint64_t, uint64_t> Evaluations();

} // namespace Libs::Graphics::Dlss

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_UPSCALER_DLSS_H_
