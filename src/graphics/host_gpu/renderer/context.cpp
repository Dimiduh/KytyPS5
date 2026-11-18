#include "common/assert.h"
#include "common/common.h"
#include "common/emulatorConfig.h"
#include "common/gpuTiming.h"
#include "common/liveControl.h"
#include "common/profiler.h"
#include "common/threads.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/colorRenderTarget.h"
#include "graphics/host_gpu/renderer/debug.h"
#include "graphics/host_gpu/renderer/depthRenderTarget.h"
#include "graphics/host_gpu/renderer/image/imageView.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <algorithm>
#include <bit>
#include <cinttypes>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>
namespace Libs::Graphics {

// For `measure`: render pass instances begun (a draw whose targets or state differ from the
// open instance's, or the first draw after anything that ended it: a barrier, an upload, a
// dispatch).
static Common::LiveControl::Counter g_render_passes("render_passes");

// The GPU time profile. While the live switch `gpu_prof` is on, a timestamp is recorded before
// and after every guest dispatch and every render pass instance; the GPU's time between two
// timestamps of one submission goes to what was recorded between them: a compute shader (by its
// hash), the draws of render pass instances, or the rest (copies, clears, barriers, the
// emulator's own passes). A timestamp is taken when all earlier commands have finished, so the
// numbers are those of commands running one after the other, and the timestamps themselves
// cost some time. Turning the switch off writes the list to the log.
namespace {

Common::LiveControl::Switch g_gpu_prof("gpu_prof", 0);

class GpuProfile {
public:
	// The start of a submission (the time before it is nobody's), and the fixed names
	// (CommandBuffer::ProfileScope).
	static constexpr uint64_t Base  = 0;
	static constexpr uint64_t Other = static_cast<uint64_t>(CommandBuffer::ProfileScope::Other);
	static constexpr uint64_t Draws = static_cast<uint64_t>(CommandBuffer::ProfileScope::Draws);
	static constexpr uint64_t Dispatches =
	    static_cast<uint64_t>(CommandBuffer::ProfileScope::Dispatches);
	static constexpr uint64_t BufferUploads =
	    static_cast<uint64_t>(CommandBuffer::ProfileScope::BufferUploads);
	static constexpr uint64_t Fixed = static_cast<uint64_t>(CommandBuffer::ProfileScope::Count);

	void Mark(GraphicContext& graphics, vk::CommandBuffer buffer, uint64_t tick, uint64_t key) {
		if (m_pool == nullptr && !Create(graphics)) {
			return;
		}
		if (m_pending.size() >= Queries - 1u) {
			m_dropped++;
			return;
		}
		const auto query = static_cast<uint32_t>(m_next++ % Queries);
		buffer.resetQueryPool(m_pool, query, 1);
		buffer.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, m_pool, query);
		m_pending.push_back({tick, key, query});
	}

	// Once a frame: takes the timestamps of the submissions the GPU has finished.
	void Collect(GraphicContext& graphics, uint64_t known_tick) {
		if (m_pool == nullptr) {
			return;
		}
		bool collected = false;
		while (!m_pending.empty() && m_pending.front().tick <= known_tick) {
			// The timestamps of finished submissions whose queries follow one another, with one
			// call (a host call waits for the recording thread).
			size_t count = 1;
			while (count < m_pending.size() && count < MaxRead &&
			       m_pending[count].tick <= known_tick &&
			       m_pending[count].query == m_pending[count - 1].query + 1u) {
				count++;
			}
			m_values.resize(count);
			const bool read =
			    graphics.device.getQueryPoolResults(
			        m_pool, m_pending.front().query, static_cast<uint32_t>(count),
			        count * sizeof(uint64_t), m_values.data(), sizeof(uint64_t),
			        vk::QueryResultFlagBits::e64) == vk::Result::eSuccess;
			for (size_t i = 0; i < count && read; i++) {
				const auto& stamp = m_pending[i];
				const auto  value = m_values[i] & m_valid_mask;
				if (stamp.key != Base && m_have_last && stamp.tick == m_last_tick) {
					// A timestamp read before its submission wrote it holds an older value: time
					// that runs backwards, or for longer than any submission, is left out.
					const auto ns = static_cast<double>((value - m_last) & m_valid_mask) * m_period_ns;
					if (value >= m_last && ns < MaxScopeNs) {
						auto& time = m_times[stamp.key];
						time.ns += ns;
						time.count++;
					} else {
						m_dropped++;
					}
				}
				m_last      = value;
				m_last_tick = stamp.tick;
				m_have_last = true;
				collected   = true;
			}
			m_have_last = m_have_last && read;
			m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<ptrdiff_t>(count));
		}
		m_frames += collected ? 1u : 0u;
		if (!g_gpu_prof.On() && m_pending.empty() && m_frames != 0) {
			Report();
		}
	}

private:
	static constexpr size_t Queries = size_t {1} << 16u;
	static constexpr size_t MaxRead    = 4096;
	static constexpr double MaxScopeNs = 250e6;

	struct Stamp {
		uint64_t tick  = 0;
		uint64_t key   = 0;
		uint32_t query = 0;
	};
	struct Time {
		double   ns    = 0.0;
		uint64_t count = 0;
	};

	bool Create(GraphicContext& graphics) {
		if (m_failed) {
			return false;
		}
		uint32_t family_count = 0;
		graphics.physical_device.getQueueFamilyProperties(&family_count, nullptr);
		std::vector<vk::QueueFamilyProperties> families(family_count);
		graphics.physical_device.getQueueFamilyProperties(&family_count, families.data());
		const uint32_t bits = graphics.queue_family < family_count
		                          ? families[graphics.queue_family].timestampValidBits
		                          : 0;
		m_period_ns         = graphics.GetPhysicalDeviceProperties().limits.timestampPeriod;
		vk::QueryPoolCreateInfo create {};
		create.queryType  = vk::QueryType::eTimestamp;
		create.queryCount = static_cast<uint32_t>(Queries);
		if (bits == 0 || !(m_period_ns > 0.0) ||
		    graphics.device.createQueryPool(&create, nullptr, &m_pool) != vk::Result::eSuccess) {
			m_pool   = nullptr;
			m_failed = true;
			LOGF("GpuProf: no timestamps on this queue\n");
			return false;
		}
		m_valid_mask = bits >= 64 ? ~uint64_t {0} : (uint64_t {1} << bits) - 1u;
		return true;
	}

	void Report() {
		std::vector<std::pair<uint64_t, Time>> list(m_times.begin(), m_times.end());
		std::ranges::sort(list, [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
		const double frames = static_cast<double>(m_frames);
		double       all = 0.0, shaders = 0.0;
		uint64_t     dispatches = 0;
		size_t programs = 0;
		for (const auto& [key, time]: list) {
			all += time.ns;
			if (key == Dispatches || key >= Fixed) {
				shaders += time.ns;
				dispatches += time.count;
				programs += key >= Fixed ? 1u : 0u;
			}
		}
		LOGF("GpuProf: %" PRIu64 " frames, %.2f ms a frame between timestamps: %.2f ms in %.0f "
		     "dispatches of %zu compute shaders, %.2f ms in %.0f render pass instances; %" PRIu64
		     " timestamps left out\n",
		     m_frames, all / frames / 1e6, shaders / frames / 1e6,
		     static_cast<double>(dispatches) / frames, programs, m_times[Draws].ns / frames / 1e6,
		     static_cast<double>(m_times[Draws].count) / frames, m_dropped);
		static constexpr std::array<const char*, Fixed> Names {
		    "",               "the rest",        "draws",     "unnamed dispatches",
		    "buffer uploads", "texture uploads", "downloads", "the upscaler"};
		for (uint64_t key = Other; key < Fixed; key++) {
			if (const auto it = m_times.find(key); it != m_times.end() && key != Draws) {
				LOGF("GpuProf: %7.3f ms a frame, %6.1f a frame, %8.1f us each, %s\n",
				     it->second.ns / frames / 1e6, static_cast<double>(it->second.count) / frames,
				     it->second.ns / static_cast<double>(it->second.count) / 1e3, Names[key]);
			}
		}
		size_t shown = 0;
		for (const auto& [key, time]: list) {
			if (key < Fixed || shown++ == 40) {
				continue;
			}
			LOGF("GpuProf: %7.3f ms a frame, %6.1f a frame, %8.1f us each, shader 0x%016" PRIx64 "\n",
			     time.ns / frames / 1e6, static_cast<double>(time.count) / frames,
			     time.ns / static_cast<double>(time.count) / 1e3, key);
		}
		m_times.clear();
		m_frames  = 0;
		m_dropped = 0;
	}

	vk::QueryPool                      m_pool = nullptr;
	bool                               m_failed     = false;
	uint64_t                           m_valid_mask = 0;
	double                             m_period_ns  = 0.0;
	std::deque<Stamp>                  m_pending;
	std::vector<uint64_t>              m_values;
	uint64_t                           m_next      = 0;
	uint64_t                           m_last      = 0;
	uint64_t                           m_last_tick = 0;
	bool                               m_have_last = false;
	std::unordered_map<uint64_t, Time> m_times;
	uint64_t                           m_frames  = 0;
	uint64_t                           m_dropped = 0;
};

GpuProfile g_gpu_profile;

} // namespace

void CollectGpuProfile(RenderContext& context) {
	auto& scheduler = context.GetCommandScheduler();
	g_gpu_profile.Collect(scheduler.Graphics(), scheduler.GetMasterSemaphore().KnownGpuTick());
}

void CommandBuffer::ProfileMark(uint64_t key) const {
	if (g_gpu_prof.On()) {
		g_gpu_profile.Mark(m_graphics, m_buffer, m_context.GetCommandScheduler().CurrentTick(), key);
	}
}

void CommandBuffer::ProfileBegin() const {
	if (g_gpu_prof.On() && !m_rendering) {
		FinishCopies();
		ProfileClose();
		ProfileMark(GpuProfile::Other);
	}
}

void CommandBuffer::ProfileEnd(ProfileScope scope) const {
	m_note.scopes |= 1u << static_cast<uint32_t>(scope);
	if (g_gpu_prof.On() && !m_rendering) {
		FinishCopies();
		ProfileClose();
		ProfileMark(static_cast<uint64_t>(scope));
	}
}

void CommandBuffer::ProfileClose() const {
	// A render pass instance stays open while its draws are recorded.
	if (m_profile_open != 0 && !m_rendering) {
		ProfileMark(m_profile_open);
		m_profile_open = 0;
	}
}

CommandBuffer::CommandBuffer(CommandScheduler& scheduler)
    : m_context(scheduler.Context()), m_graphics(scheduler.Graphics()) {
	m_barriers.SetSerializeDispatches(Config::SerializeDispatchesEnabled());
}

void CommandBuffer::QueueRead(vk::Buffer source, uint64_t source_offset, vk::Buffer destination,
                              uint64_t destination_offset, uint64_t size) const {
	m_reads.push_back({source, destination, {source_offset, destination_offset, size}});
}

void CommandBuffer::FlushReads() const {
	if (m_reads.empty() || m_rendering) {
		return;
	}
	FinishCopies();
	ProfileClose();
	// The copies read what dispatches wrote.
	RecordBarrier(m_barriers.BeforeCommand());
	for (size_t first = 0; first < m_reads.size();) {
		// One copy command for the reads between the same two buffers.
		m_direct_regions.clear();
		size_t last = first;
		for (; last < m_reads.size() && m_reads[last].source == m_reads[first].source &&
		       m_reads[last].destination == m_reads[first].destination;
		     last++) {
			m_direct_regions.push_back(m_reads[last].region);
		}
		m_buffer.copyBuffer(m_reads[first].source, m_reads[first].destination,
		                    static_cast<uint32_t>(m_direct_regions.size()), m_direct_regions.data());
		first = last;
	}
	m_reads.clear();
	m_has_commands = true;
	m_command_count++;
}

vk::CommandBuffer CommandBuffer::Handle() const {
	EXIT_IF(IsInvalid());
	FlushReads();
	FinishCopies();
	ProfileClose();
	RecordBarrier(m_barriers.BeforeCommand());
	m_has_commands = true;
	m_command_count++;
	return m_buffer;
}

vk::CommandBuffer CommandBuffer::PipelineHandle() const {
	const auto handle   = Handle();
	m_compute_pipeline  = nullptr;
	m_graphics_pipeline = nullptr;
	return handle;
}

vk::CommandBuffer CommandBuffer::BarrierHandle() const {
	EXIT_IF(IsInvalid());
	FinishCopies();
	ProfileClose();
	RecordBarrier(m_barriers.BeforeBarrier());
	return m_buffer;
}

vk::CommandBuffer CommandBuffer::StateHandle() const {
	EXIT_IF(IsInvalid());
	m_has_commands = true;
	return m_buffer;
}

void CommandBuffer::BindComputePipeline(vk::Pipeline pipeline) const {
	if (pipeline == m_compute_pipeline) {
		return;
	}
	StateHandle().bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
	m_compute_pipeline = pipeline;
}

void CommandBuffer::BindGraphicsPipeline(vk::Pipeline pipeline) const {
	if (pipeline == m_graphics_pipeline) {
		return;
	}
	StateHandle().bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
	m_graphics_pipeline = pipeline;
}

vk::CommandBuffer CommandBuffer::DispatchHandle(bool writes, bool shared) const {
	EXIT_IF(IsInvalid() || m_rendering);
	Common::GpuTiming::Sync().AddUnmergedBarriers(writes ? 2 : 1);
	FlushReads();
	FinishCopies();
	ProfileClose();
	RecordBarrier(m_barriers.BeforeDispatch(writes, shared));
	m_note.dispatches++;
	m_note.AddShader(m_profile_key);
	// What was recorded since the last scope is the rest's; the dispatch recorded next is its
	// shader's.
	ProfileMark(GpuProfile::Other);
	m_profile_open = m_profile_key >= GpuProfile::Fixed ? m_profile_key : GpuProfile::Dispatches;
	m_profile_key  = 0;
	m_has_commands = true;
	m_command_count++;
	return m_buffer;
}

void CommandBuffer::GuestFlush() const {
	EXIT_IF(IsInvalid());
	Common::GpuTiming::Sync().AddUnmergedBarriers(1);
	FinishCopies();
	const auto barrier = m_barriers.Flush();
	if (barrier != BarrierTracker::Barrier::None) {
		EndRendering();
		RecordBarrier(barrier);
	}
}

void CommandBuffer::OrderDispatches() const {
	EXIT_IF(IsInvalid());
	FinishCopies();
	ProfileClose();
	RecordBarrier(m_barriers.BeforeBarrier());
}

void CommandBuffer::OrderIndirectDispatchReads() const {
	EXIT_IF(IsInvalid() || m_rendering);
	if (!m_barriers.BeforeIndirectDispatch()) {
		return;
	}
	// Barriers to all commands leave indirect-argument reads out (LaterAccess), so this one
	// covers every earlier write.
	vk::MemoryBarrier barrier {};
	barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
	m_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
	                         vk::PipelineStageFlagBits::eDrawIndirect, {}, 1, &barrier, 0, nullptr,
	                         0, nullptr);
}

void CommandBuffer::OrderIndirectReads() const {
	EXIT_IF(IsInvalid());
	if (m_rendering && m_render_passes == m_indirect_reads_since) {
		return;
	}
	EndRendering();
	vk::MemoryBarrier2 memory {};
	memory.srcStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	memory.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
	memory.dstStageMask  = vk::PipelineStageFlagBits2::eDrawIndirect;
	memory.dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead;
	vk::DependencyInfo dependency {};
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers    = &memory;
	BarrierHandle().pipelineBarrier2(dependency);
	m_indirect_reads_since = m_render_passes + 1;
}

void CommandBuffer::CopyToBuffer(vk::Buffer source, vk::Buffer destination,
                                 std::span<const vk::BufferCopy> regions,
                                 vk::DeviceAddress               source_address,
                                 vk::DeviceAddress               destination_address) const {
	EXIT_IF(IsInvalid());
	if (regions.empty()) {
		return;
	}
	EndRendering();
	FlushReads();
	m_note.uploads++;
	// The copies of a run, their scatter dispatch included, are the buffer uploads' time.
	if (m_profile_open != GpuProfile::BufferUploads) {
		ProfileClose();
		ProfileMark(GpuProfile::Other);
		m_profile_open = GpuProfile::BufferUploads;
	}
	RecordBarrier(m_barriers.BeforeCommand());
	// Outside a batch no later copy joins the run, so its ranges are not needed.
	m_copy_ranges.clear();
	if (m_copy_batches != 0) {
		const auto key =
		    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<VkBuffer>(destination)));
		for (const auto& region: regions) {
			m_copy_ranges.push_back({key, region.dstOffset, region.dstOffset + region.size});
		}
	}
	const auto barrier = m_copies.BeforeCopy(m_copy_ranges);
	if (barrier != CopyRunTracker::Barrier::None) {
		// Copies of a run may be scatter dispatches.
		constexpr auto Stages =
		    vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader;
		constexpr auto Writes =
		    vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite;
		vk::MemoryBarrier2 memory {};
		if (barrier == CopyRunTracker::Barrier::AllToTransfer) {
			memory.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
			memory.srcAccessMask =
			    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
		} else {
			// The copies the barrier orders this one after.
			RecordScatter();
			memory.srcStageMask  = Stages;
			memory.srcAccessMask = Writes;
		}
		memory.dstStageMask  = Stages;
		memory.dstAccessMask = Writes | vk::AccessFlagBits2::eShaderStorageRead;
		vk::DependencyInfo dependency {};
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers    = &memory;
		m_buffer.pipelineBarrier2(dependency);
	}
	// Only the copies of a batch can share a dispatch; the shader copies whole dwords.
	const bool scatter = m_copy_batches != 0 && source_address != 0 && destination_address != 0;
	if (scatter && source != m_scatter_source) {
		RecordScatter();
		m_scatter_source         = source;
		m_scatter_source_address = source_address;
	}
	m_direct_regions.clear();
	const auto pieces = m_scatter_pieces.size();
	for (const auto& region: regions) {
		constexpr auto Alignment = UploadScatter::Alignment;
		const auto     body      = region.size / Alignment * Alignment;
		if (!scatter || body == 0 || region.srcOffset % Alignment != 0 ||
		    region.dstOffset % Alignment != 0 || region.srcOffset + body > UINT32_MAX) {
			m_direct_regions.push_back(region);
			continue;
		}
		UploadScatter::AddPieces(m_scatter_pieces, destination_address + region.dstOffset,
		                         static_cast<uint32_t>(region.srcOffset),
		                         static_cast<uint32_t>(body));
		if (body != region.size) {
			m_direct_regions.emplace_back(region.srcOffset + body, region.dstOffset + body,
			                              region.size - body);
		}
	}
	if (m_scatter_pieces.size() != pieces) {
		if (!m_scatter_destinations.empty() &&
		    m_scatter_destinations.back().buffer == destination) {
			m_scatter_destinations.back().end = m_scatter_pieces.size();
		} else {
			m_scatter_destinations.push_back(
			    {destination, destination_address, m_scatter_pieces.size()});
		}
	}
	if (!m_direct_regions.empty()) {
		m_buffer.copyBuffer(source, destination, static_cast<uint32_t>(m_direct_regions.size()),
		                    m_direct_regions.data());
	}
	m_has_commands = true;
	m_command_count++;
	if (m_copy_batches == 0) {
		FinishCopies();
	}
}

void CommandBuffer::RecordScatter() const {
	if (m_scatter_pieces.empty()) {
		return;
	}
	if (!m_context.GetUploadScatter().Record(m_buffer, m_scatter_source_address,
	                                         m_scatter_pieces)) {
		// The ring is full: a copy command per destination buffer.
		std::vector<vk::BufferCopy> copies;
		size_t                      first = 0;
		for (const auto& destination: m_scatter_destinations) {
			copies.clear();
			for (size_t i = first; i < destination.end; i++) {
				const auto& piece   = m_scatter_pieces[i];
				const auto  address = static_cast<uint64_t>(piece.destination_low) |
				                      (static_cast<uint64_t>(piece.destination_high) << 32u);
				copies.emplace_back(piece.source_offset, address - destination.address, piece.size);
			}
			m_buffer.copyBuffer(m_scatter_source, destination.buffer,
			                    static_cast<uint32_t>(copies.size()), copies.data());
			first = destination.end;
		}
	} else if (m_compute_pipeline) {
		// The pipeline bound for a guest dispatch about to be recorded.
		m_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_compute_pipeline);
	}
	m_scatter_pieces.clear();
	m_scatter_destinations.clear();
}

void CommandBuffer::FinishCopies() const {
	RecordScatter();
	if (!m_copies.BeforeOther()) {
		return;
	}
	// A copy ends the render pass instance, and starting one records this barrier first.
	EXIT_IF(m_rendering);
	vk::MemoryBarrier2 memory {};
	memory.srcStageMask =
	    vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader;
	memory.srcAccessMask =
	    vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite;
	memory.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	memory.dstAccessMask = LaterAccess2;
	vk::DependencyInfo dependency {};
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers    = &memory;
	m_buffer.pipelineBarrier2(dependency);
}

static_assert(static_cast<uint8_t>(BarrierTracker::Barrier::ComputeToCompute) - 1u ==
                  static_cast<uint8_t>(Common::GpuTiming::BarrierKind::ComputeToCompute) &&
              static_cast<uint8_t>(BarrierTracker::Barrier::AllToAll) - 1u ==
                  static_cast<uint8_t>(Common::GpuTiming::BarrierKind::AllToAll));

void CommandBuffer::RecordBarrier(BarrierTracker::Barrier barrier) const {
	using Barrier = BarrierTracker::Barrier;
	if (barrier == Barrier::None) {
		return;
	}
	// Dispatches end a render pass instance and the start of one orders them first, so no
	// tracked barrier falls inside one.
	EXIT_IF(m_rendering);
	constexpr auto ShaderAccess =
	    vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
	constexpr auto ComputeReads = vk::AccessFlagBits2::eShaderRead |
	                              vk::AccessFlagBits2::eShaderWrite |
	                              vk::AccessFlagBits2::eUniformRead;
	constexpr auto MemoryAccess =
	    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
	vk::MemoryBarrier2 memory {};
	switch (barrier) {
		case Barrier::ComputeToCompute:
			memory.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
			memory.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
			memory.dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
			memory.dstAccessMask = ComputeReads;
			break;
		case Barrier::AllToCompute:
			memory.srcStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
			memory.srcAccessMask = MemoryAccess;
			memory.dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
			memory.dstAccessMask = ComputeReads;
			break;
		case Barrier::ComputeToAll:
			memory.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
			memory.srcAccessMask = ShaderAccess;
			memory.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
			memory.dstAccessMask = LaterAccess2;
			break;
		case Barrier::AllToAll:
			memory.srcStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
			memory.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
			memory.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
			memory.dstAccessMask = LaterAccess2;
			break;
		case Barrier::None: return;
	}
	vk::DependencyInfo dependency {};
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers    = &memory;
	m_buffer.pipelineBarrier2(dependency);
	Common::GpuTiming::Sync().AddBarrier(
	    static_cast<Common::GpuTiming::BarrierKind>(static_cast<uint8_t>(barrier) - 1u));
}

void CommandBuffer::Begin() {
	EXIT_IF(m_rendering || IsInvalid());

	vk::CommandBufferBeginInfo begin_info {};
	begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	auto result = m_buffer.begin(&begin_info);

	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess);
	// The previous buffer recorded the barrier of its copies when it ended.
	(void)m_copies.BeforeOther();
	m_barriers.BeginBuffer();
	m_note         = {};
	m_profile_open = 0;
	ProfileMark(GpuProfile::Base);
	EXIT_IF(!m_scatter_pieces.empty());
	m_has_commands         = false;
	m_command_count        = 0;
	m_compute_pipeline     = nullptr;
	m_graphics_pipeline    = nullptr;
	m_indirect_reads_since = UINT64_MAX;
}

void CommandBuffer::End() const {
	EndRendering();
	FlushReads();
	// The deferred barriers after the last copies and dispatches stay in this submission.
	OrderDispatches();
	ProfileMark(GpuProfile::Other);
	if (m_has_commands) {
		m_note.tick     = m_context.GetCommandScheduler().CurrentTick();
		m_note.commands = m_command_count;
		SubmissionNotes::Add(m_note);
	}

	auto result = m_buffer.end();

	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess);
}

void CommandBuffer::BeginRendering(const RenderState& state) const {
	if (m_rendering && m_render_state == state) {
		return;
	}
	const bool ordered = m_rendering && m_render_passes == m_indirect_reads_since;
	EndRendering();
	g_render_passes.Add();

	std::array<vk::RenderingAttachmentInfo, RENDER_COLOR_ATTACHMENTS_MAX> colors {};
	for (uint32_t i = 0; i < state.num_color_attachments; i++) {
		const auto& attachment = state.color_attachments[i];
		colors[i].imageView    = attachment.image_view;
		colors[i].imageLayout  = attachment.image_layout;
		colors[i].loadOp       = vk::AttachmentLoadOp::eLoad;
		colors[i].storeOp      = vk::AttachmentStoreOp::eStore;
	}

	const auto&                 depth_stencil = state.depth_stencil_attachment;
	vk::RenderingAttachmentInfo depth {};
	depth.imageView   = depth_stencil.image_view;
	depth.imageLayout = depth_stencil.image_layout;
	depth.loadOp =
	    depth_stencil.depth_clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
	depth.storeOp                       = vk::AttachmentStoreOp::eStore;
	depth.clearValue.depthStencil.depth = std::bit_cast<float>(depth_stencil.clear_value[0]);

	vk::RenderingAttachmentInfo stencil {};
	stencil.imageView   = depth_stencil.image_view;
	stencil.imageLayout = depth_stencil.image_layout;
	stencil.loadOp =
	    depth_stencil.stencil_clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
	stencil.storeOp                         = vk::AttachmentStoreOp::eStore;
	stencil.clearValue.depthStencil.stencil = depth_stencil.clear_value[1];

	vk::RenderingInfo rendering {};
	rendering.renderArea.extent    = {state.width, state.height};
	rendering.layerCount           = state.num_layers;
	rendering.colorAttachmentCount = state.num_color_attachments;
	rendering.pColorAttachments    = colors.data();
	rendering.pDepthAttachment     = depth_stencil.has_depth ? &depth : nullptr;
	rendering.pStencilAttachment   = depth_stencil.has_stencil ? &stencil : nullptr;
	// Attachment loads and stores access memory like any other command.
	const auto handle = Handle();
	m_note.passes++;
	ProfileMark(GpuProfile::Other);
	m_profile_open = GpuProfile::Draws;
	handle.beginRendering(rendering);
	m_render_state = state;
	// The open pass holds the cleared contents, so a later clear begins a new pass.
	m_render_state.depth_stencil_attachment.depth_clear   = false;
	m_render_state.depth_stencil_attachment.stencil_clear = false;
	m_rendering                                           = true;
	// A pass that directly follows one OrderIndirectReads covers is covered too: nothing was
	// recorded between them.
	m_render_passes++;
	if (ordered) {
		m_indirect_reads_since = m_render_passes;
	}
}

void CommandBuffer::EndRendering() const {
	if (!m_rendering) {
		return;
	}
	Handle().endRendering();
	m_rendering = false;
	ProfileClose();
}

} // namespace Libs::Graphics
